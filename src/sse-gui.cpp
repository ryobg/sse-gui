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
#include <sse-hooks/sse-hooks.h>

#include <cstring>
#include <string>
#include <array>
#include <vector>
#include <locale>
#include <algorithm>
#include <fstream>

#include <windows.h>
#include <d3d11.h>

//--------------------------------------------------------------------------------------------------

using namespace std::string_literals;

/// Supports SSGUI specific errors in a manner of #GetLastError() and #FormatMessage()
static std::string ssgui_error;

/// Hooked and gathered data to support the GUI

struct
{
    HWND window; ///< Skyrim Special Edition
}
target = {};

//--------------------------------------------------------------------------------------------------

static_assert (std::is_same<std::wstring::value_type, TCHAR>::value, "Not an _UNICODE build.");

/// Safe convert from UTF-8 (Skyrim) encoding to UTF-16 (Windows).

static bool
utf8_to_utf16 (char const* bytes, std::wstring& out)
{
    ssgui_error.clear ();
    if (!bytes) return true;
    int bytes_size = static_cast<int> (std::strlen (bytes));
    if (bytes_size < 1) return true;
    int sz = ::MultiByteToWideChar (CP_UTF8, 0, bytes, bytes_size, NULL, 0);
    if (sz < 1) return false;
    out.resize (sz, 0);
    ::MultiByteToWideChar (CP_UTF8, 0, bytes, bytes_size, &out[0], sz);
    return true;
}

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

/// Common try/catch block used in API methods

template<class Function>
static bool
try_call (Function&& func)
{
    ssgui_error.clear ();
    try
    {
        func ();
    }
    catch (std::exception const& ex)
    {
        ssgui_error = __func__ + " "s + ex.what ();
        return false;
    }
    return true;
}

//--------------------------------------------------------------------------------------------------
std::wofstream wlog ("ssgui1.log");

static BOOL CALLBACK
find_window (HWND hwnd, LPARAM lParam)
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

SSGUI_API int SSGUI_CCONV
ssgui_init (void* p_sseh)
{
    auto sseh = *reinterpret_cast<sseh_api*> (p_sseh);

    int api;
    sseh.version (&api, nullptr, nullptr, nullptr);
    if (api != SSEH_API_VERSION)
    {
        ssgui_error = __func__ + " incompatible API versions"s;
        return false;
    }

    auto error = [&] {
        std::size_t n;
        sseh.last_error (&n, nullptr);
        std::string s (n, '\0');
        if (n) sseh.last_error (&n, &s[0]);
        ssgui_error = "ssgui_detour " + s;
        return false;
    };

    PFN_D3D11_CREATE_DEVICE_AND_SWAP_CHAIN create_device;
    if (!sseh.find_address ("d3d11.dll", "D3D11CreateDeviceAndSwapChain", (void**) &create_device))
        return error ();

    HWND window = nullptr;
    ::EnumWindows (find_window, (LPARAM) &window);
    if (!window)
    {
        ssgui_error = __func__ + " unable to find window"s;
        return false;
    }

	DXGI_SWAP_CHAIN_DESC sd = [window]
	{
        DXGI_SWAP_CHAIN_DESC sd;
        ::ZeroMemory (&sd, sizeof (sd));
		sd.BufferCount                 = 1;
		sd.BufferDesc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
		sd.BufferDesc.Scaling          = DXGI_MODE_SCALING_UNSPECIFIED;
		sd.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
		sd.BufferUsage                 = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		sd.Flags                       = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
		sd.SampleDesc.Quality          = 0;
		sd.SampleDesc.Count            = 1;
		sd.OutputWindow                = window;
        sd.Windowed                    = !(::GetWindowLongPtr (window, GWL_STYLE) & WS_POPUP);
		sd.SwapEffect                  = DXGI_SWAP_EFFECT_DISCARD;
		sd.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
		sd.BufferDesc.Scaling          = DXGI_MODE_SCALING_UNSPECIFIED;
		sd.BufferDesc.Width            = 1;
		sd.BufferDesc.Height           = 1;
		sd.BufferDesc.RefreshRate.Numerator   = 60;
		sd.BufferDesc.RefreshRate.Denominator = 1;
        return sd;
	} ();

	D3D_FEATURE_LEVEL obtained;
    std::array<D3D_FEATURE_LEVEL, 2> levels = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1 };
    IDXGISwapChain* swap = nullptr;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    if (FAILED (create_device (
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels.data (), levels.size (),
            D3D11_SDK_VERSION, &sd, &swap, &device, &obtained, &context)))
	{
		ssgui_error = __func__ + " D3D11CreateDeviceAndSwapChain"s;
		return false;
	}

    device->Release ();
    context->Release ();
    swap->Release ();

    if (!sseh.profile ("SSGUI"))
        return error ();

    return true;
}

//--------------------------------------------------------------------------------------------------

SSGUI_API void SSGUI_CCONV
ssgui_uninit ()
{
}

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
	api.init       = ssgui_init;
	api.uninit     = ssgui_uninit;
	api.execute    = ssgui_execute;
    return api;
}

//--------------------------------------------------------------------------------------------------

