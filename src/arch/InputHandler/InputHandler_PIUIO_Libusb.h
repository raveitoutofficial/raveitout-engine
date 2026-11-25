#ifndef INPUT_HANDLER_PIUIO_LIBUSB_H
#define INPUT_HANDLER_PIUIO_LIBUSB_H

#include "InputHandler.h"
#include "RageThreads.h"
#include "io/PIUIO.h"

/* Userspace PIUIO (Andamiro arcade I/O board, USB 0547:1002) input handler.
 * Talks to the board directly through libusb, so it needs no kernel driver
 * (unlike InputHandler_Linux_PIUIO, which reads /dev/piuio0). Used on macOS,
 * where no system driver claims the board. Also drives the pad lamps from the
 * "Export" lights driver, since lamp data rides in the same USB packets. */
class InputHandler_PIUIO_Libusb: public InputHandler
{
public:
	InputHandler_PIUIO_Libusb();
	~InputHandler_PIUIO_Libusb();
	void GetDevicesAndDescriptions( vector<InputDeviceInfo>& vDevicesOut );

private:
	static int InputThread_Start( void *p );
	void InputThread();
	bool PollOnce();
	void BuildOutputPacket( int iSensorSet, uint8_t out[PIUIO_libusb::PACKET_SIZE] ) const;
	void ReleaseAll();

	PIUIO_libusb m_Board;
	bool m_bConnected;
	uint8_t m_LastInputs[PIUIO_libusb::PACKET_SIZE];
	RageThread m_InputThread;
	volatile bool m_bShutdown;
};

#endif

/*
 * PIUIO protocol (8-byte vendor control transfers, request 0xAE; 4-way sensor
 * multiplexer selected by bits 0-1 of output bytes 0 and 2; active-low inputs;
 * pad lamps at bits 2-6 of output bytes 0 and 2) as documented by
 * InputHandler_Linux_PIUIO (Devin J. Pohly) and piuio2key
 * (Copyright (c) 2022 Ckristian Duran, MIT License).
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
