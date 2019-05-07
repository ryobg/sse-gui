/**
 * @file input.cpp
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
 * DirectInput hooking, handling, reporting and all others related to capturing input for the GUI.
 */

#include <sse-hooks/sse-hooks.h>
#include <gsl/gsl_assert>

#include <array>
#include <string>
#include <memory>
#include <fstream>
#include <algorithm>

#include <windows.h>
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

//--------------------------------------------------------------------------------------------------

/// All in one holder of DirectInput & Co. fields
struct input_t
{
    HRESULT (WINAPI *input_create_orig) (HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
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

/// @see https://docs.microsoft.com/en-us/previous-versions/windows/desktop/ee417816(v%3dvs.85)

class input_device : public IDirectInputDevice8A
{
    IDirectInputDevice8A* p;
    ULONG refs;
    bool kbd;
public:
    explicit input_device (IDirectInputDevice8A* np, bool keyboard)
        : p (np), refs (1), kbd (keyboard) { Ensures (p); }
    virtual ~input_device () {}
    // IUnknown:
    STDMETHOD (QueryInterface) (REFIID riid, void** ppvObj) {
        return p->QueryInterface (riid, ppvObj);
    }
    STDMETHOD_ (ULONG, AddRef) () {
        return ++refs;
    }
    STDMETHOD_ (ULONG, Release) () {
        if (--refs == 0) { p->Release (); delete this; }
        return refs;
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
    STDMETHOD (SetCooperativeLevel) (HWND hwnd, DWORD dwFlags) {
        return p->SetCooperativeLevel (hwnd, dwFlags);
    }
    STDMETHOD (SetDataFormat) (LPCDIDATAFORMAT lpdf) {
        return p->SetDataFormat (lpdf);
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

    STDMETHOD (GetDeviceState) (DWORD cbData, LPVOID lpvData)
    {
        HRESULT hres;
        if (kbd) // Seems to works though ignoring SetDataFormat/SetActionMap
        {
            constexpr DWORD N = 256;
            std::array<std::uint8_t,  N> raw;
            hres = p->GetDeviceState (N, raw.data ());
            if (hres == DI_OK)
                std::copy_n (raw.data (), std::min (N, cbData), (uint8_t*) lpvData);
        }
        else
        {
            hres = p->GetDeviceState (cbData, lpvData);
        }
        return hres;
    }

    STDMETHOD (GetDeviceData) (
            DWORD cbObjectData, LPDIDEVICEOBJECTDATA rgdod, LPDWORD pdwInOut, DWORD dwFlags) {
        return p->GetDeviceData (cbObjectData, rgdod, pdwInOut, dwFlags);
    }
};

//--------------------------------------------------------------------------------------------------

/// @see https://docs.microsoft.com/en-us/previous-versions/windows/desktop/ee417799(v%3dvs.85)

class direct_input : public IDirectInput8A
{
    IDirectInput8A* p;
    ULONG refs;
public:
    explicit direct_input (IDirectInput8A* np) : p (np), refs (1) { Ensures (p); }
    virtual ~direct_input () {}
    // IUnknown:
    STDMETHOD (QueryInterface) (REFIID riid, void** ppvObj) {
        return p->QueryInterface (riid, ppvObj);
    }
    STDMETHOD_ (ULONG, AddRef) () {
        return ++refs;
    }
    STDMETHOD_ (ULONG, Release) () {
        if (--refs == 0) { p->Release (); delete this; }
        return refs;
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
                *lplpDirectInputDevice = new input_device (orig, rguid == guid_keyboard);
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
    ssgui_error.clear ();
    if (!sseh->profile ("SSGUI"))
    {
        ssgui_error = __func__ + " profile "s + sseh_error ();
        return false;
    }
    if (!sseh->detour ("DirectInput8Create@dinput8.dll",
                (void*) &input_create, (void**) &di.input_create_orig))
    {
        ssgui_error = __func__ + " "s + sseh_error ();
        return false;
    }
    Ensures (di.input_create_orig);
    return true;
}

//--------------------------------------------------------------------------------------------------

