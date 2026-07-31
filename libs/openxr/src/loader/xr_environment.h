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

#pragma once

#include <optional>
#include <string>

#include <android/dlext.h>

namespace android {

/**
 * Active runtime package configuration.
 */
struct XrRuntimeEnvironment {
    // The name of the selected runtime package.
    std::string package_name;

    // The absolute path to the entrypoint of the runtime library.
    std::string runtime_soname;

    // The linker namespace in which to load native libraries.
    android_namespace_t* library_namespace;

    std::string GetFunctionName(const std::string& func_name) const { return func_name; }
};

/**
 * Performs Android OpenXR runtime discovery.
 *
 * @return The resolved runtime environment properties, or std::nullopt on failure.
 */
std::optional<XrRuntimeEnvironment> GetXrRuntimeEnvironment();

} // namespace android
