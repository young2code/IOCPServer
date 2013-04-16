#include "EchoService.h"

#include <algorithm>

#include "Server.h"
#include "Client.h"
#include "Packet.h"
#include "CSLocker.h"

#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>


EchoService::EchoService(void)
{
}


EchoService::~EchoService(void)
{
}


void EchoService::Init()
{
	InitializeCriticalSection(&m_CSForClients);
}


void EchoService::Shutdown()
{
	DeleteCriticalSection(&m_CSForClients);
}


void EchoService::Update()
{
	CSLocker lock(&m_CSForClients);
	for(ClientList::iterator itor = m_Clients.begin() ; itor != m_Clients.end() ; ++itor)
	{
		Client* client = *itor;
		rapidjson::Document jsonData;

		if (client->PopRecvData(jsonData))
		{
			rapidjson::Value& type = jsonData["type"];
			if (type.IsString() && 0 == _stricmp(type.GetString(), "echo"))
			{
				// Echo...
				rapidjson::StringBuffer buffer;
				rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
				jsonData.Accept(writer);

				Packet* packet = Packet::Create(client, (const BYTE*)buffer.GetString(), buffer.Size()+1);

				Server::Instance()->PostSend(client, packet);
			}
		}
	}
}


void EchoService::AddClient(Client* client)
{
	CSLocker lock(&m_CSForClients);
	m_Clients.push_back(client);
}


void EchoService::RemoveClient(Client* client)
{
	CSLocker lock(&m_CSForClients);
	ClientList::iterator itor = std::find(m_Clients.begin(), m_Clients.end(), client);
	if (itor != m_Clients.end())
	{
		m_Clients.erase(itor);
	}
}