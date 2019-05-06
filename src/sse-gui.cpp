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

static_assert (std::is_same<std::wstring::value_type, TCHAR>::value, "Not an _UNICODE build.");

/// Safe convert from UTF-16 (Windows) encoding to UTF-8 (Skyrim).

static bool
utf16_to_utf8 (wchar_t const* wide, std::string& out)
{
    ssgui_error.clear ();
    if (!wide) return true;
    int wide_size = static_cast<int> (std::wcslen (wide));
    if (wide_size < 1) return true;
    int sz = ::WideCharToMultiByte (CP_UTF8, 0, wide, wide_size, NULL, 0, NULL, NULL);
    if (sz < 1) return false;
    out.resize (sz, 0);
    ::WideCharToMultiByte (CP_UTF8, 0, wide, wide_size, &out[0], sz, NULL, NULL);
    return true;
}

//--------------------------------------------------------------------------------------------------

/// Helper function to upload to API callers a managed range of bytes

static void
copy_string (std::string const& src, std::size_t* n, char* dst)
{
    if (!n)
        return;
    if (dst)
    {
        if (*n > 0)
            *std::copy_n (src.cbegin (), std::min (*n-1, src.size ()), dst) = '\0';
        else *dst = 0;
    }
    *n = src.size () + 1;
}

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

    LPTSTR buff = nullptr;
    FormatMessage (
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, err, MAKELANGID (LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPTSTR) &buff, 0, nullptr);

    std::string m;
    if (!utf16_to_utf8 (buff, m))
    {
        ::LocalFree (buff);
        return;
    }
    ::LocalFree (buff);

    copy_string (m, size, message);
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

