// Copyright (c) 2017-2025 The Khronos Group Inc.
// Copyright (c) 2017 Valve Corporation
// Copyright (c) 2017 LunarG, Inc.
//
// SPDX-License-Identifier: Apache-2.0 OR MIT
//
// Initial Author: Mark Young <marky@lunarg.com>
//

#pragma once

#include <string>
#include <vector>

// Determine if the provided path exists on the filesystem
bool FileSysUtilsPathExists(const std::string& path);

// Determine if the provided path is an absolute path
bool FileSysUtilsIsAbsolutePath(const std::string& path);

// Combine a parent and child directory
bool FileSysUtilsCombinePaths(const std::string& parent, const std::string& child,
                              std::string& combined);
