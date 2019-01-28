all:	myhttpd mycrawler Worker

Worker: Worker.o Trie.o ListPost.o ListChar.o MessageInfo.o
	gcc -o Worker Worker.o Trie.o ListPost.o ListChar.o MessageInfo.o
	
Worker.o: Worker.c
	gcc -c Worker.c
	
Trie.o: Trie.c
	gcc -c Trie.c
	
ListChar.o: ListChar.c 
	gcc -c ListChar.c

ListPost.o: ListPost.c
	gcc -c ListPost.c
	
MessageInfo.o: MessageInfo.c
	gcc -c MessageInfo.c

myhttpd: WebServer.o ServerClient.o
	gcc -o myhttpd WebServer.o ServerClient.o -lpthread

mycrawler: WebCrawler.o ServerClient.o MessageInfo.o ListChar.o ListPost.o
	gcc -o mycrawler WebCrawler.o ServerClient.o MessageInfo.o ListChar.o ListPost.o -lpthread

WebCrawler.o: WebCrawler.c
	gcc -c WebCrawler.c

WebServer.o: WebServer.c
	gcc -c WebServer.c

ServerClient.o: ServerClient.c
	gcc -c ServerClient.c
	
clean:
	rm *.o myhttpd mycrawler Worker
	
