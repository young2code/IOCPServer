#pragma once

#include <rapidjson\document.h>

class Client;
class EchoService
{
public:
	static void Init();
	static void Shutdown();

	static void OnRecv(Client* client, rapidjson::Document& data);
};
