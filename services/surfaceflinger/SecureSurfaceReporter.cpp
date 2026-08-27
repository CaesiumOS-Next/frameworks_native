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

#define ATRACE_TAG ATRACE_TAG_GRAPHICS

#include "SecureSurfaceReporter.h"
#include <log/log.h>

namespace android {

void SecureSurfaceReporter::dispatchSecureSurfaceState(bool hasSecureSurface) {
    std::vector<sp<gui::ISecureSurfaceListener>> localListeners;
    {
        std::scoped_lock lock(mMutex);
        if (mLastReportedState == hasSecureSurface) {
            return;
        }
        mLastReportedState = hasSecureSurface;
        ALOGD("dispatchSecureSurfaceState: SecureSurface state toggled to %d, notifying %zu listeners", hasSecureSurface, mListeners.size());

        for (auto& [binder, trackedListener] : mListeners) {
            if (!trackedListener.lastState || *trackedListener.lastState != hasSecureSurface) {
                trackedListener.lastState = hasSecureSurface;
                localListeners.push_back(trackedListener.listener);
            }
        }
    }

    for (const auto& listener : localListeners) {
        listener->onSecureSurfaceChanged(hasSecureSurface);
    }
}

void SecureSurfaceReporter::binderDied(const wp<IBinder>& who) {
    std::scoped_lock lock(mMutex);
    mListeners.erase(who);
}

void SecureSurfaceReporter::addListener(const sp<gui::ISecureSurfaceListener>& listener) {
    sp<IBinder> asBinder = IInterface::asBinder(listener);
    asBinder->linkToDeath(sp<DeathRecipient>::fromExisting(this));
    bool lastState = false;
    {
        std::scoped_lock lock(mMutex);
        lastState = mLastReportedState;
        mListeners[wp<IBinder>(asBinder)] = {listener, lastState};
    }
    listener->onSecureSurfaceChanged(lastState);
}

void SecureSurfaceReporter::removeListener(const sp<gui::ISecureSurfaceListener>& listener) {
    std::scoped_lock lock(mMutex);
    mListeners.erase(wp<IBinder>(IInterface::asBinder(listener)));
}

} // namespace android
