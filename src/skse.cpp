/**
 * @file skse.cpp
 * @brief Implementing SSGUI as plugin for SKSE
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
 * This file only depends on one header file provided by SKSE. As it looks, that plugin interface
 * does not change much, if at all. Hence, this means there is stable interface which can make SSGUI
 * version independent of SKSE. This file by itself is standalone enough so it can be a separate DLL
 * project - binding SSGUI to SKSE.
 */

#include <sse-gui/sse-gui.h>
#include <sse-hooks/sse-hooks.h>

#include <cstdint>
typedef std::uint32_t UInt32;
typedef std::uint64_t UInt64;
#include <skse/PluginAPI.h>

#include <vector>
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

/// Is SS GUI in valid state
static bool initialized = false;

//--------------------------------------------------------------------------------------------------

/// TODO: Better location

static void open_log ()
{
    logfile.open ("ssgui.log");
}

//--------------------------------------------------------------------------------------------------

static decltype(logfile)&
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
        << "] ";
    return logfile;
}

//--------------------------------------------------------------------------------------------------

/// Frequent scenario to get the last error and log it

static void
log_error ()
{
    size_t n = 0;
    ssgui_last_error (&n, nullptr);
    if (n)
    {
        std::string s (n, '\0');
        ssgui_last_error (&n, &s[0]);
        log () << s << std::endl;
    }
}

//--------------------------------------------------------------------------------------------------

static void handle_sseh_message (SKSEMessagingInterface::Message* m)
{
    if (m->type)
    {
        log () << "Accepting SSEH interface." << std::endl;

        if (m->type != SSEH_API_VERSION)
        {
            log () << "Unsupported SSEH interface v" << m->type
                   << " (it is not v" << SSEH_API_VERSION
                   << "). Bailing out." << std::endl;
            return;
        }

        if (!ssgui_detour (m->data))
            log_error ();

        return;
    }

    initialized = ssgui_init ();
    if (!initialized)
    {
        log_error ();
        return;
    }
    log () << "Initialized." << std::endl;

    int api;
    ssgui_version (&api, nullptr, nullptr, nullptr);
    auto data = ssgui_make_api ();
    messages->Dispatch (plugin, UInt32 (api), &data, sizeof (data), nullptr);

    log () << "SSGUI interface broadcasted." << std::endl;
}

//--------------------------------------------------------------------------------------------------

/// Post load ensure SSEH is loaded and can accept listeners

static void handle_skse_message (SKSEMessagingInterface::Message* m)
{
    if (m->type != SKSEMessagingInterface::kMessage_PostLoad)
        return;
    messages->RegisterListener (plugin, "SSEH", handle_sseh_message);
}

//--------------------------------------------------------------------------------------------------

/// @see SKSE.PluginAPI.h

extern "C" SSGUI_API bool SSGUI_CCONV
SKSEPlugin_Query (SKSEInterface const* skse, PluginInfo* info)
{
    int api;
    ssgui_version (&api, nullptr, nullptr, nullptr);

    info->infoVersion = PluginInfo::kInfoVersion;
    info->name = "SSGUI";
    info->version = api;

    plugin = skse->GetPluginHandle ();

    if (skse->isEditor)
        return false;

    return true;
}

//--------------------------------------------------------------------------------------------------

/// @see SKSE.PluginAPI.h

extern "C" SSGUI_API bool SSGUI_CCONV
SKSEPlugin_Load (SKSEInterface const* skse)
{
    open_log ();

    messages = (SKSEMessagingInterface*) skse->QueryInterface (kInterface_Messaging);
    if (!messages)
        return false;

    messages->RegisterListener (plugin, "SKSE", handle_skse_message);

    int a, m, p;
    const char* b;
    ssgui_version (&a, &m, &p, &b);
    log () << "SSGUI "<< a <<'.'<< m <<'.'<< p <<" ("<< b <<')' << std::endl;
    return true;
}

//--------------------------------------------------------------------------------------------------

