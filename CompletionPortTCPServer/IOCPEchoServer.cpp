#include "IOCPEchoServer.h"
#include "lib/CPacket.h"

IOCPEchoServer::IOCPEchoServer()
{
	InitializeCriticalSection(&_playerLock);
}

IOCPEchoServer::~IOCPEchoServer()
{
	DeleteCriticalSection(&_playerLock);
}

bool IOCPEchoServer::OnConnectionRequest(const wchar_t* ip, int port)
{
	return true;
}

void IOCPEchoServer::OnClientJoin(const ClientInfo& clientInfo, SessionID sessionId)
{
	EnterCriticalSection(&_playerLock);
	_players[sessionId] = { sessionId };
	LeaveCriticalSection(&_playerLock);
}

void IOCPEchoServer::OnClientLeave(SessionID sessionId)
{
	EnterCriticalSection(&_playerLock);
	_players.erase(sessionId);
	LeaveCriticalSection(&_playerLock);
}

void IOCPEchoServer::OnRecv(SessionID sessionId, CPacket* packet)
{
	EnterCriticalSection(&_playerLock);
	auto it = _players.find(sessionId);
	if (it == _players.end())
	{
		LeaveCriticalSection(&_playerLock);
		//wprintf(L"[OnRecv] SessionID %llu not in player map — drop\n", sessionId);
		return;
	}
	LeaveCriticalSection(&_playerLock);

	// 에코: 받은 패킷 그대로 돌려보냄
	SendPacket(sessionId, packet);
}


void IOCPEchoServer::OnError(int errorCode, const wchar_t* msg)
{
	wprintf(L"[Error] Code: %d  Msg: %s\n", errorCode, msg);
}

void IOCPEchoServer::SendPacket(SessionID sessionId, CPacket* packet)
{
	SendPost(sessionId, packet);
	/*if (!)
		wprintf(L"[SendPacket] SendPost failed — SessionID %llu (disconnected or reused)\n", sessionId);*/
}
