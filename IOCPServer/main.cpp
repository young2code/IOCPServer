
#include <string>
#include <iostream>
using namespace std;

#include "Log.h"
#include "Network.h"
#include "Server.h"

void main(int argc, char* argv[])
{
	Log::Init();

	if( argc != 3)
	{
		LOG("Please add port and max number of accept posts.");
		LOG("(ex) 17000 100");
		return;
	}

	u_short port = static_cast<u_short>( atoi(argv[1]) );
	int maxPostAccept = atoi(argv[2]);

	LOG("Input : port : %d, max accept : %d", port, maxPostAccept);

	if(Network::Init() == false)
	{
		ERROR_MSG("Network::Init() failed");
		return;
	}

	Server::Create();
	
	if(Server::Instance()->Init(port, maxPostAccept) == false)
	{
		ERROR_MSG("Server::Init() failed");
		return;
	}

#ifndef _DEBUG
	Log::EnableTrace(false);
#endif

	string input;
	bool loop = true;
	while(loop)
	{
		std::getline(cin, input);

		if(input == "`client_size")
		{
			LOG(" Number of Clients : %d", Server::Instance()->GetNumClients());
		}
		if(input == "`accept_size")
		{
			LOG(" Number of Accept posts : %d", Server::Instance()->GetNumPostAccepts());
		}
		else if(input == "`enable_trace")
		{
			Log::EnableTrace(true);
		}
		else if(input == "`disable_trace")
		{
			Log::EnableTrace(false);
		}
		// we should have a way to cancel thread works started by TrySubmitThreadpoolCallback
		//else if  (input == "`shutdown")
		//{
		//	loop = false;
		//}
	}

	Server::Instance()->Shutdown();

	Server::Destroy();

	Network::Shutdown();

	Log::Shutdown();

	return;
}
