// Copyright (c) 2017-2025 The Khronos Group Inc.
// Copyright (c) 2017-2019 Valve Corporation
// Copyright (c) 2017-2019 LunarG, Inc.
//
// SPDX-License-Identifier: Apache-2.0 OR MIT
//
// Initial Author: Mark Young <marky@lunarg.com>
//

#include "loader_init_data.hpp"

#include <android/asset_manager_jni.h>
#include <jni.h>

#include "loader_instance.hpp"
#include "loader_logger.hpp"
#include "loader_properties.hpp"
#include "runtime_interface.hpp"

XrResult LoaderInitData::initialize(const XrLoaderInitInfoBaseHeaderKHR* info) {
    XrResult result = initializeProperties(info);
    if (result != XR_SUCCESS) {
        return result;
    }

    result = initializePlatform(info);
    if (result != XR_SUCCESS) {
        return result;
    }

    _initialized = true;
    return XR_SUCCESS;
}

XrResult LoaderInitData::initializeProperties(const XrLoaderInitInfoBaseHeaderKHR* info) {
    while (info != nullptr) {
        if (info->type == XR_TYPE_LOADER_INIT_INFO_PROPERTIES_EXT) {
            const auto* propertyInfo = reinterpret_cast<XrLoaderInitInfoPropertiesEXT const*>(info);

            for (uint32_t i = 0; i < propertyInfo->propertyValueCount; i++) {
                if (propertyInfo->propertyValues[i].name == nullptr ||
                    propertyInfo->propertyValues[i].value == nullptr) {
                    return XR_ERROR_VALIDATION_FAILURE;
                }
                if (strlen(propertyInfo->propertyValues[i].name) == 0) {
                    return XR_ERROR_VALIDATION_FAILURE;
                }
            }

            LoaderProperty::ClearOverrides();
            for (uint32_t i = 0; i < propertyInfo->propertyValueCount; i++) {
                LoaderProperty::SetOverride(propertyInfo->propertyValues[i].name,
                                            propertyInfo->propertyValues[i].value);
            }
            return XR_SUCCESS;
        }
        info = reinterpret_cast<const XrLoaderInitInfoBaseHeaderKHR*>(info->next);
    }
    return XR_SUCCESS;
}

XrResult LoaderInitData::initializePlatform(const XrLoaderInitInfoBaseHeaderKHR* info) {
    while (info != nullptr) {
        if (info->type == XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR) {
            auto cast_info = reinterpret_cast<XrLoaderInitInfoAndroidKHR const*>(info);

            if (cast_info->applicationVM == nullptr || cast_info->applicationContext == nullptr) {
                return XR_ERROR_VALIDATION_FAILURE;
            }

            _platform_info = *cast_info;
            _platform_info.next = nullptr;

            JavaVM* vm = static_cast<JavaVM*>(_platform_info.applicationVM);
            jobject context = static_cast<jobject>(_platform_info.applicationContext);
            JNIEnv* env = nullptr;

            if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
                return XR_ERROR_INITIALIZATION_FAILED;
            }

            // --- Retrieve AssetManager ---
            jclass contextClass = env->GetObjectClass(context);
            jmethodID getAssetsMethod = env->GetMethodID(contextClass, "getAssets",
                                                         "()Landroid/content/res/AssetManager;");
            jobject assetManagerObj = env->CallObjectMethod(context, getAssetsMethod);
            _android_asset_manager = AAssetManager_fromJava(env, assetManagerObj);

            // --- Retrieve Native Library Path ---
            jmethodID getAppInfoMethod = env->GetMethodID(contextClass, "getApplicationInfo",
                                                          "()Landroid/content/pm/ApplicationInfo;");
            jobject appInfoObj = env->CallObjectMethod(context, getAppInfoMethod);

            jclass appInfoClass = env->GetObjectClass(appInfoObj);
            jfieldID nativeLibDirField =
                    env->GetFieldID(appInfoClass, "nativeLibraryDir", "Ljava/lang/String;");
            jstring nativeLibDirStr = (jstring)env->GetObjectField(appInfoObj, nativeLibDirField);
            if (nativeLibDirStr != nullptr) {
                const char* rawPath = env->GetStringUTFChars(nativeLibDirStr, nullptr);
                if (rawPath != nullptr) {
                    _android_native_library_path = rawPath;
                    env->ReleaseStringUTFChars(nativeLibDirStr, rawPath);
                }
            }

            return XR_SUCCESS;
        }
        info = reinterpret_cast<const XrLoaderInitInfoBaseHeaderKHR*>(info->next);
    }
    return XR_ERROR_VALIDATION_FAILURE;
}

XrResult InitializeLoaderInitData(const XrLoaderInitInfoBaseHeaderKHR* loaderInitInfo) {
    if (!ActiveLoaderInstance::IsAvailable()) {
        LoaderLogger::LogVerboseMessage("InitializeLoaderInitData",
                                        "Unloading any previously loaded runtime");
        RuntimeInterface::UnloadRuntime("InitializeLoaderInitData");
    } else {
        LoaderLogger::LogErrorMessage("InitializeLoaderInitData",
                                      "An active instance currently exists");
        return XR_ERROR_INITIALIZATION_FAILED;
    }
    return LoaderInitData::instance().initialize(loaderInitInfo);
}

std::string GetAndroidNativeLibraryDir() {
    return LoaderInitData::instance()._android_native_library_path;
}

void* GetAndroidAssetManager() {
    return LoaderInitData::instance()._android_asset_manager;
}