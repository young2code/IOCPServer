#include "EchoService.h"

#include <string>

#include "Server.h"
#include "Client.h"
#include "Packet.h"
#include "Log.h"

#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>


/*static*/ void EchoService::Init()
{
	LOG("EchoService::Init()");
}

/*static*/ void EchoService::Shutdown()
{
	LOG("EchoService::Init()");
}

/*static*/ void EchoService::OnRecv(Client* client, rapidjson::Document& data)
{
	assert(data["type"].IsString());
	std::string type(data["type"].GetString());
	if (type == "echo")
	{
		rapidjson::StringBuffer buffer;
		rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
		data.Accept(writer);

		Packet* packet = Packet::Create(client, (const BYTE*)buffer.GetString(), buffer.Size()+1);

		Server::Instance()->PostSend(client, packet);
	}
}
