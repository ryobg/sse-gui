/**
 * @file sse-hooks.h
 * @brief Public C API for users of SSE Hooks
 * @internal
 *
 * This file is part of SSE Hooks project (aka SSEGUI).
 *
 *   SSEGUI is free software: you can redistribute it and/or modify it
 *   under the terms of the GNU Lesser General Public License as published
 *   by the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   SSEGUI is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *   GNU Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public
 *   License along with SSEGUI. If not, see <http://www.gnu.org/licenses/>.
 *
 * @endinternal
 *
 * @ingroup Public API
 *
 * @note This API is not thread-safe.
 * @note Unless mentioned, all strings are null-terminated and in UTF-8.
 *
 * @details
 * This file encompass all the functions which are presented to the users of
 * SSEGUI. The interface targets to be maximum compatible and portable
 * across the expected build and usage scenarios. This file uses generic C, but
 * is compatible with C++. As the methods are to be exported in the DLL, this
 * lib interface can be also accessed from other languages too.
 */

#ifndef SSEGUI_SSEGUI_H
#define SSEGUI_SSEGUI_H

#include <stdint.h>
#include <sse-gui/platform.h>

/// To match a compiled in API against one loaded at run-time.
#define SSEGUI_API_VERSION (1)

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/

/**
 * Run-time version of this API and its implementation details.
 *
 * This function can be used to detect in run-time what kind of feature & fixes
 * on the API, the loaded SSEGUI is compiled with. This function is the only
 * one, which is guaranteed to preseve through the whole lifecycle of this
 * product.
 *
 * The @param api tells what version is the current API. Any version different
 * than the one expected guarantees a broken interface. Most likely it will
 * mean that a function is missing or its prototype is different.
 *
 * The @param maj describes major, but compatible changes within the API. Maybe
 * a new function is added or the behaviour of an old one was extended in
 * compatible way i.e. it won't break the callee.
 *
 * The @param imp is an implementation detail, in most cases may not be of
 * interest. It is reserved for patches, bug fixes, maybe documentation updates
 * re-release of something and etc.
 *
 * It is advised to check @param api against #SSEGUI_API_VERSION.
 *
 * @param[out] api (optional) non-portable
 * @param[out] maj (optional) new features and enhancements
 * @param[out] imp (optional) patches
 * @param[out] timestamp (optional) in ISO format
 */

SSEGUI_API void SSEGUI_CCONV
ssegui_version (int* api, int* maj, int* imp, const char** timestamp);

/** @see #ssegui_version() */

typedef void (SSEGUI_CCONV* ssegui_version_t) (int*, int*, int*, const char**);

/******************************************************************************/

/**
 * Report the last message in more human-readable form.
 *
 * @param[in,out] size in bytes of @param message, on exit how many bytes were
 * actually written (excluding the terminating null) or how many bytes are
 * needed in order to get the full message. Can be zero, if there is no error.
 *
 * @param[out] message in human readable form, can be nullptr if @param size is
 * needed to pre-allocate a buffer.
 */

SSEGUI_API void SSEGUI_CCONV
ssegui_last_error (size_t* size, char* message);

/** @see #ssegui_last_error() */

typedef void (SSEGUI_CCONV* ssegui_last_error_t) (size_t*, char*);

/******************************************************************************/

/**
 * Enable (default) the DInput for the hooked application.
 *
 * By default the control key, toggles the keyboard and mouse state, but this
 * function can be used for explicit requests (though the key will overwride
 * them if pressed).
 *
 * When the input is enabled, the hooked appliation will receive data, if
 * disabled - the keyboard and/or the mouse will freeze for the game. The
 * Windows message loop though will continue receive messages, so SSE GUI
 * plugins can continue reacting.
 *
 * @param[in,out] keyboard to enable (positive), disable (zero) or only report
 *  state for (negative). On exit it will contain the old value (positive/zero)
 *  or the current (negative) value.
 * @param[in,out] mouse see @param keyboard
 */

SSEGUI_API void SSEGUI_CCONV
ssegui_enable_input (int* keyboard, int* mouse);

/** @see #ssegui_enable_input() */

typedef void (SSEGUI_CCONV* ssegui_enable_input_t) (int*, int*);

/**
 * Change the SSE GUI input control key.
 *
 * The control key is one of the DInput DIK_* constants (scan code) used to
 * toggle the input device on or off for the game. These codes are the same used
 * by SKSE and the KeyPressed(...) and etc. functions available for Papyrus.
 *
 * @see https://docs.microsoft.com/en-us/previous-versions/windows/desktop/ee418641(v=vs.85)
 * @param[in,out] dik constant to be used from now on, if the param is negative
 *  or out of bounds (>255) it won't change the constant. On exit it will
 *  contain the previous, or the current (if not changed) key used.
 */

SSEGUI_API void SSEGUI_CCONV
ssegui_control_key (int* dik);

/** @see #ssegui_control_key() */

typedef void (SSEGUI_CCONV* ssegui_control_key_t) (int*);

/******************************************************************************/

/// @see https://docs.microsoft.com/en-us/windows/desktop/api/dxgi/nf-dxgi-idxgiswapchain-present

typedef void (SSEGUI_CCONV* ssegui_render_callback)
    (void* pSwapChain, unsigned SyncInterval, unsigned Flags);

/**
 * Register or remove a render listener
 *
 * These functions are invoked on each frame rendering, so plug ins can render
 * elements above or something. Note that, less and faster is better, or the
 * FPS can suffer.
 *
 * @param[in] callback to call or @param remove
 * @param[in] remove if positive, append if zero.
 */

SSEGUI_API void SSEGUI_CCONV
ssegui_render_listener (ssegui_render_callback callback, int remove);

/** @see #ssegui_render_listener() */

typedef void (SSEGUI_CCONV* ssegui_render_listener_t)
    (ssegui_render_callback, int);

/******************************************************************************/

/** @see https://msdn.microsoft.com/en-us/library/windows/desktop/ms633573(v=vs.85).aspx */

typedef intptr_t (SSEGUI_CCONV* ssegui_message_callback)
    (void* hWnd, unsigned msg, uintptr_t wParam, intptr_t lParam);

/**
 * Register or remove a windows message listener
 *
 * The callback is called on each received window message, before forwarding to
 * the rest of the subclass chain. It is somehow easy to install such hook
 * through ::FindWindow() and ::SetWindowLongPtr(), but this is exposed as
 * complement to the rendering.
 *
 * @param[in] callback to call or @param remove
 * @param[in] remove if positive, append if zero.
 */

SSEGUI_API void SSEGUI_CCONV
ssegui_message_listener (ssegui_message_callback callback, int remove);

/** @see #ssegui_message_listener() */

typedef void (SSEGUI_CCONV* ssegui_message_listener_t)
    (ssegui_message_callback, int);

/******************************************************************************/

/**
 * Read a parameter value
 *
 * This function is useful to retrieve some of the stored values which may be
 * of an interest, like the D11 chain pointer or the render window address.
 *
 * Current supported parameters (@param name, @param value type):
 * * "ID3D11Device", ID3D11Device**
 * * "ID3D11DeviceContext", ID3D11Device**
 * * "IDXGISwapChain", ID3D11Device**
 * * "window", HWND*
 *
 * @param[in] name of the parameter to obtain value for
 * @param[out] value to store in
 * @return non-zero if found, zero if no such parameter can be obtained
 */

SSEGUI_API int SSEGUI_CCONV
ssegui_parameter (const char* name, void* value);

/** @see #ssegui_parameter() */

typedef int (SSEGUI_CCONV* ssegui_parameter_t) (const char*, void*);

/******************************************************************************/

/**
 * Execute custom command.
 *
 * This is highly implementation specific and may change any moment. It is like
 * patch hole for development use.
 *
 * @param[in] command identifier
 * @param[in,out] arg to pass in or out data
 * @returns non-zero on success, otherwise see #ssegui_last_error ()
 */

SSEGUI_API int SSEGUI_CCONV
ssegui_execute (const char* command, void* arg);

/** @see #ssegui_execute() */

typedef int (SSEGUI_CCONV* ssegui_execute_t) (const char*, void*);

/******************************************************************************/

/**
 * Set of function pointers as found in this file.
 *
 * Compatible changes are function pointers appened to the end of this
 * structure.
 */

struct ssegui_api_v1
{
    /** @see #ssegui_version() */
    ssegui_version_t version;
    /** @see #ssegui_last_error() */
    ssegui_last_error_t last_error;
    /** @see #ssegui_enable_input() */
    ssegui_enable_input_t enable_input;
    /** @see #ssegui_control_key() */
    ssegui_control_key_t control_key;
    /** @see #ssegui_render_listener() */
    ssegui_render_listener_t render_listener;
    /** @see #ssegui_message_listener() */
    ssegui_message_listener_t message_listener;
    /** @see #ssegui_parameter() */
    ssegui_parameter_t parameter;
    /** @see #ssegui_execute() */
    ssegui_execute_t execute;
};

/** Points to the current API version in use. */
typedef struct ssegui_api_v1 ssegui_api;

/******************************************************************************/

/**
 * Create an instance of #ssegui_api, ready for use.
 *
 * @returns an API
 */

SSEGUI_API ssegui_api SSEGUI_CCONV
ssegui_make_api ();

/** @see #ssegui_make_api() */

typedef ssegui_api (SSEGUI_CCONV* ssegui_make_api_t) ();

/******************************************************************************/

#ifdef __cplusplus
}
#endif

#endif /* SSEGUI_SSEGUI_H */

/* EOF */

