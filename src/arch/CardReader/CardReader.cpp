#include "global.h"
#include "CardReader.h"
#include "PrefsManager.h"
#include "MessageManager.h"
#include "RageLog.h"
#include "RageTimer.h"
#include "LuaManager.h"
#include "arch/COM/serial.h"

#include <chrono>
#include <thread>

CardReaderManager *CARDREADER = NULL;

static Preference<RString> g_sCardReaderPortP1( "CardReaderPortP1", "" );
static Preference<RString> g_sCardReaderPortP2( "CardReaderPortP2", "" );
static Preference<int> g_iCardReaderBaud( "CardReaderBaud", 9600 );

static const size_t PACKET_SIZE = 11;
static const size_t ID_BYTES = 8;
// Ignore the same card on the same side again for this long (a card held on
// the reader is reported every scan).
static const float SAME_CARD_IGNORE_SECONDS = 3.0f;
static const int RECONNECT_DELAY_MS = 2000;

CardReaderManager::CardReaderManager():
	m_bShutdown(false), m_QueueLock("CardReaderQueue")
{
	const RString sPorts[NUM_PLAYERS] = { g_sCardReaderPortP1.Get(), g_sCardReaderPortP2.Get() };
	FOREACH_PlayerNumber( pn )
	{
		RString sPort = sPorts[pn];
		TrimLeft( sPort );
		TrimRight( sPort );
		if( sPort.empty() )
			continue;
		if( pn == PLAYER_2 && sPort == sPorts[PLAYER_1] )
		{
			LOG->Warn( "Card reader: P1 and P2 are both set to %s; using it for P1 only.", sPort.c_str() );
			continue;
		}
		Reader *r = new Reader;
		r->pn = pn;
		r->sPort = sPort;
		r->pManager = this;
		m_vReaders.push_back( r );
	}

	// Register with Lua, so the theme can ask CARDREADER:IsEnabled().
	{
		Lua *L = LUA->Get();
		lua_pushstring( L, "CARDREADER" );
		this->PushSelf( L );
		lua_settable( L, LUA_GLOBALSINDEX );
		LUA->Release( L );
	}

	// Dormant: no ports set, so nothing else happens.
	if( m_vReaders.empty() )
		return;

	for( Reader *r : m_vReaders )
	{
		LOG->Info( "Card reader: %s on %s", PlayerNumberToString(r->pn).c_str(), r->sPort.c_str() );
		r->thread.SetName( ssprintf("Card reader %s", PlayerNumberToString(r->pn).c_str()) );
		r->thread.Create( ReaderThread_Start, r );
	}
}

CardReaderManager::~CardReaderManager()
{
	LUA->UnsetGlobal( "CARDREADER" );
	m_bShutdown = true;
	for( Reader *r : m_vReaders )
	{
		if( r->thread.IsCreated() )
			r->thread.Wait();
		delete r;
	}
	m_vReaders.clear();
}

bool CardReaderManager::IsEnabled( PlayerNumber pn ) const
{
	for( const Reader *r : m_vReaders )
		if( r->pn == pn )
			return true;
	return false;
}

int CardReaderManager::ReaderThread_Start( void *p )
{
	Reader *r = (Reader *) p;
	r->pManager->ReaderThread( r );
	return 0;
}

static void SleepUnlessShutdown( volatile bool &bShutdown, int iMs )
{
	for( int i = 0; i < iMs && !bShutdown; i += 100 )
		std::this_thread::sleep_for( std::chrono::milliseconds(100) );
}

void CardReaderManager::ReaderThread( Reader *r )
{
	const char *sPlayer = PlayerNumberToString( r->pn ).c_str();
	bool bWarnedMissing = false;
	RString sLastCard;
	float fLastCardTime = -1000;

	while( !m_bShutdown )
	{
		// Open (or reopen after the reader was unplugged).
		// Open with DTR off, then raise it below: some ports (virtual ones,
		// some adapters) can't do DTR, and that shouldn't stop them working.
		serial::Serial port( "", g_iCardReaderBaud.Get(), serial::Timeout::simpleTimeout(100),
			serial::eightbits, serial::parity_none, serial::stopbits_one,
			serial::flowcontrol_none, false );
		try
		{
			port.setPort( r->sPort.c_str() );
			port.open();
			// open() resets the timeout, so set it after: read returns every 100 ms.
			serial::Timeout t = serial::Timeout::simpleTimeout( 100 );
			port.setTimeout( t );
		}
		catch( std::exception &e )
		{
			if( !bWarnedMissing )
			{
				LOG->Warn( "Card reader %s: can't open %s (%s); will keep trying.", sPlayer, r->sPort.c_str(), e.what() );
				RString sPorts;
				try
				{
					for( const serial::PortInfo &pi : serial::list_ports() )
						sPorts += ssprintf( "\n    %s  (%s)", pi.port.c_str(), pi.description.c_str() );
				}
				catch( std::exception & ) {}
				if( !sPorts.empty() )
					LOG->Info( "Card reader: serial ports on this computer:%s", sPorts.c_str() );
				bWarnedMissing = true;
			}
			SleepUnlessShutdown( m_bShutdown, RECONNECT_DELAY_MS );
			continue;
		}

		// Arduino Leonardo / Pro Micro only send while DTR is on.
		try { port.setDTR( true ); } catch( std::exception & ) {}

		LOG->Info( "Card reader %s: connected on %s.", sPlayer, r->sPort.c_str() );
		bWarnedMissing = false;
		std::vector<unsigned char> vBuf;

		try
		{
			while( !m_bShutdown )
			{
				unsigned char chunk[64];
				size_t n = port.read( chunk, sizeof(chunk) ); // waits up to 100 ms
				if( n == 0 )
					continue;
				vBuf.insert( vBuf.end(), chunk, chunk + n );

				std::vector<RString> vCards;
				ParsePackets( vBuf, vCards );
				for( const RString &sCard : vCards )
				{
					float fNow = RageTimer::GetTimeSinceStart();
					if( sCard == sLastCard && fNow - fLastCardTime < SAME_CARD_IGNORE_SECONDS )
					{
						fLastCardTime = fNow; // still held on the reader
						continue;
					}
					sLastCard = sCard;
					fLastCardTime = fNow;
					QueueScan( r->pn, sCard );
				}
			}
		}
		catch( std::exception &e )
		{
			LOG->Warn( "Card reader %s: lost %s (%s); will keep trying.", sPlayer, r->sPort.c_str(), e.what() );
		}
		try { port.close(); } catch( std::exception & ) {}
		SleepUnlessShutdown( m_bShutdown, RECONNECT_DELAY_MS );
	}
}

void CardReaderManager::ParsePackets( std::vector<unsigned char> &vBuf, std::vector<RString> &vCardsOut )
{
	size_t i = 0;
	while( vBuf.size() - i >= PACKET_SIZE )
	{
		if( vBuf[i] != 0xAA || vBuf[i+1] != 0xAA )
		{
			++i; // not a packet start; resync
			continue;
		}
		unsigned char sum = 0;
		for( size_t b = 0; b < PACKET_SIZE - 1; ++b )
			sum += vBuf[i+b];
		if( (unsigned char) ~sum != vBuf[i + PACKET_SIZE - 1] )
		{
			++i; // 0xAA 0xAA inside other data, or a corrupted packet
			continue;
		}
		RString sCard;
		bool bAllZero = true;
		for( size_t b = 0; b < ID_BYTES; ++b )
		{
			unsigned char c = vBuf[i + 2 + b];
			if( c != 0 )
				bAllZero = false;
			sCard += ssprintf( "%02X", c );
		}
		if( !bAllZero )
			vCardsOut.push_back( sCard );
		i += PACKET_SIZE;
	}
	vBuf.erase( vBuf.begin(), vBuf.begin() + i );
	// Never let junk pile up if a non-reader device is on the port.
	if( vBuf.size() > 4096 )
		vBuf.clear();
}

void CardReaderManager::QueueScan( PlayerNumber pn, const RString &sCard )
{
	LockMut( m_QueueLock );
	Scan s = { pn, sCard };
	m_vQueue.push_back( s );
}

void CardReaderManager::Update()
{
	if( m_vReaders.empty() )
		return;

	std::vector<Scan> vScans;
	{
		LockMut( m_QueueLock );
		vScans.swap( m_vQueue );
	}
	for( const Scan &s : vScans )
	{
		LOG->Info( "Card reader %s: card scanned.", PlayerNumberToString(s.pn).c_str() );
		Message msg( "CardScanned" );
		msg.SetParam( "Player", s.pn );
		msg.SetParam( "card", s.sCard );
		MESSAGEMAN->Broadcast( msg );
	}
}

// lua start
#include "LuaBinding.h"

/** @brief Allow Lua to have access to the CardReaderManager. */
class LunaCardReaderManager: public Luna<CardReaderManager>
{
public:
	static int IsEnabled( T* p, lua_State *L )
	{
		if( lua_gettop(L) >= 1 && !lua_isnil(L, 1) )
			lua_pushboolean( L, p->IsEnabled(Enum::Check<PlayerNumber>(L, 1)) );
		else
			lua_pushboolean( L, p->IsEnabled() );
		return 1;
	}

	LunaCardReaderManager()
	{
		ADD_METHOD( IsEnabled );
	}
};

LUA_REGISTER_CLASS( CardReaderManager )
// lua end

/*
 * Packet format from the InuReader firmware by Alex123123, adapted for
 * StepMania by Accelerator. This driver: Rave It Out project, released
 * under the same license as the rest of the engine.
 */
