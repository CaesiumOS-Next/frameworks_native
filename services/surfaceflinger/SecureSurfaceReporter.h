/*
 * Copyright 2024 The Android Open Source Project
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

#include <android-base/thread_annotations.h>
#include <android/gui/ISecureSurfaceListener.h>
#include <binder/IBinder.h>

#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

#include "WpHash.h"

namespace android {

class SecureSurfaceReporter final : public IBinder::DeathRecipient {
public:
    SecureSurfaceReporter() = default;
    ~SecureSurfaceReporter() final = default;

    void dispatchSecureSurfaceState(bool hasSecureSurface) EXCLUDES(mMutex);

    void binderDied(const wp<IBinder>& who) override EXCLUDES(mMutex);

    void addListener(const sp<gui::ISecureSurfaceListener>& listener) EXCLUDES(mMutex);
    void removeListener(const sp<gui::ISecureSurfaceListener>& listener) EXCLUDES(mMutex);

    bool hasListeners() const EXCLUDES(mMutex) {
        std::scoped_lock lock(mMutex);
        return !mListeners.empty();
    }

    bool getLastReportedState() const EXCLUDES(mMutex) {
        std::scoped_lock lock(mMutex);
        return mLastReportedState;
    }

private:
    mutable std::mutex mMutex;

    struct TrackedListener {
        sp<gui::ISecureSurfaceListener> listener;
        std::optional<bool> lastState;
    };

    std::unordered_map<wp<IBinder>, TrackedListener, WpHash> mListeners GUARDED_BY(mMutex);
    bool mLastReportedState GUARDED_BY(mMutex) = false;
};

} // namespace android
