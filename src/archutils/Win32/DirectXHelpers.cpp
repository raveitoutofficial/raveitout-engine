#include "global.h"
#include "DirectXHelpers.h"
#include "RageUtil.h"

#include <windows.h>

// The legacy DirectX SDK's DxErr library (DXGetErrorString) isn't in the
// modern Windows SDK, so describe HRESULTs with the system message table.
RString DirectXErrorString( long hr )
{
	char *buf = nullptr;
	DWORD len = FormatMessageA(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		nullptr, (DWORD) hr, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR) &buf, 0, nullptr );
	RString s;
	if( len != 0 && buf != nullptr )
	{
		s = buf;
		TrimRight( s );
	}
	if( buf != nullptr )
		LocalFree( buf );
	if( s.empty() )
		s = ssprintf( "HRESULT 0x%08lX", (unsigned long) hr );
	return s;
}

RString hr_ssprintf( int hr, const char *fmt, ... )
{
	va_list	va;
	va_start(va, fmt);
	RString s = vssprintf( fmt, va );
	va_end(va);

	return s + ssprintf( " (%s)", DirectXErrorString( hr ).c_str() );
}

/*
 * Copyright (c) 2001-2005 Chris Danford, Glenn Maynard
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
