/* CardReader - arcade-style card login through a serial card reader.
 *
 * Works with the InuReader Arduino firmware in ArduinoCodes/ (MFRC522,
 * HSJ632TR or LMRF3060 module). Each scan arrives as an 11-byte packet:
 *   0xAA 0xAA, 8 card-ID bytes (zero padded), checksum = ~(sum of the 10 bytes before it)
 *
 * DORMANT BY DEFAULT: nothing runs unless the CardReaderPortP1 and/or
 * CardReaderPortP2 preference names a serial port, for example
 *   macOS:   /dev/cu.usbmodem14101
 *   Windows: COM3
 *   Linux:   /dev/ttyACM0 or /dev/ttyUSB0
 * Each port belongs to that player's side of the cabinet. With both empty
 * (the default), no thread starts and no port is touched.
 *
 * On a valid scan it broadcasts the "CardScanned" message with
 *   { Player = PlayerNumber, card = "16 uppercase hex digits" }
 * from the main thread. The theme decides what a card means (see
 * Themes/RaveItOut: profile = card ID).
 */

#ifndef CARD_READER_H
#define CARD_READER_H

#include "PlayerNumber.h"
#include "RageThreads.h"
#include <vector>

struct lua_State;

class CardReaderManager
{
public:
	CardReaderManager();
	~CardReaderManager();

	// Main thread, once per frame: hands queued scans to the theme.
	void Update();

	bool IsEnabled() const { return !m_vReaders.empty(); }
	bool IsEnabled( PlayerNumber pn ) const;

	/* Turns the raw bytes received so far into card IDs. Valid packets are
	 * removed from vBuf; bytes that can't start a packet are dropped. Exposed
	 * so the packet parsing can be tested without hardware. */
	static void ParsePackets( std::vector<unsigned char> &vBuf, std::vector<RString> &vCardsOut );

	void PushSelf( lua_State *L );

private:
	struct Reader
	{
		PlayerNumber pn;
		RString sPort;
		RageThread thread;
		CardReaderManager *pManager;
	};
	static int ReaderThread_Start( void *p );
	void ReaderThread( Reader *r );
	void QueueScan( PlayerNumber pn, const RString &sCard );

	std::vector<Reader*> m_vReaders;
	volatile bool m_bShutdown;

	RageMutex m_QueueLock;
	struct Scan { PlayerNumber pn; RString sCard; };
	std::vector<Scan> m_vQueue;
};

extern CardReaderManager *CARDREADER; // global; NULL before startup

#endif
