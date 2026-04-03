#pragma once
#include "Types.h"
#include "Session.h"
#include "CLFStack.h"
#include <queue>

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

	int GetAcceptTPS();
	int GetRecvMessageTPS();
	int GetSendMessageTPS();


protected:
	// 콘텐츠 레이어 → 네트워크 송신 진입점
	// SessionID로 인덱스 추출 → uniqueId 검증 → sendBuffer 적재 → I/O 요청
	bool SendPost(SessionID sessionId, CPacket* packet);


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
	// -------------------------------------------------------
	// 콘텐츠 큐 아이템
	// -------------------------------------------------------
	struct ContentJob
	{
		SessionID sessionId;
		Session*  session;
		CPacket*  packet;
	};

	// -------------------------------------------------------
	// 세션 슬롯
	// -------------------------------------------------------
	struct Sessions
	{
		Session            session;
		volatile unsigned long long uniqueId = 0;   // 0 = 비활성
	};

	// SessionID 인코딩/디코딩
	// [상위 16bit: 배열 index] [하위 48bit: uniqueId]
	static SessionID          MakeSessionID(WORD index, unsigned long long uniqueId);
	static WORD               GetIndex     (SessionID id);
	static unsigned long long GetUniqueID  (SessionID id);

	// -------------------------------------------------------
	// 스레드 진입점 (static → 인스턴스 메서드로 위임)
	// -------------------------------------------------------
	static DWORD WINAPI WorkerThread (LPVOID arg);
	static DWORD WINAPI AcceptThread (LPVOID arg);
	static DWORD WINAPI MonitorThread(LPVOID arg);
	static DWORD WINAPI ContentThread(LPVOID arg);

	void WorkerThreadProc();
	void AcceptThreadProc();
	void MonitorThreadProc();
	void ContentThreadProc();

	// -------------------------------------------------------
	// 내부 I/O 헬퍼
	// -------------------------------------------------------
	void RecvPost  (Session* session);
	void SendPost  (Session* session);   // 내부용: send 완료 후 재스케줄
	void DeleteSession(Session* session);
	void ReleaseSession(Session* session);

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
	HANDLE              _contentThread;

	int  _workerRunningCount;
	int  _maxConnections;
	bool _running;

	// -------------------------------------------------------
	// 세션 관리 (고정 배열 + lock-free free-list)
	// -------------------------------------------------------
	Sessions         _sessions[MAX_SESSION];

	CLFStack<WORD>   _freeIndices;
	volatile LONGLONG _uniqueIdCounter;
	volatile long     _sessionCount;

	// -------------------------------------------------------
	// 콘텐츠 스레드 큐
	// -------------------------------------------------------
	CRITICAL_SECTION       _contentQueueLock;
	std::queue<ContentJob> _contentQueue;
	HANDLE                 _contentEvent;   // auto-reset event

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
