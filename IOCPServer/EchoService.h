#pragma once

#include <winsock2.h>
#include <vector>

class Client;
class EchoService
{
public:
	EchoService(void);
	~EchoService(void);

	void Init();
	void Shutdown();

	void Update();

	void AddClient(Client* client);
	void RemoveClient(Client* client);

private:
	typedef std::vector<Client*> ClientList;
	ClientList m_Clients;
	CRITICAL_SECTION m_CSForClients;
};
