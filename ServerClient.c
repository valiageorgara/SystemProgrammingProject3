#include "ServerClient.h"

int createClient(char* hostname, int port)
{
	int sock;
	struct sockaddr_in server ;
	struct sockaddr *serverptr = ( struct sockaddr *)& server ;
	struct hostent *rem ;

	/* Create socket */
	sock = socket (AF_INET, SOCK_STREAM, 0);
	if (sock < 0)
	{
		perror("socket");
		exit(1);
	}

	/* Find server address */
	if ((rem = gethostbyname(hostname)) == 0)
	{
		herror("gethostbyname");
		exit(1) ;
	}
	server.sin_family = AF_INET; /* Internet domain */
	memcpy(&server.sin_addr, rem->h_addr, rem->h_length);
	server.sin_port = htons(port); /* Server port */
	/* Initiate connection */
	if (connect(sock, serverptr, sizeof (server)) < 0)
	{
		perror("connect");
		exit(1);
	}
	printf("Connecting to %s port %d\n", hostname, port);

	return sock;
}

int createServer(int port)
{
	int sock;
	struct sockaddr_in server;
	struct sockaddr* serverptr = (struct sockaddr*)&server;

	/* Create socket */
	sock = socket (AF_INET, SOCK_STREAM, 0);
	if (sock < 0)
	{
		perror("socket");
		exit(1);
	}
	server.sin_family = AF_INET; /* Internet domain */
	server.sin_port = htons(port); /* The given port */
	server.sin_addr.s_addr = htonl(INADDR_ANY);
	/* Bind socket to address */
	if (bind(sock, serverptr, sizeof (server)) < 0)
	{
		perror("bind");
		exit(1);
	}

	/* Listen for connections */
	if (listen(sock, 5) < 0)
	{
		perror("listen");
		exit(1);
	}
	printf("Listening for connections to port %d\n", port);
	return sock;
}

