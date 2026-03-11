#include "Types.h"
#include "IOCPEchoServer.h"

int main()
{
	SYSTEM_INFO si;
	GetSystemInfo(&si);
	int workerCount = (int)si.dwNumberOfProcessors * 2;

	IOCPEchoServer server;

	if (!server.Start(L"0.0.0.0", 6000, workerCount, workerCount, false, 1000))
	{
		printf("[Error] Server start failed.\n");
		return 1;
	}

	printf("[System] Echo server running. Press Enter to shutdown...\n");

	// Enter 입력마다 모니터링 출력, 'q' 입력 시 종료
	while (true)
	{
		int ch = getchar();
		if (ch == 'q' || ch == 'Q')
			break;

		printf("[Monitor] Sessions: %d  AcceptTPS: %d  RecvTPS: %d  SendTPS: %d\n",
			server.GetSessionCount(),
			server.GetAcceptTPS(),
			server.GetRecvMessageTPS(),
			server.GetSendMessageTPS());
	}

	server.Stop();
	printf("[System] Server stopped.\n");
	return 0;
}
