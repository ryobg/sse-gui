/**
 * @file sse-hooks.cpp
 * @copybrief sse-hooks.h
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
 * Implements the public API.
 */

#include <sse-gui/sse-gui.h>
#include <utils/winutils.hpp>

#include <cstring>
#include <string>
#include <array>
#include <locale>
#include <algorithm>
#include <fstream>

#include <windows.h>

//--------------------------------------------------------------------------------------------------

using namespace std::string_literals;

/// Opened from within skse.cpp
extern std::ofstream& log ();

/// [shared] Supports SSGUI specific errors in a manner of #GetLastError() and #FormatMessage()
std::string ssgui_error;

//--------------------------------------------------------------------------------------------------

//--------------------------------------------------------------------------------------------------

//--------------------------------------------------------------------------------------------------

SSGUI_API void SSGUI_CCONV
ssgui_version (int* api, int* maj, int* imp, const char** build)
{
    constexpr std::array<int, 3> ver = {
#include "../VERSION"
    };
    if (api) *api = ver[0];
    if (maj) *maj = ver[1];
    if (imp) *imp = ver[2];
    if (build) *build = SSGUI_TIMESTAMP; //"2019-04-15T08:37:11.419416+00:00"
    static_assert (ver[0] == SSGUI_API_VERSION, "API in files VERSION and sse-hooks.h must match");
}

//--------------------------------------------------------------------------------------------------

SSGUI_API void SSGUI_CCONV
ssgui_last_error (size_t* size, char* message)
{
    if (ssgui_error.size ())
    {
        copy_string (ssgui_error, size, message);
        return;
    }

    auto err = ::GetLastError ();
    if (!err)
    {
        *size = 0;
        if (message) *message = 0;
        return;
    }

    copy_string (format_utf8message (err), size, message);
}

//--------------------------------------------------------------------------------------------------

/// Convert to std::string #ssgui_last_error(size_t*,char*)

std::string
ssgui_last_error ()
{
    std::size_t n;
    ssgui_last_error (&n, nullptr);
    std::string s (n, '\0');
    if (n) ssgui_last_error (&n, &s[0]);
    return s;
};

//--------------------------------------------------------------------------------------------------

SSGUI_API int SSGUI_CCONV
ssgui_execute (const char* command, void* arg)
{
    return false;
}

//--------------------------------------------------------------------------------------------------

SSGUI_API ssgui_api SSGUI_CCONV
ssgui_make_api ()
{
    ssgui_api api  = {};
    api.version    = ssgui_version;
    api.last_error = ssgui_last_error;
    api.execute    = ssgui_execute;
    return api;
}

//--------------------------------------------------------------------------------------------------

