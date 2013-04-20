#pragma once

#include <winsock2.h>
#include <vector>
#include <rapidjson/document.h>

#include "FSM.h"


class Client;
class TicTacToeService
{
public:
	TicTacToeService(void);
	~TicTacToeService(void);

	void Init();
	void Shutdown();

	void Update();

	void AddClient(Client* client);
	void RemoveClient(Client* client);

private:
	enum State
	{
		kStateWait,
		kStateStart,
		kStateSetPlayers,
		kStatePlayer1Turn,
		kStatePlayer2Turn,
		kStateCheckResult,
		kStateGameCanceled,
	};

	struct Player
	{
		Player() : client(NULL) {}

		Client* client;
		std::string name;
	};

	enum Symbol
	{
		kSymbolNone = 0, 
		kSymbolOOO,
		kSymbolXXX, 
	};

	enum CellCount
	{
		kCellRows = 3,
		kCellColumns = 3,
	};

private:
	void InitFSM();
	void ShutdownFSM();

	void OnEnterWait(int nPrevState);
	void OnUpdateWait();
	void OnLeaveWait(int nNextState);

	void OnEnterStart(int nPrevState);
	void OnUpdateStart();
	void OnLeaveStart(int nNextState);

	void OnEnterSetPlayers(int nPrevState);
	void OnUpdateSetPlayers();
	void OnLeaveSetPlayers(int nNextState);

	void OnEnterPlayer1Turn(int nPrevState);
	void OnUpdatePlayer1Turn();
	void OnLeavePlayer1Turn(int nNextState);

	void OnEnterPlayer2Turn(int nPrevState);
	void OnUpdatePlayer2Turn();
	void OnLeavePlayer2Turn(int nNextState);

	void OnEnterCheckResult(int nPrevState);
	void OnUpdateCheckResult();
	void OnLeaveCheckResult(int nNextState);

	void OnEnterGameCanceled(int nPrevState);
	void OnUpdateGameCanceled();
	void OnLeaveGameCanceled(int nNextState);

	void CheckPlayerConnection();


	void SetPlayerName(Player& player);
	void SetPlayerTurn(int playerTurn);
	void CheckPlayerMove(Player& player, Symbol symbol);
	void SetGameEnd(Symbol winning);

	void Send(Player& player, rapidjson::Document& data);
	void Broadcast(rapidjson::Document& data);

private:
	typedef std::vector<Client*> ClientList;
	ClientList m_Clients;
	CRITICAL_SECTION m_CSForClients;

	FSM mFSM;
	Player mPlayer1;
	Player mPlayer2;
	
	Symbol mBoard[kCellRows][kCellColumns];

	int mLastMoveRow;
	int mLastMoveCol;
};
