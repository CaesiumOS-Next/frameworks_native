/*
 * Copyright 2026 The Android Open Source Project
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

//! Safe Rust bindings for `libtracing_perfetto`, the framework tracing facade that emits
//! trace events as Perfetto track events, falling back to the legacy atrace
//! (`trace_marker`) path when a concurrent atrace session needs the same category.
//!
//! Use this crate from platform Rust code that wants to emit the same framework trace
//! events as `android.os.Trace` does in Java and `tracing_perfetto.h` does in C++. Event
//! and track names are passed as [`&CStr`](std::ffi::CStr) so that hot trace calls forward
//! the pointer straight to the C++ implementation without allocating or copying.
//!
//! Call [`register_with_perfetto`] once during process startup; events emitted before
//! registration can only reach the legacy atrace path.
//!
//! ```
//! use tracing_perfetto::{register_with_perfetto, trace_begin, trace_end, Backend, Category};
//!
//! register_with_perfetto(Backend::System);
//!
//! trace_begin(Category::INPUT, c"processMotionEvent");
//! // ... the work being traced ...
//! trace_end(Category::INPUT);
//! ```

use std::ffi::CStr;
use std::ptr::NonNull;

pub use ffi::PerfettoTeCategory;

/// A framework trace category, used to filter which trace events are recorded.
///
/// The constants mirror the `TRACE_CATEGORY_*` bits in `trace_categories.h`, which are the
/// same bit values as the `ATRACE_TAG_*` constants in `cutils/trace.h` and the
/// `TRACE_TAG_*` constants in `android.os.Trace`.
#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub struct Category(u64);

impl Category {
    /// The `always` category, traced whenever tracing is enabled at all.
    pub const ALWAYS: Category = Category(1 << 0);
    /// The `gfx` category (graphics).
    pub const GRAPHICS: Category = Category(1 << 1);
    /// The `input` category.
    pub const INPUT: Category = Category(1 << 2);
    /// The `view` category (view system).
    pub const VIEW: Category = Category(1 << 3);
    /// The `webview` category.
    pub const WEBVIEW: Category = Category(1 << 4);
    /// The `wm` category (window manager).
    pub const WINDOW_MANAGER: Category = Category(1 << 5);
    /// The `am` category (activity manager).
    pub const ACTIVITY_MANAGER: Category = Category(1 << 6);
    /// The `sm` category (sync manager).
    pub const SYNC_MANAGER: Category = Category(1 << 7);
    /// The `audio` category.
    pub const AUDIO: Category = Category(1 << 8);
    /// The `video` category.
    pub const VIDEO: Category = Category(1 << 9);
    /// The `camera` category.
    pub const CAMERA: Category = Category(1 << 10);
    /// The `hal` category (hardware modules).
    pub const HAL: Category = Category(1 << 11);
    /// The `app` category (application-defined events).
    pub const APP: Category = Category(1 << 12);
    /// The `res` category (resource loading).
    pub const RESOURCES: Category = Category(1 << 13);
    /// The `dalvik` category (ART and Dalvik).
    pub const DALVIK: Category = Category(1 << 14);
    /// The `rs` category (RenderScript).
    pub const RS: Category = Category(1 << 15);
    /// The `bionic` category (Bionic C library).
    pub const BIONIC: Category = Category(1 << 16);
    /// The `power` category.
    pub const POWER: Category = Category(1 << 17);
    /// The `pm` category (package manager).
    pub const PACKAGE_MANAGER: Category = Category(1 << 18);
    /// The `ss` category (system server).
    pub const SYSTEM_SERVER: Category = Category(1 << 19);
    /// The `database` category.
    pub const DATABASE: Category = Category(1 << 20);
    /// The `network` category.
    pub const NETWORK: Category = Category(1 << 21);
    /// The `adb` category.
    pub const ADB: Category = Category(1 << 22);
    /// The `vibrator` category.
    pub const VIBRATOR: Category = Category(1 << 23);
    /// The `aidl` category (AIDL calls).
    pub const AIDL: Category = Category(1 << 24);
    /// The `nnapi` category (neural networks API).
    pub const NNAPI: Category = Category(1 << 25);
    /// The `rro` category (runtime resource overlay).
    pub const RRO: Category = Category(1 << 26);
    /// The `thermal` category.
    pub const THERMAL: Category = Category(1 << 27);

    /// Creates a category from its raw `TRACE_CATEGORY_*` bit value, for example one
    /// received from managed code that uses the `android.os.Trace` constants.
    ///
    /// `bits` should be a single category bit. A combination of several bits is honored
    /// only by the legacy atrace fallback; the Perfetto track-event path requires an exact
    /// single-category value and treats anything else as disabled.
    #[inline]
    pub const fn from_bits(bits: u64) -> Category {
        Category(bits)
    }

    /// Returns the raw `TRACE_CATEGORY_*` bit value of this category.
    #[inline]
    pub const fn bits(self) -> u64 {
        self.0
    }
}

/// Selects which Perfetto backend [`register_with_perfetto`] connects this process to.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Backend {
    /// The system-wide `traced` daemon. Production code should use this.
    System,
    /// A backend contained entirely within this process, letting tests collect trace
    /// events without a system tracing daemon.
    InProcess,
}

/// Registers this process with Perfetto as a producer of the framework track-event
/// categories.
///
/// Call this once during process startup. Only the first call in a process has an effect;
/// subsequent calls, with any backend, are ignored. Until a process registers, its trace
/// events can still reach the legacy atrace path but never Perfetto.
pub fn register_with_perfetto(backend: Backend) {
    ffi::register_with_perfetto(backend == Backend::InProcess);
}

/// Begins a synchronous trace section named `name` on the current thread.
///
/// Sections nest: each call must be matched by a later [`trace_end`] with the same
/// category on the same thread.
#[inline]
pub fn trace_begin(category: Category, name: &CStr) {
    // SAFETY: `name` is a valid NUL-terminated string that outlives the call, and the C++
    // implementation copies it into the trace buffer before returning rather than
    // retaining the pointer.
    unsafe { ffi::trace_begin(category.bits(), name.as_ptr()) }
}

/// Ends the innermost trace section begun by [`trace_begin`] on the current thread.
#[inline]
pub fn trace_end(category: Category) {
    ffi::trace_end(category.bits());
}

/// Begins an asynchronous trace section named `name`.
///
/// Asynchronous sections do not nest and may end on a different thread. A section is
/// identified by its category, name, and `cookie`, all of which a later
/// [`trace_async_end`] must repeat exactly.
#[inline]
pub fn trace_async_begin(category: Category, name: &CStr, cookie: i32) {
    // SAFETY: `name` is a valid NUL-terminated string that outlives the call, and the C++
    // implementation copies it into the trace buffer before returning rather than
    // retaining the pointer.
    unsafe { ffi::trace_async_begin(category.bits(), name.as_ptr(), cookie) }
}

/// Ends the asynchronous trace section identified by `category`, `name`, and `cookie`.
#[inline]
pub fn trace_async_end(category: Category, name: &CStr, cookie: i32) {
    // SAFETY: `name` is a valid NUL-terminated string that outlives the call, and the C++
    // implementation copies it into the trace buffer before returning rather than
    // retaining the pointer.
    unsafe { ffi::trace_async_end(category.bits(), name.as_ptr(), cookie) }
}

/// Begins an asynchronous trace section named `name` on the track named `track_name`.
///
/// The `cookie` distinguishes concurrent sections that share a track name and must be
/// repeated by the matching [`trace_async_end_for_track`].
#[inline]
pub fn trace_async_begin_for_track(
    category: Category,
    name: &CStr,
    track_name: &CStr,
    cookie: i32,
) {
    // SAFETY: `name` and `track_name` are valid NUL-terminated strings that outlive the
    // call, and the C++ implementation copies them into the trace buffer before returning
    // rather than retaining the pointers.
    unsafe {
        ffi::trace_async_begin_for_track(
            category.bits(),
            name.as_ptr(),
            track_name.as_ptr(),
            cookie,
        )
    }
}

/// Ends the asynchronous trace section on the track named `track_name` identified by
/// `cookie`.
#[inline]
pub fn trace_async_end_for_track(category: Category, track_name: &CStr, cookie: i32) {
    // SAFETY: `track_name` is a valid NUL-terminated string that outlives the call, and
    // the C++ implementation copies it into the trace buffer before returning rather than
    // retaining the pointer.
    unsafe { ffi::trace_async_end_for_track(category.bits(), track_name.as_ptr(), cookie) }
}

/// Emits an instantaneous trace event named `name`.
#[inline]
pub fn trace_instant(category: Category, name: &CStr) {
    // SAFETY: `name` is a valid NUL-terminated string that outlives the call, and the C++
    // implementation copies it into the trace buffer before returning rather than
    // retaining the pointer.
    unsafe { ffi::trace_instant(category.bits(), name.as_ptr()) }
}

/// Emits an instantaneous trace event named `name` on the track named `track_name`.
#[inline]
pub fn trace_instant_for_track(category: Category, track_name: &CStr, name: &CStr) {
    // SAFETY: `track_name` and `name` are valid NUL-terminated strings that outlive the
    // call, and the C++ implementation copies them into the trace buffer before returning
    // rather than retaining the pointers.
    unsafe { ffi::trace_instant_for_track(category.bits(), track_name.as_ptr(), name.as_ptr()) }
}

/// Records `value` for the process-scoped counter named `name`.
#[inline]
pub fn trace_counter(category: Category, name: &CStr, value: i64) {
    // SAFETY: `name` is a valid NUL-terminated string that outlives the call, and the C++
    // implementation copies it into the trace buffer before returning rather than
    // retaining the pointer.
    unsafe { ffi::trace_counter(category.bits(), name.as_ptr(), value) }
}

/// Records `value` for the process-scoped counter named `name`, taking a 32-bit value.
///
/// This is the 32-bit variant of [`trace_counter`]; the value is widened to 64 bits on the
/// Perfetto path.
#[inline]
pub fn trace_counter32(category: Category, name: &CStr, value: i32) {
    // SAFETY: `name` is a valid NUL-terminated string that outlives the call, and the C++
    // implementation copies it into the trace buffer before returning rather than
    // retaining the pointer.
    unsafe { ffi::trace_counter32(category.bits(), name.as_ptr(), value) }
}

/// Returns whether `category` is traced right now, either by an active Perfetto session or
/// by the legacy atrace machinery.
///
/// Use this to skip computing expensive trace arguments while tracing is off.
#[inline]
pub fn is_tag_enabled(category: Category) -> bool {
    ffi::is_tag_enabled(category.bits())
}

/// Returns the underlying Perfetto category handle for `category`, for interop with the
/// Perfetto C API (`libperfetto_c`).
///
/// Returns `None` while the category is not enabled by an active tracing session, which
/// includes any time before [`register_with_perfetto`] has run.
#[inline]
pub fn perfetto_category(category: Category) -> Option<NonNull<PerfettoTeCategory>> {
    NonNull::new(ffi::get_perfetto_category(category.bits()))
}

#[cxx::bridge]
mod ffi {
    unsafe extern "C++" {
        include!("tracing_perfetto.h");

        /// An opaque Perfetto track-event category as defined by the Perfetto client API
        /// (`libperfetto_c`). Only ever handled behind a pointer.
        type PerfettoTeCategory;
    }

    #[namespace = "tracing_perfetto"]
    unsafe extern "C++" {
        #[cxx_name = "registerWithPerfetto"]
        fn register_with_perfetto(test: bool);

        #[cxx_name = "traceBegin"]
        unsafe fn trace_begin(category: u64, name: *const c_char);

        #[cxx_name = "traceEnd"]
        fn trace_end(category: u64);

        #[cxx_name = "traceAsyncBegin"]
        unsafe fn trace_async_begin(category: u64, name: *const c_char, cookie: i32);

        #[cxx_name = "traceAsyncEnd"]
        unsafe fn trace_async_end(category: u64, name: *const c_char, cookie: i32);

        #[cxx_name = "traceAsyncBeginForTrack"]
        unsafe fn trace_async_begin_for_track(
            category: u64,
            name: *const c_char,
            track_name: *const c_char,
            cookie: i32,
        );

        #[cxx_name = "traceAsyncEndForTrack"]
        unsafe fn trace_async_end_for_track(category: u64, track_name: *const c_char, cookie: i32);

        #[cxx_name = "traceInstant"]
        unsafe fn trace_instant(category: u64, name: *const c_char);

        #[cxx_name = "traceInstantForTrack"]
        unsafe fn trace_instant_for_track(
            category: u64,
            track_name: *const c_char,
            name: *const c_char,
        );

        #[cxx_name = "traceCounter"]
        unsafe fn trace_counter(category: u64, name: *const c_char, value: i64);

        #[cxx_name = "traceCounter32"]
        unsafe fn trace_counter32(category: u64, name: *const c_char, value: i32);

        #[cxx_name = "isTagEnabled"]
        fn is_tag_enabled(category: u64) -> bool;

        #[cxx_name = "getPerfettoCategory"]
        fn get_perfetto_category(category: u64) -> *mut PerfettoTeCategory;
    }
}

// End-to-end trace collection cannot be tested here: enabling a category requires starting
// a Perfetto tracing session through the Perfetto C API, which has no Rust bindings. The
// C++ gtest in ../tests covers that path; these tests exercise the bridge and the
// documented behavior that holds without an active session.
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn category_bits_match_trace_categories_header() {
        assert_eq!(Category::ALWAYS.bits(), 1 << 0);
        assert_eq!(Category::GRAPHICS.bits(), 1 << 1);
        assert_eq!(Category::INPUT.bits(), 1 << 2);
        assert_eq!(Category::VIEW.bits(), 1 << 3);
        assert_eq!(Category::WEBVIEW.bits(), 1 << 4);
        assert_eq!(Category::WINDOW_MANAGER.bits(), 1 << 5);
        assert_eq!(Category::ACTIVITY_MANAGER.bits(), 1 << 6);
        assert_eq!(Category::SYNC_MANAGER.bits(), 1 << 7);
        assert_eq!(Category::AUDIO.bits(), 1 << 8);
        assert_eq!(Category::VIDEO.bits(), 1 << 9);
        assert_eq!(Category::CAMERA.bits(), 1 << 10);
        assert_eq!(Category::HAL.bits(), 1 << 11);
        assert_eq!(Category::APP.bits(), 1 << 12);
        assert_eq!(Category::RESOURCES.bits(), 1 << 13);
        assert_eq!(Category::DALVIK.bits(), 1 << 14);
        assert_eq!(Category::RS.bits(), 1 << 15);
        assert_eq!(Category::BIONIC.bits(), 1 << 16);
        assert_eq!(Category::POWER.bits(), 1 << 17);
        assert_eq!(Category::PACKAGE_MANAGER.bits(), 1 << 18);
        assert_eq!(Category::SYSTEM_SERVER.bits(), 1 << 19);
        assert_eq!(Category::DATABASE.bits(), 1 << 20);
        assert_eq!(Category::NETWORK.bits(), 1 << 21);
        assert_eq!(Category::ADB.bits(), 1 << 22);
        assert_eq!(Category::VIBRATOR.bits(), 1 << 23);
        assert_eq!(Category::AIDL.bits(), 1 << 24);
        assert_eq!(Category::NNAPI.bits(), 1 << 25);
        assert_eq!(Category::RRO.bits(), 1 << 26);
        assert_eq!(Category::THERMAL.bits(), 1 << 27);
    }

    #[test]
    fn category_round_trips_through_raw_bits() {
        let category = Category::from_bits(1 << 5);
        assert_eq!(category, Category::WINDOW_MANAGER);
        assert_eq!(category.bits(), 1 << 5);
    }

    #[test]
    fn trace_events_without_session_are_noops() {
        trace_begin(Category::INPUT, c"section");
        trace_end(Category::INPUT);
        trace_async_begin(Category::INPUT, c"async", 1);
        trace_async_end(Category::INPUT, c"async", 1);
        trace_async_begin_for_track(Category::INPUT, c"async", c"track", 2);
        trace_async_end_for_track(Category::INPUT, c"track", 2);
        trace_instant(Category::INPUT, c"instant");
        trace_instant_for_track(Category::INPUT, c"track", c"instant");
        trace_counter(Category::INPUT, c"counter", i64::MAX);
        trace_counter32(Category::INPUT, c"counter32", -1);
        let _enabled = is_tag_enabled(Category::INPUT);
    }

    #[test]
    fn perfetto_category_is_none_without_active_session() {
        // Nothing in this test process ever starts a tracing session, so no category can
        // become enabled, regardless of test ordering.
        assert!(perfetto_category(Category::GRAPHICS).is_none());
    }

    #[test]
    fn registration_is_idempotent() {
        register_with_perfetto(Backend::InProcess);
        register_with_perfetto(Backend::InProcess);
        trace_instant(Category::SYSTEM_SERVER, c"after-registration");
    }
}
