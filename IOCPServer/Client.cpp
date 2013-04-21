#include "Client.h"
#include "Log.h"
#include "Network.h"
#include "CSLocker.h"

#include <boost/array.hpp>

#pragma warning(disable:4996) //4996: 'std::copy': Function call with parameters that may be unsafe - this call relies on the caller to check that the passed values are correct. To disable this warning, use -D_SCL_SECURE_NO_WARNINGS. See documentation on how to use Visual C++ 'Checked Iterators'

namespace
{
	static int kCount = 0;
}

/*static*/ std::vector<Client*> Client::sClients;
/* static */ Client::PoolType Client::sPool;
/* static */ CRITICAL_SECTION Client::sPoolCS;


/* static */ void Client::Init()
{
	InitializeCriticalSection(&sPoolCS);
}

/* static */ void Client::Shutdown()
{
	EnterCriticalSection(&sPoolCS);
	{
		for (int i = 0 ; i < sClients.size() ; ++i)
		{
			sPool.destroy(sClients[i]);
		}
		sClients.clear();
	}
	DeleteCriticalSection(&sPoolCS);
}


/* static */ Client* Client::Create()
{
	CSLocker lock(&sPoolCS);
	Client* client = sPool.construct();
	sClients.push_back(client);

	if (client->CreateSocket())
	{
		return client;
	}
	else
	{
		sPool.destroy(client);
		return NULL;
	}
}


/* static */ void Client::Destroy(Client* client)
{
	CSLocker lock(&sPoolCS);
	sClients.erase(std::remove(sClients.begin(), sClients.end(), client));
	sPool.destroy(client);
}

Client::Client(void)
: m_pTPIO(NULL)
, m_State(WAIT)
, m_Socket(INVALID_SOCKET)
, m_RecvBuffer(MAX_DATA_SIZE)
{
	InitializeCriticalSection(&m_RecvBufferCS);
}

Client::~Client(void)
{
	EnterCriticalSection(&m_RecvBufferCS);

	if( m_Socket != INVALID_SOCKET )
	{
		Network::CloseSocket(m_Socket);
		CancelIoEx(reinterpret_cast<HANDLE>(m_Socket), NULL);
		m_Socket = INVALID_SOCKET;
		m_State = DISCONNECTED;
	}

	if( m_pTPIO != NULL )
	{
		WaitForThreadpoolIoCallbacks(m_pTPIO, true);
		CloseThreadpoolIo( m_pTPIO );
		m_pTPIO = NULL;
	}

	DeleteCriticalSection(&m_RecvBufferCS);
}


bool Client::CreateSocket()
{
	m_Socket = Network::CreateSocket(false, 0);
	if(m_Socket == INVALID_SOCKET)
	{
		ERROR_MSG("Could not create socket.");	
		return false;	
	}
	return true;
}


void Client::OnRecvComplete(int size)
{
	CSLocker lock(&m_RecvBufferCS);

	int available = m_RecvBuffer.capacity() - m_RecvBuffer.size();
	if (available < size)
	{
		m_RecvBuffer.set_capacity(m_RecvBuffer.capacity() + size*2);
	}
	assert(m_RecvBuffer.capacity() - m_RecvBuffer.size() >= static_cast<size_t>(size));

	m_RecvBuffer.insert(m_RecvBuffer.end(), m_RecvCallBuffer, m_RecvCallBuffer + size);
}


bool Client::PopRecvData(rapidjson::Document& jsonData)
{
	CSLocker lock(&m_RecvBufferCS);

	RingBuffer::iterator itorEnd = std::find(m_RecvBuffer.begin(), m_RecvBuffer.end(), '\0');

	if (itorEnd != m_RecvBuffer.end())
	{
		++itorEnd;

		boost::array<char, MAX_DATA_SIZE> jsonStr;

		assert(std::distance(m_RecvBuffer.begin(), itorEnd) <= MAX_DATA_SIZE);
		std::copy(m_RecvBuffer.begin(), itorEnd, jsonStr.begin());

		m_RecvBuffer.erase(m_RecvBuffer.begin(), itorEnd);

		jsonData.Parse<0>(jsonStr.data());

		if (jsonData.HasParseError())
		{
			LOG("Client::GenerateJSON - parsing failed. %s error[%s]", jsonStr.data(), jsonData.GetParseError());

			// error handling! //

			return false;
		}
		else
		{
			LOG("Client::GenerateJSON - parsing succeeded. %s", jsonStr.data());
			return true;
		}		
	}

	return false;
}
