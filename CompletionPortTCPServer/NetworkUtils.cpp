#include "NetworkUtils.h"
#include "SessionManager.h"

bool InitWSAandIOCP(HANDLE& outHcp)
{
	WSADATA wsa;

	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
	{
		printf("[Error] WSAStartup 실패\n");
		return false;
	}

	outHcp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);

	if (outHcp == NULL)
	{
		printf("[Error] IOCP 핸들 생성 실패. Error: %d\n", WSAGetLastError());

		WSACleanup();
		return false;
	}

	printf("[System] IOCP 생성 완료.\n");
	return true;

}


SOCKET BindAndListen(int port)
{
	SOCKET listenSock = socket(AF_INET, SOCK_STREAM, 0);
	if (listenSock == INVALID_SOCKET)
	{
		printf("[Error] 리슨 소켓 생성 실패: %d\n", WSAGetLastError());
		return INVALID_SOCKET;
	}

	SOCKADDR_IN serveraddr;
	ZeroMemory(&serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.S_un.S_addr = htonl(INADDR_ANY);
	serveraddr.sin_port = htons(port);

	int retval = bind(listenSock, (SOCKADDR*)&serveraddr, sizeof(serveraddr));
	if (retval == SOCKET_ERROR)
	{
		printf("[Error] 바인드 실패: %d\n", WSAGetLastError());
		closesocket(listenSock);
		return INVALID_SOCKET;
	}

	retval = listen(listenSock, SOMAXCONN);
	if (retval == SOCKET_ERROR)
	{
		printf("[Error] 리스닝 실패: %d\n", WSAGetLastError());
		closesocket(listenSock);
		return INVALID_SOCKET;
	}

	printf("[System] 서버 시작.....PORT: %d...\n", port);
	return listenSock;
}

volatile int flaga = 0;
volatile int flagb = 0;
void RecvPost(Session* session)
{
	CRingBuffer* pRb = &session->recvBuffer;

	int freeSize = pRb->GetFreeSize();
	if (freeSize <= 0)
	{
		return;
	}

	char* ptr1 = pRb->GetRearBufferPtr();
	int len1 = pRb->DirectEnqueueSize();

	WSABUF wsaBufs[2];
	int bufCount = 0;

	wsaBufs[0].buf = ptr1;
	wsaBufs[0].len = len1;
	bufCount = 1;

	int len2 = freeSize - len1;
	if (len2 > 0)
	{
		wsaBufs[1].buf = pRb->GetBufferPtr();
		wsaBufs[1].len = len2;
		bufCount = 2;
	}

	DWORD recvBytes = 0;
	DWORD flags = 0;

	ZeroMemory(&session->recvOverlapped, sizeof(OVERLAPPED));

	
	int retval = WSARecv(session->sock, wsaBufs, bufCount, &recvBytes, &flags, &session->recvOverlapped, NULL);

	if (retval == SOCKET_ERROR&& WSAGetLastError()!= ERROR_IO_PENDING)
	{
		if (session->sock != INVALID_SOCKET)
			closesocket(session->sock);
		ReleaseSession(session);
	}
}


// NetworkUtils.cpp 의 SendPost 함수

void SendPost(Session* session)
{
	if (InterlockedCompareExchange(&session->sendFlag, 1, 0) == 1)
	{
		return;
	}

	// [수정5] 여기서부터 링버퍼를 읽으므로 락을 걸어야 함!
	AcquireSRWLockExclusive(&session->lock);

	int useSize = session->sendBuffer.GetUseSize();
	if (useSize == 0)
	{
		InterlockedExchange(&session->sendFlag, 0);
		ReleaseSRWLockExclusive(&session->lock); // 락 해제 잊지말기
		return;
	}

	char* ptr1 = session->sendBuffer.GetFrontBufferPtr();
	int len1 = session->sendBuffer.DirectDequeueSize();

	WSABUF wsaBufs[2];
	int bufCount = 1;

	wsaBufs[0].buf = ptr1;
	wsaBufs[0].len = len1;

	int remain = useSize - len1;
	if (remain > 0)
	{
		wsaBufs[1].buf = session->sendBuffer.GetBufferPtr();
		wsaBufs[1].len = remain;
		bufCount = 2;
	}

	InterlockedIncrement(&session->ioCount);

	DWORD sendBytes = 0;
	DWORD flags = 0;
	ZeroMemory(&session->sendOverlapped, sizeof(OVERLAPPED));

	int retval = WSASend(session->sock, wsaBufs, bufCount, &sendBytes, 0, &session->sendOverlapped, NULL);

	// [수정6] WSASend 호출 후 셋업이 끝났으니 락 해제
	ReleaseSRWLockExclusive(&session->lock);

	if (retval == SOCKET_ERROR)
	{
		int err = WSAGetLastError();
		if (err != ERROR_IO_PENDING)
		{
			printf("[Error] WSASend 실패: %d\n", err);
			DeleteSession(session);
			ReleaseSession(session);
		}
	}
}
void ReleaseSession(Session* session)
{
	if (InterlockedDecrement(&session->ioCount) == 0)
	{
		SessionManager::GetInstance().RemoveSession(session);
		if (session->sock != INVALID_SOCKET) closesocket(session->sock);
		delete session;
		printf("소켓 종료\n ");
	}

}
void DeleteSession(Session* session)
{
	if (session->sock == INVALID_SOCKET) return;

	closesocket(session->sock);
	session->sock = INVALID_SOCKET;

	ReleaseSession(session);
}



void SendPacket(Session* session, const char* data, int size)
{
	AcquireSRWLockExclusive(&session->lock);
	session->sendBuffer.Enqueue(data, size);
	ReleaseSRWLockExclusive(&session->lock);

	SendPost(session);
}