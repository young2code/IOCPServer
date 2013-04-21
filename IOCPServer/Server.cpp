#include "Server.h"
#include "Client.h"
#include "Packet.h"
#include "IOEvent.h"
#include "CSLocker.h"

#include "Log.h"
#include "Network.h"

#include <iostream>
#include <cassert>
#include <algorithm>

#include "EchoService.h"
#include "TicTacToeService.h"

using namespace std;


//---------------------------------------------------------------------------------//
//---------------------------------------------------------------------------------//
/* static */ void CALLBACK Server::IoCompletionCallback(PTP_CALLBACK_INSTANCE /* Instance */, PVOID /* Context */,
														PVOID Overlapped, ULONG IoResult, ULONG_PTR NumberOfBytesTransferred, PTP_IO /* Io */)
{
	IOEvent* event = CONTAINING_RECORD(Overlapped, IOEvent, GetOverlapped());
	assert(event);

	if(IoResult != ERROR_SUCCESS)
	{
		ERROR_CODE(IoResult, "I/O operation failed. type[%d]", event->GetType());

		Server::Instance()->OnClose(event);
	}
	else
	{	
		switch(event->GetType())
		{
		case IOEvent::ACCEPT:	
			Server::Instance()->OnAccept(event);
			break;

		case IOEvent::RECV:		
			if(NumberOfBytesTransferred > 0)
			{
				Server::Instance()->OnRecv(event, NumberOfBytesTransferred);
			}
			else
			{
				Server::Instance()->OnClose(event);
			}
			break;

		case IOEvent::SEND:
			Server::Instance()->OnSend(event, NumberOfBytesTransferred);
			break;

		default: assert(false); break;
		}
	}

	IOEvent::Destroy(event);
}


void CALLBACK Server::WorkerPostAccept(PTP_CALLBACK_INSTANCE /* Instance */, PVOID Context, PTP_WORK /* Work */)
{
	Server* server = static_cast<Server*>(Context);
	assert(server);

	while(server->m_LoopPostAccept)
	{
		server->PostAccept();
	}
}


void CALLBACK Server::WorkerServiceUpdate(PTP_CALLBACK_INSTANCE /* Instance */, PVOID Context, PTP_WORK /* Work */)
{
	Server* server = static_cast<Server*>(Context);
	assert(server);

	while(server->m_LoopServiceUpdate)
	{
		server->UpdateServices();
	}
}


void CALLBACK Server::WorkerAddClient(PTP_CALLBACK_INSTANCE /* Instance */, PVOID Context)
{
	Client* client = static_cast<Client*>(Context);
	assert(client);

	Server::Instance()->AddClient(client);
}


void CALLBACK Server::WorkerRemoveClient(PTP_CALLBACK_INSTANCE /* Instance */, PVOID Context)
{
	Client* client = static_cast<Client*>(Context);
	assert(client);

	Server::Instance()->RemoveClient(client);
}


//---------------------------------------------------------------------------------//
//---------------------------------------------------------------------------------//
Server::Server(void)
: m_pTPIO(NULL),
  m_AcceptTPWORK(NULL),
  m_LoopPostAccept(false),
  m_listenSocket(INVALID_SOCKET),
  m_MaxPostAccept(0),
  m_NumPostAccept(0),
  m_EchoService(NULL),
  m_ServiceTPWORK(NULL),
  m_LoopServiceUpdate(false)
{
}


Server::~Server(void)
{
}


bool Server::Init(unsigned short port, int maxPostAccept)
{	
	assert(maxPostAccept > 0);

	Client::Init();
	IOEvent::Init();
	Packet::Init();

	// Create Service
	InitializeCriticalSection(&m_CSForServices);
	m_EchoService = new EchoService;
	m_ServiceTPWORK = CreateThreadpoolWork(Server::WorkerServiceUpdate, this, NULL);
	if(m_ServiceTPWORK == NULL)
	{
		ERROR_CODE(GetLastError(), "Could not create service worker TPIO.");
		delete m_EchoService;
		Destroy();
		return false;
	}	
	m_EchoService->Init();
	m_LoopServiceUpdate = true;
	SubmitThreadpoolWork(m_ServiceTPWORK);	

	// Create Listen Socket
	m_MaxPostAccept = maxPostAccept;
	m_listenSocket = Network::CreateSocket(true, port);
	if(m_listenSocket == INVALID_SOCKET)
	{
		return false;
	}

	// Make the address re-usable to re-run the same server instantly.
	bool reuseAddr = true;
	if(setsockopt(m_listenSocket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuseAddr), sizeof(reuseAddr)) == SOCKET_ERROR)
	{
		ERROR_CODE(WSAGetLastError(), "setsockopt() failed with SO_REUSEADDR.");
		Destroy();
		return false;
	}

	// We will use AcceptEx() so we need to enalbe conditional accpet to avoid listen() accepts a connection which shuold be completed by normal 'accept()' func.
	bool conditionalAccept = true;
	if( setsockopt(m_listenSocket, SOL_SOCKET, SO_CONDITIONAL_ACCEPT, reinterpret_cast<const char*>(&conditionalAccept), sizeof(conditionalAccept)) == SOCKET_ERROR )
	{
		ERROR_CODE(WSAGetLastError(), "setsockopt() failed with SO_CONDITIONAL_ACCEPT.");
		Destroy();
		return false;
	}

	// Create & Start ThreaddPool for socket IO
	m_pTPIO = CreateThreadpoolIo(reinterpret_cast<HANDLE>(m_listenSocket), Server::IoCompletionCallback, NULL, NULL);
	if( m_pTPIO == NULL )
	{
		ERROR_CODE(WSAGetLastError(), "Could not assign the listen socket to the IOCP handle.");
		Destroy();
		return false;
	}

	// Start listening
	StartThreadpoolIo( m_pTPIO );
	if(listen(m_listenSocket, SOMAXCONN) == SOCKET_ERROR)
	{
		ERROR_CODE(WSAGetLastError(), "listen() failed.");
		return false;
	}

	// Create critical sections for m_Clients
	InitializeCriticalSection(&m_CSForClients);

	// Create Accept worker
	m_AcceptTPWORK = CreateThreadpoolWork(Server::WorkerPostAccept, this, NULL);
	if(m_AcceptTPWORK == NULL)
	{
		ERROR_CODE(GetLastError(), "Could not create AcceptEx worker TPIO.");
		Destroy();
		return false;
	}	
	m_LoopPostAccept = true;
	SubmitThreadpoolWork(m_AcceptTPWORK);

	return true;
}


void Server::Shutdown()
{
	if( m_AcceptTPWORK != NULL )
	{
		m_LoopPostAccept = false;
		WaitForThreadpoolWorkCallbacks( m_AcceptTPWORK, true );
		CloseThreadpoolWork( m_AcceptTPWORK );
		m_AcceptTPWORK = NULL;
	}

	if( m_listenSocket != INVALID_SOCKET )
	{
		Network::CloseSocket(m_listenSocket);
		CancelIoEx(reinterpret_cast<HANDLE>(m_listenSocket), NULL);
		m_listenSocket = INVALID_SOCKET;
	}

	if( m_pTPIO != NULL )
	{
		WaitForThreadpoolIoCallbacks( m_pTPIO, true );
		CloseThreadpoolIo( m_pTPIO );
		m_pTPIO = NULL;
	}

	EnterCriticalSection(&m_CSForClients);
	{
		for(ClientList::iterator itor = m_Clients.begin() ; itor != m_Clients.end() ; ++itor)	
		{
			Client::Destroy(*itor);
		}
		m_Clients.clear();
	}
	DeleteCriticalSection(&m_CSForClients);

	m_LoopServiceUpdate = false;
	WaitForThreadpoolWorkCallbacks( m_ServiceTPWORK, true );
	CloseThreadpoolWork( m_ServiceTPWORK );
	m_ServiceTPWORK = NULL;

	EnterCriticalSection(&m_CSForServices);
	{
		if (m_EchoService != NULL)
		{
			m_EchoService->Shutdown();
			delete m_EchoService;
			m_EchoService = NULL;
		}

		for (size_t i = 0 ; i < m_TicTacToeServices.size() ; ++i)
		{
			m_TicTacToeServices[i]->Shutdown();
			delete m_TicTacToeServices[i];
		}
		m_TicTacToeServices.clear();
	}
	DeleteCriticalSection(&m_CSForServices);

	Packet::Shutdown();
	IOEvent::Shutdown();
	Client::Shutdown();
}


void Server::PostAccept()
{
	// If the number of clients is too big, we can just stop posting aceept.
	// That's one of the benefits from AcceptEx.
	int count = m_MaxPostAccept - m_NumPostAccept;
	if( count > 0 )
	{
		int i = 0;
		for(  ; i < count ; ++i )
		{
			Client* client = Client::Create();			
			if( !client )
			{
				break;
			}

			IOEvent* event = IOEvent::Create(IOEvent::ACCEPT, client);
			assert(event);

			StartThreadpoolIo( m_pTPIO );
			if ( FALSE == Network::AcceptEx(m_listenSocket, client->GetSocket(), &event->GetOverlapped()))
			{
				int error = WSAGetLastError();

				if(error != ERROR_IO_PENDING)
				{
					CancelThreadpoolIo( m_pTPIO );

					ERROR_CODE(error, "AcceptEx() failed.");
					Client::Destroy(client);
					IOEvent::Destroy(event);
					break;
				}
			}
			else
			{
				OnAccept(event);
				IOEvent::Destroy(event);
			}
		}

		InterlockedExchangeAdd(&m_NumPostAccept, i);	

		LOG("[%d] Post AcceptEx : %d", GetCurrentThreadId(), m_NumPostAccept);
	}
}


void Server::PostRecv(Client* client)
{
	assert(client);

	if (client->GetState() != Client::ACCEPTED)
	{
		return;
	}

	WSABUF recvBufferDescriptor;
	recvBufferDescriptor.buf = reinterpret_cast<char*>(client->GetRecvCallBuff());
	recvBufferDescriptor.len = Client::MAX_DATA_SIZE;

	DWORD numberOfBytes = 0;
	DWORD recvFlags = 0;

	IOEvent* event = IOEvent::Create(IOEvent::RECV, client);
	assert(event);

	StartThreadpoolIo(client->GetTPIO());

	if(WSARecv(client->GetSocket(), &recvBufferDescriptor, 1, &numberOfBytes, &recvFlags, &event->GetOverlapped(), NULL) == SOCKET_ERROR)
	{
		int error = WSAGetLastError();

		if(error != ERROR_IO_PENDING)
		{
			CancelThreadpoolIo(client->GetTPIO());

			ERROR_CODE(error, "WSARecv() failed.");
			
			OnClose(event);
			IOEvent::Destroy(event);
		}
	}
	else
	{
		// In this case, the completion callback will have already been scheduled to be called.
	}
}


void Server::PostSend(Client* client, Packet* packet)
{
	assert(client);
	assert(packet);

	if (client->GetState() != Client::ACCEPTED)
	{
		return;
	}

	WSABUF recvBufferDescriptor;
	recvBufferDescriptor.buf = reinterpret_cast<char*>(packet->GetData());
	recvBufferDescriptor.len = packet->GetSize();

	DWORD sendFlags = 0;

	IOEvent* event = IOEvent::Create(IOEvent::SEND, client, packet);
	assert(event);
	
	StartThreadpoolIo(client->GetTPIO());

	if(WSASend(client->GetSocket(), &recvBufferDescriptor, 1, NULL, sendFlags, &event->GetOverlapped(), NULL) == SOCKET_ERROR)
	{
		int error = WSAGetLastError();

		if(error != ERROR_IO_PENDING)
		{
			CancelThreadpoolIo(client->GetTPIO());

			ERROR_CODE(error, "WSASend() failed.");

			RequestRemoveClient(client);
		}
	}
	else
	{
		// In this case, the completion callback will have already been scheduled to be called.
	}
}


void Server::OnAccept(IOEvent* event)
{
	assert(event);

	LOG("[%d] Enter OnAccept()", GetCurrentThreadId());
	assert(event->GetType() == IOEvent::ACCEPT);

	// Check if we need to post more accept requests.
	InterlockedDecrement(&m_NumPostAccept);

	// Add client in a different thread.
	// It is because we need to return this function ASAP so that this IO worker thread can process the other IO notifications.
	// If adding client is fast enough, we can call it here but I assume it's slow.	
	if(TrySubmitThreadpoolCallback(Server::WorkerAddClient, event->GetClient(), NULL) == false)
	{
		ERROR_CODE(GetLastError(), "Could not start `.");

		AddClient(event->GetClient());
	}

	LOG("[%d] Leave OnAccept()", GetCurrentThreadId());
}


void Server::OnRecv(IOEvent* event, DWORD dwNumberOfBytesTransfered)
{
	assert(event);

	LOG("[%d] Enter OnRecv()", GetCurrentThreadId());

	Client* client = event->GetClient();
	assert(client);

	LOG("[%d] OnRecv : %s", GetCurrentThreadId(), client->GetRecvCallBuff());

	client->OnRecvComplete(dwNumberOfBytesTransfered);

	PostRecv(event->GetClient());

	LOG("[%d] Leave OnRecv()", GetCurrentThreadId());
}


void Server::OnSend(IOEvent* event, DWORD dwNumberOfBytesTransfered)
{
	assert(event);

	LOG("[%d] OnSend : %d, client(%p)", GetCurrentThreadId(), dwNumberOfBytesTransfered, event->GetClient());

	// This should be fast enough to do in this I/O thread.
	// if not, we need to queue it like what we do in OnRecv().
	Packet::Destroy(event->GetPacket());
}


void Server::OnClose(IOEvent* event)
{
	assert(event);

	LOG("Client's socket has been closed.");

	if (event->GetPacket())
	{
		Packet::Destroy(event->GetPacket());
	}

	// we should remove this client in a different thread as client will wait i/o for its socket.
	RequestRemoveClient(event->GetClient());
}


void Server::AddClient(Client* client)
{
	assert(client);

	// The socket sAcceptSocket does not inherit the properties of the socket associated with sListenSocket parameter until SO_UPDATE_ACCEPT_CONTEXT is set on the socket.
	if (setsockopt(client->GetSocket(), SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, reinterpret_cast<const char *>(&m_listenSocket), sizeof(m_listenSocket)) == SOCKET_ERROR)
	{
		ERROR_CODE(WSAGetLastError(), "setsockopt() for AcceptEx() failed.");

		RequestRemoveClient(client);
	}		
	else
	{
		client->SetState(Client::ACCEPTED);

		// Connect the socket to IOCP
		TP_IO* pTPIO = CreateThreadpoolIo(reinterpret_cast<HANDLE>(client->GetSocket()), Server::IoCompletionCallback, NULL, NULL);
		if(pTPIO == NULL)
		{
			ERROR_CODE(GetLastError(), "CreateThreadpoolIo failed for a client.");

			RequestRemoveClient(client);
		}
		else
		{
			std::string ip;
			u_short port = 0;
			Network::GetRemoteAddress(client->GetSocket(), ip, port);
			LOG("[%d] Accept succeeded. client address : ip[%s], port[%d]", GetCurrentThreadId(), ip.c_str(), port);

			client->SetTPIO(pTPIO);

			{
				CSLocker lock(&m_CSForClients);
				m_Clients.push_back(client);
			}

	
			PostRecv(client);
		}
	}
}


void Server::RemoveClient(Client* client)
{
	assert(client);

	{
		CSLocker lock(&m_CSForClients);
		ClientList::iterator itor = std::remove(m_Clients.begin(), m_Clients.end(), client);

		if(itor != m_Clients.end())
		{
			m_Clients.erase(itor);
		}
	}

	RemoveClientFromServices(client);

	Client::Destroy(client);
}

void Server::PostBoradcast(Packet* packet)
{
	assert(packet);
	assert(packet->GetSender());

	CSLocker lock(&m_CSForClients);

	for(ClientList::iterator itor = m_Clients.begin() ; itor != m_Clients.end() ; ++itor)
	{
		PostSend(*itor, packet);
	}
}

void Server::RequestRemoveClient(Client* client)
{
	if(TrySubmitThreadpoolCallback(Server::WorkerRemoveClient, client, NULL) == false)
	{
		ERROR_CODE(GetLastError(), "can't start WorkerRemoveClient. call it directly.");

		RemoveClient(client);
	}
}

size_t Server::GetNumClients()
{
	CSLocker lock(&m_CSForClients);

	return m_Clients.size();
}

long Server::GetNumPostAccepts()
{
	return m_NumPostAccept;
}


void Server::UpdateServices()
{
	CSLocker lock(&m_CSForServices);

	{
		CSLocker lockClients(&m_CSForClients);
		for(ClientList::iterator itor = m_Clients.begin() ; itor != m_Clients.end() ; ++itor)
		{
			Client* client = *itor;
			rapidjson::Document data;

			if (client->PopRecvData(data))
			{
				m_EchoService->OnRecv(client, data);

				for (size_t i = 0; i < m_TicTacToeServices.size() ; ++i)
				{
					m_TicTacToeServices[i]->OnRecv(client, data);
				}

				TicTacToeService::CreateOrEnter(client, data, m_TicTacToeServices);
			}
		}
	}

	m_EchoService->Update();

	for (size_t i = 0; i < m_TicTacToeServices.size() ; ++i)
	{
		m_TicTacToeServices[i]->Update();
	}

	TicTacToeService::Flush(m_TicTacToeServices);
}

void Server::RemoveClientFromServices(Client* client)
{
	CSLocker lock(&m_CSForServices);

	for (size_t i = 0; i < m_TicTacToeServices.size() ; ++i)
	{
		m_TicTacToeServices[i]->RemoveClient(client);
	}
}

