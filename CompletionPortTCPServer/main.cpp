#pragma comment(lib,"ws2_32")
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <WinSock2.h>
#include <stdlib.h>
#include <stdio.h>
#include <vector>

#include "Types.h"
#include "CRingBuffer.h"
#include "Session.h"



std::vector<HANDLE> workerThreads;

int bi = 0;
int nbi = 0;


DWORD WINAPI WorkerThread(LPVOID arg);

void err_quit(const char* msg);
void err_display(const char* msg);


bool InitWSAandIOCP(HANDLE& outHcp)
{
	WSADATA wsa;

	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
	{
		printf("[Error] WSAStartup Failed\n");
		return false;
	}

	outHcp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);

	if (outHcp == NULL)
	{
		printf("[Error] CreateIoCompletionPort Failed. Error: %d\n", WSAGetLastError());

		WSACleanup();
		return false;
	}

	printf("[System] Winsock & IOCP Initialized Successfully.\n");
	return true;

}

void CreateWorkerThreads(HANDLE hIocp)
{
	SYSTEM_INFO si;
	GetSystemInfo(&si);

	int threadCount = (int)si.dwNumberOfProcessors * 2;

	for (int i = 0; i < threadCount; i++)
	{
		DWORD threadId;
		HANDLE hThread = CreateThread(NULL, 0, WorkerThread, hIocp, 0, &threadId);


		if (hThread != NULL)
		{
			workerThreads.push_back(hThread);
		}
	}

	printf("[System] %d Worker Threads Created.\n", threadCount);
}

SOCKET BindAndListen(int port)
{
	SOCKET listenSock = socket(AF_INET, SOCK_STREAM, 0);
	if (listenSock == INVALID_SOCKET)
	{
		printf("[Error] socket() failed with error: %d\n", WSAGetLastError());
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
		printf("[Error] bind() failed with error: %d\n", WSAGetLastError());
		closesocket(listenSock);
		return INVALID_SOCKET;
	}

	retval = listen(listenSock, SOMAXCONN);
	if (retval == SOCKET_ERROR)
	{
		printf("[Error] listen() failed with error: %d\n", WSAGetLastError());
		closesocket(listenSock);
		return INVALID_SOCKET;
	}

	printf("[System] Server is listening on port %d...\n", port);
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
			printf("[Error] WSARecv Failed: %d\n", err);
		}
	}
}


int main()
{
	int retval;
	HANDLE hcp;

	if (InitWSAandIOCP(hcp) == false)
	{
		return 1;
	}

	CreateWorkerThreads(hcp);

	SOCKET listen_sock = BindAndListen(SERVERPORT);
	if (listen_sock == INVALID_SOCKET)
	{
		WSACleanup();
		return 1;
	}


	SOCKET client_sock;
	SOCKADDR_IN clientaddr;
	int addrlen;
	DWORD recvbytes, flags;

	while (1)
	{
		addrlen = sizeof(clientaddr);
		client_sock = accept(listen_sock, (SOCKADDR*)&clientaddr, &addrlen);
		if (client_sock == INVALID_SOCKET)
		{
			err_display("accept");
			break;
		}

		printf("클라이언트 접속 IP: %s PORT: %d\n", inet_ntoa(clientaddr.sin_addr), ntohs(clientaddr.sin_port));

		Session* session = new Session;
		session->sock = client_sock;

		CreateIoCompletionPort((HANDLE)client_sock, hcp, (ULONG_PTR)session, 0);
		;

		RecvPost(session);


	}

	WSACleanup();
	return 0;

}

DWORD WINAPI WorkerThread(LPVOID arg)
{
	int retval;
	HANDLE hcp = (HANDLE)arg;

	while (true)
	{
		DWORD cbTransferred;
		SOCKET client_sock;
		Session* session;
		retval = GetQueuedCompletionStatus(hcp, &cbTransferred, (PULONG_PTR)&client_sock, (LPOVERLAPPED*)&session, INFINITE);

		SOCKADDR_IN clientaddr;
		int addrlen = sizeof(clientaddr);
		getpeername(session->sock, (SOCKADDR*)&clientaddr, &addrlen);


	}



	void err_display(const char* msg)
	{
		int err = WSAGetLastError();

		char* lpMsgBuf = NULL;
		FormatMessageA(
			FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL,
			err,
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			(LPSTR)&lpMsgBuf,
			0,
			NULL
		);

		if (lpMsgBuf)
		{
			printf("[%s] WSAGetLastError=%d: %s", msg, err, lpMsgBuf);
			LocalFree(lpMsgBuf);
		}
		else
		{
			printf("[%s] WSAGetLastError=%d\n", msg, err);
		}
	}

	void err_quit(const char* msg)
	{
		err_display(msg);
		WSACleanup();   // 이미 startup 했다는 가정 하에 정리
		exit(1);
	}