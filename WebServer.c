#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "ServerClient.h"

#define BUFFER_SIZE 10

struct Buffer
{
	int buffer[BUFFER_SIZE];
	int start;
	int end;
	int count;
};

typedef struct Buffer Buffer;

int numOfThreads;
pthread_t *cons, prod;
pthread_mutex_t mtx;
pthread_cond_t cond_nonempty;
pthread_mutex_t stats;
pthread_cond_t cond_nonfull;
Buffer sharedBuffer;
int pages;
long unsigned bytes;
struct timeval start;
char *server_port, *rootDir;

double getTimeDiff(struct timeval st, struct timeval en)
{
	double timeInMs = ((en.tv_sec - st.tv_sec) * 1000.0 +
			(en.tv_usec - st.tv_usec) / 1000.0);
	return timeInMs / 1000;
}

void initialize(Buffer* bf)
{
	bf->start = bf->count = 0;
	bf->end = -1;
}

void place(Buffer* bf, int data)
{
	if (pthread_mutex_lock(&mtx) < 0)
	{
		fprintf(stderr, "Error in mutex lock");
		exit(1);
	}
	while (bf->count >= BUFFER_SIZE )
	{
		printf(" >> Found Buffer Full\n");
		if (pthread_cond_wait(&cond_nonfull, &mtx) < 0)
		{
			fprintf(stderr, "Error in pthread cond wait");
			exit(1);
		}
	}
	bf->end = (bf->end + 1) % BUFFER_SIZE;
	bf->buffer[bf->end] = data;
	++bf->count;
	if (pthread_mutex_unlock(&mtx) < 0) { fprintf(stderr, "Error in mutex unlock"); exit(1); }
	if (pthread_cond_signal(&cond_nonempty) < 0) { fprintf(stderr, "Error in pthread cond signal"); exit(1); }
}

int obtain(Buffer* bf)
{
	int data;
	pthread_mutex_lock (&mtx);
	data = 0;
	while (bf->count == 0)
	{
		printf (" >> Found Buffer Empty\n");
		if (pthread_cond_wait(&cond_nonempty, &mtx) < 0)
		{
			fprintf(stderr, "Error in pthread cond wait");
			exit(1);
		}
	}
	data = bf->buffer[bf->start];
	bf->start = (bf->start + 1) % BUFFER_SIZE;
	--bf->count;
	pthread_mutex_unlock (&mtx);
	pthread_cond_signal(&cond_nonfull);
	return data;
}

void* producer(void* ptr)
{
	int port = atoi((char*)server_port);
	int sock = createServer(port);
	while (1)
	{
		struct sockaddr_in client;
		socklen_t clientlen;
		struct sockaddr* clientptr =(struct sockaddr*) &client;
		int clientSocket;
		/* accept connection */
		if ((clientSocket = accept(sock, clientptr, &clientlen)) < 0)
		{
			perror("accept");
			exit(1);
		}
		printf("Accepted connection\n");
		place(&sharedBuffer, clientSocket);
		printf(">> Producer thread placed: %d\n", clientSocket);
	}
	pthread_exit(NULL);
}

void terminate()
{
	int i;

	//Kill all consumer threads first
	for (i = 0; i < numOfThreads; i++)
		place(&sharedBuffer, -1);

	//Kill producer thread
	pthread_cancel(prod);
}

void* commander(void* ptr)
{
	char buf[512];
	struct timeval end;
	int port = atoi((char*)ptr);
	int sock = createServer(port);
	while (1)
	{
		struct sockaddr_in client;
		socklen_t clientlen;
		struct sockaddr* clientptr =(struct sockaddr*) &client;
		int clientSocket;
		/* accept connection */
		if ((clientSocket = accept(sock, clientptr, &clientlen)) < 0)
		{
			perror("accept");
			exit(1);
		}
		printf("Accepted connection\n");
		while(1)
		{
			bzero(buf, sizeof buf);
			int b = read(clientSocket, buf, sizeof(buf) - 1);
			if (b == -1) //ekleise i sundesi
				break;

			char* token = strtok(buf, "\n\r ");
			if (token == NULL) //ekleise i sundesi
				break;

			if (!strcmp(token, "STATS"))
			{
				gettimeofday(&end, NULL); /* end timer */
				double runTime = getTimeDiff(start, end);
				int minutesUp = runTime/60;
				runTime -= (minutesUp*60);
				pthread_mutex_lock(&stats);
				sprintf(buf, "Server up for %02d:%05.02lf, served %d pages, %lu bytes\n", minutesUp, runTime, pages, bytes);
				pthread_mutex_unlock(&stats);
				write(clientSocket, buf, strlen(buf));
			}
			else if (!strcmp(token, "SHUTDOWN"))
			{
				sprintf(buf, "Server is shutting down gently.\n");
				write(clientSocket, buf, strlen(buf));
				terminate();
				pthread_exit(NULL);
			}
		}
	}
	pthread_exit(NULL);
}

void* consumer(void* ptr)
{
	while (1)
	{
		int obtainedData = obtain(&sharedBuffer);
		if (obtainedData == -1)
		{
			printf("Consumer thread is exiting.\n");
			pthread_exit(NULL);
		}
		printf(">> Consumer thread obtained: %d\n", obtainedData);
		int sock = obtainedData;

		char httpHeader[1000];
		int b = read(sock, httpHeader, sizeof httpHeader);
		if (b == -1) continue; //ekleise i sundesi

		char* firstWord = strtok(httpHeader, " ");
		if (firstWord == NULL) continue;

		if (strcmp(firstWord, "GET") != 0)
		{
			printf("This is not a GET request.\n");
			continue;
		}

		char* requestedFile = strtok(NULL, " ");
		if (requestedFile == NULL) continue;

		char *fullFilename = malloc(strlen(rootDir) + strlen(requestedFile) + 2);
		sprintf(fullFilename, "%s/%s", rootDir, requestedFile); //compose full filename

		struct stat sbuf;
		int fileExists = stat(fullFilename, &sbuf);
		char mydate[100];
		time_t cur = time(NULL);
		struct tm *curtm = localtime(&cur);
		const char* daysMonths[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun", "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
		sprintf(mydate, "Date: %s, %02d %s %d %02d:%02d:%02d", daysMonths[curtm->tm_wday-1], curtm->tm_mday, daysMonths[curtm->tm_mon+7], 1900+curtm->tm_year, curtm->tm_hour, curtm->tm_min, curtm->tm_sec);

		if (fileExists < 0) //an to arxeio den uparxei
		{
			//404 Not Found
			char message[] = "<html>Requested file could not be found.</html>";
			sprintf(httpHeader, "HTTP/1.1 404 Not Found\nServer: myhttpd/1.0.0 (Ubuntu64)\nContent-Type: text/html\n%s\nConnection: Closed\nContent-Length: %ld\n\n%s", mydate, strlen(message), message);
			write(sock, httpHeader, strlen(httpHeader));
		}
		else //to arxeio uparxei
		{
			if (!(sbuf.st_mode & S_IRUSR)) //den exw dikaiwmata read
			{
				//403 Forbidden
				char message[] = "<html>Server does not have access to the requested file.</html>";
				sprintf(httpHeader, "HTTP/1.1 403 Forbidden\nServer: myhttpd/1.0.0 (Ubuntu64)\nContent-Type: text/html\n%s\nConnection: Closed\nContent-Length: %ld\n\n%s", mydate, strlen(message), message);
				write(sock, httpHeader, strlen(httpHeader));
			}
			else //ola ok
			{
				//200 OK
				sprintf(httpHeader, "HTTP/1.1 200 OK\nServer: myhttpd/1.0.0 (Ubuntu64)\nContent-Type: text/html\n%s\nConnection: Closed\nContent-Length: %ld\n\n", mydate, sbuf.st_size);
				write(sock, httpHeader, strlen(httpHeader));
				char block[sbuf.st_size];
				int fd = open(fullFilename, 0644);
				read(fd, block, sbuf.st_size);
				close(fd);
				write(sock, block, sizeof block);

				//Update ta statistika
				pthread_mutex_lock(&stats);
				pages += 1;
				bytes += sbuf.st_size;
				pthread_mutex_unlock(&stats);
			}
		}
		close(sock);
	}
	pthread_exit(NULL);
}


int main(int argc, char** argv)
{
	char *command_port = NULL;
	if (argc != 9)
	{
		printf("Wrong syntax. Correct arguments are:\n");
		printf("%s -p serving_port -c command_port -t num_of_threads -d root_dir\n", *argv);
		return 1;
	}

	//Read arguments
	int i;
	for (i = 1; i < argc; )
	{
		if (strcmp(argv[i], "-p") == 0)
		{
			server_port = argv[++i];
			++i;
		}
		else if (strcmp(argv[i], "-c") == 0)
		{
			command_port = argv[++i];
			++i;
		}
		else if (strcmp(argv[i], "-t") == 0)
		{
			numOfThreads = atoi(argv[++i]);
			++i;
		}
		else if (strcmp(argv[i], "-d") == 0)
		{
			rootDir = argv[++i];
			++i;
		}
		else
		{
			printf("Wrong syntax. Correct arguments are:\n");
			printf("%s -p serving_port -c command_port -t num_of_threads -d root_dir\n", *argv);
			return 1;
		}
	}

	gettimeofday(&start, NULL); /* start timer */
	pthread_t comm;
	initialize(&sharedBuffer);
	cons = malloc(sizeof(pthread_t) * numOfThreads);
	pthread_mutex_init(&stats, 0);
	pthread_mutex_init(&mtx, 0);
	pthread_cond_init(&cond_nonempty, 0);
	pthread_cond_init(&cond_nonfull, 0);
	for (i = 0; i < numOfThreads; i++)
		pthread_create(&cons[i], 0, consumer, NULL);
	pthread_create(&prod, 0, producer, NULL);
	pthread_create(&comm, 0, commander, (void*)command_port);


	pthread_join(prod, 0);
	for (i = 0; i < numOfThreads; i++)
		pthread_join(cons[i], 0);
	pthread_join(comm, 0);
	pthread_mutex_destroy(&stats);
	pthread_cond_destroy(&cond_nonempty);
	pthread_cond_destroy(&cond_nonfull);
	pthread_mutex_destroy(&mtx);
	printf("All threads exited normally, allocated memory is released.\n");
	free(cons);
	return 0;
}


