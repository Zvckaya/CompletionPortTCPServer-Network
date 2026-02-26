#pragma once
#include "Types.h"
#include "Session.h"

bool InitWSAandIOCP(HANDLE& outHcp);
SOCKET BindAndListen(int port);
void RecvPost(Session* session);
void SendPost(Session* session);

void ReleaseSession(Session* session);
void DeleteSession(Session* session);