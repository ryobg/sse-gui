/**
 * @file input.cpp
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
 * DirectInput hooking, handling, reporting and all others related to capturing input for the GUI.
 */

#include <sse-gui/platform.h>
#include <sse-hooks/sse-hooks.h>
#include <gsl/span>

#include <utils/winutils.hpp>

#include <array>
#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include <functional>
#include <fstream>

#include <windows.h>
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

//--------------------------------------------------------------------------------------------------

/// All in one holder of DirectInput & Co. fields
struct input_t
{
    HRESULT (WINAPI *input_create_orig) (HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
    HWND window;   ///< Persistent cooperative top-level window, should equal the ImGUI one.

    /// DInput buffered and unbuffered
    struct {
        bool disabled; ///< For the DInput callee (i.e. hijack)
        IDirectInputDevice8A* input;
        LPCDIDATAFORMAT data_format;
        DWORD cooperative_flags;
    } keyboard;
    /// Based on DIMOUSESTATE2
    struct {
        bool disabled;
        IDirectInputDevice8A* input;
        LPCDIDATAFORMAT data_format;
        DWORD cooperative_flags;
    } mouse;

    bool disable_dinput_key_pressed;
    unsigned disable_dinput_key;
    std::vector<void(SSEGUI_CCONV*)(int,int)> disable_listeners;
};

/// One and only one object
static input_t di = {};

/// Saves on linking to a library
static const GUID guid_mouse    = {
    0x6F1D2B60, 0xD5A0, 0x11CF, { 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 }};
/// Saves on linking to a library
static const GUID guid_keyboard = {
    0x6F1D2B61, 0xD5A0, 0x11CF, { 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 }};

//--------------------------------------------------------------------------------------------------

static void
mouse_callback (std::array<std::int32_t, 3> const& axes, gsl::span<std::uint8_t, 8> const& keys)
{
}

//--------------------------------------------------------------------------------------------------

static void
keyboard_callback (gsl::span<std::uint8_t, 256> const& keys)
{
    auto const& disable = keys[di.disable_dinput_key];

    if (std::exchange (di.disable_dinput_key_pressed, disable) && !disable)
    {
        di.mouse.disabled = !di.mouse.disabled;
        di.keyboard.disabled = !di.keyboard.disabled;

        extern void handle_input_changed ();
        handle_input_changed ();
    }
}

//--------------------------------------------------------------------------------------------------

/// @see https://docs.microsoft.com/en-us/previous-versions/windows/desktop/ee417816(v%3dvs.85)

template<bool Keyboard>
class input_device : public IDirectInputDevice8A
{
    IDirectInputDevice8A* p;
public:
    explicit input_device (IDirectInputDevice8A* np) : p (np) { Ensures (p); }
    virtual ~input_device () {}
    // IUnknown:
    STDMETHOD (QueryInterface) (REFIID riid, void** ppvObj) {
        return p->QueryInterface (riid, ppvObj);
    }
    STDMETHOD_ (ULONG, AddRef) () {
        return p->AddRef ();
    }
    STDMETHOD_ (ULONG, Release) () {
        auto r = p->Release ();
        if (!r) delete this;
        return r;
    }
    // IDirectInputDevice8:
    STDMETHOD (Acquire) () {
        return p->Acquire ();
    }
    STDMETHOD (BuildActionMap) (LPDIACTIONFORMATA lpdiaf, LPCSTR lpszUserName, DWORD dwFlags) {
        return p->BuildActionMap (lpdiaf, lpszUserName, dwFlags);
    }
    STDMETHOD (CreateEffect) (
            REFGUID rguid, LPCDIEFFECT lpeff, LPDIRECTINPUTEFFECT* ppdeff, LPUNKNOWN punkOuter) {
        return p->CreateEffect (rguid, lpeff, ppdeff, punkOuter);
    }
    STDMETHOD (EnumCreatedEffectObjects) (
            LPDIENUMCREATEDEFFECTOBJECTSCALLBACK lpCallback, LPVOID pvRef, DWORD fl) {
        return p->EnumCreatedEffectObjects (lpCallback, pvRef, fl);
    }
    STDMETHOD (EnumEffects) (LPDIENUMEFFECTSCALLBACKA lpCallback, LPVOID pvRef, DWORD dwEffType) {
        return p->EnumEffects (lpCallback, pvRef, dwEffType);
    }
    STDMETHOD (EnumEffectsInFile) (
            LPCSTR lpszFileName, LPDIENUMEFFECTSINFILECALLBACK pec, LPVOID pvRef, DWORD dwFlags) {
        return p->EnumEffectsInFile (lpszFileName, pec, pvRef, dwFlags);
    }
    STDMETHOD (EnumObjects) (
            LPDIENUMDEVICEOBJECTSCALLBACKA lpCallback, LPVOID pvRef, DWORD dwFlags) {
        return p->EnumObjects (lpCallback, pvRef, dwFlags);
    }
    STDMETHOD (Escape) (LPDIEFFESCAPE pesc) {
        return p->Escape (pesc);
    }
    STDMETHOD (GetCapabilities) (LPDIDEVCAPS lpDIDevCaps) {
        return p->GetCapabilities (lpDIDevCaps);
    }
    STDMETHOD (GetDeviceInfo) (LPDIDEVICEINSTANCEA pdidi) {
        return p->GetDeviceInfo (pdidi);
    }
    STDMETHOD (GetEffectInfo) (LPDIEFFECTINFOA pdei, REFGUID rguid) {
        return p->GetEffectInfo (pdei, rguid);
    }
    STDMETHOD (GetForceFeedbackState) (LPDWORD pdwOut) {
        return p->GetForceFeedbackState (pdwOut);
    }
    STDMETHOD (GetImageInfo) (LPDIDEVICEIMAGEINFOHEADERA lpdiDevImageInfoHeader) {
        return p->GetImageInfo (lpdiDevImageInfoHeader);
    }
    STDMETHOD (GetObjectInfo) (LPDIDEVICEOBJECTINSTANCEA pdidoi, DWORD dwObj, DWORD dwHow) {
        return p->GetObjectInfo (pdidoi, dwObj, dwHow);
    }
    STDMETHOD (GetProperty) (REFGUID rguidProp, LPDIPROPHEADER pdiph) {
        return p->GetProperty (rguidProp, pdiph);
    }
    STDMETHOD (Initialize) (HINSTANCE hinst, DWORD dwVersion, REFGUID rguid) {
        return p->Initialize (hinst, dwVersion, rguid);
    }
    STDMETHOD (Poll) () {
        return p->Poll ();
    }
    STDMETHOD (RunControlPanel) (HWND hwndOwner, DWORD dwFlags) {
        return p->RunControlPanel (hwndOwner, dwFlags);
    }
    STDMETHOD (SendDeviceData) (
            DWORD cbObjectData, LPCDIDEVICEOBJECTDATA rgdod, LPDWORD pdwInOut, DWORD fl) {
        return p->SendDeviceData (cbObjectData, rgdod, pdwInOut, fl);
    }
    STDMETHOD (SendForceFeedbackCommand) (DWORD dwFlags) {
        return p->SendForceFeedbackCommand (dwFlags);
    }
    STDMETHOD (SetActionMap) (
            LPDIACTIONFORMATA lpdiActionFormat, LPCSTR lptszUserName, DWORD dwFlags) {
        return p->SetActionMap (lpdiActionFormat, lptszUserName, dwFlags);
    }
    STDMETHOD (SetEventNotification) (HANDLE hEvent) {
        return p->SetEventNotification (hEvent);
    }
    STDMETHOD (SetProperty) (REFGUID rguidProp, LPCDIPROPHEADER pdiph) {
        return p->SetProperty (rguidProp, pdiph);
    }
    STDMETHOD (Unacquire) () {
        return p->Unacquire ();
    }
    STDMETHOD (WriteEffectToFile) (
            LPCSTR lpszFileName, DWORD dwEntries, LPDIFILEEFFECT rgDiFileEft, DWORD dwFlags) {
        return p->WriteEffectToFile (lpszFileName, dwEntries, rgDiFileEft, dwFlags);
    }

    STDMETHOD (SetDataFormat) (LPCDIDATAFORMAT lpdf)
    {
        if (Keyboard)
            di.keyboard.data_format = lpdf;
        else
            di.mouse.data_format = lpdf;

        return p->SetDataFormat (lpdf);
    }

    STDMETHOD (SetCooperativeLevel) (HWND hwnd, DWORD dwFlags)
    {
        HRESULT hres = p->SetCooperativeLevel (hwnd, dwFlags);
        if (hres == DI_OK)
        {
            di.window = hwnd;
            if (Keyboard)
                di.keyboard.cooperative_flags = dwFlags;
            else
                di.mouse.cooperative_flags = dwFlags;
        }
        return hres;
    }

    STDMETHOD (GetDeviceState) (DWORD cbData, LPVOID lpvData)
    {
        HRESULT hres = p->GetDeviceState (cbData, lpvData);
        if (hres != DI_OK)
            return hres;

        // Ignores SetDataFormat/SetActionMap
        if (Keyboard)
        {
            Expects (cbData == 256);
            auto callee = reinterpret_cast<std::uint8_t*> (lpvData);
            keyboard_callback (gsl::make_span (callee, cbData));

            if (di.keyboard.disabled)
                std::fill_n (callee, cbData, 0);
        }
        else
        {
            Expects (cbData == sizeof (DIMOUSESTATE2));
            auto callee = reinterpret_cast<DIMOUSESTATE2*> (lpvData);
            static_assert (sizeof (callee->rgbButtons) / sizeof (callee->rgbButtons[0]) == 8, "!");

            mouse_callback (
                    { callee->lX, callee->lY, callee->lZ },
                    gsl::make_span (callee->rgbButtons, 8));

            if (di.mouse.disabled)
                *callee = DIMOUSESTATE2 {};
        }

        return hres;
    }

    STDMETHOD (GetDeviceData) (
            DWORD cbObjectData, LPDIDEVICEOBJECTDATA rgdod, LPDWORD pdwInOut, DWORD dwFlags)
    {
        if (Keyboard)
        {
            std::array<std::uint8_t, 256> raw = {};
            auto hres = p->GetDeviceState (raw.size (), raw.data ());
            if (hres == DI_OK)
            {
                keyboard_callback (raw);
            }

            if (di.keyboard.disabled)
            {
                DWORD dwItems = INFINITE;
                hres = p->GetDeviceData (sizeof (DIDEVICEOBJECTDATA), nullptr, &dwItems, 0);
                *pdwInOut = 0;
                return hres;
            }
        }
        // Mouse case looks unused

        return p->GetDeviceData (cbObjectData, rgdod, pdwInOut, dwFlags);
    }
};

//--------------------------------------------------------------------------------------------------

/// @see https://docs.microsoft.com/en-us/previous-versions/windows/desktop/ee417799(v%3dvs.85)

class direct_input : public IDirectInput8A
{
    IDirectInput8A* p;
public:
    explicit direct_input (IDirectInput8A* np) : p (np) { Ensures (p); }
    virtual ~direct_input () {}
    // IUnknown:
    STDMETHOD (QueryInterface) (REFIID riid, void** ppvObj) {
        return p->QueryInterface (riid, ppvObj);
    }
    STDMETHOD_ (ULONG, AddRef) () {
        return p->AddRef ();
    }
    STDMETHOD_ (ULONG, Release) () {
        auto r = p->Release ();
        if (!r) delete this;
        return r;
    }
    // IDirectInput8:
    STDMETHOD (EnumDevices) (
            DWORD dwDevType, LPDIENUMDEVICESCALLBACKA lpCallback, LPVOID pvRef, DWORD dwFlags) {
        return p->EnumDevices (dwDevType, lpCallback, pvRef, dwFlags);
    }
    STDMETHOD (GetDeviceStatus) (REFGUID rguidInstance) {
        return p->GetDeviceStatus (rguidInstance);
    }
    STDMETHOD (RunControlPanel) (HWND hwndOwner, DWORD dwFlags) {
        return p->RunControlPanel (hwndOwner, dwFlags);
    }
    STDMETHOD (Initialize) (HINSTANCE hinst, DWORD dwVersion) {
        return p->Initialize (hinst, dwVersion);
    }
    STDMETHOD (FindDevice) (REFGUID rguidClass, LPCSTR ptszName, LPGUID pguidInstance) {
        return p->FindDevice (rguidClass, ptszName, pguidInstance);
    }
    STDMETHOD (EnumDevicesBySemantics) (
            LPCSTR ptszUserName, LPDIACTIONFORMATA lpdiActionFormat,
            LPDIENUMDEVICESBYSEMANTICSCBA lpCallback, LPVOID pvRef, DWORD dwFlags) {
        return p->EnumDevicesBySemantics (
                ptszUserName, lpdiActionFormat, lpCallback, pvRef, dwFlags);
    }
    STDMETHOD (ConfigureDevices) (
            LPDICONFIGUREDEVICESCALLBACK lpdiCallback, LPDICONFIGUREDEVICESPARAMSA lpdiCDParams,
            DWORD dwFlags, LPVOID pvRefData) {
        return p->ConfigureDevices (lpdiCallback, lpdiCDParams, dwFlags, pvRefData);
    }
    STDMETHOD (CreateDevice) (
            REFGUID rguid, IDirectInputDevice8A** lplpDirectInputDevice, LPUNKNOWN pUnkOuter)
    {
        if (rguid != guid_keyboard && rguid != guid_mouse)
        {
            return p->CreateDevice (rguid, lplpDirectInputDevice, pUnkOuter);
        }
        else
        {
            IDirectInputDevice8A* orig = nullptr;
            HRESULT hr = p->CreateDevice (rguid, &orig, pUnkOuter);
            if (hr == DI_OK)
            {
                if (rguid == guid_keyboard)
                     di.keyboard.input = *lplpDirectInputDevice = new input_device<true > (orig);
                else di.mouse.input    = *lplpDirectInputDevice = new input_device<false> (orig);
            }
            return hr;
        }
    }
};

//--------------------------------------------------------------------------------------------------

/// @see https://docs.microsoft.com/en-us/previous-versions/windows/desktop/ee416756(v=vs.85)

static HRESULT WINAPI
input_create (HINSTANCE hinst, DWORD dwVersion, REFIID riidltf, LPVOID* ppvOut, LPUNKNOWN punkOuter)
{
    IDirectInput8A* orig;
    auto hr = di.input_create_orig (hinst, dwVersion, riidltf, (LPVOID*) &orig, punkOuter);
    if (hr == DI_OK)
    {
        *(IDirectInput8A**) ppvOut = new direct_input (orig);
    }
    return hr;
}

//--------------------------------------------------------------------------------------------------

bool
detour_dinput ()
{
    Expects (sseh);
    ssegui_error.clear ();
    if (!sseh->profile ("SSEGUI"))
    {
        ssegui_error = __func__ + " profile "s + sseh_error ();
        return false;
    }
    if (!sseh->detour ("DirectInput8Create@dinput8.dll",
                (void*) &input_create, (void**) &di.input_create_orig))
    {
        ssegui_error = __func__ + " "s + sseh_error ();
        return false;
    }
    Ensures (di.input_create_orig);

    return true;
}

//--------------------------------------------------------------------------------------------------

bool
keyboard_enable (bool* optional)
{
    return !std::exchange (di.keyboard.disabled, optional ? !*optional : di.keyboard.disabled);
}

bool
mouse_enable (bool* optional)
{
    return !std::exchange (di.mouse.disabled, optional ? !*optional : di.mouse.disabled);
}

unsigned
dinput_disable_key (unsigned* optional)
{
    Expects (!optional || *optional < 256);
    return std::exchange (di.disable_dinput_key, optional ? *optional : di.disable_dinput_key);
}

//--------------------------------------------------------------------------------------------------

void
update_disable_listener (void* callback, bool remove)
{
    Expects (callback);
    if (update_listener (di.disable_listeners, callback, remove))
        log () << "Disable callback " << callback << (remove ? " removed.":" added.") << std::endl;
}

//--------------------------------------------------------------------------------------------------

void
dinput_exclusive_mode (int keyboard, int mouse)
{
    Expects (di.keyboard.input && di.mouse.input);

    DWORD flags;

    flags = di.keyboard.cooperative_flags & ~(DISCL_EXCLUSIVE | DISCL_NONEXCLUSIVE);
    flags |= keyboard ? DISCL_EXCLUSIVE : DISCL_NONEXCLUSIVE;
    di.keyboard.input->Unacquire ();
    di.keyboard.input->SetCooperativeLevel (di.window, flags);
    di.keyboard.input->SetDataFormat (di.keyboard.data_format);
    di.keyboard.input->Acquire ();

    flags = di.mouse.cooperative_flags & ~(DISCL_EXCLUSIVE | DISCL_NONEXCLUSIVE);
    flags |= mouse ? DISCL_EXCLUSIVE : DISCL_NONEXCLUSIVE;
    di.mouse.input->Unacquire ();
    di.mouse.input->SetCooperativeLevel (di.window, flags);
    di.mouse.input->SetDataFormat (di.mouse.data_format);
    di.mouse.input->Acquire ();
}

//--------------------------------------------------------------------------------------------------

void handle_input_changed ()
{
    dinput_exclusive_mode (!di.keyboard.disabled, !di.mouse.disabled);

    for (auto const& f: di.disable_listeners)
        f (!di.keyboard.disabled, !di.mouse.disabled);
}

//--------------------------------------------------------------------------------------------------

