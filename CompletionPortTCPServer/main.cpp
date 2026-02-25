#include "Types.h"
#include "Session.h"
#include "NetworkUtils.h"
#include "ServerThread.h"

int main()
{
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

    printf("Press Any Key to Shutdown...\n");
    getchar();

    closesocket(listen_sock);

    WaitForSingleObject(hAcceptThread, INFINITE);
    CloseHandle(hAcceptThread);

    CloseHandle(hcp);
    WSACleanup();

    printf("모든 리소스 정리 완료... 종료\n");
    return 0;
}