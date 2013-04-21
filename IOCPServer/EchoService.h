#pragma once

#include <rapidjson\document.h>

class Client;
class EchoService
{
public:
	EchoService(void);
	~EchoService(void);

	void Init();
	void Shutdown();

	void Update();
	void OnRecv(Client* client, rapidjson::Document& data);
};
