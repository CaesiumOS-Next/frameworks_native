// Copyright (c) 2017-2025 The Khronos Group Inc.
// Copyright (c) 2017-2019 Valve Corporation
// Copyright (c) 2017-2019 LunarG, Inc.
//
// SPDX-License-Identifier: Apache-2.0 OR MIT
//
// Initial Author: Rylie Pavlik <rylie.pavlik@collabora.com>
//

#pragma once

#include "loader_logger.hpp"

#include <openxr/openxr.h>

#include <memory>

//! Android liblog ("logcat") logger
std::unique_ptr<LoaderLogRecorder> MakeLogcatLoaderLogRecorder();

// Debug Utils logger used with XR_EXT_debug_utils
std::unique_ptr<LoaderLogRecorder> MakeDebugUtilsLoaderLogRecorder(
        const XrDebugUtilsMessengerCreateInfoEXT* create_info,
        XrDebugUtilsMessengerEXT debug_messenger);

// TODO: Add other Derived classes:
//  - FileLoaderLogRecorder     - During/after xrCreateInstance
//  - PipeLoaderLogRecorder?    - During/after xrCreateInstance
