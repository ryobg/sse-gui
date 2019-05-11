/**
 * @file skse.cpp
 * @brief Implementing SSEGUI as plugin for SKSE
 * @internal
 *
 * This file is part of SSE GUI project (aka SSEGUI).
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
 * This file only depends on one header file provided by SKSE. As it looks, that plugin interface
 * does not change much, if at all. Hence, this means there is stable interface which can make
 * SSEGUI version independent of SKSE. This file by itself is standalone enough so it can be a
 * separate DLL project - binding SSEGUI to SKSE.
 */

#include <sse-gui/sse-gui.h>
#include <sse-hooks/sse-hooks.h>
#include <gsl/gsl_assert>
#include <utils/winutils.hpp>

#include <cstdint>
typedef std::uint32_t UInt32;
typedef std::uint64_t UInt64;
#include <skse/PluginAPI.h>

#include <vector>
#include <memory>
#include <chrono>
#include <fstream>
#include <iomanip>

//--------------------------------------------------------------------------------------------------

/// Given by SKSE to uniquely identify this DLL
static PluginHandle plugin = 0;

/// To send events to the other plugins.
static SKSEMessagingInterface* messages = nullptr;

/// Log file in pre-defined location
static std::ofstream logfile;

/// [shared] In order to hook upon D3D11
std::unique_ptr<sseh_api> sseh;

/// Defined in sse-gui.cpp
extern std::string ssegui_last_error ();

//--------------------------------------------------------------------------------------------------

static void
open_log ()
{
    std::string path;
    if (known_folder_path (FOLDERID_Documents, path))
    {
        // Before plugins are loaded, SKSE takes care to create the directiories
        path += "\\My Games\\Skyrim Special Edition\\SKSE\\";
    }
    path += "sse-gui.log";
    logfile.open (path);
}

//--------------------------------------------------------------------------------------------------

decltype(logfile)&
log ()
{
    // MinGW 4.9.1 have no std::put_time()
    using std::chrono::system_clock;
    auto now_c = system_clock::to_time_t (system_clock::now ());
    auto loc_c = std::localtime (&now_c);
    logfile << '['
            << 1900 + loc_c->tm_year
            << '-' << std::setw (2) << std::setfill ('0') << loc_c->tm_mon
            << '-' << std::setw (2) << std::setfill ('0') << loc_c->tm_mday
            << ' ' << std::setw (2) << std::setfill ('0') << loc_c->tm_hour
            << ':' << std::setw (2) << std::setfill ('0') << loc_c->tm_min
            << ':' << std::setw (2) << std::setfill ('0') << loc_c->tm_sec
        << "] ";
    return logfile;
}

//--------------------------------------------------------------------------------------------------

/// [shared] Convert to std::string #sseh_last_error()

std::string
sseh_error ()
{
    Expects (sseh);
    std::size_t n;
    sseh->last_error (&n, nullptr);
    std::string s (n, '\0');
    if (n) sseh->last_error (&n, &s[0]);
    return s;
};

//--------------------------------------------------------------------------------------------------

static void
handle_sseh_message (SKSEMessagingInterface::Message* m)
{
    if (m->type != SSEH_API_VERSION)
    {
        log () << "Unsupported SSEH interface v" << m->type
               << " (it is not v" << SSEH_API_VERSION
               << "). Bailing out." << std::endl;
        return;
    }

    if (m->dataLen == 0) // After sseh_apply ()
        return;

    sseh.reset (new sseh_api (*reinterpret_cast<sseh_api*> (m->data)));
    log () << "Accepted SSEH interface v" << SSEH_API_VERSION << std::endl;

    extern bool detour_create_device ();
    if (!detour_create_device ())
    {
        log () << ssegui_last_error () << std::endl;
        log () << "Unable to detour DirectX. Bailing out." << std::endl;
    }

    // SKSE hooks DInput after PostPostLoad and SSEH broadcasts during PostPostLoad
    // hence its object will wrap this one, hence this one will filter the traffic for SKSE.
    // Which should be fine, as it will enable control of capturing the input for the GUI.
    extern bool detour_dinput ();
    if (!detour_dinput ())
    {
        log () << ssegui_last_error () << std::endl;
        log () << "Unable to detour DirectInput. Bailing out." << std::endl;
    }
}

//--------------------------------------------------------------------------------------------------

/// Post Load ensure SSEH is loaded and can accept listeners
/// Post Post load starts to sniff about D11 context, devices, windows and etc.
/// Input Loaded ensures these are already created and we can install SSEGUI

static void
handle_skse_message (SKSEMessagingInterface::Message* m)
{
    if (m->type == SKSEMessagingInterface::kMessage_PostLoad)
    {
        log () << "SKSE Post Load. Registering SSEH listener..." << std::endl;
        messages->RegisterListener (plugin, "SSEH", handle_sseh_message);
        return;
    }

    if (!sseh || m->type != SKSEMessagingInterface::kMessage_InputLoaded)
        return;

    log () << "SKSE Input Loaded. Setting up window..." << std::endl;
    extern bool setup_window ();
    if (!setup_window ())
    {
        log () << ssegui_last_error () << std::endl;
        log () << "Unable to setup window. Bailing out." << std::endl;
        return;
    }

    int api;
    ssegui_version (&api, nullptr, nullptr, nullptr);
    auto data = ssegui_make_api ();
    messages->Dispatch (plugin, UInt32 (api), &data, sizeof (data), nullptr);
    log () << "SSEGUI interface broadcasted." << std::endl;

    extern bool enable_rendering (bool* optional);
    extern bool enable_messaging (bool* optional);
    bool en = true;
    enable_rendering (&en);
    enable_messaging (&en);
    log () << "SSEGUI enabled." << std::endl;
}

//--------------------------------------------------------------------------------------------------

/// @see SKSE.PluginAPI.h

extern "C" SSEGUI_API bool SSEGUI_CCONV
SKSEPlugin_Query (SKSEInterface const* skse, PluginInfo* info)
{
    int api;
    ssegui_version (&api, nullptr, nullptr, nullptr);

    info->infoVersion = PluginInfo::kInfoVersion;
    info->name = "SSEGUI";
    info->version = api;

    plugin = skse->GetPluginHandle ();

    if (skse->isEditor)
        return false;

    return true;
}

//--------------------------------------------------------------------------------------------------

/// @see SKSE.PluginAPI.h

extern "C" SSEGUI_API bool SSEGUI_CCONV
SKSEPlugin_Load (SKSEInterface const* skse)
{
    open_log ();

    messages = (SKSEMessagingInterface*) skse->QueryInterface (kInterface_Messaging);
    messages->RegisterListener (plugin, "SKSE", handle_skse_message);

    int a, m, p;
    const char* b;
    ssegui_version (&a, &m, &p, &b);
    log () << "SSEGUI "<< a <<'.'<< m <<'.'<< p <<" ("<< b <<')' << std::endl;
    return true;
}

//--------------------------------------------------------------------------------------------------

