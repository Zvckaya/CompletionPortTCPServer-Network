#pragma once;
#include "Types.h"
#include "CRingBuffer.h"


struct Session
{
	SOCKET sock;
	unsigned int sessionId; 


	OVERLAPPED recvOverlapped;
	OVERLAPPED sendOverlapped;

	CRingBuffer recvBuffer;
	CRingBuffer sendBuffer;

	long ioCount; 

	SRWLOCK lock;

	Session()
	{
		sock = INVALID_SOCKET;
		sessionId = 0;
		ioCount = 0;
		ZeroMemory(&recvOverlapped, sizeof(recvOverlapped));
		ZeroMemory(&sendOverlapped, sizeof(sendOverlapped));
		InitializeSRWLock(&lock);
	}

	~Session()
	{
		if (sock != INVALID_SOCKET) closesocket(sock);
	}
};