#include "ServerThread.h"
#include "NetworkUtils.h"
#include "Session.h"

std::vector<HANDLE> workerThreads;

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

			char* tempBuf = new char[recvLen + 1];
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
