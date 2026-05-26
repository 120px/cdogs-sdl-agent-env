#include "tcp_server.h"

#include <winsock2.h>
#include <stdlib.h>
#include "log.h"

static SOCKET sListenSock = INVALID_SOCKET;
static SOCKET sClientSock = INVALID_SOCKET;

void TcpServerInit(const int port)
{
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
	{
		LOG(LM_MAIN, LL_ERROR, "TcpServerInit: WSAStartup failed");
		return;
	}
	sListenSock = socket(AF_INET, SOCK_STREAM, 0);
	if (sListenSock == INVALID_SOCKET)
	{
		LOG(LM_MAIN, LL_ERROR, "TcpServerInit: socket failed");
		return;
	}
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof addr);
	addr.sin_family = AF_INET;
	addr.sin_port = htons((u_short)port);
	addr.sin_addr.s_addr = INADDR_ANY;
	if (bind(sListenSock, (struct sockaddr *)&addr, sizeof addr) != 0)
	{
		LOG(LM_MAIN, LL_ERROR, "TcpServerInit: bind failed");
		return;
	}
	listen(sListenSock, 1);
	// Non-blocking so the game loop never stalls waiting for a connection
	u_long mode = 1;
	ioctlsocket(sListenSock, FIONBIO, &mode);
	LOG(LM_MAIN, LL_INFO, "TCP server listening on port %d", port);
}

void TcpServerSendJSON(json_t *root)
{
	// Accept a new client if none is connected
	if (sClientSock == INVALID_SOCKET)
	{
		sClientSock = accept(sListenSock, NULL, NULL);
		if (sClientSock != INVALID_SOCKET)
		{
			LOG(LM_MAIN, LL_INFO, "TCP client connected");
		}
	}
	if (sClientSock == INVALID_SOCKET)
		return;

	char *str = NULL;
	if (json_tree_to_string(root, &str) != JSON_OK || str == NULL)
		return;

	// Send 4-byte little-endian length prefix then the JSON string
	int len = (int)strlen(str);
	if (send(sClientSock, (char *)&len, sizeof len, 0) == SOCKET_ERROR ||
		send(sClientSock, str, len, 0) == SOCKET_ERROR)
	{
		LOG(LM_MAIN, LL_WARN, "TCP send failed, dropping client");
		closesocket(sClientSock);
		sClientSock = INVALID_SOCKET;
	}
	free(str);
}

void TcpServerTerminate(void)
{
	if (sClientSock != INVALID_SOCKET)
	{
		closesocket(sClientSock);
		sClientSock = INVALID_SOCKET;
	}
	if (sListenSock != INVALID_SOCKET)
	{
		closesocket(sListenSock);
		sListenSock = INVALID_SOCKET;
	}
	WSACleanup();
}
