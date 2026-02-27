#include "ServerThread.h"
#include "NetworkUtils.h"
#include "Session.h"
#include "SessionManager.h"

std::vector<HANDLE> workerThreads;

void CreateWorkerThreads(HANDLE hIocp,int threadCount)
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

		session = reinterpret_cast<Session*>(session);

		if (session == nullptr && lpOverlapped == nullptr)//0,0,nullptr 넣어서 post 했을때만  이렇게 
			break;
		
		if (ret == FALSE || cbTransferred == 0)
		{
			if (session->sock != INVALID_SOCKET)
			{
				closesocket(session->sock);
				session->sock = INVALID_SOCKET;
			}
		}

		if (lpOverlapped == &session->recvOverlapped)
		{
			if (ret == FALSE || cbTransferred == 0)
			{
				ReleaseSession(session);
			}
			else {
				session->recvBuffer.MoveRear(cbTransferred);

				int recvLen = session->recvBuffer.GetUseSize();

				// [수정1] +1 제거 (바이너리이므로 불필요한 메모리 낭비)
				char* tempBuf = new char[recvLen];

				session->recvBuffer.Dequeue(tempBuf, recvLen);

				// [수정2] 직접 Enqueue 대신 SendPacket 호출 (내부에서 Lock 보호 됨)
				SendPacket(session, tempBuf, recvLen);

				delete[] tempBuf;

				// RecvPost(session); 는 SendPacket 밖에서, 즉 여기서 하는게 맞습니다.
				RecvPost(session);
			}
		}
		else if (lpOverlapped == &session->sendOverlapped)
		{
			if (ret == TRUE && cbTransferred > 0)
			{
				// [수정3] Front를 미는 행위도 링버퍼 조작이므로 반드시 Lock 보호!
				AcquireSRWLockExclusive(&session->lock);
				session->sendBuffer.MoveFront(cbTransferred);
				ReleaseSRWLockExclusive(&session->lock);
			}

			// 전송 1회 완료 -> 플래그 해제
			InterlockedExchange(&session->sendFlag, 0);

			// [수정4] 내가 전송하는 동안 누군가 SendPacket으로 데이터를 또 넣었을 수 있음.
			// 남은 데이터가 있다면 다시 SendPost를 트리거 해줘야 안 멈춤!
			if (session->sendBuffer.GetUseSize() > 0)
			{
				SendPost(session);
			}

			ReleaseSession(session);
		}
		else
		{

			//예외처리 
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

	//	printf("클라이언트 접속 IP: %s PORT: %d\n", inet_ntoa(clientaddr.sin_addr), ntohs(clientaddr.sin_port));

		Session* session = new Session;
		session->sock = client_sock;
		
		SessionManager::GetInstance().AddSession(session);
		

		CreateIoCompletionPort((HANDLE)client_sock, hcp, (ULONG_PTR)session, 0);

		RecvPost(session);
	}

	return 0;
}
