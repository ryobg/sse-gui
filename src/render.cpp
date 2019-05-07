/**
 * @file render.cpp
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
 * All related to DirectX, window handling and actual rendering of GUI elements.
 */

#include <sse-gui/sse-gui.h>
#include <sse-hooks/sse-hooks.h>
#include <imgui/imgui.h>
#include <imgui/imgui_impl_win32.h>
#include <imgui/imgui_impl_dx11.h>
#include <gsl/gsl_util>
#include <gsl/gsl_assert>

#include <string>
#include <memory>
#include <vector>
#include <algorithm>
#include <fstream>
#include <mutex>

#include <windows.h>
#include <dwmapi.h>
#include <d3d11.h>

//--------------------------------------------------------------------------------------------------

using namespace std::string_literals;

/// Opened from within skse.cpp
extern std::ofstream& log ();

/// Defined in sse-gui.cpp
extern std::string ssgui_error;

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
    ID3D11RenderTargetView* view;
    HWND                    window;
    LRESULT (CALLBACK *window_proc_orig) (HWND, UINT, WPARAM, LPARAM);
    HRESULT (WINAPI *chain_present_orig) (IDXGISwapChain*, UINT, UINT);

    ImGuiContext* imgui_context;
    bool imgui_win32;
    bool imgui_dx11;
    bool imgui_show;

    PFN_D3D11_CREATE_DEVICE_AND_SWAP_CHAIN create_device_orig;
    struct device_record
    {
        IDXGISwapChain* chain;
        ID3D11Device* device;
        ID3D11DeviceContext* context;
        HWND window;
    };
    std::vector<device_record> device_history;
    std::mutex device_mutex;
};

/// One and only one object
static render_t dx = {};

//--------------------------------------------------------------------------------------------------

static void cleanup_dx ()
{
    if (dx.imgui_dx11)
    {
         ImGui_ImplDX11_Shutdown ();
         dx.imgui_dx11 = false;
    }
    if (dx.imgui_win32)
    {
         ImGui_ImplWin32_Shutdown ();
         dx.imgui_win32 = false;
    }
    if (dx.imgui_context)
        ImGui::DestroyContext (std::exchange (dx.imgui_context, nullptr));

    if (dx.view) std::exchange (dx.view, nullptr)->Release ();
}

//--------------------------------------------------------------------------------------------------

static void create_dx_view ()
{
    ID3D11Texture2D* back = nullptr;
    dx.chain->GetBuffer (0, IID_PPV_ARGS (&back));
    dx.device->CreateRenderTargetView (back, NULL, &dx.view);
    back->Release();
}

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

static LRESULT CALLBACK
window_proc (HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (dx.imgui_show)
    {
        extern ImGui_ImplWin32_WndProcHandler (HWND, UINT, WPARAM, LPARAM);
        return ImGui_ImplWin32_WndProcHandler (hWnd,  msg, wParam, lParam);
    }

    return ::CallWindowProc (dx.window_proc_orig, hWnd, msg, wParam, lParam);
}

//--------------------------------------------------------------------------------------------------

static HRESULT WINAPI
chain_present (IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags)
{
    ImGui_ImplDX11_NewFrame ();
    ImGui_ImplWin32_NewFrame ();
    ImGui::NewFrame ();

    static bool show_demo_window = true;
    static bool show_another_window = false;
    static ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
    if (dx.imgui_show)
    {
        if (show_demo_window)
            ImGui::ShowDemoWindow(&show_demo_window);
        {
            static float f = 0.0f;
            static int counter = 0;

            ImGui::Begin("Hello, world!");
            ImGui::Text("This is some useful text.");
            ImGui::Checkbox("Demo Window", &show_demo_window);
            ImGui::Checkbox("Another Window", &show_another_window);
            ImGui::SliderFloat("float", &f, 0.0f, 1.0f);
            ImGui::ColorEdit3("clear color", (float*)&clear_color);

            if (ImGui::Button("Button"))
                counter++;
            ImGui::SameLine();
            ImGui::Text("counter = %d", counter);

            ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
            ImGui::End();
        }
        if (show_another_window)
        {
            ImGui::Begin("Another Window", &show_another_window);
            ImGui::Text("Hello from another window!");
            if (ImGui::Button("Close Me"))
                show_another_window = false;
            ImGui::End();
        }
    }

    // Rendering
    ImGui::Render ();
    dx.context->OMSetRenderTargets (1, &dx.view, nullptr);
    //dx.context->ClearRenderTargetView (dx.view, (float*) &clear_color);
    ImGui_ImplDX11_RenderDrawData (ImGui::GetDrawData ());

    return dx.chain_present_orig (pSwapChain, SyncInterval, Flags);
}

//--------------------------------------------------------------------------------------------------

bool
setup_imgui ()
{
    bool do_cleanup = true;
    auto cleanup = gsl::finally ([&do_cleanup] { if (do_cleanup) cleanup_dx (); });

    HWND top_window = nullptr;
    ::EnumWindows (find_top_window_callback, (LPARAM) &top_window);

    HWND named_window = ::FindWindow (0, L"Skyrim Special Edition");

    log () << "Top window: "    << top_window
           << " Named window: " << named_window << std::endl;

    bool device_selected = false;
    if (dx.device_history.size ())
    {
        std::lock_guard<std::mutex> lock (dx.device_mutex);
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
        ssgui_error = "Unable to find Skyrim DirectX"s;
        return false;
    }

    create_dx_view ();
    log () << "DirectX setup done." << std::endl;

    IMGUI_CHECKVERSION ();
    dx.imgui_context = ImGui::CreateContext ();
    ImGuiIO& io = ImGui::GetIO ();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ImeWindowHandle = dx.window;
    ImGui::StyleColorsDark ();
    dx.imgui_win32 = ImGui_ImplWin32_Init (dx.window);
    dx.imgui_dx11 = ImGui_ImplDX11_Init (dx.device, dx.context);
    log () << "ImGUI setup done." << std::endl;

    /*
    IUnknown: QueryInterface, AddRef, Release = 2,
    IDXGIObject: SetPrivateData, SetPrivateDataInterface, GetPrivateData, GetParent = 6,
    IDXGIDeviceSubObject: GetDevice = 7,
    IDXGISwapChain: Present, GetBuffer, SetFullscreenState, GetFullscreenState, GetDesc = 12,
                    ResizeBuffers, ResizeTarget, GetContainingOutput, GetFrameStatistics = 16,
                    GetLastPresentCount = 17
    */
    if (!sseh->profile ("SSGUI"))
    {
        ssgui_error = __func__ + " SSEH/SSGUI profile "s + sseh_error ();
        return false;
    }
    auto d3d11present = (*(std::uintptr_t**) dx.chain)[8];
    auto present_name = "Skyrim.IDXGISwapChain::Present";
    sseh->map_name (present_name, d3d11present);
    if (!sseh->detour (present_name, (void*) &chain_present, (void**) &dx.chain_present_orig)
            || !sseh->apply ())
    {
        ssgui_error = __func__ + " detouring "s + present_name + " "s + sseh_error ();
        return false;
    }

    dx.window_proc_orig = (WNDPROC) ::SetWindowLongPtr (
            dx.window, GWLP_WNDPROC, (LONG_PTR) window_proc);

    log () << present_name << " hooked and window subclassed." << std::endl;
    do_cleanup = false;
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
        std::lock_guard<std::mutex> lock (dx.device_mutex);
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
    if (!sseh->profile ("SSGUI"))
    {
        ssgui_error = __func__ + " profile "s + sseh_error ();
        return false;
    }
    if (!sseh->detour ("D3D11CreateDeviceAndSwapChain@d3d11.dll",
                (void*) &create_device, (void**) &dx.create_device_orig))
    {
        ssgui_error = __func__ + " "s + sseh_error ();
        return false;
    }
    Ensures (dx.create_device_orig);
    return true;
}

//--------------------------------------------------------------------------------------------------

