#include "IOCPEchoServer.h"
#include "CPacket.h"

bool IOCPEchoServer::OnConnectionRequest(const wchar_t* ip, int port)
{
	// 모든 접속 허용
	return true;
}

void IOCPEchoServer::OnClientJoin(const ClientInfo& clientInfo, SessionID sessionId)
{
//	wprintf(L"[Join] SessionID: %llu  IP: %s  Port: %d\n",
//		sessionId, clientInfo.ip, clientInfo.port);
}

void IOCPEchoServer::OnClientLeave(SessionID sessionId)
{
//	wprintf(L"[Leave] SessionID: %llu\n", sessionId);
}

void IOCPEchoServer::OnRecv(SessionID sessionId, CPacket* packet)
{
	// 받은 패킷 그대로 돌려보냄
	SendPacket(sessionId, packet);
}

void IOCPEchoServer::OnError(int errorCode, const wchar_t* msg)
{
	wprintf(L"[Error] Code: %d  Msg: %s\n", errorCode, msg);
}
