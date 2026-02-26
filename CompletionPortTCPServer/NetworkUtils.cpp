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

	InterlockedIncrement(&session->ioCount);

	int retval = WSARecv(session->sock, wsaBufs, bufCount, &recvBytes, &flags, &session->recvOverlapped, NULL);

	if (retval == SOCKET_ERROR)
	{
		int err = WSAGetLastError();
		if (err != ERROR_IO_PENDING)
		{
			printf("[Error] WSARecv 실패: %d\n", err);
		}
	}
}


void SendPost(Session* session)
{
	CRingBuffer* pRb = &session->sendBuffer;
	if (pRb->GetUseSize() <= 0)
	{
		return;
	}


	char* ptr1 = pRb->GetFrontBufferPtr();
	int len1 = pRb->DirectDequeueSize();

	WSABUF wsaBufs[2];
	int bufCount = 0;

	wsaBufs[0].buf = ptr1;
	wsaBufs[0].len = len1;
	bufCount = 1;

	int totalSendSize = len1;

	int remain = pRb->GetUseSize() - len1;
	if (remain > 0)
	{
		wsaBufs[1].buf = pRb->GetBufferPtr();
		wsaBufs[1].len = remain;
		bufCount = 2;
		totalSendSize += remain;
	}

	InterlockedIncrement(&session->ioCount);

	DWORD sendBytes = 0;
	DWORD flags = 0;
	ZeroMemory(&session->sendOverlapped, sizeof(OVERLAPPED));


	int retval = WSASend(session->sock, wsaBufs, bufCount, &sendBytes, 0, &session->sendOverlapped, NULL);

	if (retval == SOCKET_ERROR)
	{
		int err = WSAGetLastError();
		if (err != ERROR_IO_PENDING)
		{
			printf("[Error] WSASend 실패: %d\n", err);

			InterlockedDecrement(&session->ioCount);
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
		printf("세션 삭제 완료\n");
	}

}
void DeleteSession(Session* session)
{
	if (session->sock == INVALID_SOCKET) return;

	closesocket(session->sock);
	session->sock = INVALID_SOCKET;

	ReleaseSession(session);
}

