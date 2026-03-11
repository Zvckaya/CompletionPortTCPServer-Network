#pragma once
#include "Types.h"

class CPacket;

using SessionID = unsigned long long;

struct ClientInfo
{
	wchar_t ip[16];
	int     port;
};


class CLanServer
{
public:
	CLanServer();
	virtual ~CLanServer();

	bool Start(const wchar_t* ip, int port,
		int workerThreadCount, int workerRunningCount,
		bool nagle, int maxConnections);
	void Stop();

	int  GetSessionCount();

	bool Disconnect(SessionID sessionId);
	bool SendPacket(SessionID sessionId, CPacket* packet);

	int GetAcceptTPS();
	int GetRecvMessageTPS();
	int GetSendMessageTPS();


protected:
	virtual bool OnConnectionRequest(const wchar_t* ip, int port) = 0;

	// 접속 처리 완료 후 호출.
	virtual void OnClientJoin(const ClientInfo& clientInfo, SessionID sessionId) = 0;

	// 세션 해제 후 호출.
	virtual void OnClientLeave(SessionID sessionId) = 0;

	// 패킷 수신 완료 후 호출. packet 사용 후 반납 불필요 (내부 관리).
	virtual void OnRecv(SessionID sessionId, CPacket* packet) = 0;

	// 에러 발생 시 호출.
	virtual void OnError(int errorCode, const wchar_t* msg) = 0;


private:
	static DWORD WINAPI WorkerThread(LPVOID arg);
	static DWORD WINAPI AcceptThread(LPVOID arg);
	static DWORD WINAPI MonitorThread(LPVOID arg);

	HANDLE  _hIocp;
	SOCKET  _listenSock;

	std::vector<HANDLE> _workerThreads;
	HANDLE              _acceptThread;
	HANDLE              _monitorThread;

	int _workerRunningCount;
	int _maxConnections;

	bool _running;

	long _acceptCount;
	long _recvCount;
	long _sendCount;

	long _acceptTPS;
	long _recvTPS;
	long _sendTPS;
};
