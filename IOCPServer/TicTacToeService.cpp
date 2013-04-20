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
	EnterCriticalSection(&m_CSForClients);

	m_Clients.clear();

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
	mFSM.RegisterState(kStateCheckResult, BIND_CALLBACKS(CheckResult));
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


void TicTacToeService::Broadcast(rapidjson::Document& data)
{
	Send(mPlayer1, data);
	Send(mPlayer2, data);
}


// Wait
void TicTacToeService::OnEnterWait(int nPrevState)
{
	LOG("TicTacToeService::OnEnterWait()");
}

void TicTacToeService::OnUpdateWait()
{
	for(ClientList::iterator itor = m_Clients.begin() ; itor != m_Clients.end() ; ++itor)
	{
		Client* client = *itor;
		rapidjson::Document jsonData;

		if (client->PopRecvData(jsonData))
		{
			LOG("TicTacToeService::OnUpdateWait() - recieved packet ignored..");
		}
	}

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

	for (int row = 0 ; row < kCellRows ; ++row)
	{
		for (int col = 0 ; col < kCellColumns ; ++col)
		{
			mBoard[row][col] = kSymbolNone;
		}
	}

	mLastMoveRow = 0;
	mLastMoveCol = 0;

	rapidjson::Document data;
	data.SetObject();
	data.AddMember("type", "game_start", data.GetAllocator());
	data.AddMember("game", "tictactoe", data.GetAllocator());
	Broadcast(data);

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

void TicTacToeService::SetPlayerName(Player& player)
{
	rapidjson::Document data;

	if (player.client->PopRecvData(data))
	{
		assert(data["type"].IsString());
		std::string type(data["type"].GetString());
		if (type == "tictactoe")
		{
			assert(data["name"].IsString());
			player.name = data["name"].GetString();
		}
	}
}


void TicTacToeService::OnUpdateSetPlayers() 
{
	SetPlayerName(mPlayer1);
	SetPlayerName(mPlayer2);

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


void TicTacToeService::SetPlayerTurn(int playerTurn)
{
	rapidjson::Document data;
	data.SetObject();
	data.AddMember("type", "tictactoe", data.GetAllocator());
	data.AddMember("subtype", "setturn", data.GetAllocator());
	data.AddMember("player", playerTurn, data.GetAllocator());
	Broadcast(data);
}

void TicTacToeService::CheckPlayerMove(Player& player, Symbol symbol)
{
	rapidjson::Document data;

	if (player.client->PopRecvData(data))
	{
		assert(data["type"].IsString());
		std::string type(data["type"].GetString());
		if (type == "tictactoe")
		{
			assert(data["row"].IsInt());
			int row = data["row"].GetInt();

			assert(data["col"].IsInt());
			int col = data["col"].GetInt();

			if (row >=0 && row < kCellRows && col >= 0 && col < kCellColumns)
			{
				if (mBoard[row][col] == kSymbolNone)
				{
					LOG("TicTacToeService::CheckPlayerMove() - row[%d] / col[%d] set to [%d].", row, col, symbol);
					mBoard[row][col] = symbol;

					mLastMoveRow = row;
					mLastMoveCol = col;

					rapidjson::Document data;
					data.SetObject();
					data.AddMember("type", "tictactoe", data.GetAllocator());
					data.AddMember("subtype", "move", data.GetAllocator());
					data.AddMember("player", symbol == kSymbolOOO ? 1 : 2, data.GetAllocator());
					data.AddMember("row", row, data.GetAllocator());
					data.AddMember("col", col, data.GetAllocator());
					Broadcast(data);

					mFSM.SetState(kStateCheckResult);
				}
				else
				{
					LOG("TicTacToeService::CheckPlayerMove() - row[%d] col[%d] is already set to [%d]. ignored.", row, col, mBoard[row][col]);
				}
			}
			else
			{
				LOG("TicTacToeService::CheckPlayerMove() - row[%d] / col[%d] is invalid. ignored.", row, col);
			}
		}
	}
}


void TicTacToeService::OnEnterPlayer1Turn(int nPrevState)
{
	LOG("TicTacToeService::OnEnterPlayer1Turn()");
	SetPlayerTurn(1);
}
void TicTacToeService::OnUpdatePlayer1Turn()
{
	CheckPlayerMove(mPlayer1, kSymbolOOO);

	rapidjson::Document data;
	if (mPlayer2.client->PopRecvData(data))
	{
		LOG("TicTacToeService::OnUpdatePlayer1Turn() - player2 packet igonred.");
	}
}
void TicTacToeService::OnLeavePlayer1Turn(int nNextState)
{
	LOG("TicTacToeService::OnLeavePlayer1Turn()");
}

void TicTacToeService::OnEnterPlayer2Turn(int nPrevState)
{
	LOG("TicTacToeService::OnEnterPlayer2Turn()");
	SetPlayerTurn(2);
}
void TicTacToeService::OnUpdatePlayer2Turn()
{
	CheckPlayerMove(mPlayer2, kSymbolXXX);

	rapidjson::Document data;
	if (mPlayer1.client->PopRecvData(data))
	{
		LOG("TicTacToeService::OnUpdatePlayer2Turn() - player1 packet igonred.");
	}
}
void TicTacToeService::OnLeavePlayer2Turn(int nNextState)
{
	LOG("TicTacToeService::OnLeavePlayer2Turn()");
}

void TicTacToeService::SetGameEnd(Symbol winning)
{
	rapidjson::Document data;
	data.SetObject();
	data.AddMember("type", "tictactoe", data.GetAllocator());
	data.AddMember("subtype", "result", data.GetAllocator());
	data.AddMember("winner", winning == kSymbolOOO ? 1 : 2, data.GetAllocator());
	Broadcast(data);

	// this service should be terminated!
	mFSM.SetState(kStateWait);
}

void TicTacToeService::OnEnterCheckResult(int nPrevState)
{
	LOG("TicTacToeService::OnEnterCheckResult()");

	assert(mLastMoveRow >= 0 && mLastMoveRow < kCellRows);
	assert(mLastMoveCol >= 0 && mLastMoveCol < kCellColumns);

	Symbol lastSymbol = mBoard[mLastMoveRow][mLastMoveCol];
	assert(lastSymbol != kSymbolNone);

	// check - row straight
	bool straight = true;
	for (int row = 0 ; row < kCellRows ; ++row)
	{
		if(mBoard[row][mLastMoveCol] != lastSymbol)
		{
			straight = false;
			break;
		}
	}
	if (straight)
	{
		LOG("TicTacToeService::OnUpdateCheckResult() - row straight. [%d]", lastSymbol);
		SetGameEnd(lastSymbol);
		return;
	}

	// check | col straight
	straight = true;
	for (int col = 0 ; col < kCellColumns ; ++col)
	{
		if(mBoard[mLastMoveRow][col] != lastSymbol)
		{
			straight = false;
			break;
		}
	}
	if (straight)
	{
		LOG("TicTacToeService::OnUpdateCheckResult() - col straight. [%d]", lastSymbol);
		SetGameEnd(lastSymbol);
		return;
	}

	// check \ diagonal straight. 
	straight = true;
	int curRow = 0;
	int curCol = 0;
	while(curRow < kCellRows && curCol < kCellColumns)
	{
		if (mBoard[curRow][curCol] != lastSymbol)
		{
			straight = false;
			break;
		}
		++curRow;
		++curCol;
	}
	if (straight)
	{
		LOG("TicTacToeService::OnUpdateCheckResult() - \\ straight. [%d]", lastSymbol);
		SetGameEnd(lastSymbol);
		return;
	}

	// check / diagonal straight. 
	curRow = 0;
	curCol = kCellColumns-1;
	while(curRow < kCellRows && curCol >= 0)
	{
		if (mBoard[curRow][curCol] != lastSymbol)
		{
			straight = false;
			break;
		}
		++curRow;
		--curCol;
	}
	if (straight)
	{
		LOG("TicTacToeService::OnUpdateCheckResult() - / straight. [%d]", lastSymbol);
		SetGameEnd(lastSymbol);
		return;
	}

	mFSM.SetState(lastSymbol == kSymbolOOO ? kStatePlayer2Turn : kStatePlayer1Turn);
}

void TicTacToeService::OnUpdateCheckResult() 
{
}

void TicTacToeService::OnLeaveCheckResult(int nNextState)
{
	LOG("TicTacToeService::OnLeaveCheckResult()");
}


void TicTacToeService::OnEnterGameCanceled(int nPrevState)
{
	LOG("TicTacToeService::OnEnterGameCanceled()");

	rapidjson::Document data;
	data.SetObject();
	data.AddMember("type", "tictactoe", data.GetAllocator());
	data.AddMember("subtype", "canceled", data.GetAllocator());
	Broadcast(data);

	// this service should be terminated!
	mFSM.SetState(kStateWait);
}

void TicTacToeService::OnUpdateGameCanceled()
{
}

void TicTacToeService::OnLeaveGameCanceled(int nNextState)
{
	LOG("TicTacToeService::OnLeaveGameCanceled()");
}

