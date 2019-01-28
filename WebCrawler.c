#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <signal.h>
#include <sys/wait.h>
#include "ListChar.h"
#include "MessageInfo.h"
#include "ServerClient.h"

#define BUFFER_SIZE 10

struct Buffer
{
	char **buffer;
	int start;
	int end;
	int count;
};

typedef struct Buffer Buffer;

//Askisi 2 globals
struct ListChar directories;
int *fdIn, *fdOut, *pids, w;

int canWrite;
//end of askisi 2 globals

int numOfThreads;
pthread_t *cons, prod;
pthread_mutex_t mtx;
pthread_cond_t cond_nonempty;
pthread_mutex_t stats;
pthread_cond_t cond_nonfull;
pthread_cond_t cond_notready;
Buffer sharedBuffer;
int pages;
long unsigned bytes;
struct timeval start;
char *server_port, *saveDir;
char *initUrl, *hostname;

double getTimeDiff(struct timeval st, struct timeval en)
{
	double timeInMs = ((en.tv_sec - st.tv_sec) * 1000.0 +
			(en.tv_usec - st.tv_usec) / 1000.0);
	return timeInMs / 1000;
}

void initialize(Buffer* bf)
{
	bf->buffer = malloc(sizeof(char*)*BUFFER_SIZE);
	bzero(bf->buffer, BUFFER_SIZE);
	bf->start = bf->count = 0;
	bf->end = -1;
}

void createSubFolders(char *filename)
{
	//saveDir/site0/page0_1234.html

	//Copy filename
	char *filenameCopy = malloc(strlen(filename) + 1);
	strcpy(filenameCopy, filename);

	char* prev = NULL;
	char* token = strtok(filenameCopy, "/");
	while(token)
	{
		//An to token teleiwnei se .html stamatame
		unsigned int len = strlen(token);
		if (strncmp(token+len-5, ".html", 5) == 0)
			break;

		if (prev == NULL)
			mkdir(token, 0755);
		else
		{
			char* dirToCreate = malloc(strlen(prev) + strlen(token) + 2);
			sprintf(dirToCreate, "%s/%s", prev, token);
			mkdir(dirToCreate, 0755);
			pthread_mutex_lock(&stats);
			if (searchLC(directories, dirToCreate) == false)
				insertLC(&directories, dirToCreate);
			pthread_mutex_unlock(&stats);
			free(dirToCreate);
			free(prev);
			prev = NULL;
		}
		prev = malloc(strlen(token) + 1);
		strcpy(prev, token);
		token = strtok(NULL, "/");
	}

	if (prev)
		free(prev);
	free(filenameCopy);
}

void place(Buffer* bf, char* data)
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
	bf->buffer[bf->end] = malloc(strlen(data) + 1);
	strcpy(bf->buffer[bf->end], data);
	++(bf->count);
	if (pthread_mutex_unlock(&mtx) < 0) { fprintf(stderr, "Error in mutex unlock"); exit(1); }
	if (pthread_cond_signal(&cond_nonempty) < 0) { fprintf(stderr, "Error in pthread cond signal"); exit(1); }
}

char* obtain(Buffer* bf)
{
	char* data;
	pthread_mutex_lock (&mtx);
	data = NULL;
	while (bf->count == 0)
	{
		printf (" >> Found Buffer Empty\n");
		if (pthread_cond_wait(&cond_nonempty, &mtx) < 0)
		{
			fprintf(stderr, "Error in pthread cond wait");
			exit(1);
		}
	}
	data = malloc(strlen(bf->buffer[bf->start]) + 1);
	strcpy(data, bf->buffer[bf->start]);
	free(bf->buffer[bf->start]);
	bf->buffer[bf->start] = NULL;
	bf->start = (bf->start + 1) % BUFFER_SIZE;
	--(bf->count);
	char *filename = strchr(data, ':');
	filename = strchr(filename+1, ':');
	filename = strchr(filename+1, '/');
	char* fullFilename = malloc(strlen(saveDir) + 1 + strlen(filename));
	sprintf(fullFilename, "%s/%s", saveDir, filename);
	createSubFolders(fullFilename);
	int fd = creat(fullFilename, 0644); //create empty file
	free(fullFilename);
	close(fd); //close empty file
	pthread_mutex_unlock (&mtx);
	pthread_cond_signal(&cond_nonfull);
	return data;
}

void terminate()
{
	int i;
	//Kill all consumer threads
	for (i = 0; i < numOfThreads; i++)
		place(&sharedBuffer, (char*)"000");
}

void* producer(void* command_port)
{
	struct timeval end;
	//   http://localhost:8080/site0/page0_1234.html
	char *initial = malloc(strlen("http://") + strlen(hostname)+1+strlen((char*)server_port)+strlen(initUrl)+1);
	sprintf(initial, "http://%s:%s%s", hostname, (char*)server_port, initUrl);
	place(&sharedBuffer, initial);
	free(initial);

	printf("Producer is waiting for files to be downloaded.\n");

	usleep(500000);

	//Wait threads to finish
	pthread_mutex_lock(&mtx);
	while (sharedBuffer.count > 0)
		pthread_cond_wait(&cond_notready, &mtx);
	pthread_mutex_unlock(&mtx);

	printf("Producer is sending directories to Workers.\n");

	//Askisi 2 initials
	int w = 2, i;
	char line[MSGSIZE], reply[MSGSIZE]; //in and out message buffers

	mkdir("fifo", 0755);

	pids = malloc(sizeof(int) *  w);
	fdIn = malloc(sizeof(int) *  w);
	fdOut = malloc(sizeof(int) * w);

	//Create, open named pipes and create w workers
	for (i = 0; i < w; i++)
	{
		char npIn[15], npOut[15];
		composePipeNames(i, npIn, npOut);
		unlink(npIn);  mkfifo(npIn,  0644);
		unlink(npOut); mkfifo(npOut, 0644);
		fdIn[i]  = open(npIn,  O_RDWR | O_NONBLOCK);
		fdOut[i] = open(npOut, O_RDWR | O_NONBLOCK);

		if ((pids[i] = fork()) < 0) { perror("fork"); exit(1); }
		else if (!pids[i])
		{
			//Worker process
			execlp("./Worker", "Worker", npOut, npIn, NULL);
			perror("exec"); exit(1);
		}
	}

	//Send directories to workers
	struct LCNode *cur;
	for (i = 0, cur = directories.start; cur; i++, cur = cur->next)
	{
		bzero(line, MSGSIZE);
		strcpy(line, cur->word);
		writeLine(line, fdOut[i % w]);
	}
	bzero(line, MSGSIZE);
	strcpy(line, "end");
	for (i = 0; i < w; i++)
		writeLine(line, fdOut[i]);
	//End of Askisi 2 initials

	int sock = createServer(atoi(command_port));
	printf("Producer is ready to receive telnet connections.\n");

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
			char buf[MSGSIZE];
			bzero(buf, sizeof buf);

			int b = read(clientSocket, buf, sizeof buf - 1);
			if (b == -1)
				continue; //ekleise i sundesi

			char* token = strtok(buf, "\n\r ");
			if (token == NULL) //ekleise i sundesi
				break;

			if (!strcmp(token, "STATS"))
			{
				gettimeofday(&end, NULL); /* end timer */
				double runTime = getTimeDiff(start, end);
				int minutesUp = runTime / 60;
				runTime -= (minutesUp*60);
				pthread_mutex_lock(&stats);
				sprintf(buf, "Crawler up for %02d:%05.02lf, downloaded %d pages, %lu bytes\n", minutesUp, runTime, pages, bytes);
				pthread_mutex_unlock(&stats);
				write(clientSocket, buf, strlen(buf));
			}
			else if (!strcmp(token, "SHUTDOWN"))
			{
				//Send /exit
				int total=0;
				strcpy(line, "/exit");
				for (i = 0; i < w; i++)
				{
					bzero(reply, MSGSIZE);
					writeLine(line, fdOut[i]);
					readLine(reply, fdIn[i]);
					sprintf(reply, "Reply: %d\n", atoi(reply));
					write(clientSocket, reply, strlen(reply));
					total += atoi(reply);
				}

				//Wait workers to exit and destroy named pipes
				for (i = 0; i < w; i++)
				{
					waitpid(pids[i], NULL, 0);
					char npIn[15], npOut[15];
					composePipeNames(i, npIn, npOut);
					close(fdIn[i]); close(fdOut[i]);
					unlink(npIn);  	unlink(npOut);
					rmdir("fifo");
				}

				sprintf(reply, "All workers found %d strings and exited.\n", total);
				write(clientSocket, reply, strlen(reply));

				//Free memory
				free(pids);
				free(fdIn);
				free(fdOut);
				destructLC(&directories);

				sprintf(buf, "Crawler is shutting down gently.\n");
				write(clientSocket, buf, strlen(buf));
				terminate();
				pthread_exit(NULL);
			}
			else if (!strcmp(token, "SEARCH"))
			{
				//Askisi 2
				memcpy(line, buf, MSGSIZE);
				line[strlen("SEARCH")] = ' ';
				for (i = 0; i < w; i++) //send search command
					writeLine(line, fdOut[i]);

				int normal=0, forced=0;
				for (i = 0; i < w; i++)
				{
					while(1)
					{
						bzero(reply, MSGSIZE);
						readLine(reply, fdIn[i]);
						if (strcmp(reply, "end") == 0)
						{
							normal++;
							break;
						}
						else if (strcmp(reply, "endD") == 0)
						{
							forced++;
							break;
						}
						else
						{
							strcat(reply, "\n");
							write(clientSocket, reply, strlen(reply));
						}
					}
				}

				sprintf(reply, "%d out of %d workers replied on time\n", normal, normal+forced);
				write(clientSocket, reply, strlen(reply));
			}
		}
	}
	pthread_exit(NULL);
}

void scanFileForUrls(char* fileContents)
{
	int i;
	//<a href="../site0/page0_1234.html">aaaaaa</a>
	char *url = strstr(fileContents, "<a href=");
	do
	{
		if (!url) //no more urls
			break;

		url = strtok(url+strlen("<a href=\"")+2, "\"");
		char *fullFilename = malloc(strlen(saveDir) + strlen(url) + 1);
		sprintf(fullFilename, "%s/%s", saveDir, url);
		int exists = 0;
		struct stat sbuf;
		pthread_mutex_lock(&mtx);
		//Elegxos an uparxei san arxeio
		if (stat(fullFilename, &sbuf) != -1)
			exists = 1;

		if (exists == 0)
		{
			//Elegxos an uparxei sto buffer
			for (i = 0; i < BUFFER_SIZE; i++)
			{
				if (sharedBuffer.buffer[i] == NULL)
					continue;
				//http://localhost:8080/siteX/pageX_1234.html
				char *filename = strchr(sharedBuffer.buffer[i], ':');
				filename = strchr(filename+1, ':');
				filename = strchr(filename+1, '/');
				if (strcmp(sharedBuffer.buffer[i], url) == 0)
				{
					exists = 1;
					break;
				}
			}
		}
		pthread_mutex_unlock(&mtx);
		free(fullFilename);

		//an den uparxei, to vazoume stin oura
		char *request = malloc(strlen("http://") + strlen(hostname) + 1 + strlen(server_port) + strlen(url) + 1);
		sprintf(request, "http://%s:%s%s", hostname, server_port, url);
		if (exists == 0)
			place(&sharedBuffer, request);
		free(request);

		//Pame sto epomeno url
		url += (strlen(url) + 1);
		url = strstr(url, "<a href");
	}while(1);
}

void* consumer(void* ptr)
{
	while (1)
	{
		char* obtainedData = obtain(&sharedBuffer);
		if (strncmp(obtainedData, (char*)"000", 3) == 0)
		{
			printf("Consumer thread is exiting.\n");
			free(obtainedData);
			pthread_exit(NULL);
		}

		printf("Consumer obtained: %s\n", obtainedData); fflush(stdout);

		//http://localhost:8080/site0/page0_1234.html
		//part1: localhost
		//part2: 8080
		//part3: /site0/page0_1234.html

		char* part1 = strtok(obtainedData + strlen("http://"), ":"); //hostname
		if (!part1) continue;
		char* part2 = strtok(NULL, "/"); //port
		if (!part2) continue;
		char* part3 = strtok(NULL, "\n"); //page
		if (!part3) continue;
		int port = atoi(part2);

		//Connect to server
		int sock = createClient(part1, port);
		//Request (GET) page
		char request[1024];
		sprintf(request, "GET /%s HTTP/1.1\nUser-Agent: Mozilla/4.0 (compatible; MSIE5.01; Windows NT)\nContent-Type: text/html\nConnection: Closed\n\n", part3);
		write(sock, request, strlen(request));

		char response[1024];
		bzero(response, sizeof response);
		int b = read(sock, response, sizeof response - 1);
		if (b == -1) continue; //ekleise i sundesi
		char *body = strstr(response, "\n\n");
		if (body == NULL) continue; else body += 2;

		//Find file size
		char *contentLength = strstr(response, "Content-Length");
		int fileSize = 0;
		if (contentLength == NULL) continue; else fileSize = atoi(contentLength + 16);

		//Receive file
		char *fileContents = malloc(fileSize);
		memset(fileContents, 0, fileSize);
		int received = b - (body - response);
		int remaining = fileSize - received;
		memcpy(fileContents, body, received); //prwto meros arxeiou
		while(remaining) //gia to upoloipo meros tou arxeiou
		{
			b = read(sock, fileContents + received, remaining);
			if (b == -1)
				break;
			else
			{
				remaining -= b;
				received += b;
			}
		}
		if (b == -1)
		{
			free(fileContents);
			continue;
		}

		//Update ta statistika
		pthread_mutex_lock(&stats);
		pages += 1;
		bytes += fileSize;
		pthread_mutex_unlock(&stats);

		//Write file to disk
		char *fullFilename = malloc(strlen(saveDir) + strlen(part3) + 2);
		sprintf(fullFilename, "%s/%s", saveDir, part3);

		int fd = open(fullFilename, O_WRONLY | O_APPEND);
		write(fd, fileContents, fileSize);
		close(fd);
		free(fullFilename);

		//Scan file for urls
		scanFileForUrls(fileContents);

		free(fileContents);
		close(sock);
	}
	pthread_exit(NULL);
}


int main(int argc, char** argv)
{
	char *command_port = NULL;
	if (argc != 12)
	{
		printf("Wrong syntax. Correct arguments are:\n");
		printf("%s -h host_or_IP -p port -c command_port -t num_of_threads -d save_dir starting_URL\n", *argv);
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
		else if (strcmp(argv[i], "-h") == 0)
		{
			hostname = argv[++i];
			++i;
		}
		else if (strcmp(argv[i], "-t") == 0)
		{
			numOfThreads = atoi(argv[++i]);
			++i;
		}
		else if (strcmp(argv[i], "-d") == 0)
		{
			saveDir = argv[++i];
			initUrl = argv[++i];
			++i;
		}
		else
		{
			printf("Wrong syntax. Correct arguments are:\n");
			printf("%s -h host_or_IP -p port -c command_port -t num_of_threads -d save_dir starting_URL\n", *argv);
			return 1;
		}
	}

	gettimeofday(&start, NULL); /* start timer */
	initialize(&sharedBuffer);
	cons = malloc(sizeof(pthread_t) * numOfThreads);
	pthread_mutex_init(&stats, 0);
	pthread_mutex_init(&mtx, 0);
	pthread_cond_init(&cond_nonempty, 0);
	pthread_cond_init(&cond_nonfull, 0);
	pthread_cond_init(&cond_notready, 0);
	for (i = 0; i < numOfThreads; i++)
		pthread_create(&cons[i], 0, consumer, NULL);
	pthread_create(&prod, 0, producer, command_port);


	pthread_join(prod, 0);
	for (i = 0; i < numOfThreads; i++)
		pthread_join(cons[i], 0);
	pthread_mutex_destroy(&stats);
	pthread_cond_destroy(&cond_nonempty);
	pthread_cond_destroy(&cond_nonfull);
	pthread_cond_destroy(&cond_notready);
	pthread_mutex_destroy(&mtx);
	printf("All threads exited normally, allocated memory is released.\n");
	free(cons);
	for (i = 0; i < BUFFER_SIZE; i++)
		if (sharedBuffer.buffer[i])
			free(sharedBuffer.buffer[i]);
	free(sharedBuffer.buffer);
	return 0;
}

