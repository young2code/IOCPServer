#pragma once

#include <winsock2.h>
#include <vector>
#include <rapidjson\document.h>

#include "TSingleton.h"

class Client;
class Packet;
class IOEvent;

class TicTacToeService;

class Server :  public TSingleton<Server>
{
private:
	// Callback Routine
	static void CALLBACK IoCompletionCallback(PTP_CALLBACK_INSTANCE Instance, PVOID Context, PVOID Overlapped, ULONG IoResult, ULONG_PTR NumberOfBytesTransferred, PTP_IO Io);

	// Worker Thread Functions
	static void CALLBACK WorkerPostAccept(PTP_CALLBACK_INSTANCE /* Instance */, PVOID Context, PTP_WORK /* Work */);
	static void CALLBACK WorkerServiceUpdate(PTP_CALLBACK_INSTANCE /* Instance */, PVOID Context, PTP_WORK /* Work */);

	static void CALLBACK WorkerAddClient(PTP_CALLBACK_INSTANCE /* Instance */, PVOID Context);
	static void CALLBACK WorkerRemoveClient(PTP_CALLBACK_INSTANCE /* Instance */, PVOID Context);

public:
	Server();
	virtual ~Server();

	bool Init(unsigned short port, int maxPostAccept);
	void Shutdown();

	size_t GetNumClients();
	long GetNumPostAccepts();

	void PostSend(Client* client, Packet* packet);
	void PostBoradcast(Packet* packet);

	void RequestRemoveClient(Client* client);

private:
	void PostAccept();
	void PostRecv(Client* client);

	void OnAccept(IOEvent* event);
	void OnRecv(IOEvent* event, DWORD dwNumberOfBytesTransfered);
	void OnSend(IOEvent* event, DWORD dwNumberOfBytesTransfered);
	void OnClose(IOEvent* event);

	void AddClient(Client* client);
	void RemoveClient(Client* client);

	void UpdateServices();
	void RemoveClientFromServices(Client* client);

private:
	Server& operator=(Server& rhs);
	Server(const Server& rhs);

private:
	TP_IO* m_ListenTPIO;
	SOCKET m_listenSocket;

	TP_WORK* m_AcceptTPWORK; 

	int	m_MaxPostAccept;
	volatile long m_NumPostAccept;

	TP_CALLBACK_ENVIRON m_ClientTPENV;
	TP_CLEANUP_GROUP* m_ClientTPCLEAN;

	typedef std::vector<Client*> ClientList;
	ClientList m_Clients;
	CRITICAL_SECTION m_CSForClients;

	TP_WORK* m_ServiceTPWORK; 
	CRITICAL_SECTION m_CSForServices;

	volatile bool m_ShuttingDown;
};
