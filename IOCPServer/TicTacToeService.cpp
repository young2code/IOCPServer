#include "TicTacToeService.h"

#include <algorithm>

#include "Server.h"
#include "Client.h"
#include "Packet.h"
#include "Log.h"

#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include <boost/bind.hpp>

/*static*/  void TicTacToeService::CreateOrEnter(Client* client, rapidjson::Document& data, std::vector<TicTacToeService*>& list)
{
	assert(data["type"].IsString());
	std::string type(data["type"].GetString());
	if (type == "service_create")
	{
		assert(data["name"].IsString());
		std::string name(data["name"].GetString());

		if (name == "tictactoe")
		{
			for (size_t i = 0 ; i < list.size() ; ++i)
			{
				TicTacToeService* service = list[i];

				if (service->mFSM.GetState() == kStateWait && service->m_Clients.size() < 2)
				{
					service->AddClient(client);
					return;
				}
			}

			TicTacToeService* newService = new TicTacToeService;
			newService->Init();
			newService->AddClient(client);
			list.push_back(newService);
			return;
		}
	}
}


/*static*/ void TicTacToeService::Flush(std::vector<TicTacToeService*>& list)
{
	// do whatever you want for caching.

	for (auto itor = list.begin() ; itor != list.end() ; )
	{
		TicTacToeService* service = *itor;

		if (service->mFSM.GetState() == kStateWait && service->m_Clients.empty())
		{
			service->Shutdown();
			delete service;
			itor = list.erase(itor);
		}
		else
		{
			++itor;
		}
	}
}


TicTacToeService::TicTacToeService(void)
{
}


TicTacToeService::~TicTacToeService(void)
{
}


void TicTacToeService::Init()
{
	InitFSM();
}


void TicTacToeService::Shutdown()
{
	m_Clients.clear();

	ShutdownFSM();
}

void TicTacToeService::InitFSM()
{
#define BIND_CALLBACKS(State) boost::bind(&TicTacToeService::OnEnter##State, this, _1), \
							  boost::bind(&TicTacToeService::DummyUpdate, this), \
							  boost::bind(&TicTacToeService::OnLeave##State, this, _1)

	mFSM.RegisterState(kStateWait, BIND_CALLBACKS(Wait));
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
	CheckPlayerConnection();

	mFSM.Update();
}


void TicTacToeService::OnRecv(Client* client, rapidjson::Document& data)
{
	switch(mFSM.GetState())
	{
	case kStateWait:			OnUpdateWait(client, data);			break;
	case kStatePlayer1Turn:		OnUpdatePlayer1Turn(client, data);	break;
	case kStatePlayer2Turn:		OnUpdatePlayer2Turn(client, data);	break;
	case kStateCheckResult:		OnUpdateCheckResult(client, data);	break;
	case kStateGameCanceled:	OnUpdateGameCanceled(client, data);	break;

	default:
		assert(0);
		return;
	}
}


void TicTacToeService::AddClient(Client* client)
{
	assert(mFSM.GetState() == kStateWait);
	assert(m_Clients.size() < 2);

	m_Clients.push_back(client);

	if (m_Clients.size() == 1)
	{
		mPlayer1.client = client;
		mPlayer1.name.clear();
	}
	else if (m_Clients.size() == 2)
	{
		mPlayer2.client = client;
		mPlayer2.name.clear();
	}
}


void TicTacToeService::RemoveClient(Client* client)
{
	ClientList::iterator itor = std::find(m_Clients.begin(), m_Clients.end(), client);
	if (itor != m_Clients.end())
	{
		m_Clients.erase(itor);
	}
}


void TicTacToeService::CheckPlayerConnection()
{
	if (mFSM.GetState() == kStateWait)
	{
		if (m_Clients.empty())
		{
			mFSM.SetState(kStateGameCanceled);
		}
	}
	else
	{
		ClientList::iterator itor1 = std::find(m_Clients.begin(), m_Clients.end(), mPlayer1.client);
		ClientList::iterator itor2 = std::find(m_Clients.begin(), m_Clients.end(), mPlayer2.client);
		if (itor1 == m_Clients.end() || itor2 == m_Clients.end())
		{
			mFSM.SetState(kStateGameCanceled);
		}
	}
}


void TicTacToeService::Send(Client* client, rapidjson::Document& data)
{
	rapidjson::StringBuffer buffer;
	rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
	data.Accept(writer);

	Packet* packet = Packet::Create(client, (const BYTE*)buffer.GetString(), buffer.Size()+1); // includnig null.

	Server::Instance()->PostSend(client, packet);
}


void TicTacToeService::Broadcast(rapidjson::Document& data)
{
	for (size_t i = 0 ; i < m_Clients.size() ; ++i)
	{
		Send(m_Clients[i], data);
	}
}

void TicTacToeService::SetPlayerName(Player& player, rapidjson::Document& data)
{
	assert(data["type"].IsString());
	std::string type(data["type"].GetString());
	if (type == "tictactoe")
	{
		assert(data["name"].IsString());
		player.name = data["name"].GetString();
	}
}

// Wait
void TicTacToeService::OnEnterWait(int nPrevState)
{
	LOG("TicTacToeService::OnEnterWait()");

	mPlayer1.client = NULL;
	mPlayer1.name.clear();

	mPlayer2.client = NULL;
	mPlayer2.name.clear();
	
	for (int row = 0 ; row < kCellRows ; ++row)
	{
		for (int col = 0 ; col < kCellColumns ; ++col)
		{
			mBoard[row][col] = kSymbolNone;
		}
	}

	mLastMoveRow = 0;
	mLastMoveCol = 0;
}

void TicTacToeService::OnUpdateWait(Client* client, rapidjson::Document& data)
{
	if (mPlayer1.client == client)
	{
		SetPlayerName(mPlayer1, data);
	}
	else if (mPlayer2.client == client)
	{
		SetPlayerName(mPlayer2, data);
	}
	else
	{
		return;
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
		Send(mPlayer1.client, playerData);

		playerData["assigned_to"] = 2;
		Send(mPlayer2.client, playerData);

		mFSM.SetState(kStatePlayer1Turn);
	}
}

void TicTacToeService::OnLeaveWait(int nNextState)
{
	LOG("TicTacToeService::OnLeaveWait()");
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

void TicTacToeService::CheckPlayerMove(Player& player, Symbol symbol, rapidjson::Document& data)
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


void TicTacToeService::OnEnterPlayer1Turn(int nPrevState)
{
	LOG("TicTacToeService::OnEnterPlayer1Turn()");
	SetPlayerTurn(1);
}

void TicTacToeService::OnUpdatePlayer1Turn(Client* client, rapidjson::Document& data)
{
	if (mPlayer1.client == client)
	{
		CheckPlayerMove(mPlayer1, kSymbolOOO, data);
	}
	else
	{
		return;
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

void TicTacToeService::OnUpdatePlayer2Turn(Client* client, rapidjson::Document& data)
{
	if (mPlayer2.client == client)
	{
		CheckPlayerMove(mPlayer2, kSymbolXXX, data);
	}
	else
	{
		return;
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

	switch(winning)
	{
	case kSymbolNone:	data.AddMember("winner", -1, data.GetAllocator());	break;
	case kSymbolOOO:	data.AddMember("winner", 1, data.GetAllocator());	break;
	case kSymbolXXX:	data.AddMember("winner", 2, data.GetAllocator());	break;

	default:
		assert(0);
		return;
	}

	Broadcast(data);

	m_Clients.clear();

	mFSM.SetState(kStateWait);
}

bool TicTacToeService::CheckRowStraight(int col, Symbol symbol)
{
	for (int row = 0 ; row < kCellRows ; ++row)
	{
		if (mBoard[row][col] != symbol)
		{
			return false;
		}
	}

	return true;
}

bool TicTacToeService::CheckColStraight(int row, Symbol symbol)
{
	for (int col = 0 ; col < kCellColumns ; ++col)
	{
		if (mBoard[row][col] != symbol)
		{
			return false;
		}
	}

	return true;
}

bool TicTacToeService::CheckSlashStraight(Symbol symbol)
{
	int row = 0;
	int col = kCellColumns-1;
	while(row < kCellRows && col >= 0)
	{
		if (mBoard[row][col] != symbol)
		{
			return false;
		}
		++row;
		--col;
	}

	return true;

}

bool TicTacToeService::CheckBackSlashStraight(Symbol symbol)
{
	int row = 0;
	int col = 0;
	while(row < kCellRows && col < kCellColumns)
	{
		if (mBoard[row][col] != symbol)
		{
			return false;
		}
		++row;
		++col;
	}

	return true;
}

bool TicTacToeService::CheckBoardIsFull()
{
	for (int row = 0 ; row < kCellRows ; ++row)
	{
		for (int col = 0 ; col < kCellColumns ; ++col)
		{
			if (mBoard[row][col] == kSymbolNone)
			{
				return false;
			}
		}
	}
	return true;
}


void TicTacToeService::OnEnterCheckResult(int nPrevState)
{
	LOG("TicTacToeService::OnEnterCheckResult()");

	assert(mLastMoveRow >= 0 && mLastMoveRow < kCellRows);
	assert(mLastMoveCol >= 0 && mLastMoveCol < kCellColumns);

	Symbol lastSymbol = mBoard[mLastMoveRow][mLastMoveCol];
	assert(lastSymbol != kSymbolNone);

	if (CheckRowStraight(mLastMoveCol, lastSymbol))
	{
		LOG("TicTacToeService::OnUpdateCheckResult() - row straight. [%d]", lastSymbol);
		SetGameEnd(lastSymbol);
		return;
	}

	if (CheckColStraight(mLastMoveRow, lastSymbol))
	{
		LOG("TicTacToeService::OnUpdateCheckResult() - col straight. [%d]", lastSymbol);
		SetGameEnd(lastSymbol);
		return;
	}

	if (CheckBackSlashStraight(lastSymbol))
	{
		LOG("TicTacToeService::OnUpdateCheckResult() - \\ straight. [%d]", lastSymbol);
		SetGameEnd(lastSymbol);
		return;
	}

	if (CheckSlashStraight(lastSymbol))
	{
		LOG("TicTacToeService::OnUpdateCheckResult() - / straight. [%d]", lastSymbol);
		SetGameEnd(lastSymbol);
		return;
	}

	if (CheckBoardIsFull())
	{
		LOG("TicTacToeService::OnUpdateCheckResult() - draw");
		SetGameEnd(kSymbolNone);
		return;
	}

	mFSM.SetState(lastSymbol == kSymbolOOO ? kStatePlayer2Turn : kStatePlayer1Turn);
}

void TicTacToeService::OnUpdateCheckResult(Client* client, rapidjson::Document& data) 
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

	m_Clients.clear();

	mFSM.SetState(kStateWait);
}


void TicTacToeService::OnUpdateGameCanceled(Client* client, rapidjson::Document& data)
{
}

void TicTacToeService::OnLeaveGameCanceled(int nNextState)
{
	LOG("TicTacToeService::OnLeaveGameCanceled()");
}

