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

#include <string>
#include <memory>
#include <fstream>

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

//--------------------------------------------------------------------------------------------------

class input_device : public IDirectInputDevice8
{
    IDirectInputDevice8* p;
    bool kbd;
public:
    explicit input_device (IDirectInputDevice8* np, bool keyboard)
        : p (np), kbd (keyboard) { Ensures (p); }
    virtual ~input_device () {}
};

//--------------------------------------------------------------------------------------------------

/// @see https://docs.microsoft.com/en-us/previous-versions/windows/desktop/ee417799(v%3dvs.85)

class direct_input : public IDirectInput8
{
    IDirectInput8* p;
    ULONG refs;
public:
    explicit direct_input (IDirectInput8* np) : p (np), refs (1) { Ensures (p); }
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
            DWORD dwDevType, LPDIENUMDEVICESCALLBACK lpCallback, LPVOID pvRef, DWORD dwFlags) {
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
    STDMETHOD (FindDevice) (REFGUID rguidClass, LPCTSTR ptszName, LPGUID pguidInstance) {
		return p->FindDevice (rguidClass, ptszName, pguidInstance);
	}
    STDMETHOD (EnumDevicesBySemantics) (
            LPCTSTR ptszUserName, LPDIACTIONFORMAT lpdiActionFormat,
            LPDIENUMDEVICESBYSEMANTICSCB lpCallback, LPVOID pvRef, DWORD dwFlags) {
		return p->EnumDevicesBySemantics (
                ptszUserName, lpdiActionFormat, lpCallback, pvRef, dwFlags);
	}
    STDMETHOD (ConfigureDevices) (
            LPDICONFIGUREDEVICESCALLBACK lpdiCallback, LPDICONFIGUREDEVICESPARAMS lpdiCDParams,
            DWORD dwFlags, LPVOID pvRefData) {
		return p->ConfigureDevices (lpdiCallback, lpdiCDParams, dwFlags, pvRefData);
	}
    STDMETHOD (CreateDevice) (
            REFGUID rguid, LPDIRECTINPUTDEVICE* lplpDirectInputDevice, LPUNKNOWN pUnkOuter)
    {
	    if (rguid != GUID_SysKeyboard && rguid != GUID_SysMouse || !lplpDirectInputDevice)
		{
			return p->CreateDevice (rguid, lplpDirectInputDevice, pUnkOuter);
		}
		else
		{
			IDirectInputDevice8* orig = nullptr;
			HRESULT hr = p->CreateDevice (rguid, &orig, pUnkOuter);
			if (hr == DI_OK)
                *lplpDirectInputDevice = new input_device (orig, rguid == GUID_SysKeyboard);
			return hr;
		}
    }
};

//--------------------------------------------------------------------------------------------------

/// @see https://docs.microsoft.com/en-us/previous-versions/windows/desktop/ee416756(v=vs.85)

static HRESULT WINAPI
input_create (HINSTANCE hinst, DWORD dwVersion, REFIID riidltf, LPVOID* ppvOut, LPUNKNOWN punkOuter)
{
    return di.input_create_orig (hinst, dwVersion, riidltf, ppvOut, punkOuter);
}

//--------------------------------------------------------------------------------------------------

bool
detour_dinput ()
{
    Expects (sseh);
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

