#include "NetworkUtils.h"
#include "SessionManager.h"

bool InitWSAandIOCP(HANDLE& outHcp)
{
	WSADATA wsa;

	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
	{
		printf("[Error] WSAStartup err\n");
		return false;
	}

	outHcp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);

	if (outHcp == NULL)
	{
		printf("[Error] IOCP create error. Error: %d\n", WSAGetLastError());

		WSACleanup();
		return false;
	}

	printf("[System] IOCP Init complete.\n");
	return true;

}


SOCKET BindAndListen(int port)
{
	SOCKET listenSock = socket(AF_INET, SOCK_STREAM, 0);
	if (listenSock == INVALID_SOCKET)
	{
		printf("[Error] listen socket start err: %d\n", WSAGetLastError());
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
		printf("[Error] binding err: %d\n", WSAGetLastError());
		closesocket(listenSock);
		return INVALID_SOCKET;
	}

	retval = listen(listenSock, SOMAXCONN);
	if (retval == SOCKET_ERROR)
	{
		printf("[Error] listening err: %d\n", WSAGetLastError());
		closesocket(listenSock);
		return INVALID_SOCKET;
	}

	printf("[System] listening.....PORT: %d...\n", port);
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

	if (retval == SOCKET_ERROR && WSAGetLastError() != ERROR_IO_PENDING)
	{
		DeleteSession(session);
	}
}


void SendPost(Session* session)
{
	if (InterlockedCompareExchange(&session->sendFlag, 1, 0) == 1)
	{
		return;
	}

	int useSize = session->sendBuffer.GetUseSize();
	if (useSize == 0)
	{
		InterlockedExchange(&session->sendFlag, 0);

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

	

	if (retval == SOCKET_ERROR)
	{
		int err = WSAGetLastError();
		if (err != ERROR_IO_PENDING)
		{
		
			DeleteSession(session);
		}
	}
}



void ReleaseSession(Session* session)
{
	if (InterlockedDecrement(&session->ioCount) == 0)
	{
		SessionManager::GetInstance().RemoveSession(session);
		closesocket(session->sock);
		delete session;
		
	}

}
void DeleteSession(Session* session)
{
	AcquireSRWLockExclusive(&session->lock);
	SOCKET sock = session->sock;
	session->sock = INVALID_SOCKET;
	ReleaseSRWLockExclusive(&session->lock);

	if (sock == INVALID_SOCKET)
	{
		ReleaseSession(session);
		return;
	}

	closesocket(sock);
	ReleaseSession(session);
}



void SendPacket(Session* session, const char* data, int size)
{
	AcquireSRWLockExclusive(&session->lock);
	session->sendBuffer.Enqueue(data, size);
	ReleaseSRWLockExclusive(&session->lock);

	SendPost(session);
}