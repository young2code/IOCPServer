#include "Packet.h"
#include "CSLocker.h"

#include <boost/pool/singleton_pool.hpp>
#include <cassert>

/* static */ Packet::PoolType Packet::sPool;
/* static */ CRITICAL_SECTION Packet::sPoolCS;

/* static */ void Packet::Init()
{
	InitializeCriticalSection(&sPoolCS);
}

/* static */ void Packet::Shutdown()
{
	DeleteCriticalSection(&sPoolCS);
}



/* static */ Packet* Packet::Create(Client* sender, const BYTE* buff, DWORD size)
{
	CSLocker lock(&sPoolCS);

	Packet* packet = sPool.construct();

	packet->m_Sender = sender; 
	packet->m_Size = size;

	assert(size <= Packet::MAX_BUFF_SIZE);
	CopyMemory(packet->m_Data, buff, size);

	return packet;
}

/* static */ void Packet::Destroy(Packet* packet)
{
	CSLocker lock(&sPoolCS);
	sPool.destroy(packet);
}


Packet::Packet()
{
}

Packet::~Packet()
{
}
