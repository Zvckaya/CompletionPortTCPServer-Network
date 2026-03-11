#pragma once
#include "CLanServer.h"

class IOCPEchoServer : public CLanServer
{
protected:
	bool OnConnectionRequest(const wchar_t* ip, int port) override;
	void OnClientJoin(const ClientInfo& clientInfo, SessionID sessionId) override;
	void OnClientLeave(SessionID sessionId) override;
	void OnRecv(SessionID sessionId, CPacket* packet) override;
	void OnError(int errorCode, const wchar_t* msg) override;
};
