/**
 * @file sse-hooks.cpp
 * @copybrief sse-hooks.h
 * @internal
 *
 * This file is part of Skyrim SE GUI mod (aka SSEGUI).
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

/// [shared] Supports SSEGUI specific errors in a manner of #GetLastError() and #FormatMessage()
std::string ssegui_error;

//--------------------------------------------------------------------------------------------------

/// Convert to std::string #ssegui_last_error(size_t*,char*)

std::string
ssegui_last_error ()
{
    std::size_t n;
    ssegui_last_error (&n, nullptr);
    std::string s (n, '\0');
    if (n) ssegui_last_error (&n, &s[0]);
    return s;
};

//--------------------------------------------------------------------------------------------------

//--------------------------------------------------------------------------------------------------

//--------------------------------------------------------------------------------------------------

SSEGUI_API void SSEGUI_CCONV
ssegui_version (int* api, int* maj, int* imp, const char** build)
{
    constexpr std::array<int, 3> ver = {
#include "../VERSION"
    };
    if (api) *api = ver[0];
    if (maj) *maj = ver[1];
    if (imp) *imp = ver[2];
    if (build) *build = SSEGUI_TIMESTAMP; //"2019-04-15T08:37:11.419416+00:00"
    static_assert (ver[0] == SSEGUI_API_VERSION, "API in files VERSION and sse-hooks.h must match");
}

//--------------------------------------------------------------------------------------------------

SSEGUI_API void SSEGUI_CCONV
ssegui_last_error (size_t* size, char* message)
{
    if (ssegui_error.size ())
    {
        copy_string (ssegui_error, size, message);
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

SSEGUI_API void SSEGUI_CCONV
ssegui_enable_input (int* keyboard, int* mouse)
{
    extern bool keyboard_enable (bool* optional);
    extern bool mouse_enable (bool* optional);
    bool f;

    if (*keyboard  > 0) f = true;
    if (*keyboard == 0) f = false;
    *keyboard = keyboard_enable (*keyboard < 0 ? nullptr : &f);

    if (*mouse  > 0) f = true;
    if (*mouse == 0) f = false;
    *mouse = mouse_enable (*mouse < 0 ? nullptr : &f);
}

//--------------------------------------------------------------------------------------------------

SSEGUI_API void SSEGUI_CCONV
ssegui_control_key (int* dik)
{
    extern unsigned dinput_disable_key (unsigned* optional);
    *dik = (int) dinput_disable_key ((*dik >= 0 && *dik < 256) ? (unsigned*) &dik : nullptr);
}

//--------------------------------------------------------------------------------------------------

SSEGUI_API void SSEGUI_CCONV
ssegui_render_listener (ssegui_render_callback callback, int remove)
{
    static_assert (
            std::is_same< unsigned, UINT>::value &&
            std::is_same< unsigned, UINT>::value,
            "Render callback type mismatch");

    extern void update_render_listener (void* callback, bool remove);
    update_render_listener ((void*) callback, !!remove);
}

//--------------------------------------------------------------------------------------------------

SSEGUI_API void SSEGUI_CCONV
ssegui_message_listener (ssegui_message_callback callback, int remove)
{
    static_assert (
            std::is_same<uintptr_t,  WPARAM>::value &&
            std::is_same< intptr_t,  LPARAM>::value &&
            std::is_same< intptr_t, LRESULT>::value,
            "Message callback type mismatch");

    extern void update_message_listener (void* callback, bool remove);
    update_message_listener ((void*) callback, !!remove);
}

//--------------------------------------------------------------------------------------------------

SSEGUI_API int SSEGUI_CCONV
ssegui_parameter (const char* name, void* value)
{
    extern bool render_parameter (std::string const&, void*);
    return render_parameter (name, value);
}

//--------------------------------------------------------------------------------------------------

SSEGUI_API int SSEGUI_CCONV
ssegui_execute (const char* command, void* arg)
{
    return false;
}

//--------------------------------------------------------------------------------------------------

SSEGUI_API ssegui_api SSEGUI_CCONV
ssegui_make_api ()
{
    ssegui_api api       = {};
    api.version          = ssegui_version;
    api.last_error       = ssegui_last_error;
    api.enable_input     = ssegui_enable_input;
    api.control_key      = ssegui_control_key;
    api.render_listener  = ssegui_render_listener;
    api.message_listener = ssegui_message_listener;
    api.parameter        = ssegui_parameter;
    api.execute          = ssegui_execute;
    return api;
}

//--------------------------------------------------------------------------------------------------

