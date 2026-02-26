#pragma once
#include "Types.h"

struct AcceptThreadArgs
{
	SOCKET socket;
	HANDLE hIocp;
};

DWORD WINAPI WorkerThread(LPVOID arg);
DWORD WINAPI AcceptThread(LPVOID arg);
void CreateWorkerThreads(HANDLE hIocp,int threadCount);

