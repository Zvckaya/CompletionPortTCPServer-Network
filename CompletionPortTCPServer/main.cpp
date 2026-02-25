#pragma comment(lib,"ws2_32")
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <WinSock2.h>
#include <stdlib.h>
#include <stdio.h>
#include <vector>

#include "Types.h"
#include "AcceptThreadArgs.h"
#include "CRingBuffer.h"
#include "Session.h"



std::vector<HANDLE> workerThreads;


DWORD WINAPI WorkerThread(LPVOID arg);
DWORD WINAPI AcceptThread(LPVOID arg);



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

	printf("[System] %d 개 워커 스레드 생성 .\n", threadCount);
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
			// 진짜 실패 상황
			printf("[Error] WSASend 실패: %d\n", err);

			InterlockedDecrement(&session->ioCount);
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

	AcceptThreadArgs args;
	args.socket = listen_sock;
	args.hIocp = hcp;

	HANDLE hAcceptThread = CreateThread(NULL, 0, AcceptThread, &args, 0, NULL);
	if (hAcceptThread == NULL)
	{
		return 1;
	}

	getchar();


	closesocket(listen_sock);

	WaitForSingleObject(hAcceptThread, INFINITE);
	CloseHandle(hAcceptThread);

	CloseHandle(hcp);
	WSACleanup();
	printf("모든 리스소 정리 완료... 종료");
	return 0;

}

DWORD WINAPI WorkerThread(LPVOID arg)
{
	HANDLE hcp = (HANDLE)arg;

	while (true)
	{
		DWORD cbTransferred = 0;
		Session* session = nullptr;
		LPOVERLAPPED lpOverlapped = nullptr;

		BOOL ret = GetQueuedCompletionStatus(hcp, &cbTransferred, (PULONG_PTR)&session, &lpOverlapped, INFINITE);

		if (session == nullptr)
			continue;

		if (ret == FALSE || cbTransferred == 0)
		{
			closesocket(session->sock);

			if (InterlockedDecrement(&session->ioCount) == 0)
			{

				delete session;
			}
			continue;
		}
	
		if (lpOverlapped == &session->recvOverlapped)
		{
			session->recvBuffer.MoveRear(cbTransferred);

			int recvLen = session->recvBuffer.GetUseSize();

			char* tempBuf = new char[recvLen+1];
			session->recvBuffer.Dequeue(tempBuf, recvLen);
			
			session->sendBuffer.Enqueue(tempBuf, recvLen);

			delete[] tempBuf;

			SendPost(session);

			RecvPost(session);

			if (InterlockedDecrement(&session->ioCount) == 0)
			{
				delete session;
			}
		}
		else if (lpOverlapped == &session->sendOverlapped)
		{
			session->sendBuffer.MoveFront(cbTransferred);


			if (InterlockedDecrement(&session->ioCount) == 0)
			{
				delete session;
			}
		}
		else
		{

			//예외처리 
		}
	}
	

}


DWORD WINAPI AcceptThread(LPVOID arg)
{
	AcceptThreadArgs* args = (AcceptThreadArgs*)arg;
	SOCKET listenSock = args->socket;
	HANDLE hcp = args->hIocp;

	SOCKET client_sock;
	SOCKADDR_IN clientaddr;
	int addrlen;


	printf("[System] Accept 스레드 시작 .\n");

	while (true)
	{
		addrlen = sizeof(clientaddr);
		client_sock = accept(listenSock, (SOCKADDR*)&clientaddr, &addrlen);
		if (client_sock == INVALID_SOCKET)
		{
			int err = WSAGetLastError();

			if (err == WSAEINTR || err == WSAENOTSOCK)
			{
				break;
			}
			continue;
		}

		int zero = 0;
		setsockopt(client_sock, SOL_SOCKET, SO_SNDBUF, (char*)&zero, sizeof(zero));

		printf("클라이언트 접속 IP: %s PORT: %d\n", inet_ntoa(clientaddr.sin_addr), ntohs(clientaddr.sin_port));

		Session* session = new Session;
		session->sock = client_sock;

		CreateIoCompletionPort((HANDLE)client_sock, hcp, (ULONG_PTR)session, 0);

		RecvPost(session);
	}

	return 0;
}
