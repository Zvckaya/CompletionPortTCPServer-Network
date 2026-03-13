#pragma once
#include "Types.h"
#include "lib/CRingBuffer.h"

using SessionID = unsigned long long;

struct Session
{
	SOCKET    sock;
	SessionID sessionId;

	OVERLAPPED recvOverlapped;
	OVERLAPPED sendOverlapped;

	CRingBuffer recvBuffer{ BUFSIZE };
	CRingBuffer sendBuffer{ BUFSIZE };

	long ioCount;
	long sendFlag;

	CRITICAL_SECTION lock;

	Session()
	{
		sock      = INVALID_SOCKET;
		sessionId = 0;
		ioCount   = 0;
		sendFlag  = 0;

		ZeroMemory(&recvOverlapped, sizeof(recvOverlapped));
		ZeroMemory(&sendOverlapped, sizeof(sendOverlapped));
		InitializeCriticalSection(&lock);
	}

	~Session()
	{
		if (sock != INVALID_SOCKET) closesocket(sock);
		DeleteCriticalSection(&lock);
	}
};
