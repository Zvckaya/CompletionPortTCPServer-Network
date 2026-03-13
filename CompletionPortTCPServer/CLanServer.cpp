#include "CLanServer.h"
#include "lib/CPacket.h"

// ============================================================
// 생성 / 소멸
// ============================================================

CLanServer::CLanServer()
	: _hIocp(NULL), _listenSock(INVALID_SOCKET)
	, _acceptThread(NULL), _monitorThread(NULL)
	, _workerRunningCount(0), _maxConnections(0), _running(false)
	, _sessionIdCounter(0LL)
	, _acceptCount(0), _recvCount(0), _sendCount(0)
	, _acceptTPS(0), _recvTPS(0), _sendTPS(0)
{
	InitializeSRWLock(&_sessionLock);
}

CLanServer::~CLanServer()
{
	Stop();
}


// ============================================================
// 서버 제어
// ============================================================

bool CLanServer::Start(const wchar_t* ip, int port,
	int workerThreadCount, int workerRunningCount,
	bool nagle, int maxConnections)
{
	_workerRunningCount = workerRunningCount;
	_maxConnections     = maxConnections;
	_running            = true;

	// WSA 초기화
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
	{
		OnError(WSAGetLastError(), L"WSAStartup failed");
		return false;
	}

	// IOCP 생성
	_hIocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, workerRunningCount);
	if (_hIocp == NULL)
	{
		OnError(GetLastError(), L"CreateIoCompletionPort failed");
		WSACleanup();
		return false;
	}

	// 리슨 소켓
	_listenSock = socket(AF_INET, SOCK_STREAM, 0);
	if (_listenSock == INVALID_SOCKET)
	{
		OnError(WSAGetLastError(), L"listen socket failed");
		return false;
	}

	// 나글 옵션
	if (!nagle)
	{
		int opt = 1;
		setsockopt(_listenSock, IPPROTO_TCP, TCP_NODELAY, (char*)&opt, sizeof(opt));
	}

	// 바인드 / 리슨
	SOCKADDR_IN addr;
	ZeroMemory(&addr, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port   = htons(port);

	if (ip == nullptr || wcscmp(ip, L"0.0.0.0") == 0)
		addr.sin_addr.S_un.S_addr = htonl(INADDR_ANY);
	else
		InetPtonW(AF_INET, ip, &addr.sin_addr);

	if (bind(_listenSock, (SOCKADDR*)&addr, sizeof(addr)) == SOCKET_ERROR)
	{
		OnError(WSAGetLastError(), L"bind failed");
		return false;
	}

	if (listen(_listenSock, SOMAXCONN) == SOCKET_ERROR)
	{
		OnError(WSAGetLastError(), L"listen failed");
		return false;
	}

	// 워커 스레드 생성
	for (int i = 0; i < workerThreadCount; i++)
	{
		HANDLE h = CreateThread(NULL, 0, WorkerThread, this, 0, NULL);
		_workerThreads.push_back(h);
	}

	// 어셉트 스레드 / 모니터 스레드 생성
	_acceptThread  = CreateThread(NULL, 0, AcceptThread,  this, 0, NULL);
	_monitorThread = CreateThread(NULL, 0, MonitorThread, this, 0, NULL);

	return true;
}

void CLanServer::Stop()
{
	if (!_running)
		return;

	_running = false;

	// 어셉트 스레드 종료 — listenSock 닫으면 accept() 리턴
	closesocket(_listenSock);
	_listenSock = INVALID_SOCKET;

	if (_acceptThread != NULL)
	{
		WaitForSingleObject(_acceptThread, INFINITE);
		CloseHandle(_acceptThread);
		_acceptThread = NULL;
	}

	// 워커 스레드 종료 — 스레드 수만큼 0 바이트 완료 패킷 전송
	for (size_t i = 0; i < _workerThreads.size(); i++)
		PostQueuedCompletionStatus(_hIocp, 0, 0, nullptr);

	WaitForMultipleObjects((DWORD)_workerThreads.size(), _workerThreads.data(), TRUE, INFINITE);
	for (HANDLE h : _workerThreads) CloseHandle(h);
	_workerThreads.clear();

	// 모니터 스레드 종료
	if (_monitorThread != NULL)
	{
		WaitForSingleObject(_monitorThread, INFINITE);
		CloseHandle(_monitorThread);
		_monitorThread = NULL;
	}

	CloseHandle(_hIocp);
	_hIocp = NULL;

	WSACleanup();
}


// ============================================================
// 공개 API
// ============================================================

int CLanServer::GetSessionCount()
{
	AcquireSRWLockShared(&_sessionLock);
	int cnt = (int)_sessions.size();
	ReleaseSRWLockShared(&_sessionLock);
	return cnt;
}

bool CLanServer::Disconnect(SessionID sessionId)
{
	Session* s = FindSession(sessionId);
	if (s == nullptr)
		return false;

	DeleteSession(s);
	ReleaseSession(s);   // FindSession에서 증가시킨 ioCount 감소
	return true;
}

bool CLanServer::SendPacket(SessionID sessionId, CPacket* packet)
{
	Session* s = FindSession(sessionId);
	if (s == nullptr)
		return false;

	EnterCriticalSection(&s->lock);
	s->sendBuffer.Enqueue(packet->GetBufferPtr(), packet->GetDataSize());
	LeaveCriticalSection(&s->lock);

	SendPost(s);

	InterlockedIncrement(&_sendCount);

	ReleaseSession(s);   // FindSession에서 증가시킨 ioCount 감소
	return true;
}


// ============================================================
// 모니터링
// ============================================================

int CLanServer::GetAcceptTPS()     { return (int)_acceptTPS; }
int CLanServer::GetRecvMessageTPS(){ return (int)_recvTPS;   }
int CLanServer::GetSendMessageTPS(){ return (int)_sendTPS;   }


// ============================================================
// 스레드 진입점 (static → 인스턴스 위임)
// ============================================================

DWORD WINAPI CLanServer::WorkerThread(LPVOID arg)
{
	((CLanServer*)arg)->WorkerThreadProc();
	return 0;
}

DWORD WINAPI CLanServer::AcceptThread(LPVOID arg)
{
	((CLanServer*)arg)->AcceptThreadProc();
	return 0;
}

DWORD WINAPI CLanServer::MonitorThread(LPVOID arg)
{
	((CLanServer*)arg)->MonitorThreadProc();
	return 0;
}


// ============================================================
// 워커 스레드
// ============================================================

void CLanServer::WorkerThreadProc()
{
	while (true)
	{
		DWORD      cbTransferred = 0;
		Session*   session       = nullptr;
		OVERLAPPED* lpOverlapped = nullptr;

		BOOL ret = GetQueuedCompletionStatus(
			_hIocp, &cbTransferred,
			(PULONG_PTR)&session, &lpOverlapped, INFINITE);

		// Stop() 신호
		if (session == nullptr && lpOverlapped == nullptr)
			break;

		if (ret == FALSE || cbTransferred == 0)
		{
			DeleteSession(session);
			continue;
		}

		if (lpOverlapped == &session->recvOverlapped)
		{
			session->recvBuffer.MoveRear(cbTransferred);

			int   dataLen = session->recvBuffer.GetUseSize();
			CPacket packet;
			{
				char* tmp = new char[dataLen];
				session->recvBuffer.Dequeue(tmp, dataLen);
				packet.SetData(tmp, dataLen);
				delete[] tmp;
			}

			InterlockedIncrement(&_recvCount);
			OnRecv(session->sessionId, &packet);

			RecvPost(session);
			ReleaseSession(session);
		}
		else if (lpOverlapped == &session->sendOverlapped)
		{
			EnterCriticalSection(&session->lock);
			session->sendBuffer.MoveFront(cbTransferred);
			LeaveCriticalSection(&session->lock);

			InterlockedExchange(&session->sendFlag, 0);

			if (session->sendBuffer.GetUseSize() > 0)
				SendPost(session);

			ReleaseSession(session);
		}
	}
}


// ============================================================
// 어셉트 스레드
// ============================================================

void CLanServer::AcceptThreadProc()
{
	SOCKADDR_IN clientAddr;
	int         addrLen = sizeof(clientAddr);

	while (_running)
	{
		SOCKET clientSock = accept(_listenSock, (SOCKADDR*)&clientAddr, &addrLen);

		if (clientSock == INVALID_SOCKET)
		{
			int err = WSAGetLastError();
			if (err == WSAEINTR || err == WSAENOTSOCK)
				break;
			continue;
		}

		// 최대 접속자 초과
		if (GetSessionCount() >= _maxConnections)
		{
			closesocket(clientSock);
			continue;
		}

		// IP / 포트 추출
		wchar_t ipStr[16];
		InetNtopW(AF_INET, &clientAddr.sin_addr, ipStr, 16);
		int port = ntohs(clientAddr.sin_port);

		// 서버 측 연결 허용 여부 판단
		if (!OnConnectionRequest(ipStr, port))
		{
			closesocket(clientSock);
			continue;
		}

		// 송신 버퍼 0 설정 (Zero-Copy)
		int zero = 0;
		setsockopt(clientSock, SOL_SOCKET, SO_SNDBUF, (char*)&zero, sizeof(zero));

		// 세션 생성
		Session* session  = new Session;
		session->sock     = clientSock;
		session->sessionId = (SessionID)InterlockedIncrement64(&_sessionIdCounter);

		AddSession(session);

		CreateIoCompletionPort((HANDLE)clientSock, _hIocp, (ULONG_PTR)session, 0);

		// 접속 완료 이벤트
		ClientInfo info;
		wcscpy_s(info.ip, ipStr);
		info.port = port;
		OnClientJoin(info, session->sessionId);

		InterlockedIncrement(&_acceptCount);

		RecvPost(session);
	}
}


// ============================================================
// 모니터 스레드 (1초마다 TPS 갱신)
// ============================================================

void CLanServer::MonitorThreadProc()
{
	while (_running)
	{
		Sleep(1000);
		_acceptTPS = InterlockedExchange(&_acceptCount, 0);
		_recvTPS   = InterlockedExchange(&_recvCount,   0);
		_sendTPS   = InterlockedExchange(&_sendCount,   0);
	}
}


// ============================================================
// 내부 I/O 헬퍼
// ============================================================

void CLanServer::RecvPost(Session* session)
{
	CRingBuffer* rb = &session->recvBuffer;

	int freeSize = rb->GetFreeSize();
	if (freeSize <= 0)
		return;

	WSABUF wsaBufs[2];
	int    bufCount = 1;

	wsaBufs[0].buf = rb->GetRearBufferPtr();
	wsaBufs[0].len = rb->DirectEnqueueSize();

	int len2 = freeSize - wsaBufs[0].len;
	if (len2 > 0)
	{
		wsaBufs[1].buf = rb->GetBufferPtr();
		wsaBufs[1].len = len2;
		bufCount = 2;
	}

	ZeroMemory(&session->recvOverlapped, sizeof(OVERLAPPED));
	InterlockedIncrement(&session->ioCount);

	DWORD recvBytes = 0, flags = 0;
	int ret = WSARecv(session->sock, wsaBufs, bufCount, &recvBytes, &flags,
		&session->recvOverlapped, NULL);

	if (ret == SOCKET_ERROR && WSAGetLastError() != ERROR_IO_PENDING)
		DeleteSession(session);
}

void CLanServer::SendPost(Session* session)
{
	if (InterlockedCompareExchange(&session->sendFlag, 1, 0) == 1)
		return;

	int useSize = session->sendBuffer.GetUseSize();
	if (useSize == 0)
	{
		InterlockedExchange(&session->sendFlag, 0);
		return;
	}

	WSABUF wsaBufs[2];
	int    bufCount = 1;

	wsaBufs[0].buf = session->sendBuffer.GetFrontBufferPtr();
	wsaBufs[0].len = session->sendBuffer.DirectDequeueSize();

	int remain = useSize - wsaBufs[0].len;
	if (remain > 0)
	{
		wsaBufs[1].buf = session->sendBuffer.GetBufferPtr();
		wsaBufs[1].len = remain;
		bufCount = 2;
	}

	ZeroMemory(&session->sendOverlapped, sizeof(OVERLAPPED));
	InterlockedIncrement(&session->ioCount);

	DWORD sendBytes = 0, flags = 0;
	int ret = WSASend(session->sock, wsaBufs, bufCount, &sendBytes, flags,
		&session->sendOverlapped, NULL);

	if (ret == SOCKET_ERROR && WSAGetLastError() != ERROR_IO_PENDING)
		DeleteSession(session);
}

void CLanServer::DeleteSession(Session* session)
{
	EnterCriticalSection(&session->lock);
	SOCKET sock      = session->sock;
	session->sock    = INVALID_SOCKET;
	LeaveCriticalSection(&session->lock);

	if (sock == INVALID_SOCKET)
	{
		ReleaseSession(session);
		return;
	}

	closesocket(sock);
	ReleaseSession(session);
}

void CLanServer::ReleaseSession(Session* session)
{
	if (InterlockedDecrement(&session->ioCount) == 0)
	{
		SessionID id = session->sessionId;
		RemoveSession(session);
		delete session;
		OnClientLeave(id);
	}
}


// ============================================================
// 세션 맵 관리
// ============================================================

Session* CLanServer::FindSession(SessionID sessionId)
{
	AcquireSRWLockShared(&_sessionLock);

	auto it = _sessions.find(sessionId);
	if (it == _sessions.end())
	{
		ReleaseSRWLockShared(&_sessionLock);
		return nullptr;
	}

	Session* s = it->second;
	InterlockedIncrement(&s->ioCount);   // 삭제 방지

	ReleaseSRWLockShared(&_sessionLock);
	return s;
}

void CLanServer::AddSession(Session* session)
{
	AcquireSRWLockExclusive(&_sessionLock);
	_sessions.insert({ session->sessionId, session });
	ReleaseSRWLockExclusive(&_sessionLock);
}

void CLanServer::RemoveSession(Session* session)
{
	AcquireSRWLockExclusive(&_sessionLock);
	_sessions.erase(session->sessionId);
	ReleaseSRWLockExclusive(&_sessionLock);
}
