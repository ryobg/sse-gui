/**
 * @file sse-hooks.h
 * @brief Public C API for users of SSE Hooks
 * @internal
 *
 * This file is part of SSE Hooks project (aka SSGUI).
 *
 *   SSGUI is free software: you can redistribute it and/or modify it
 *   under the terms of the GNU Lesser General Public License as published
 *   by the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   SSGUI is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *   GNU Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public
 *   License along with SSGUI. If not, see <http://www.gnu.org/licenses/>.
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
 * SSGUI. The interface targets to be maximum compatible and portable
 * across the expected build and usage scenarios. This file uses generic C, but
 * is compatible with C++. As the methods are to be exported in the DLL, this
 * lib interface can be also accessed from other languages too.
 */

#ifndef SSGUI_SSEGUI_H
#define SSGUI_SSEGUI_H

#include <stdint.h>
#include <sse-gui/platform.h>

/// To match a compiled in API against one loaded at run-time.
#define SSGUI_API_VERSION (1)

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/

/**
 * Run-time version of this API and its implementation details.
 *
 * This function can be used to detect in run-time what kind of feature & fixes
 * on the API, the loaded SSGUI is compiled with. This function is the only one,
 * which is guaranteed to preseve through the whole lifecycle of this product.
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
 * It is advised to check @param api against #SSGUI_API_VERSION.
 *
 * @param[out] api (optional) non-portable
 * @param[out] maj (optional) new features and enhancements
 * @param[out] imp (optional) patches
 * @param[out] timestamp (optional) in ISO format
 */

SSGUI_API void SSGUI_CCONV
ssgui_version (int* api, int* maj, int* imp, const char** timestamp);

/** @see #ssgui_version() */

typedef void (SSGUI_CCONV* ssgui_version_t) (int*, int*, int*, const char**);

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

SSGUI_API void SSGUI_CCONV
ssgui_last_error (size_t* size, char* message);

/** @see #ssgui_last_error() */

typedef void (SSGUI_CCONV* ssgui_last_error_t) (size_t*, char*);

/******************************************************************************/

/**
 * Execute custom command.
 *
 * This is highly implementation specific and may change any moment. It is like
 * patch hole for development use.
 *
 * @param[in] command identifier
 * @param[in,out] arg to pass in or out data
 * @returns non-zero on success, otherwise see #ssgui_last_error ()
 */

SSGUI_API int SSGUI_CCONV
ssgui_execute (const char* command, void* arg);

/** @see #ssgui_execute() */

typedef int (SSGUI_CCONV* ssgui_execute_t) (const char*, void*);

/******************************************************************************/

/**
 * Set of function pointers as found in this file.
 *
 * Compatible changes are function pointers appened to the end of this
 * structure.
 */

struct ssgui_api_v1
{
	/** @see #ssgui_version() */
	ssgui_version_t version;
	/** @see #ssgui_last_error() */
	ssgui_last_error_t last_error;
	/** @see #ssgui_execute() */
	ssgui_execute_t execute;
};

/** Points to the current API version in use. */
typedef struct ssgui_api_v1 ssgui_api;

/******************************************************************************/

/**
 * Create an instance of #ssgui_api, ready for use.
 *
 * @returns an API
 */

SSGUI_API ssgui_api SSGUI_CCONV
ssgui_make_api ();

/** @see #ssgui_make_api() */

typedef ssgui_api (SSGUI_CCONV* ssgui_make_api_t) ();

/******************************************************************************/

#ifdef __cplusplus
}
#endif

#endif /* SSGUI_SSEGUI_H */

/* EOF */

