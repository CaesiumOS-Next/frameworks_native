/*
 * Copyright (C) 2026 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "xr_environment.h"

#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <android/dlext.h>
#include <android/log.h>
#include <bionic/dlext_namespaces.h>
#include <dlfcn.h>
#include <jni.h>
#include <json/json.h>
#include <openxr/openxr.h>
#include <openxr/openxr_loader_negotiation.h>

#include <sstream>
#include "loader_init_data.hpp"
#include "loader_logger.hpp"

#define LOG_TAG "XrEnvironment"
#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#if defined(__arm__)
#define CURRENT_ABI "armeabi-v7a"
#elif defined(__aarch64__)
#define CURRENT_ABI "arm64-v8a"
#elif defined(__i386__)
#define CURRENT_ABI "x86"
#elif defined(__x86_64__)
#define CURRENT_ABI "x86_64"
#else
#define CURRENT_ABI "unknown-abi"
#endif

namespace android {

std::optional<XrRuntimeEnvironment> GetXrRuntimeEnvironment() {
    if (!LoaderInitData::instance().initialized()) {
        ALOGE("Cannot load OpenXR without loader init.");
        return std::nullopt;
    }

    const auto& platform_data = LoaderInitData::instance().getPlatformData();
    JavaVM* vm = static_cast<JavaVM*>(platform_data.applicationVM);
    jobject context = static_cast<jobject>(platform_data.applicationContext);
    if (vm == nullptr || context == nullptr) {
        ALOGE("VM or Context is null inside LoaderInitData.");
        return std::nullopt;
    }

    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        ALOGE("Failed to get JNIEnv from JavaVM.");
        return std::nullopt;
    }

    jclass contextClass = env->GetObjectClass(context);
    jmethodID getContentResolverMethod = env->GetMethodID(contextClass, "getContentResolver",
                                                          "()Landroid/content/ContentResolver;");
    if (getContentResolverMethod == nullptr) {
        ALOGE("Failed to find getContentResolver method.");
        return std::nullopt;
    }

    jobject contentResolverObj = env->CallObjectMethod(context, getContentResolverMethod);
    if (env->ExceptionCheck() || contentResolverObj == nullptr) {
        env->ExceptionClear();
        ALOGE("Failed to retrieve ContentResolver.");
        return std::nullopt;
    }

    jclass settingsGlobalClass = env->FindClass("android/provider/Settings$Global");
    if (settingsGlobalClass == nullptr) {
        ALOGE("Failed to find android/provider/Settings$Global class.");
        return std::nullopt;
    }

    jmethodID getStringMethod = env->GetStaticMethodID(settingsGlobalClass, "getString",
                                                       "(Landroid/content/ContentResolver;Ljava/"
                                                       "lang/String;)Ljava/lang/String;");
    if (getStringMethod == nullptr) {
        ALOGE("Failed to find Settings.Global.getString method.");
        return std::nullopt;
    }

    jstring settingKeyStr = env->NewStringUTF("openxr_runtime_package");
    jstring runtimePackageNameStr =
            (jstring)env->CallStaticObjectMethod(settingsGlobalClass, getStringMethod,
                                                 contentResolverObj, settingKeyStr);
    if (env->ExceptionCheck() || runtimePackageNameStr == nullptr) {
        env->ExceptionClear();
        ALOGE("Failed to get openxr_runtime_package from Settings.Global.");
        return std::nullopt;
    }

    const char* runtimePackageNameChar = env->GetStringUTFChars(runtimePackageNameStr, nullptr);
    std::string package_name(runtimePackageNameChar);
    env->ReleaseStringUTFChars(runtimePackageNameStr, runtimePackageNameChar);

    ALOGI("Using runtime package: %s", package_name.c_str());

    if (package_name.empty()) {
        ALOGE("Active runtime package name is empty.");
        return std::nullopt;
    }

    jmethodID createPackageContextMethod =
            env->GetMethodID(contextClass, "createPackageContext",
                             "(Ljava/lang/String;I)Landroid/content/Context;");
    if (createPackageContextMethod == nullptr) {
        ALOGE("Failed to find createPackageContext method.");
        return std::nullopt;
    }

    // Context.CONTEXT_IGNORE_SECURITY = 2
    jobject packageContextObj =
            env->CallObjectMethod(context, createPackageContextMethod, runtimePackageNameStr, 2);
    if (env->ExceptionCheck() || packageContextObj == nullptr) {
        env->ExceptionClear();
        ALOGE("Failed to create package context for package: %s", package_name.c_str());
        return std::nullopt;
    }

    jclass packageContextClass = env->GetObjectClass(packageContextObj);
    jmethodID getApplicationInfoMethod = env->GetMethodID(packageContextClass, "getApplicationInfo",
                                                          "()Landroid/content/pm/ApplicationInfo;");
    if (getApplicationInfoMethod == nullptr) {
        ALOGE("Failed to find getApplicationInfo method.");
        return std::nullopt;
    }

    jobject applicationInfoObj = env->CallObjectMethod(packageContextObj, getApplicationInfoMethod);
    if (env->ExceptionCheck() || applicationInfoObj == nullptr) {
        env->ExceptionClear();
        ALOGE("Failed to get application info.");
        return std::nullopt;
    }

    jclass applicationInfoClass = env->GetObjectClass(applicationInfoObj);
    jfieldID sourceDirField =
            env->GetFieldID(applicationInfoClass, "sourceDir", "Ljava/lang/String;");
    if (sourceDirField == nullptr) {
        ALOGE("Failed to find sourceDir field in ApplicationInfo.");
        return std::nullopt;
    }

    jstring sourceDirStr = (jstring)env->GetObjectField(applicationInfoObj, sourceDirField);
    if (env->ExceptionCheck() || sourceDirStr == nullptr) {
        env->ExceptionClear();
        ALOGE("Failed to retrieve sourceDir.");
        return std::nullopt;
    }

    const char* sourceDirChar = env->GetStringUTFChars(sourceDirStr, nullptr);
    std::string source_dir(sourceDirChar);
    env->ReleaseStringUTFChars(sourceDirStr, sourceDirChar);

    jmethodID getAssetsMethod = env->GetMethodID(packageContextClass, "getAssets",
                                                 "()Landroid/content/res/AssetManager;");
    if (getAssetsMethod == nullptr) {
        ALOGE("Failed to find getAssets method.");
        return std::nullopt;
    }

    jobject assetManagerObj = env->CallObjectMethod(packageContextObj, getAssetsMethod);
    if (env->ExceptionCheck() || assetManagerObj == nullptr) {
        env->ExceptionClear();
        ALOGE("Failed to retrieve AssetManager.");
        return std::nullopt;
    }

    AAssetManager* assetManager = AAssetManager_fromJava(env, assetManagerObj);
    if (assetManager == nullptr) {
        ALOGE("Failed to convert Java AssetManager to NDK AAssetManager.");
        return std::nullopt;
    }

    std::string manifest_path = "openxr/" +
            std::to_string(XR_VERSION_MAJOR(XR_CURRENT_API_VERSION)) + "/active_runtime.json";
    AAsset* asset = AAssetManager_open(assetManager, manifest_path.c_str(), AASSET_MODE_BUFFER);
    if (asset == nullptr) {
        ALOGE("Failed to open asset %s in package %s", manifest_path.c_str(), package_name.c_str());
        return std::nullopt;
    }

    off_t length = AAsset_getLength(asset);
    const void* buffer = AAsset_getBuffer(asset);
    if (buffer == nullptr) {
        ALOGE("Failed to get asset buffer.");
        AAsset_close(asset);
        return std::nullopt;
    }

    std::string json_content(static_cast<const char*>(buffer), length);
    AAsset_close(asset);

    Json::Value root;
    Json::CharReaderBuilder builder;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    std::string parse_errors;
    if (!reader->parse(json_content.c_str(), json_content.c_str() + json_content.length(), &root,
                       &parse_errors)) {
        ALOGE("Failed to parse active_runtime.json. Errors: %s", parse_errors.c_str());
        return std::nullopt;
    }

    std::string library_path = root["runtime"]["library_path"].asString();
    if (library_path.empty()) {
        ALOGE("runtime.library_path not found in active_runtime.json");
        return std::nullopt;
    }

    ALOGI("Using runtime library: %s", library_path.c_str());

    jfieldID nativeLibraryDirField =
            env->GetFieldID(applicationInfoClass, "nativeLibraryDir", "Ljava/lang/String;");
    std::string native_library_dir = "";
    if (nativeLibraryDirField != nullptr) {
        jstring nativeLibraryDirStr =
                (jstring)env->GetObjectField(applicationInfoObj, nativeLibraryDirField);
        if (!env->ExceptionCheck() && nativeLibraryDirStr != nullptr) {
            const char* nativeLibraryDirChar = env->GetStringUTFChars(nativeLibraryDirStr, nullptr);
            native_library_dir = std::string(nativeLibraryDirChar);
            env->ReleaseStringUTFChars(nativeLibraryDirStr, nativeLibraryDirChar);
        } else {
            env->ExceptionClear();
        }
    }

    std::ostringstream search_path_stream;
    if (!native_library_dir.empty()) {
        search_path_stream << native_library_dir << ":";
    }
    search_path_stream << source_dir << "!/lib/" << CURRENT_ABI;
    std::string library_search_path = search_path_stream.str();

    ALOGI("Using library search path: %s", library_search_path.c_str());

    android_namespace_t* openxr_namespace =
            android_create_namespace("openxr",
                                     library_search_path.c_str(), // ld_library_path
                                     library_search_path.c_str(), // default_library_path
                                     ANDROID_NAMESPACE_TYPE_SHARED_ISOLATED,
                                     nullptr, // permitted_when_isolated_path
                                     nullptr);

    return XrRuntimeEnvironment{
            .package_name = std::move(package_name),
            .runtime_soname = std::move(library_path),
            .library_namespace = openxr_namespace,
    };
}

} // namespace android
