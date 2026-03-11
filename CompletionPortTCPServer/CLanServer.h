#pragma once
#include "Types.h"
#include "Session.h"

class CPacket;

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
	// Accept 직후 — false 반환 시 연결 거부
	virtual bool OnConnectionRequest(const wchar_t* ip, int port) = 0;

	// 접속 처리 완료 후
	virtual void OnClientJoin(const ClientInfo& clientInfo, SessionID sessionId) = 0;

	// 세션 해제 후
	virtual void OnClientLeave(SessionID sessionId) = 0;

	// 패킷 수신 완료 후
	virtual void OnRecv(SessionID sessionId, CPacket* packet) = 0;

	// 에러 발생 시
	virtual void OnError(int errorCode, const wchar_t* msg) = 0;


private:
	// 스레드 진입점 (static → 인스턴스 메서드로 위임)
	static DWORD WINAPI WorkerThread(LPVOID arg);
	static DWORD WINAPI AcceptThread(LPVOID arg);
	static DWORD WINAPI MonitorThread(LPVOID arg);

	void WorkerThreadProc();
	void AcceptThreadProc();
	void MonitorThreadProc();

	// 내부 I/O 헬퍼
	void RecvPost(Session* session);
	void SendPost(Session* session);
	void DeleteSession(Session* session);
	void ReleaseSession(Session* session);

	// 세션 맵 관리
	// FindSession은 ioCount를 증가시켜 반환 — 사용 후 반드시 ReleaseSession 호출
	Session* FindSession(SessionID sessionId);
	void     AddSession(Session* session);
	void     RemoveSession(Session* session);

	// -------------------------------------------------------
	// IOCP / 소켓
	// -------------------------------------------------------
	HANDLE _hIocp;
	SOCKET _listenSock;

	// -------------------------------------------------------
	// 스레드
	// -------------------------------------------------------
	std::vector<HANDLE> _workerThreads;
	HANDLE              _acceptThread;
	HANDLE              _monitorThread;

	int  _workerRunningCount;
	int  _maxConnections;
	bool _running;

	// -------------------------------------------------------
	// 세션 관리
	// -------------------------------------------------------
	SRWLOCK                              _sessionLock;
	std::unordered_map<SessionID, Session*> _sessions;
	volatile LONGLONG                    _sessionIdCounter;

	// -------------------------------------------------------
	// TPS 카운터
	// raw 카운터 : Interlocked 증가
	// TPS 스냅샷 : MonitorThread가 1초마다 갱신
	// -------------------------------------------------------
	long _acceptCount;
	long _recvCount;
	long _sendCount;

	long _acceptTPS;
	long _recvTPS;
	long _sendTPS;
};
