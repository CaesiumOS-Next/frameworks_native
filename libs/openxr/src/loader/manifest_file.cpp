// Copyright (c) 2017-2025 The Khronos Group Inc.
// Copyright (c) 2017-2019 Valve Corporation
// Copyright (c) 2017-2019 LunarG, Inc.
//
// SPDX-License-Identifier: Apache-2.0 OR MIT
//
// Initial Authors: Mark Young <marky@lunarg.com>, Dave Houlton <daveh@lunarg.com>
//

#include "manifest_file.hpp"
#include <android/asset_manager.h>
#include <json/json.h>
#include <openxr/openxr.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "filesystem_utils.hpp"
#include "loader_init_data.hpp"
#include "loader_logger.hpp"
#include "loader_platform.hpp"
#include "loader_properties.hpp"
#include "runtime_interface.hpp"
#include "unique_asset.h"

ManifestFile::ManifestFile(ManifestFileType type, const std::string& filename,
                           const std::string& library_path)
      : _filename(filename), _type(type), _library_path(library_path) {}

bool ManifestFile::IsValidJson(const Json::Value& root_node, JsonVersion& version) {
    if (root_node["file_format_version"].isNull() || !root_node["file_format_version"].isString()) {
        LoaderLogger::LogErrorMessage("",
                                      "ManifestFile::IsValidJson - JSON file missing "
                                      "\"file_format_version\"");
        return false;
    }
    std::string file_format = root_node["file_format_version"].asString();
    const int num_fields =
            sscanf(file_format.c_str(), "%u.%u.%u", &version.major, &version.minor, &version.patch);

    // Only version 1.0.0 is defined currently.  Eventually we may have more version, but
    // some of the versions may only be valid for layers or runtimes specifically.
    if (num_fields != 3 || version.major != 1 || version.minor != 0 || version.patch != 0) {
        std::ostringstream error_ss;
        error_ss << "ManifestFile::IsValidJson - JSON \"file_format_version\" " << version.major
                 << "." << version.minor << "." << version.patch << " is not supported";
        LoaderLogger::LogErrorMessage("", error_ss.str());
        return false;
    }

    return true;
}

static void GetExtensionProperties(const std::vector<ExtensionListing>& extensions,
                                   std::vector<XrExtensionProperties>& props) {
    for (const auto& ext : extensions) {
        auto it = std::find_if(props.begin(), props.end(), [&](XrExtensionProperties& prop) {
            return prop.extensionName == ext.name;
        });
        if (it != props.end()) {
            it->extensionVersion = std::max(it->extensionVersion, ext.extension_version);
        } else {
            XrExtensionProperties prop{};
            prop.type = XR_TYPE_EXTENSION_PROPERTIES;
            strncpy(prop.extensionName, ext.name.c_str(), XR_MAX_EXTENSION_NAME_SIZE - 1);
            prop.extensionName[XR_MAX_EXTENSION_NAME_SIZE - 1] = '\0';
            prop.extensionVersion = ext.extension_version;
            props.push_back(prop);
        }
    }
}

// Return any instance extensions found in the manifest files in the proper form for
// OpenXR (XrExtensionProperties).
void ManifestFile::GetInstanceExtensionProperties(std::vector<XrExtensionProperties>& props) {
    GetExtensionProperties(_instance_extensions, props);
}

std::string ManifestFile::GetFunctionName(const std::string& func_name) const {
    if (!_functions_renamed.empty()) {
        auto found = _functions_renamed.find(func_name);
        if (found != _functions_renamed.end()) {
            return found->second;
        }
    }
    return func_name;
}

static void ParseExtension(Json::Value const& ext, std::vector<ExtensionListing>& extensions) {
    Json::Value ext_name = ext["name"];
    Json::Value ext_version = ext["extension_version"];

    // Allow "extension_version" as a String or a UInt to maintain backwards compatibility, even
    // though it should be a String. Internal Issue 1411:
    // https://gitlab.khronos.org/openxr/openxr/-/issues/1411 Internal MR !1867:
    // https://gitlab.khronos.org/openxr/openxr/-/merge_requests/1867
    if (ext_name.isString() && (ext_version.isString() || ext_version.isUInt())) {
        ExtensionListing ext_listing = {};
        ext_listing.name = ext_name.asString();
        if (ext_version.isUInt()) {
            ext_listing.extension_version = ext_version.asUInt();
        } else {
            ext_listing.extension_version = atoi(ext_version.asString().c_str());
        }
        extensions.push_back(ext_listing);
    }
}

void ManifestFile::ParseCommon(Json::Value const& root_node) {
    const Json::Value& inst_exts = root_node["instance_extensions"];
    if (!inst_exts.isNull() && inst_exts.isArray()) {
        for (const auto& ext : inst_exts) {
            ParseExtension(ext, _instance_extensions);
        }
    }
    const Json::Value& funcs_renamed = root_node["functions"];
    if (!funcs_renamed.isNull() && !funcs_renamed.empty()) {
        for (Json::ValueConstIterator func_it = funcs_renamed.begin();
             func_it != funcs_renamed.end(); ++func_it) {
            if (!(*func_it).isString()) {
                LoaderLogger::LogWarningMessage("",
                                                "ManifestFile::ParseCommon " + _filename +
                                                        " \"functions\" section contains "
                                                        "non-string values.");
                continue;
            }
            std::string original_name = func_it.key().asString();
            std::string new_name = (*func_it).asString();
            _functions_renamed.emplace(original_name, new_name);
        }
    }
}

ApiLayerManifestFile::ApiLayerManifestFile(ManifestFileType type, const std::string& filename,
                                           const std::string& layer_name,
                                           const std::string& description,
                                           const JsonVersion& api_version,
                                           const uint32_t& implementation_version,
                                           const std::string& library_path)
      : ManifestFile(type, filename, library_path),
        _api_version(api_version),
        _layer_name(layer_name),
        _description(description),
        _implementation_version(implementation_version) {}

void ApiLayerManifestFile::AddManifestFilesAndroid(
        const std::string& openxr_command, ManifestFileType type,
        std::vector<std::unique_ptr<ApiLayerManifestFile>>& manifest_files) {
    if (!LoaderInitData::instance().initialized()) {
        // This will happen for applications that do not call xrInitializeLoaderKHR
        LoaderLogger::LogWarningMessage(openxr_command,
                                        "ApiLayerManifestFile::AddManifestFilesAndroid unable to "
                                        "add manifest files LoaderInitData not initialized.");
        return;
    }

    AAssetManager* assetManager = (AAssetManager*)GetAndroidAssetManager();
    std::vector<std::string> filenames;
    {
        std::string search_path = "";
        switch (type) {
            case MANIFEST_TYPE_IMPLICIT_API_LAYER:
                search_path = "openxr/1/api_layers/implicit.d/";
                break;
            case MANIFEST_TYPE_EXPLICIT_API_LAYER:
                search_path = "openxr/1/api_layers/explicit.d/";
                break;
            default:
                return;
        }

        UniqueAssetDir dir{AAssetManager_openDir(assetManager, search_path.c_str())};
        if (!dir) {
            return;
        }
        const std::string json = ".json";
        const char* fn = nullptr;
        while ((fn = AAssetDir_getNextFileName(dir.get())) != nullptr) {
            const std::string filename = search_path + fn;
            if (filename.size() < json.size()) {
                continue;
            }
            if (filename.compare(filename.size() - json.size(), json.size(), json) == 0) {
                filenames.push_back(filename);
            }
        }
    }
    for (const auto& filename : filenames) {
        UniqueAsset asset{AAssetManager_open(assetManager, filename.c_str(), AASSET_MODE_BUFFER)};
        if (!asset) {
            LoaderLogger::LogWarningMessage(openxr_command,
                                            "ApiLayerManifestFile::AddManifestFilesAndroid unable "
                                            "to open asset " +
                                                    filename + ", skipping");

            continue;
        }
        size_t length = AAsset_getLength(asset.get());
        const char* buf = reinterpret_cast<const char*>(AAsset_getBuffer(asset.get()));
        if (!buf) {
            LoaderLogger::LogWarningMessage(openxr_command,
                                            "ApiLayerManifestFile::AddManifestFilesAndroid unable "
                                            "to access asset" +
                                                    filename + ", skipping");

            continue;
        }
        std::istringstream json_stream(std::string{buf, length});

        CreateIfValid(type, filename, json_stream, &ApiLayerManifestFile::LocateLibraryInAssets,
                      manifest_files);
    }
}

void ApiLayerManifestFile::CreateIfValid(
        ManifestFileType type, const std::string& filename, std::istream& json_stream,
        LibraryLocator locate_library,
        std::vector<std::unique_ptr<ApiLayerManifestFile>>& manifest_files) {
    std::ostringstream error_ss("ApiLayerManifestFile::CreateIfValid ");
    Json::CharReaderBuilder builder;
    std::string errors;
    Json::Value root_node = Json::nullValue;
    if (!Json::parseFromStream(builder, json_stream, &root_node, &errors) ||
        !root_node.isObject()) {
        error_ss << "failed to parse " << filename << ".";
        if (!errors.empty()) {
            error_ss << " (Error message: " << errors << ")";
        }
        error_ss << " Is it a valid layer manifest file?";
        LoaderLogger::LogErrorMessage("", error_ss.str());
        return;
    }
    JsonVersion file_version = {};
    if (!ManifestFile::IsValidJson(root_node, file_version)) {
        error_ss << "isValidJson indicates " << filename << " is not a valid manifest file.";
        LoaderLogger::LogErrorMessage("", error_ss.str());
        return;
    }

    Json::Value layer_root_node = root_node["api_layer"];

    // The API Layer manifest file needs the "api_layer" root as well as other sub-nodes.
    // If any of those aren't there, fail.
    if (layer_root_node.isNull() || layer_root_node["name"].isNull() ||
        !layer_root_node["name"].isString() || layer_root_node["api_version"].isNull() ||
        !layer_root_node["api_version"].isString() || layer_root_node["library_path"].isNull() ||
        !layer_root_node["library_path"].isString() ||
        layer_root_node["implementation_version"].isNull() ||
        !layer_root_node["implementation_version"].isString()) {
        error_ss << filename << " is missing required fields.  Verify all proper fields exist.";
        LoaderLogger::LogErrorMessage("", error_ss.str());
        return;
    }
    if (MANIFEST_TYPE_IMPLICIT_API_LAYER == type) {
        bool enabled = true;
        // Implicit layers require the disable environment variable.
        if (layer_root_node["disable_environment"].isNull() ||
            !layer_root_node["disable_environment"].isString()) {
            error_ss << "Implicit layer " << filename << " is missing \"disable_environment\"";
            LoaderLogger::LogErrorMessage("", error_ss.str());
            return;
        }
        // Check if there's an enable environment variable provided
        if (!layer_root_node["enable_environment"].isNull() &&
            layer_root_node["enable_environment"].isString()) {
            std::string env_var = layer_root_node["enable_environment"].asString();
            // If it's not set in the environment, disable the layer
            if (!LoaderProperty::IsSet(env_var)) {
                enabled = false;
            }
        }
        // Check for the disable environment variable, which must be provided in the JSON
        std::string env_var = layer_root_node["disable_environment"].asString();
        // If the env var is set, disable the layer. Disable env var overrides enable above
        if (LoaderProperty::IsSet(env_var)) {
            enabled = false;
        }

        // Not enabled, so pretend like it isn't even there.
        if (!enabled) {
            error_ss << "Implicit layer " << filename << " is disabled";
            LoaderLogger::LogInfoMessage("", error_ss.str());
            return;
        }
    }
    std::string layer_name = layer_root_node["name"].asString();
    std::string api_version_string = layer_root_node["api_version"].asString();
    JsonVersion api_version = {};
    const int num_fields =
            sscanf(api_version_string.c_str(), "%u.%u", &api_version.major, &api_version.minor);
    api_version.patch = 0;

    if ((num_fields != 2) || (api_version.major == 0 && api_version.minor == 0) ||
        api_version.major > XR_VERSION_MAJOR(XR_CURRENT_API_VERSION)) {
        error_ss << "layer " << filename << " has invalid API Version.  Skipping layer.";
        LoaderLogger::LogWarningMessage("", error_ss.str());
        return;
    }

    uint32_t implementation_version =
            atoi(layer_root_node["implementation_version"].asString().c_str());
    std::string library_path = layer_root_node["library_path"].asString();

    // If the library_path variable has no directory symbol, it's just a file name and should be
    // accessible on the global library path.
    if (library_path.find('\\') != std::string::npos ||
        library_path.find('/') != std::string::npos) {
        // If the library_path is an absolute path, just use that if it exists
        if (FileSysUtilsIsAbsolutePath(library_path)) {
            if (!FileSysUtilsPathExists(library_path)) {
                error_ss << filename << " library " << library_path << " does not appear to exist";
                LoaderLogger::LogErrorMessage("", error_ss.str());
                return;
            }
        } else {
            // Otherwise, treat the library path as a relative path based on the JSON file.
            std::string combined_path;
            if (!locate_library(filename, library_path, combined_path)) {
                error_ss << filename << " library " << combined_path << " does not appear to exist";
                LoaderLogger::LogErrorMessage("", error_ss.str());
                return;
            }
            library_path = combined_path;
        }
    }

    std::string description;
    if (!layer_root_node["description"].isNull() && layer_root_node["description"].isString()) {
        description = layer_root_node["description"].asString();
    }

    // Add this layer manifest file
    manifest_files.emplace_back(new ApiLayerManifestFile(type, filename, layer_name, description,
                                                         api_version, implementation_version,
                                                         library_path));

    // Add any extensions to it after the fact.
    manifest_files.back()->ParseCommon(layer_root_node);
}

bool ApiLayerManifestFile::LocateLibraryInAssets(const std::string& /* json_filename */,
                                                 const std::string& library_path,
                                                 std::string& out_combined_path) {
    std::string combined_path;
    std::string file_parent = GetAndroidNativeLibraryDir();
    if (!FileSysUtilsCombinePaths(file_parent, library_path, combined_path) ||
        !FileSysUtilsPathExists(combined_path)) {
        out_combined_path = combined_path;
        return false;
    }
    out_combined_path = combined_path;
    return true;
}

void ApiLayerManifestFile::PopulateApiLayerProperties(XrApiLayerProperties& props) const {
    props.layerVersion = _implementation_version;
    props.specVersion = XR_MAKE_VERSION(_api_version.major, _api_version.minor, _api_version.patch);
    strncpy(props.layerName, _layer_name.c_str(), XR_MAX_API_LAYER_NAME_SIZE - 1);
    if (_layer_name.size() >= XR_MAX_API_LAYER_NAME_SIZE - 1) {
        props.layerName[XR_MAX_API_LAYER_NAME_SIZE - 1] = '\0';
    }
    strncpy(props.description, _description.c_str(), XR_MAX_API_LAYER_DESCRIPTION_SIZE - 1);
    if (_description.size() >= XR_MAX_API_LAYER_DESCRIPTION_SIZE - 1) {
        props.description[XR_MAX_API_LAYER_DESCRIPTION_SIZE - 1] = '\0';
    }
}

// Find all layer manifest files in the appropriate search paths/registries for the given type.
XrResult ApiLayerManifestFile::FindManifestFiles(
        const std::string& openxr_command, ManifestFileType type,
        std::vector<std::unique_ptr<ApiLayerManifestFile>>& manifest_files) {
    ApiLayerManifestFile::AddManifestFilesAndroid(openxr_command, type, manifest_files);
    return XR_SUCCESS;
}
