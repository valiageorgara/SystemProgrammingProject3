#ifndef SERVERCLIENT_H_
#define SERVERCLIENT_H_

#include <stdio.h>
#include <sys/types.h> /* sockets */
#include <sys/socket.h> /* sockets */
#include <netinet/in.h> /* internet sockets */
#include <unistd.h> /* read , write , close */
#include <netdb.h> /* gethostbyaddr */
#include <stdlib.h> /* exit */
#include <string.h> /* strlen */

int createClient(char* hostname, int port);
int createServer(int port);

#endif /* SERVERCLIENT_H_ */
