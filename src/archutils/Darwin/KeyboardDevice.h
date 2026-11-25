#ifndef KEYBOARD_DEVICE_H
#define KEYBOARD_DEVICE_H

#include "HIDDevice.h"
#include <set>

class KeyboardDevice : public HIDDevice
{
private:
	__gnu_cxx::hash_map<IOHIDElementCookie, DeviceButton> m_Mapping;
	int m_iVendorID = 0;
	// F-keys whose press was treated as a media key (volume, brightness...),
	// so the matching release is swallowed too.
	mutable std::set<DeviceButton> m_MediaKeysDown;

protected:
	bool InitDevice( int vid, int pid ) { m_iVendorID = vid; return true; }
	bool AddLogicalDevice( int usagePage, int usage );
	void AddElement( int usagePage, int usage, IOHIDElementCookie cookie, const CFDictionaryRef properties );
	void Open();

public:
	void GetButtonPresses( vector<DeviceInput>& vPresses, IOHIDElementCookie cookie, int value, const RageTimer& now ) const;
	bool IsMediaKeyPress( DeviceButton button, int value ) const;
	void GetDevicesAndDescriptions( vector<InputDeviceInfo>& vDevices ) const;

	static bool DeviceButtonToMacVirtualKey( DeviceButton button, UInt8 &iMacVKOut );
};

#endif

/*
 * (c) 2005-2006 Steve Checkoway
 * All rights reserved.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, and/or sell copies of the Software, and to permit persons to
 * whom the Software is furnished to do so, provided that the above
 * copyright notice(s) and this permission notice appear in all copies of
 * the Software and that both the above copyright notice(s) and this
 * permission notice appear in supporting documentation.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF
 * THIRD PARTY RIGHTS. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS
 * INCLUDED IN THIS NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT
 * OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */
