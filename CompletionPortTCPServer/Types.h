#pragma once

#pragma comment(lib,"ws2_32")
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <WinSock2.h>
#include <Windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <vector>
#include <unordered_map>

#define SERVERPORT 9000
#define BUFSIZE 1024