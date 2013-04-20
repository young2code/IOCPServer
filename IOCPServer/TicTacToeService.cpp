#include "TicTacToeService.h"

#include <algorithm>

#include "Server.h"
#include "Client.h"
#include "Packet.h"
#include "CSLocker.h"
#include "Log.h"

#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include <boost/bind.hpp>


TicTacToeService::TicTacToeService(void)
{
}


TicTacToeService::~TicTacToeService(void)
{
}


void TicTacToeService::Init()
{
	InitializeCriticalSection(&m_CSForClients);

	InitFSM();
}


void TicTacToeService::Shutdown()
{
	ShutdownFSM();

	DeleteCriticalSection(&m_CSForClients);
}

void TicTacToeService::InitFSM()
{
#define BIND_CALLBACKS(State) boost::bind(&TicTacToeService::OnEnter##State, this, _1), \
							  boost::bind(&TicTacToeService::OnUpdate##State, this), \
							  boost::bind(&TicTacToeService::OnLeave##State, this, _1)

	mFSM.RegisterState(kStateWait, BIND_CALLBACKS(Wait));
	mFSM.RegisterState(kStateStart, BIND_CALLBACKS(Start));
	mFSM.RegisterState(kStateSetPlayers, BIND_CALLBACKS(SetPlayers));
	mFSM.RegisterState(kStatePlayer1Turn, BIND_CALLBACKS(Player1Turn));
	mFSM.RegisterState(kStatePlayer2Turn, BIND_CALLBACKS(Player2Turn));
	mFSM.RegisterState(kStateGameEnd, BIND_CALLBACKS(GameEnd));
	mFSM.RegisterState(kStateGameCanceled, BIND_CALLBACKS(GameCanceled));

#undef BIND_CALLBACKS

	mFSM.SetState(kStateWait);
}

void TicTacToeService::ShutdownFSM()
{
	mFSM.Reset(false);
}


void TicTacToeService::Update()
{
	CSLocker lock(&m_CSForClients);

	CheckPlayerConnection();

	mFSM.Update();
}


void TicTacToeService::AddClient(Client* client)
{
	CSLocker lock(&m_CSForClients);
	m_Clients.push_back(client);
}


void TicTacToeService::RemoveClient(Client* client)
{
	CSLocker lock(&m_CSForClients);
	ClientList::iterator itor = std::find(m_Clients.begin(), m_Clients.end(), client);
	if (itor != m_Clients.end())
	{
		m_Clients.erase(itor);
	}
}


void TicTacToeService::CheckPlayerConnection()
{
	if (mFSM.GetState() != kStateWait)
	{
		ClientList::iterator itor1 = std::find(m_Clients.begin(), m_Clients.end(), mPlayer1.client);
		ClientList::iterator itor2 = std::find(m_Clients.begin(), m_Clients.end(), mPlayer2.client);
		if (itor1 == m_Clients.end() || itor2 == m_Clients.end())
		{
			mFSM.SetState(kStateGameCanceled);
		}
	}
}


void TicTacToeService::Send(Player& player, rapidjson::Document& data)
{
	rapidjson::StringBuffer buffer;
	rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
	data.Accept(writer);

	Packet* packet = Packet::Create(player.client, (const BYTE*)buffer.GetString(), buffer.Size()+1); // includnig null.

	Server::Instance()->PostSend(player.client, packet);
}

// Wait
void TicTacToeService::OnEnterWait(int nPrevState)
{
	LOG("TicTacToeService::OnEnterWait()");
}

void TicTacToeService::OnUpdateWait()
{
	// we will handle mutiple games later.. 
	if (m_Clients.size() >= 2)
	{
		mPlayer1.client = m_Clients[0];
		mPlayer1.name.clear();

		mPlayer2.client = m_Clients[1];
		mPlayer2.name.clear();

		mFSM.SetState(kStateStart);
	}
}

void TicTacToeService::OnLeaveWait(int nNextState)
{
	LOG("TicTacToeService::OnLeaveWait()");
}


// Start
void TicTacToeService::OnEnterStart(int nPrevState)
{
	LOG("TicTacToeService::OnEnterStart()");

	rapidjson::Document startData;
	startData.SetObject();
	startData.AddMember("type", "game_start", startData.GetAllocator());
	startData.AddMember("game", "tictactoe", startData.GetAllocator());

	Send(mPlayer1, startData);
	Send(mPlayer2, startData);

	mFSM.SetState(kStateSetPlayers);
}
void TicTacToeService::OnUpdateStart(){}
void TicTacToeService::OnLeaveStart(int nNextState)
{
	LOG("TicTacToeService::OnLeaveStart()");
}

void TicTacToeService::OnEnterSetPlayers(int nPrevState)
{
	LOG("TicTacToeService::OnEnterSetPlayers()");

}

void TicTacToeService::OnUpdateSetPlayers() 
{
	rapidjson::Document jsonData;

	if (mPlayer1.client->PopRecvData(jsonData))
	{
		rapidjson::Value& type = jsonData["type"];
		if (type.IsString() && 0 == _stricmp(type.GetString(), "tictactoe"))
		{
			mPlayer1.name = jsonData["name"].GetString();
		}
	}

	if (mPlayer2.client->PopRecvData(jsonData))
	{
		rapidjson::Value& type = jsonData["type"];
		if (type.IsString() && 0 == _stricmp(type.GetString(), "tictactoe"))
		{
			mPlayer2.name = jsonData["name"].GetString();
		}
	}

	if (!mPlayer1.name.empty() && !mPlayer2.name.empty())
	{
		const char* data = "{\"type\":\"tictactoe\", \"player1\":\"tictactoe\"}";
		int size = strlen(data)+1;

		rapidjson::Document playerData;
		playerData.SetObject();
		playerData.AddMember("type", "tictactoe", playerData.GetAllocator());
		playerData.AddMember("subtype", "setplayers", playerData.GetAllocator());
		playerData.AddMember("player1_name", mPlayer1.name.c_str(), playerData.GetAllocator());
		playerData.AddMember("player2_name", mPlayer2.name.c_str(), playerData.GetAllocator());

		playerData.AddMember("assigned_to", 1, playerData.GetAllocator());
		Send(mPlayer1, playerData);

		playerData["assigned_to"] = 2;
		Send(mPlayer2, playerData);

		mFSM.SetState(kStatePlayer1Turn);
	}
}


void TicTacToeService::OnLeaveSetPlayers(int nNextState) 
{
	LOG("TicTacToeService::OnLeaveSetPlayers()");
}


void TicTacToeService::OnEnterPlayer1Turn(int nPrevState)
{
	LOG("TicTacToeService::OnEnterPlayer1Turn()");
}
void TicTacToeService::OnUpdatePlayer1Turn()
{
}
void TicTacToeService::OnLeavePlayer1Turn(int nNextState)
{
	LOG("TicTacToeService::OnLeavePlayer1Turn()");
}

void TicTacToeService::OnEnterPlayer2Turn(int nPrevState)
{
	LOG("TicTacToeService::OnEnterPlayer2Turn()");
}
void TicTacToeService::OnUpdatePlayer2Turn()
{
}
void TicTacToeService::OnLeavePlayer2Turn(int nNextState)
{
	LOG("TicTacToeService::OnLeavePlayer2Turn()");
}

void TicTacToeService::OnEnterGameEnd(int nPrevState)
{
	LOG("TicTacToeService::OnEnterGameEnd()");
}
void TicTacToeService::OnUpdateGameEnd()
{
}
void TicTacToeService::OnLeaveGameEnd(int nNextState)
{
	LOG("TicTacToeService::OnLeaveGameEnd()");
}


void TicTacToeService::OnEnterGameCanceled(int nPrevState)
{
	LOG("TicTacToeService::OnEnterGameCanceled()");
	mFSM.SetState(kStateWait);
}

void TicTacToeService::OnUpdateGameCanceled()
{
}

void TicTacToeService::OnLeaveGameCanceled(int nNextState)
{
	LOG("TicTacToeService::OnLeaveGameCanceled()");
}

