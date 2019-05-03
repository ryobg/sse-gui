/**
 * @file platform.h
 * @brief Detect the current operating environment
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
 * @details
 * This file contains conditional macro definitions determining compile time
 * attributes according to the operating environment. As of now it detects
 * the intended operating system where the exectuable will run under, which
 * compiler is used for the build and API build helpers.
 */

#ifndef SSGUI_PLATFORM_H
#define SSGUI_PLATFORM_H

/*----------------------------------------------------------------------------*/
/* Select operating system: */

#undef SSGUI_WINDOWS
#undef SSGUI_POSIX

#if defined(_WIN32) \
    || defined(_WIN64) \
    || defined(__WIN32__) \
    || defined(__WINDOWS__) \
    || defined(__MINGW32__) \
    || defined(__MINGW64__)

/** Defined when targeting Microsoft Windows operating system */
#define SSGUI_WINDOWS

#else

/** Defined when NOT targeting Windows but POSIX compatible system */
#define SSGUI_POSIX

#endif

/*----------------------------------------------------------------------------*/
/* Select compiler: */

#undef SSGUI_GNUC
#undef SSGUI_MSVC
#undef SSGUI_MINGW

#if defined(__GNUC__)

/** Any GNU GCC C++ compiler */
#define SSGUI_GNUC \
    (__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__)

#if defined(__MINGW32__) || defined(__MINGW64__)
/** GNU GCC as cross compiler or native under Windows. */
#define SSGUI_MINGW SSGUI_GNUC
#endif

#elif defined(_MSC_VER) /* Last as other vendors also define this. */

/** Any Microsoft Visual Studio C++ compiler. */
#define SSGUI_MSVC (_MSC_VER)

#endif

/*----------------------------------------------------------------------------*/
/* Select the C calling convention: */

#undef SSGUI_CCONV

#if defined(SSGUI_WINDOWS) && !defined(SSGUI_MINGW)
#if defined(SSGUI_GNUC)
/** GCC on Windows understands stdcall */
#define SSGUI_CCONV __attribute__((stdcall))

#elif defined(SSGUI_MSVC)
/** Visual C++ on Windows uses stdcall */
#define SSGUI_CCONV __stdcall
#endif

#elif defined(SSGUI_POSIX) || defined(SSGUI_MINGW)
/** Linux/Unix/Cross and etc. use only one type of convention */
#define SSGUI_CCONV

#endif

/*----------------------------------------------------------------------------*/
/* Select the shared library interface */

#undef SSGUI_API

#if defined(SSGUI_WINDOWS)

/* In practice this is defined as paramater to the build. */
#if defined(SSGUI_BUILD_API)
/** The current build exposes DLL functions */
#define SSGUI_API __declspec(dllexport)

#else
/** The current build imports, previously exported DLL functions */
#define SSGUI_API __declspec(dllimport)
#endif

#elif defined(SSGUI_POSIX)
/** The current build does not use any specific storage information */
#define SSGUI_API

#endif

/*----------------------------------------------------------------------------*/

#endif

