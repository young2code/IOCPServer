#pragma once

#include <winsock2.h>
#include <boost/pool/object_pool.hpp>
#include <boost/circular_buffer.hpp>
#include <rapidjson/document.h>
#include <queue>

class Client
{
public:
	enum
	{
		MAX_DATA_SIZE = 256,
	};

	enum State
	{
		WAIT,
		ACCEPTED,
		DISCONNECTED,
	};

public:
	static void Init();
	static void Shutdown();

	static Client* Create();
	static void Destroy(Client* client);

public:
	void SetTPIO(TP_IO* pTPIO) { m_pTPIO = pTPIO; }
	TP_IO* GetTPIO() { return m_pTPIO; }

	void SetState(State state) { m_State = state; }
	State GetState() { return m_State; }

	SOCKET GetSocket() { return m_Socket; }

	// recv
	BYTE* GetRecvCallBuff() { return m_RecvCallBuffer; }
	void OnRecvComplete(int size);
	bool PopRecvData(rapidjson::Document& outData);

private:
	Client(void);
	~Client(void);
	Client& operator=(Client& rhs);
	Client(const Client& rhs);

private:
	bool CreateSocket();

private:
	TP_IO* m_pTPIO;
	State m_State;
	SOCKET m_Socket;
	BYTE m_RecvCallBuffer[MAX_DATA_SIZE];

	typedef boost::circular_buffer<char> RingBuffer;
	RingBuffer m_RecvBuffer;
	CRITICAL_SECTION m_RecvBufferCS;

	typedef boost::object_pool<Client> PoolType; 
	friend PoolType;
	static PoolType sPool;
	static CRITICAL_SECTION sPoolCS;

private:
	static std::vector<Client*> sClients;
};
