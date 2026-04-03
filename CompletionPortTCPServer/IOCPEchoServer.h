#pragma once
#include "CLanServer.h"
#include <unordered_map>

class IOCPEchoServer : public CLanServer
{
public:
	IOCPEchoServer();
	~IOCPEchoServer();

protected:
	bool OnConnectionRequest(const wchar_t* ip, int port) override;
	void OnClientJoin(const ClientInfo& clientInfo, SessionID sessionId) override;
	void OnClientLeave(SessionID sessionId) override;
	void OnRecv(SessionID sessionId, CPacket* packet) override;
	void OnError(int errorCode, const wchar_t* msg) override;

private:
	struct Player
	{
		SessionID sessionId;
	};

	// 콘텐츠 레이어 송신 — 네트워크 라이브러리에 SessionID로 요청
	void SendPacket(SessionID sessionId, CPacket* packet);

	CRITICAL_SECTION                        _playerLock;
	std::unordered_map<SessionID, Player>   _players;
};
