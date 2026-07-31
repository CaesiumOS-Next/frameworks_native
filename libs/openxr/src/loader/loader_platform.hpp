// Copyright (c) 2017-2025 The Khronos Group Inc.
// Copyright (c) 2017-2019 Valve Corporation
// Copyright (c) 2017-2019 LunarG, Inc.
//
// SPDX-License-Identifier: Apache-2.0 OR MIT
//
// Initial Authors: Mark Young <marky@lunarg.com>, Dave Houlton <daveh@lunarg.com>
//

#pragma once

#include <cassert>
#include <string>

#if defined(__GNUC__) && __GNUC__ >= 4
#define LOADER_EXPORT __attribute__((visibility("default")))
#elif defined(__SUNPRO_C) && (__SUNPRO_C >= 0x590)
#define LOADER_EXPORT __attribute__((visibility("default")))
#else
#define LOADER_EXPORT
#endif

#include <android/dlext.h>
#include <dirent.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define PATH_SEPARATOR ':'
#define DIRECTORY_SYMBOL '/'

// Dynamic Loading of libraries:
typedef void* LoaderPlatformLibraryHandle;
static inline LoaderPlatformLibraryHandle LoaderPlatformLibraryOpen(
        const std::string& path, android_namespace_t* library_namespace) {
    // When loading the library, we use RTLD_LAZY so that not all symbols have to be
    // resolved at this time (which improves performance). Note that if not all symbols
    // can be resolved, this could cause crashes later.
    // For experimenting/debugging: Define the LD_BIND_NOW environment variable to force all
    // symbols to be resolved here.
    if (library_namespace) {
        const android_dlextinfo dlextinfo{
                .flags = ANDROID_DLEXT_USE_NAMESPACE,
                .library_namespace = library_namespace,
        };
        return android_dlopen_ext(path.c_str(), RTLD_LAZY | RTLD_LOCAL, &dlextinfo);
    } else {
        return dlopen(path.c_str(), RTLD_LAZY | RTLD_LOCAL);
    }
}

static inline const char* LoaderPlatformLibraryOpenError(const std::string& path) {
    (void)path;
    return dlerror();
}

static inline void LoaderPlatformLibraryClose(LoaderPlatformLibraryHandle library) {
    dlclose(library);
}

static inline void* LoaderPlatformLibraryGetProcAddr(LoaderPlatformLibraryHandle library,
                                                     const std::string& name) {
    assert(library);
    assert(!name.empty());
    return dlsym(library, name.c_str());
}

static inline const char* LoaderPlatformLibraryGetProcAddrError(const std::string& name) {
    (void)name;
    return dlerror();
}
