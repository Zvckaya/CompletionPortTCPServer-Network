#include "ServerThread.h"
#include "NetworkUtils.h"
#include "Session.h"
#include "SessionManager.h"

std::vector<HANDLE> workerThreads;

void CreateWorkerThreads(HANDLE hIocp, int threadCount)
{

	for (int i = 0; i < threadCount; i++)
	{
		DWORD threadId;
		HANDLE hThread = CreateThread(NULL, 0, WorkerThread, hIocp, 0, &threadId);


		if (hThread != NULL)
		{
			workerThreads.push_back(hThread);
		}
	}

	printf("[System] %d worker thread start...\n", threadCount);
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

		session = reinterpret_cast<Session*>(session);

		if (session == nullptr && lpOverlapped == nullptr)//0,0,nullptr �־ post ��������  �̷��� 
			break;

		if (ret == FALSE || cbTransferred == 0)
		{
			DeleteSession(session);
			continue;
		}

		if (lpOverlapped == &session->recvOverlapped)
		{
			session->recvBuffer.MoveRear(cbTransferred);
			int recvLen = session->recvBuffer.GetUseSize();
			char* tempBuf = new char[recvLen];
			session->recvBuffer.Dequeue(tempBuf, recvLen);
			SendPacket(session, tempBuf, recvLen);
			delete[] tempBuf;
			RecvPost(session);
			ReleaseSession(session);
		}
		else if (lpOverlapped == &session->sendOverlapped)
		{
			AcquireSRWLockExclusive(&session->lock);
			session->sendBuffer.MoveFront(cbTransferred);
			ReleaseSRWLockExclusive(&session->lock);

			InterlockedExchange(&session->sendFlag, 0);

			if (session->sendBuffer.GetUseSize() > 0)
			{
				SendPost(session);
			}

			ReleaseSession(session);
		}
		else
		{

		}
	}

	printf("thread deleting...\n");
	return 1;

}


DWORD WINAPI AcceptThread(LPVOID arg)
{
	AcceptThreadArgs* args = (AcceptThreadArgs*)arg;
	SOCKET listenSock = args->socket;
	HANDLE hcp = args->hIocp;

	SOCKET client_sock;
	SOCKADDR_IN clientaddr;
	int addrlen;


	printf("[System] Accept thread staring .\n");

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

	
		Session* session = new Session;
		session->sock = client_sock;

		SessionManager::GetInstance().AddSession(session);


		CreateIoCompletionPort((HANDLE)client_sock, hcp, (ULONG_PTR)session, 0);

		RecvPost(session);
	}

	return 0;
}
