// Copyright (c) 2017-2025 The Khronos Group Inc.
// Copyright (c) 2017 Valve Corporation
// Copyright (c) 2017 LunarG, Inc.
//
// SPDX-License-Identifier: Apache-2.0 OR MIT
//
// Initial Authors: Mark Young <marky@lunarg.com>
//                  Nat Brown <natb@valvesoftware.com>
//

#include "filesystem_utils.hpp"

#include <unistd.h>
#include <cstring>
#include <string>

#define PATH_SEPARATOR ':'
#define DIRECTORY_SYMBOL '/'

// simple POSIX-compatible implementation of the <filesystem> pieces used by OpenXR

bool FileSysUtilsPathExists(const std::string& path) {
    return (access(path.c_str(), F_OK) != -1);
}

bool FileSysUtilsIsAbsolutePath(const std::string& path) {
    return (path[0] == DIRECTORY_SYMBOL);
}

bool FileSysUtilsCombinePaths(const std::string& parent, const std::string& child,
                              std::string& combined) {
    std::string::size_type parent_len = parent.length();
    if (0 == parent_len || "." == parent || "./" == parent) {
        combined = child;
        return true;
    }
    char last_char = parent[parent_len - 1];
    if (last_char == DIRECTORY_SYMBOL) {
        parent_len--;
    }
    combined = parent.substr(0, parent_len) + DIRECTORY_SYMBOL + child;
    return true;
}
