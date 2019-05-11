/**
 * @file platform.h
 * @brief Detect the current operating environment
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
 * @details
 * This file contains conditional macro definitions determining compile time
 * attributes according to the operating environment. As of now it detects
 * the intended operating system where the exectuable will run under, which
 * compiler is used for the build and API build helpers.
 */

#ifndef SSEGUI_PLATFORM_H
#define SSEGUI_PLATFORM_H

/*----------------------------------------------------------------------------*/
/* Select operating system: */

#undef SSEGUI_WINDOWS
#undef SSEGUI_POSIX

#if defined(_WIN32) \
    || defined(_WIN64) \
    || defined(__WIN32__) \
    || defined(__WINDOWS__) \
    || defined(__MINGW32__) \
    || defined(__MINGW64__)

/** Defined when targeting Microsoft Windows operating system */
#define SSEGUI_WINDOWS

#else

/** Defined when NOT targeting Windows but POSIX compatible system */
#define SSEGUI_POSIX

#endif

/*----------------------------------------------------------------------------*/
/* Select compiler: */

#undef SSEGUI_GNUC
#undef SSEGUI_MSVC
#undef SSEGUI_MINGW

#if defined(__GNUC__)

/** Any GNU GCC C++ compiler */
#define SSEGUI_GNUC \
    (__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__)

#if defined(__MINGW32__) || defined(__MINGW64__)
/** GNU GCC as cross compiler or native under Windows. */
#define SSEGUI_MINGW SSEGUI_GNUC
#endif

#elif defined(_MSC_VER) /* Last as other vendors also define this. */

/** Any Microsoft Visual Studio C++ compiler. */
#define SSEGUI_MSVC (_MSC_VER)

#endif

/*----------------------------------------------------------------------------*/
/* Select the C calling convention: */

#undef SSEGUI_CCONV

#if defined(SSEGUI_WINDOWS) && !defined(SSEGUI_MINGW)
#if defined(SSEGUI_GNUC)
/** GCC on Windows understands stdcall */
#define SSEGUI_CCONV __attribute__((stdcall))

#elif defined(SSEGUI_MSVC)
/** Visual C++ on Windows uses stdcall */
#define SSEGUI_CCONV __stdcall
#endif

#elif defined(SSEGUI_POSIX) || defined(SSEGUI_MINGW)
/** Linux/Unix/Cross and etc. use only one type of convention */
#define SSEGUI_CCONV

#endif

/*----------------------------------------------------------------------------*/
/* Select the shared library interface */

#undef SSEGUI_API

#if defined(SSEGUI_WINDOWS)

/* In practice this is defined as paramater to the build. */
#if defined(SSEGUI_BUILD_API)
/** The current build exposes DLL functions */
#define SSEGUI_API __declspec(dllexport)

#else
/** The current build imports, previously exported DLL functions */
#define SSEGUI_API __declspec(dllimport)
#endif

#elif defined(SSEGUI_POSIX)
/** The current build does not use any specific storage information */
#define SSEGUI_API

#endif

/*----------------------------------------------------------------------------*/

#endif

