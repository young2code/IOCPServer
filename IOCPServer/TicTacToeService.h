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
		kStateGameEnd,
		kStateGameCanceled,
	};

	struct Player
	{
		Player() : client(NULL) {}

		Client* client;
		std::string name;
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

	void OnEnterGameEnd(int nPrevState);
	void OnUpdateGameEnd();
	void OnLeaveGameEnd(int nNextState);

	void OnEnterGameCanceled(int nPrevState);
	void OnUpdateGameCanceled();
	void OnLeaveGameCanceled(int nNextState);

	void CheckPlayerConnection();

	void Send(Player& player, rapidjson::Document& data);

private:
	typedef std::vector<Client*> ClientList;
	ClientList m_Clients;
	CRITICAL_SECTION m_CSForClients;

	FSM mFSM;
	Player mPlayer1;
	Player mPlayer2;
};
