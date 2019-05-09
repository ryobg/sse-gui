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
#include <gsl/span>

#include <string>
#include <memory>
#include <vector>
#include <algorithm>
#include <fstream>
#include <mutex>

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
extern std::string ssgui_error;

/// Defined in sse-gui.cpp
extern std::string sseh_error ();

/// Defined in skse.cpp
extern std::unique_ptr<sseh_api> sseh;

/// Defined in input.cpp
extern void keyboard_enable (bool enabled);
extern void mouse_enable (bool enabled);

/// Debug helper
static std::string window_message_text (unsigned msg);

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
    bool disable_dinput;
    bool disable_dinput_key_pressed;

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

    bool d3dcompile_failed;
    HMODULE d3dcompile_library;
    HRESULT (WINAPI *d3dcompile) (
        LPCVOID, SIZE_T, LPCSTR, D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR,
        LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob*);
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

    if (dx.d3dcompile_library) ::FreeLibrary (std::exchange (dx.d3dcompile_library, nullptr));
}

//--------------------------------------------------------------------------------------------------

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

void
mouse_callback (std::array<std::int32_t, 3> const& axes, gsl::span<std::uint8_t, 8> const& keys)
{
    if (dx.disable_dinput)
    {
        auto& trg = ImGui::GetIO ().MouseDown;
        std::copy_n (keys.cbegin (), 5, trg);
        static_assert ((sizeof (trg) / sizeof (trg[0])) == 5, "Fix");
        // TODO: ImGUI wndproc manages hwnd capture/release on mouse click
    }
}

//--------------------------------------------------------------------------------------------------

void
keyboard_callback (gsl::span<std::uint8_t, 256> const& keys)
{
    if (std::exchange (dx.disable_dinput_key_pressed, keys[DIK_INSERT]) && !keys[DIK_INSERT])
    {
        dx.disable_dinput = !dx.disable_dinput;
        mouse_enable (!dx.disable_dinput);
        keyboard_enable (!dx.disable_dinput);
        ImGui::GetIO ().MouseDrawCursor = dx.disable_dinput;
    }
}

//--------------------------------------------------------------------------------------------------

static LRESULT CALLBACK
window_proc (HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    LRESULT ret = 0;
    if (dx.disable_dinput)
    {
        extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
        ret = ImGui_ImplWin32_WndProcHandler (hWnd,  msg, wParam, lParam);
    }

    for (UINT i: { //?!
            WM_LBUTTONDOWN, WM_LBUTTONDBLCLK, WM_RBUTTONDOWN, WM_RBUTTONDBLCLK,
            WM_MBUTTONDOWN, WM_MBUTTONDBLCLK, WM_XBUTTONDOWN, WM_XBUTTONDBLCLK,
            WM_LBUTTONUP, WM_RBUTTONUP, WM_MBUTTONUP, WM_XBUTTONUP,
            WM_MOUSEWHEEL, 0x020E, /*WM_MOUSEHWHEEL*/
            WM_KEYDOWN, WM_SYSKEYDOWN, WM_KEYUP, WM_SYSKEYUP, WM_CHAR,
            WM_NCHITTEST, WM_SETCURSOR
        })
    {
        if (i == msg)
            return ret;
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

    if (!dx.disable_dinput)
        ImGui::GetIO ().MousePos = ImVec2 (-FLT_MAX, -FLT_MAX);

    static bool show_demo_window = true;
    static bool show_another_window = false;
    static ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
    //if (dx.imgui_active)
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
    ImGui_ImplDX11_RenderDrawData (ImGui::GetDrawData ());

    return dx.chain_present_orig (pSwapChain, SyncInterval, Flags);
}

//--------------------------------------------------------------------------------------------------

bool
setup_imgui ()
{
    bool do_cleanup = true;
    auto cleanup = gsl::finally ([&do_cleanup] { if (do_cleanup) cleanup_dx (); });

    ssgui_error.clear ();

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
    ssgui_error.clear ();
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

/// Avoid build time linking to a d3dcompiler library (as required by ImGUI). It is not very clear
/// which verion an user will have.

HRESULT WINAPI D3DCompile (
        LPCVOID p1, SIZE_T p2, LPCSTR p3, D3D_SHADER_MACRO* p4, ID3DInclude* p5, LPCSTR p6,
        LPCSTR p7, UINT p8, UINT p9, ID3DBlob** p10, ID3DBlob* p11)
{
    if (dx.d3dcompile_failed)
        return TYPE_E_ELEMENTNOTFOUND;

    if (!dx.d3dcompile)
    {
        ssgui_error.clear ();
        std::string name;
        HMODULE dll = nullptr;
        for (int i = 50; i > 30 && !dll; --i)
        {
            name = "D3DCompiler_" + std::to_string (i) + ".dll";
            dll = ::LoadLibraryA (name.c_str ());
        }
        if (!dll)
        {
            dx.d3dcompile_failed = true;
            ssgui_error = __func__ + " unable to load d3dcompiler_XX.dll"s;
            log () << ssgui_error << std::endl;
            return TYPE_E_ELEMENTNOTFOUND;
        }
        dx.d3dcompile = (decltype (dx.d3dcompile)) ::GetProcAddress (dll, "D3DCompile");
        if (!dx.d3dcompile)
        {
            ::FreeLibrary (dll);
            dx.d3dcompile_failed = true;
            ssgui_error = __func__ + " unable to find D3DCompile in "s + name;
            log () << ssgui_error << std::endl;
            return TYPE_E_ELEMENTNOTFOUND;
        }
        dx.d3dcompile_library = dll;
        log () << "D3DCompile@" << name << " loaded." << std::endl;
    }

    return dx.d3dcompile (p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11);
}

//--------------------------------------------------------------------------------------------------

#ifdef __GNUC__
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

static std::string
window_message_text (unsigned msg)
{
    static const std::vector<std::pair<unsigned, const char*>> db = {
        {   0, "WM_NULL"},
        {   1, "WM_CREATE" },
        {   2, "WM_DESTROY" },
        {   3, "WM_MOVE" },
        {   5, "WM_SIZE" },
        {   6, "WM_ACTIVATE" },
        {   7, "WM_SETFOCUS" },
        {   8, "WM_KILLFOCUS" },
        {  10, "WM_ENABLE" },
        {  11, "WM_SETREDRAW" },
        {  12, "WM_SETTEXT" },
        {  13, "WM_GETTEXT" },
        {  14, "WM_GETTEXTLENGTH" },
        {  15, "WM_PAINT" },
        {  16, "WM_CLOSE" },
        {  17, "WM_QUERYENDSESSION" },
        {  18, "WM_QUIT" },
        {  19, "WM_QUERYOPEN" },
        {  20, "WM_ERASEBKGND" },
        {  21, "WM_SYSCOLORCHANGE" },
        {  22, "WM_ENDSESSION" },
        {  24, "WM_SHOWWINDOW" },
        {  25, "WM_CTLCOLOR" },
        {  26, "WM_WININICHANGE" },
        {  27, "WM_DEVMODECHANGE" },
        {  28, "WM_ACTIVATEAPP" },
        {  29, "WM_FONTCHANGE" },
        {  30, "WM_TIMECHANGE" },
        {  31, "WM_CANCELMODE" },
        {  32, "WM_SETCURSOR" },
        {  33, "WM_MOUSEACTIVATE" },
        {  34, "WM_CHILDACTIVATE" },
        {  35, "WM_QUEUESYNC" },
        {  36, "WM_GETMINMAXINFO" },
        {  38, "WM_PAINTICON" },
        {  39, "WM_ICONERASEBKGND" },
        {  40, "WM_NEXTDLGCTL" },
        {  42, "WM_SPOOLERSTATUS" },
        {  43, "WM_DRAWITEM" },
        {  44, "WM_MEASUREITEM" },
        {  45, "WM_DELETEITEM" },
        {  46, "WM_VKEYTOITEM" },
        {  47, "WM_CHARTOITEM" },
        {  48, "WM_SETFONT" },
        {  49, "WM_GETFONT" },
        {  50, "WM_SETHOTKEY" },
        {  51, "WM_GETHOTKEY" },
        {  55, "WM_QUERYDRAGICON" },
        {  57, "WM_COMPAREITEM" },
        {  61, "WM_GETOBJECT" },
        {  65, "WM_COMPACTING" },
        {  68, "WM_COMMNOTIFY" },
        {  70, "WM_WINDOWPOSCHANGING" },
        {  71, "WM_WINDOWPOSCHANGED" },
        {  72, "WM_POWER" },
        {  73, "WM_COPYGLOBALDATA" },
        {  74, "WM_COPYDATA" },
        {  75, "WM_CANCELJOURNAL" },
        {  78, "WM_NOTIFY" },
        {  80, "WM_INPUTLANGCHANGEREQUEST" },
        {  81, "WM_INPUTLANGCHANGE" },
        {  82, "WM_TCARD" },
        {  83, "WM_HELP" },
        {  84, "WM_USERCHANGED" },
        {  85, "WM_NOTIFYFORMAT" },
        { 123, "WM_CONTEXTMENU" },
        { 124, "WM_STYLECHANGING" },
        { 125, "WM_STYLECHANGED" },
        { 126, "WM_DISPLAYCHANGE" },
        { 127, "WM_GETICON" },
        { 128, "WM_SETICON" },
        { 129, "WM_NCCREATE" },
        { 130, "WM_NCDESTROY" },
        { 131, "WM_NCCALCSIZE" },
        { 132, "WM_NCHITTEST" },
        { 133, "WM_NCPAINT" },
        { 134, "WM_NCACTIVATE" },
        { 135, "WM_GETDLGCODE" },
        { 136, "WM_SYNCPAINT" },
        { 160, "WM_NCMOUSEMOVE" },
        { 161, "WM_NCLBUTTONDOWN" },
        { 162, "WM_NCLBUTTONUP" },
        { 163, "WM_NCLBUTTONDBLCLK" },
        { 164, "WM_NCRBUTTONDOWN" },
        { 165, "WM_NCRBUTTONUP" },
        { 166, "WM_NCRBUTTONDBLCLK" },
        { 167, "WM_NCMBUTTONDOWN" },
        { 168, "WM_NCMBUTTONUP" },
        { 169, "WM_NCMBUTTONDBLCLK" },
        { 171, "WM_NCXBUTTONDOWN" },
        { 172, "WM_NCXBUTTONUP" },
        { 173, "WM_NCXBUTTONDBLCLK" },
        { 176, "EM_GETSEL" },
        { 177, "EM_SETSEL" },
        { 178, "EM_GETRECT" },
        { 179, "EM_SETRECT" },
        { 180, "EM_SETRECTNP" },
        { 181, "EM_SCROLL" },
        { 182, "EM_LINESCROLL" },
        { 183, "EM_SCROLLCARET" },
        { 185, "EM_GETMODIFY" },
        { 187, "EM_SETMODIFY" },
        { 188, "EM_GETLINECOUNT" },
        { 189, "EM_LINEINDEX" },
        { 190, "EM_SETHANDLE" },
        { 191, "EM_GETHANDLE" },
        { 192, "EM_GETTHUMB" },
        { 193, "EM_LINELENGTH" },
        { 194, "EM_REPLACESEL" },
        { 195, "EM_SETFONT" },
        { 196, "EM_GETLINE" },
        { 197, "EM_LIMITTEXT" },
        { 197, "EM_SETLIMITTEXT" },
        { 198, "EM_CANUNDO" },
        { 199, "EM_UNDO" },
        { 200, "EM_FMTLINES" },
        { 201, "EM_LINEFROMCHAR" },
        { 202, "EM_SETWORDBREAK" },
        { 203, "EM_SETTABSTOPS" },
        { 204, "EM_SETPASSWORDCHAR" },
        { 205, "EM_EMPTYUNDOBUFFER" },
        { 206, "EM_GETFIRSTVISIBLELINE" },
        { 207, "EM_SETREADONLY" },
        { 209, "EM_SETWORDBREAKPROC" },
        { 209, "EM_GETWORDBREAKPROC" },
        { 210, "EM_GETPASSWORDCHAR" },
        { 211, "EM_SETMARGINS" },
        { 212, "EM_GETMARGINS" },
        { 213, "EM_GETLIMITTEXT" },
        { 214, "EM_POSFROMCHAR" },
        { 215, "EM_CHARFROMPOS" },
        { 216, "EM_SETIMESTATUS" },
        { 217, "EM_GETIMESTATUS" },
        { 224, "SBM_SETPOS" },
        { 225, "SBM_GETPOS" },
        { 226, "SBM_SETRANGE" },
        { 227, "SBM_GETRANGE" },
        { 228, "SBM_ENABLE_ARROWS" },
        { 230, "SBM_SETRANGEREDRAW" },
        { 233, "SBM_SETSCROLLINFO" },
        { 234, "SBM_GETSCROLLINFO" },
        { 235, "SBM_GETSCROLLBARINFO" },
        { 240, "BM_GETCHECK" },
        { 241, "BM_SETCHECK" },
        { 242, "BM_GETSTATE" },
        { 243, "BM_SETSTATE" },
        { 244, "BM_SETSTYLE" },
        { 245, "BM_CLICK" },
        { 246, "BM_GETIMAGE" },
        { 247, "BM_SETIMAGE" },
        { 248, "BM_SETDONTCLICK" },
        { 255, "WM_INPUT" },
        { 256, "WM_KEYDOWN" },
        { 256, "WM_KEYFIRST" },
        { 257, "WM_KEYUP" },
        { 258, "WM_CHAR" },
        { 259, "WM_DEADCHAR" },
        { 260, "WM_SYSKEYDOWN" },
        { 261, "WM_SYSKEYUP" },
        { 262, "WM_SYSCHAR" },
        { 263, "WM_SYSDEADCHAR" },
        { 264, "WM_KEYLAST" },
        { 265, "WM_UNICHAR" },
        { 265, "WM_WNT_CONVERTREQUESTEX" },
        { 266, "WM_CONVERTREQUEST" },
        { 267, "WM_CONVERTRESULT" },
        { 268, "WM_INTERIM" },
        { 269, "WM_IME_STARTCOMPOSITION" },
        { 270, "WM_IME_ENDCOMPOSITION" },
        { 271, "WM_IME_COMPOSITION" },
        { 271, "WM_IME_KEYLAST" },
        { 272, "WM_INITDIALOG" },
        { 273, "WM_COMMAND" },
        { 274, "WM_SYSCOMMAND" },
        { 275, "WM_TIMER" },
        { 276, "WM_HSCROLL" },
        { 277, "WM_VSCROLL" },
        { 278, "WM_INITMENU" },
        { 279, "WM_INITMENUPOPUP" },
        { 280, "WM_SYSTIMER" },
        { 287, "WM_MENUSELECT" },
        { 288, "WM_MENUCHAR" },
        { 289, "WM_ENTERIDLE" },
        { 290, "WM_MENURBUTTONUP" },
        { 291, "WM_MENUDRAG" },
        { 292, "WM_MENUGETOBJECT" },
        { 293, "WM_UNINITMENUPOPUP" },
        { 294, "WM_MENUCOMMAND" },
        { 295, "WM_CHANGEUISTATE" },
        { 296, "WM_UPDATEUISTATE" },
        { 297, "WM_QUERYUISTATE" },
        { 306, "WM_CTLCOLORMSGBOX" },
        { 307, "WM_CTLCOLOREDIT" },
        { 308, "WM_CTLCOLORLISTBOX" },
        { 309, "WM_CTLCOLORBTN" },
        { 310, "WM_CTLCOLORDLG" },
        { 311, "WM_CTLCOLORSCROLLBAR" },
        { 312, "WM_CTLCOLORSTATIC" },
        { 512, "WM_MOUSEFIRST" },
        { 512, "WM_MOUSEMOVE" },
        { 513, "WM_LBUTTONDOWN" },
        { 514, "WM_LBUTTONUP" },
        { 515, "WM_LBUTTONDBLCLK" },
        { 516, "WM_RBUTTONDOWN" },
        { 517, "WM_RBUTTONUP" },
        { 518, "WM_RBUTTONDBLCLK" },
        { 519, "WM_MBUTTONDOWN" },
        { 520, "WM_MBUTTONUP" },
        { 521, "WM_MBUTTONDBLCLK" },
        { 521, "WM_MOUSELAST" },
        { 522, "WM_MOUSEWHEEL" },
        { 523, "WM_XBUTTONDOWN" },
        { 524, "WM_XBUTTONUP" },
        { 525, "WM_XBUTTONDBLCLK" },
        { 528, "WM_PARENTNOTIFY" },
        { 529, "WM_ENTERMENULOOP" },
        { 530, "WM_EXITMENULOOP" },
        { 531, "WM_NEXTMENU" },
        { 532, "WM_SIZING" },
        { 533, "WM_CAPTURECHANGED" },
        { 534, "WM_MOVING" },
        { 536, "WM_POWERBROADCAST" },
        { 537, "WM_DEVICECHANGE" },
        { 544, "WM_MDICREATE" },
        { 545, "WM_MDIDESTROY" },
        { 546, "WM_MDIACTIVATE" },
        { 547, "WM_MDIRESTORE" },
        { 548, "WM_MDINEXT" },
        { 549, "WM_MDIMAXIMIZE" },
        { 550, "WM_MDITILE" },
        { 551, "WM_MDICASCADE" },
        { 552, "WM_MDIICONARRANGE" },
        { 553, "WM_MDIGETACTIVE" },
        { 560, "WM_MDISETMENU" },
        { 561, "WM_ENTERSIZEMOVE" },
        { 562, "WM_EXITSIZEMOVE" },
        { 563, "WM_DROPFILES" },
        { 564, "WM_MDIREFRESHMENU" },
        { 640, "WM_IME_REPORT" },
        { 641, "WM_IME_SETCONTEXT" },
        { 642, "WM_IME_NOTIFY" },
        { 643, "WM_IME_CONTROL" },
        { 644, "WM_IME_COMPOSITIONFULL" },
        { 645, "WM_IME_SELECT" },
        { 646, "WM_IME_CHAR" },
        { 648, "WM_IME_REQUEST" },
        { 656, "WM_IMEKEYDOWN" },
        { 656, "WM_IME_KEYDOWN" },
        { 657, "WM_IMEKEYUP" },
        { 657, "WM_IME_KEYUP" },
        { 672, "WM_NCMOUSEHOVER" },
        { 673, "WM_MOUSEHOVER" },
        { 674, "WM_NCMOUSELEAVE" },
        { 675, "WM_MOUSELEAVE" },
        { 768, "WM_CUT" },
        { 769, "WM_COPY" },
        { 770, "WM_PASTE" },
        { 771, "WM_CLEAR" },
        { 772, "WM_UNDO" },
        { 773, "WM_RENDERFORMAT" },
        { 774, "WM_RENDERALLFORMATS" },
        { 775, "WM_DESTROYCLIPBOARD" },
        { 776, "WM_DRAWCLIPBOARD" },
        { 777, "WM_PAINTCLIPBOARD" },
        { 778, "WM_VSCROLLCLIPBOARD" },
        { 779, "WM_SIZECLIPBOARD" },
        { 780, "WM_ASKCBFORMATNAME" },
        { 781, "WM_CHANGECBCHAIN" },
        { 782, "WM_HSCROLLCLIPBOARD" },
        { 783, "WM_QUERYNEWPALETTE" },
        { 784, "WM_PALETTEISCHANGING" },
        { 785, "WM_PALETTECHANGED" },
        { 786, "WM_HOTKEY" },
        { 791, "WM_PRINT" },
        { 792, "WM_PRINTCLIENT" },
        { 793, "WM_APPCOMMAND" },
        { 856, "WM_HANDHELDFIRST" },
        { 863, "WM_HANDHELDLAST" },
        { 864, "WM_AFXFIRST" },
        { 895, "WM_AFXLAST" },
        { 896, "WM_PENWINFIRST" },
        { 897, "WM_RCRESULT" },
        { 898, "WM_HOOKRCRESULT" },
        { 899, "WM_GLOBALRCCHANGE" },
        { 899, "WM_PENMISCINFO" },
        { 900, "WM_SKB" },
        { 901, "WM_HEDITCTL" },
        { 901, "WM_PENCTL" },
        { 902, "WM_PENMISC" },
        { 903, "WM_CTLINIT" },
        { 904, "WM_PENEVENT" },
        { 911, "WM_PENWINLAST" },
        { 1024,"WM_USER" }
    };
    if (msg >= 1024)
        return "WM_USER+" + std::to_string (msg);
    auto it = std::lower_bound (db.cbegin (), db.cend (), msg,
            [] (auto const& p, unsigned v) { return p.first < v; });
    if (it != db.cend () && it->first == msg)
        return it->second;
    return "WM_" + std::to_string (msg);
}

//--------------------------------------------------------------------------------------------------
