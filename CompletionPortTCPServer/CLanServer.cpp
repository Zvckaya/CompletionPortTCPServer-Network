#include "CLanServer.h"
#include "lib/CPacket.h"

// ============================================================
// SessionID 인코딩/디코딩
// [상위 16bit: index] [하위 48bit: uniqueId]
// ============================================================

SessionID CLanServer::MakeSessionID(WORD index, unsigned long long uniqueId)
{
	return ((SessionID)index << 48) | (uniqueId & 0x0000FFFFFFFFFFFF);
}

WORD CLanServer::GetIndex(SessionID id)
{
	return (WORD)(id >> 48);
}

unsigned long long CLanServer::GetUniqueID(SessionID id)
{
	return id & 0x0000FFFFFFFFFFFF;
}


// ============================================================
// 생성 / 소멸
// ============================================================

CLanServer::CLanServer()
	: _hIocp(NULL), _listenSock(INVALID_SOCKET)
	, _acceptThread(NULL), _monitorThread(NULL), _contentThread(NULL)
	, _workerRunningCount(0), _maxConnections(0), _running(false)
	, _uniqueIdCounter(0LL), _sessionCount(0)
	, _acceptCount(0), _recvCount(0), _sendCount(0)
	, _acceptTPS(0), _recvTPS(0), _sendTPS(0)
	, _contentEvent(NULL)
{
	InitializeCriticalSection(&_freeIndexLock);
	InitializeCriticalSection(&_contentQueueLock);
	_contentEvent = CreateEvent(NULL, FALSE, FALSE, NULL);  // auto-reset

	for (WORD i = 0; i < MAX_SESSION; i++)
		_freeIndices.push(i);
}

CLanServer::~CLanServer()
{
	Stop();
	DeleteCriticalSection(&_freeIndexLock);
	DeleteCriticalSection(&_contentQueueLock);
	if (_contentEvent != NULL)
	{
		CloseHandle(_contentEvent);
		_contentEvent = NULL;
	}
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

	// 어셉트 / 모니터 / 콘텐츠 스레드 생성
	_acceptThread  = CreateThread(NULL, 0, AcceptThread,  this, 0, NULL);
	_monitorThread = CreateThread(NULL, 0, MonitorThread, this, 0, NULL);
	_contentThread = CreateThread(NULL, 0, ContentThread, this, 0, NULL);

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

	// 콘텐츠 스레드 종료 — event를 한 번 더 신호하여 대기에서 깨움
	if (_contentThread != NULL)
	{
		SetEvent(_contentEvent);
		WaitForSingleObject(_contentThread, INFINITE);
		CloseHandle(_contentThread);
		_contentThread = NULL;
	}

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
	return (int)_sessionCount;
}

bool CLanServer::Disconnect(SessionID sessionId)
{
	WORD idx = GetIndex(sessionId);
	if (idx >= MAX_SESSION)
		return false;

	Sessions& slot = _sessions[idx];
	if (slot.uniqueId != GetUniqueID(sessionId))
		return false;

	// 소켓을 닫아 IOCP 에러 완료통지를 유발 → 정상 해제 흐름으로 처리
	SOCKET sock = (SOCKET)InterlockedExchange64(
		(volatile LONGLONG*)&slot.session.sock, (LONGLONG)INVALID_SOCKET);

	if (sock == INVALID_SOCKET)
		return false;

	closesocket(sock);
	return true;
}

// 콘텐츠 레이어 → 네트워크 송신 진입점
bool CLanServer::SendPost(SessionID sessionId, CPacket* packet)
{
	WORD idx = GetIndex(sessionId);
	if (idx >= MAX_SESSION)
		return false;

	Sessions& slot = _sessions[idx];
	if (slot.uniqueId != GetUniqueID(sessionId))
		return false;   

	slot.session.sendBuffer.Enqueue(packet->GetBufferPtr(), packet->GetDataSize());
	SendPost(&slot.session);

	InterlockedIncrement(&_sendCount);
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

DWORD WINAPI CLanServer::ContentThread(LPVOID arg)
{
	((CLanServer*)arg)->ContentThreadProc();
	return 0;
}


// ============================================================
// 콘텐츠 스레드
// ============================================================

void CLanServer::ContentThreadProc()
{
	while (true)
	{
		WaitForSingleObject(_contentEvent, 100);

		while (true)
		{
			ContentJob job;

			EnterCriticalSection(&_contentQueueLock);
			if (_contentQueue.empty())
			{
				LeaveCriticalSection(&_contentQueueLock);
				break;
			}
			job = _contentQueue.front();
			_contentQueue.pop();
			LeaveCriticalSection(&_contentQueueLock);

			OnRecv(job.sessionId, job.packet);
			delete job.packet;
			ReleaseSession(job.session);
		}

		if (!_running && _contentQueue.empty())
			break;
	}
}


// ============================================================
// 워커 스레드
// ============================================================

void CLanServer::WorkerThreadProc()
{
	while (true)
	{
		DWORD       cbTransferred = 0;
		Session*    session       = nullptr;
		OVERLAPPED* lpOverlapped  = nullptr;

		BOOL ret = GetQueuedCompletionStatus(
			_hIocp, &cbTransferred,
			(PULONG_PTR)&session, &lpOverlapped, INFINITE);

		// Stop() 신호
		if (session == nullptr && lpOverlapped == nullptr)
			break;


		
		//WORD idx = GetIndex(session->sessionId);
		//if (_sessions[idx].uniqueId != GetUniqueID(session->sessionId))
		//{
		//		// 슬롯이 재사용됨 — 이 완료통지는 이전 세션의 것
		//		ReleaseSession(session);
		//		continue;
		//}
		

		if (ret == FALSE || cbTransferred == 0)
		{
			DeleteSession(session);
			continue;
		}

		if (lpOverlapped == &session->recvOverlapped)
		{
			session->recvBuffer.MoveRear(cbTransferred);

			while (session->recvBuffer.GetUseSize() >= (int)sizeof(WORD)) //2바이트(길이는 있는가)
			{
				WORD payloadLen = 0;
				session->recvBuffer.Peek((char*)&payloadLen, sizeof(WORD));

				int totalLen = (int)sizeof(WORD) + payloadLen;
				if (session->recvBuffer.GetUseSize() < totalLen)
					break;  // 패킷 아직 미완성

				CPacket* pkt = new CPacket();
				char buf[BUFSIZE];
				session->recvBuffer.Dequeue(buf, totalLen);
				pkt->SetData(buf, totalLen);

				// 콘텐츠 큐에 삽입
				InterlockedIncrement(&session->ioCount);
				EnterCriticalSection(&_contentQueueLock);
				_contentQueue.push({ session->sessionId, session, pkt });
				LeaveCriticalSection(&_contentQueueLock);

				SetEvent(_contentEvent);
				InterlockedIncrement(&_recvCount);
			}

			RecvPost(session);
			ReleaseSession(session);
		}
		else if (lpOverlapped == &session->sendOverlapped)
		{
			session->sendBuffer.MoveFront(cbTransferred);

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
		if (_sessionCount >= _maxConnections)
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

		// 빈 슬롯 확보 (free-list lock)
		EnterCriticalSection(&_freeIndexLock);
		if (_freeIndices.empty())
		{
			LeaveCriticalSection(&_freeIndexLock);
			closesocket(clientSock);
			continue;
		}
		WORD idx = _freeIndices.top();
		_freeIndices.pop();
		LeaveCriticalSection(&_freeIndexLock);

		// uniqueId 생성 및 슬롯 활성화
		unsigned long long uid = (unsigned long long)InterlockedIncrement64(&_uniqueIdCounter)
		                         & 0x0000FFFFFFFFFFFF;
		SessionID newId        = MakeSessionID(idx, uid);

		Sessions& slot         = _sessions[idx];
		slot.session.Reset();
		slot.session.sock      = clientSock;
		slot.session.sessionId = newId;
		slot.uniqueId          = uid;   

		InterlockedIncrement(&_sessionCount);

		// 송신 버퍼 0 설정 (Zero-Copy)
		int zero = 0;
		setsockopt(clientSock, SOL_SOCKET, SO_SNDBUF, (char*)&zero, sizeof(zero));

		CreateIoCompletionPort((HANDLE)clientSock, _hIocp, (ULONG_PTR)&slot.session, 0);

		// 접속 완료 이벤트
		ClientInfo info;
		wcscpy_s(info.ip, ipStr);
		info.port = port;
		OnClientJoin(info, newId);

		InterlockedIncrement(&_acceptCount);

		RecvPost(&slot.session);
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
	// CAS로 소켓을 INVALID로 교체 — 중복 close 방지
	SOCKET sock = (SOCKET)InterlockedExchange64(
		(volatile LONGLONG*)&session->sock, (LONGLONG)INVALID_SOCKET);

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
		SessionID id  = session->sessionId;
		WORD      idx = GetIndex(id);

		_sessions[idx].uniqueId = 0;

		session->Reset();

		EnterCriticalSection(&_freeIndexLock);
		_freeIndices.push(idx);
		LeaveCriticalSection(&_freeIndexLock);

		InterlockedDecrement(&_sessionCount);
		OnClientLeave(id);
	}
}
