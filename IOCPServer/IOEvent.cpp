#include "IOEvent.h"
#include "Client.h"
#include "Packet.h"
#include "CSLocker.h"


/* static */ IOEvent::PoolType IOEvent::sPool;
/* static */ CRITICAL_SECTION IOEvent::sPoolCS;

/* static */ void IOEvent::Init()
{
	InitializeCriticalSection(&sPoolCS);
}

/* static */ void IOEvent::Shutdown()
{
	DeleteCriticalSection(&sPoolCS);
}



/* static */ IOEvent* IOEvent::Create(Type type, Client* client, Packet* packet)
{
	CSLocker locker(&sPoolCS);

	IOEvent* event = sPool.construct();

	ZeroMemory(&event->m_Overlapped, sizeof(OVERLAPPED));
	event->m_Client = client;
	event->m_Type = type;
	event->m_Packet = packet;

	return event;	
}

/* static */ void IOEvent::Destroy(IOEvent* event)
{
	CSLocker locker(&sPoolCS);
	sPool.destroy(event);
}

IOEvent::IOEvent()
{
}

IOEvent::~IOEvent()
{
}



