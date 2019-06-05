/**
 * @file render.cpp
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
 * All related to DirectX, window handling and actual rendering of GUI elements.
 */

#include <sse-gui/sse-gui.h>
#include <sse-hooks/sse-hooks.h>
#include <gsl/gsl_util>
#include <gsl/span>

#include <utils/winutils.hpp>

#include <string>
#include <memory>
#include <vector>
#include <map>
#include <algorithm>
#include <fstream>

#include <windows.h>
#include <dwmapi.h>
#include <d3d11.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>

//--------------------------------------------------------------------------------------------------

using namespace std::string_literals;

/// Opened from within skse.cpp
extern std::ofstream& log ();

/// Defined in sse-gui.cpp
extern std::string ssegui_error;

/// Defined in sse-gui.cpp
extern std::string sseh_error ();

/// Defined in skse.cpp
extern std::unique_ptr<sseh_api> sseh;

/// All in one holder of DirectX & Co. fields
struct render_t
{
    ID3D11Device*           device;
    ID3D11DeviceContext*    context;
    IDXGISwapChain*         chain;
    HWND                    window;
    LRESULT (CALLBACK *window_proc_orig) (HWND, UINT, WPARAM, LPARAM);
    HRESULT (WINAPI *chain_present_orig) (IDXGISwapChain*, UINT, UINT);

    PFN_D3D11_CREATE_DEVICE_AND_SWAP_CHAIN create_device_orig;

    struct device_record
    {
        IDXGISwapChain* chain;
        ID3D11Device* device;
        ID3D11DeviceContext* context;
        HWND window;
    };
    std::vector<device_record> device_history;

    std::vector<void(SSEGUI_CCONV*)(IDXGISwapChain*,UINT,UINT)> render_listeners;
    std::vector<LRESULT(SSEGUI_CCONV*)(HWND,UINT,WPARAM,LPARAM)> message_listeners;
    bool enable_rendering;
    bool enable_messaging;
};

/// One and only one object
static render_t dx = {};

//--------------------------------------------------------------------------------------------------

static BOOL CALLBACK
find_top_window_callback (HWND hwnd, LPARAM lParam)
{
    static auto target_pid = ::GetCurrentProcessId ();

    DWORD pid = 0;
    ::GetWindowThreadProcessId (hwnd, &pid);

    if (pid != target_pid || ::GetWindow (hwnd, GW_OWNER) || !::IsWindowVisible (hwnd))
    {
        return true;
    }

    *reinterpret_cast<HWND*> (lParam) = hwnd;
    return false;
}

//--------------------------------------------------------------------------------------------------

/*
   Some of the detected messages when dinput is exclusive (default behaviour):

   WM_WINDOWPOSCHANGING, WM_NCCALCSIZE, WM_NCPAINT, WM_ERASEBKGND, WM_WINDOWPOSCHANGED,
   WM_NCACTIVATE, WM_STYLECHANGING, WM_STYLECHANGED, 49377, WM_SYNCPAINT, WM_USER, WM_NCHITTEST,
   WM_SETCURSOR, WM_PAINT, WM_GETICON, WM_ACTIVATE, WM_KILLFOCUS, WM_IME_SETCONTEXT,
   WM_IME_NOTIFY, WM_GETTEXT, WM_ACTIVATEAPP, WM_QUERYOPEN, WM_SETFOCUS, WM_SYSCOMMAND,
   WM_GETMINMAXINFO, 144, WM_DESTROY, WM_NCDESTROY

   The ones we block are the one found when dinput is switched to non exclusive mode.
*/

static LRESULT CALLBACK
window_proc (HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (dx.enable_messaging)
        for (auto const& f: dx.message_listeners)
            f (hWnd, msg, wParam, lParam);

    for (UINT i: {
            WM_LBUTTONDOWN, WM_LBUTTONDBLCLK, WM_RBUTTONDOWN, WM_RBUTTONDBLCLK,
            WM_MBUTTONDOWN, WM_MBUTTONDBLCLK, WM_XBUTTONDOWN, WM_XBUTTONDBLCLK,
            WM_LBUTTONUP, WM_RBUTTONUP, WM_MBUTTONUP, WM_XBUTTONUP,
            WM_MOUSEWHEEL, 0x020E, /*WM_MOUSEHWHEEL*/
            WM_KEYDOWN, WM_KEYUP, WM_CHAR,
        })
    {
        if (i == msg)
        {
            return 0;
        }
    }

    return ::CallWindowProc (dx.window_proc_orig, hWnd, msg, wParam, lParam);
}

//--------------------------------------------------------------------------------------------------

static HRESULT WINAPI
chain_present (IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags)
{
    if (dx.enable_rendering)
        for (auto const& f: dx.render_listeners)
            f (pSwapChain, SyncInterval, Flags);
    return dx.chain_present_orig (pSwapChain, SyncInterval, Flags);
}

//--------------------------------------------------------------------------------------------------

bool
setup_window ()
{
    ssegui_error.clear ();

    HWND top_window = nullptr;
    ::EnumWindows (find_top_window_callback, (LPARAM) &top_window);

    HWND named_window = ::FindWindow (0, L"Skyrim Special Edition");

    log () << "Top window: "    << top_window
           << " Named window: " << named_window << std::endl;

    bool device_selected = false;
    if (dx.device_history.size ())
    {
        for (auto const& r: dx.device_history)
        {
            if (top_window && top_window == r.window
                    && named_window && named_window == r.window)
            {
                if (r.context && r.device && r.chain)
                {
                    dx.window = r.window;
                    dx.chain = r.chain;
                    dx.context = r.context;
                    dx.device = r.device;
                    device_selected = true;
                    break;
                }
            }
        }
    }

    if (!device_selected)
    {
        ssegui_error = "Unable to find Skyrim DirectX"s;
        return false;
    }

    extern bool clip_cursor (bool);
    clip_cursor (true);

    /*
    IUnknown: QueryInterface, AddRef, Release = 2,
    IDXGIObject: SetPrivateData, SetPrivateDataInterface, GetPrivateData, GetParent = 6,
    IDXGIDeviceSubObject: GetDevice = 7,
    IDXGISwapChain: Present, GetBuffer, SetFullscreenState, GetFullscreenState, GetDesc = 12,
                    ResizeBuffers, ResizeTarget, GetContainingOutput, GetFrameStatistics = 16,
                    GetLastPresentCount = 17
    */
    if (!sseh->profile ("SSEGUI"))
    {
        ssegui_error = __func__ + " SSEH/SSEGUI profile "s + sseh_error ();
        return false;
    }
    auto d3d11present = (*(std::uintptr_t**) dx.chain)[8];
    auto present_name = "IDXGISwapChain.Present";
    sseh->map_name (present_name, d3d11present);
    if (!sseh->detour (present_name, (void*) &chain_present, (void**) &dx.chain_present_orig)
            || !sseh->apply ())
    {
        ssegui_error = __func__ + " detouring "s + present_name + " "s + sseh_error ();
        return false;
    }

    dx.window_proc_orig = (WNDPROC) ::SetWindowLongPtr (
            dx.window, GWLP_WNDPROC, (LONG_PTR) window_proc);

    log () << present_name << " hooked and window subclassed." << std::endl;
    return true;
}

//--------------------------------------------------------------------------------------------------

/// @see https://docs.microsoft.com/en-us/windows/desktop/api/d3d11/nf-d3d11-d3d11createdeviceandswapchain

static HRESULT WINAPI
create_device (
        IDXGIAdapter *pAdapter, D3D_DRIVER_TYPE DriverType, HMODULE Software, UINT Flags,
        const D3D_FEATURE_LEVEL *pFeatureLevels, UINT FeatureLevels, UINT SDKVersion,
        const DXGI_SWAP_CHAIN_DESC *pSwapChainDesc, IDXGISwapChain **ppSwapChain,
        ID3D11Device **ppDevice, D3D_FEATURE_LEVEL *pFeatureLevel,
        ID3D11DeviceContext **ppImmediateContext)
{
    Expects (dx.create_device_orig);

    HRESULT hres = dx.create_device_orig (pAdapter, DriverType, Software, Flags,
            pFeatureLevels, FeatureLevels, SDKVersion, pSwapChainDesc, ppSwapChain,
            ppDevice, pFeatureLevel, ppImmediateContext);

    render_t::device_record r = {};
    if (pSwapChainDesc    ) r.window  = pSwapChainDesc->OutputWindow;
    if (ppSwapChain       ) r.chain   = *ppSwapChain;
    if (ppDevice          ) r.device  = *ppDevice;
    if (ppImmediateContext) r.context = *ppImmediateContext;
    if (r.window && r.chain && r.device && r.context)
    {
        dx.device_history.push_back (r);
    }

    log () << "New DX11 device and chain "
           << "(Window: "  << r.window
           << " Chain: "   << r.chain
           << " Device: "  << r.device
           << " Context: " << r.context
           << ")" << std::endl;

    return hres;
}

//--------------------------------------------------------------------------------------------------

bool
detour_create_device ()
{
    Expects (sseh);
    ssegui_error.clear ();
    if (!sseh->profile ("SSEGUI"))
    {
        ssegui_error = __func__ + " profile "s + sseh_error ();
        return false;
    }
    if (!sseh->detour ("D3D11CreateDeviceAndSwapChain@d3d11.dll",
                (void*) &create_device, (void**) &dx.create_device_orig))
    {
        ssegui_error = __func__ + " "s + sseh_error ();
        return false;
    }
    Ensures (dx.create_device_orig);
    return true;
}

//--------------------------------------------------------------------------------------------------

bool
render_parameter (std::string const& name, void* value)
{
    if (name == "ID3D11Device")
        *((ID3D11Device**) value) = dx.device;
    else if (name == "ID3D11DeviceContext")
        *((ID3D11DeviceContext**) value) = dx.context;
    else if (name == "IDXGISwapChain")
        *((IDXGISwapChain**) value) = dx.chain;
    else if (name == "window")
        *((HWND*) value) = dx.window;
    else
        return false;
    return true;
}

//--------------------------------------------------------------------------------------------------

bool
enable_rendering (bool* optional)
{
    return std::exchange (dx.enable_rendering, optional ? *optional : dx.enable_rendering);
}

bool
enable_messaging (bool* optional)
{
    return std::exchange (dx.enable_messaging, optional ? *optional : dx.enable_messaging);
}

//--------------------------------------------------------------------------------------------------

/// void* as too lazy to type the type when needed.

template<class T>
static bool
update_listener (T& list, void* callback, bool remove)
{
    auto l = reinterpret_cast<typename T::value_type> (callback);
    if (remove)
    {
        list.erase (std::remove (list.begin (), list.end (),  l));
        return true;
    }
    else if (std::find (list.cbegin (), list.cend (), l) == list.cend ())
    {
        list.push_back (l);
        return true;
    }
    return false;
}

void
update_render_listener (void* callback, bool remove)
{
    Expects (callback);
    if (update_listener (dx.render_listeners, callback, remove))
        log () << "Render callback " << callback << (remove ? " removed.":" added.") << std::endl;
}

void
update_message_listener (void* callback, bool remove)
{
    Expects (callback);
    if (update_listener (dx.message_listeners, callback, remove))
        log () << "Message callback " << callback << (remove ? " removed.":" added.") << std::endl;
}

//--------------------------------------------------------------------------------------------------

bool
clip_cursor (bool clip)
{
    Expects (dx.window);

    if (!clip)
    {
        if (!::ClipCursor (nullptr))
        {
            ssegui_error = __func__ + " ClipCursor "s + format_utf8message (::GetLastError ());
            return false;
        }
        return true;
    }

    RECT window_rect = {};
    if (!::GetWindowRect (dx.window, &window_rect))
    {
        ssegui_error = __func__ + " GetWindowRect "s + format_utf8message (::GetLastError ());
        return false;
    }

    HMONITOR monitor = ::MonitorFromWindow (dx.window, MONITOR_DEFAULTTONEAREST);

    MONITORINFO info;
    info.cbSize = sizeof (MONITORINFO);
    if (!::GetMonitorInfo (monitor, &info))
    {
        ssegui_error = __func__ + " GetMonitorInfo "s;
        return false;
    }

    // Test for fullscreen
    auto monitor_width  = info.rcMonitor.right  - info.rcMonitor.left;
    auto monitor_height = info.rcMonitor.bottom - info.rcMonitor.top;
    auto window_width   = window_rect.right     - window_rect.left;
    auto window_height  = window_rect.bottom    - window_rect.top;

    if (window_width == monitor_width && window_height == monitor_height)
    {
        if (!::ClipCursor (&window_rect))
        {
            ssegui_error = __func__ + " ClipCursor "s + format_utf8message (::GetLastError ());
            return false;
        }
    }

    return true;
}

//--------------------------------------------------------------------------------------------------
