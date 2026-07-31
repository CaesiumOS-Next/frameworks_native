// Copyright (c) 2017-2025 The Khronos Group Inc.
//
// SPDX-License-Identifier: Apache-2.0 OR MIT
#pragma once

#include <android/asset_manager.h>
#include <memory>

namespace deleters {
struct AAssetDeleter {
    void operator()(AAsset* asset) const noexcept {
        if (asset != nullptr) {
            AAsset_close(asset);
        }
    }
};

struct AAssetDirDeleter {
    void operator()(AAssetDir* dir) const noexcept {
        if (dir != nullptr) {
            AAssetDir_close(dir);
        }
    }
};

} // namespace deleters

using UniqueAsset = std::unique_ptr<AAsset, deleters::AAssetDeleter>;
using UniqueAssetDir = std::unique_ptr<AAssetDir, deleters::AAssetDirDeleter>;
