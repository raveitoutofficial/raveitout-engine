#include "global.h"
#include "InputHandler_PIUIO_Libusb.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "GameInput.h"
#include "arch/Lights/LightsDriver_Export.h"
#include "arch/USB/USBDriver_Impl.h"

#include <unistd.h>

REGISTER_INPUT_HANDLER_CLASS2( PIUIO, PIUIO_Libusb );

namespace
{
	const uint16_t PIUIO_VID = 0x0547;
	const uint16_t PIUIO_PID = 0x1002;
	const int NUM_SENSOR_SETS = 4;
	const int PACKET = PIUIO_libusb::PACKET_SIZE;
	// How long to wait between attempts to find the board when it isn't connected.
	const unsigned RECONNECT_DELAY_US = 1000000;
	// Pad lamp bits within output bytes 0 (P1) and 2 (P2): UL, UR, C, DL, DR.
	const int LAMP_SHIFT = 2;
	const GameButton PAD_ORDER[5] = {
		PUMP_BUTTON_UPLEFT, PUMP_BUTTON_UPRIGHT, PUMP_BUTTON_CENTER,
		PUMP_BUTTON_DOWNLEFT, PUMP_BUTTON_DOWNRIGHT
	};
}

InputHandler_PIUIO_Libusb::InputHandler_PIUIO_Libusb()
{
	m_bConnected = false;
	m_bShutdown = false;
	// Inputs are active-low: all bits set means nothing pressed.
	memset( m_LastInputs, 0xFF, sizeof(m_LastInputs) );

	// The thread keeps looking for the board, so it can be plugged in after launch.
	m_InputThread.SetName( "PIUIO thread" );
	m_InputThread.Create( InputThread_Start, this );
}

InputHandler_PIUIO_Libusb::~InputHandler_PIUIO_Libusb()
{
	if( m_InputThread.IsCreated() )
	{
		m_bShutdown = true;
		LOG->Trace( "Shutting down PIUIO thread ..." );
		m_InputThread.Wait();
		LOG->Trace( "PIUIO thread shut down." );
	}

	if( m_bConnected )
	{
		// Leave the lamps off.
		uint8_t off[PACKET];
		memset( off, 0, sizeof(off) );
		m_Board.Write( off );
		m_Board.Close();
	}
}

int InputHandler_PIUIO_Libusb::InputThread_Start( void *p )
{
	((InputHandler_PIUIO_Libusb *) p)->InputThread();
	return 0;
}

void InputHandler_PIUIO_Libusb::InputThread()
{
	bool bLoggedMissing = false;

	while( !m_bShutdown )
	{
		if( !m_bConnected )
		{
			// DeviceExists() is a quiet check; Open() logs a warning when the board is absent.
			if( !USBDriver_Impl::DeviceExists(PIUIO_VID, PIUIO_PID) || !m_Board.Open() )
			{
				if( !bLoggedMissing )
				{
					LOG->Info( "PIUIO: board (0547:1002) not found; will keep checking." );
					bLoggedMissing = true;
				}
				usleep( RECONNECT_DELAY_US );
				continue;
			}
			LOG->Info( "PIUIO: connected to board." );
			m_bConnected = true;
			bLoggedMissing = false;
		}

		if( !PollOnce() )
		{
			LOG->Warn( "PIUIO: lost connection to board; releasing inputs and reconnecting." );
			ReleaseAll();
			m_Board.Close();
			m_bConnected = false;
			usleep( RECONNECT_DELAY_US );
		}
	}
}

/* One full update: select each of the four sensor sets in turn (writing the
 * current lamp state with it), read that set, and combine them. A panel counts
 * as pressed if any of its sensors is pressed. Inputs are active-low, so the
 * sets are combined with AND. */
bool InputHandler_PIUIO_Libusb::PollOnce()
{
	uint8_t combined[PACKET];
	memset( combined, 0xFF, sizeof(combined) );

	for( int iSet = 0; iSet < NUM_SENSOR_SETS; ++iSet )
	{
		uint8_t out[PACKET], in[PACKET];
		BuildOutputPacket( iSet, out );
		if( !m_Board.Write(out) || !m_Board.Read(in) )
			return false;
		for( int i = 0; i < PACKET; ++i )
			combined[i] &= in[i];
	}

	RageTimer now;
	const InputDevice id = DEVICE_PIUIO;

	// Same button numbering as InputHandler_Linux_PIUIO: bit index i counts
	// bytes in order, high bit first within each byte, as JOY_BUTTON_1 + i.
	for( int i = 0; i < PACKET * 8; ++i )
	{
		const uint8_t mask = 0x80 >> (i % 8);
		const bool bChanged = ((combined[i/8] ^ m_LastInputs[i/8]) & mask) != 0;
		if( !bChanged )
			continue;
		const bool bPressed = (combined[i/8] & mask) == 0;
		ButtonPressed( DeviceInput(id, enum_add2(JOY_BUTTON_1, i), bPressed, now) );
	}
	memcpy( m_LastInputs, combined, sizeof(m_LastInputs) );

	InputHandler::UpdateTimer();
	return true;
}

/* Byte 0 = P1, byte 2 = P2. Bits 0-1 select the sensor set; bits 2-6 are the
 * pad lamps (UL, UR, C, DL, DR). Lamps follow the "Export" lights driver;
 * if that driver isn't enabled they stay off. Cabinet lamps (marquee, neon)
 * are not mapped yet. */
void InputHandler_PIUIO_Libusb::BuildOutputPacket( int iSensorSet, uint8_t out[PACKET] ) const
{
	memset( out, 0, PACKET );

	const LightsState ls = LightsDriver_Export::GetState();
	for( int p = 0; p < 2; ++p )
	{
		uint8_t b = (uint8_t)(iSensorSet & 0x3);
		const GameController gc = (p == 0) ? GameController_1 : GameController_2;
		for( int k = 0; k < 5; ++k )
			if( ls.m_bGameButtonLights[gc][PAD_ORDER[k]] )
				b |= (uint8_t)(1 << (LAMP_SHIFT + k));
		out[p == 0 ? 0 : 2] = b;
	}
}

void InputHandler_PIUIO_Libusb::ReleaseAll()
{
	RageTimer now;
	for( int i = 0; i < PACKET * 8; ++i )
	{
		const uint8_t mask = 0x80 >> (i % 8);
		if( (m_LastInputs[i/8] & mask) == 0 )
			ButtonPressed( DeviceInput(DEVICE_PIUIO, enum_add2(JOY_BUTTON_1, i), false, now) );
	}
	memset( m_LastInputs, 0xFF, sizeof(m_LastInputs) );
}

void InputHandler_PIUIO_Libusb::GetDevicesAndDescriptions( vector<InputDeviceInfo>& vDevicesOut )
{
	vDevicesOut.push_back( InputDeviceInfo(InputDevice(DEVICE_PIUIO), "PIUIO") );
}

/*
 * Copyright (c) 2026 the RaveItOut contributors.
 * PIUIO protocol details from InputHandler_Linux_PIUIO (Devin J. Pohly, 2012)
 * and piuio2key (Copyright (c) 2022 Ckristian Duran, MIT License).
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
