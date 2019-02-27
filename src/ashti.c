#include "threads.h"
#include <sys/socket.h>
#include <stdlib.h>
#include <semaphore.h>
#include <stdio.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <sys/sendfile.h>
#include <fcntl.h>

#define PORTNUM 9001

typedef struct socketStruct
{
    int             socketFd;
    struct sockaddr *address;
    int             sockaddrlen;
} socketStruct;

void parseHTML(uint64_t job);
int main(void)
{
	t_pool *myPool = init_t_pool(8);

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
	if (setsockopt
		(fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt,
		 sizeof(opt)) != 0)
	{
		perror("Unable to set socket options.\n");
		exit(1);
	}
	struct sockaddr_in address = {
		.sin_family = AF_INET,
		.sin_addr.s_addr = INADDR_ANY,
		.sin_port = htons(PORTNUM)
	};
	socketStruct    mySock = {
		.socketFd = fd,
		.address = (struct sockaddr *) &address,
		.sockaddrlen = sizeof(address)
	};
	if (bind(fd, (struct sockaddr *) &address, sizeof(address)) < 0)
	{
		perror("Unable to bind socket.\n");
		exit(1);
	}
    listen(mySock.socketFd, 2);
	int socketNum = 0;
	while(1)
	{
		socketNum = accept(mySock.socketFd, 
				mySock.address, 
				(socklen_t *)&mySock.sockaddrlen);
		add_job(myPool, socketNum);
	}

	//sleep(1);
	destroy_t_pool(myPool);
	return 0;
}

void parseHTML(uint64_t job)
{
	//char reply[] = "You have reached my server.";
	char ret200[] = "HTTP/1.1 200 OK\r\nContent-type:text/html\r\nContent-Length:%d\r\n\r\n";
	char error400[] = "HTTP/1.1 400 Bad Request\r\nContent-type:text/html\r\nContent-Length:105\r\n\r\n\
<!doctype html>\n\
<html lang=\"en\">\n\
<head>\n\
<title>Error 400</title>\n\
</head>\n\
<body>\n\
<h2>Invalid Request</h2>\n\
</body>\n\
</html>\n";
	uint64_t size = 256;
	char *buff = calloc(size, sizeof(*buff));
	read(job, buff, size);
	uint64_t tokenId = 0;
	char *savePtr = NULL;
	char *token = strtok_r(buff, " ", &savePtr);
	while(token != NULL)
	{
		switch(tokenId)
		{
			case(0):
			{
				if(strcmp(token, "GET") == 0)
				{
					printf("GET REQUEST HERE\n");
				}
				else
				{
					write(job, error400, strlen(error400));
				}
			}
			case(1):
			{
				if(strcmp(token, "/") == 0 || strcmp(token, "/index.html") == 0)
				{
					uint64_t index = open("www/index.html", O_RDONLY);
					uint64_t fSize = lseek(index, 0, SEEK_END);
					lseek(index, 0, SEEK_SET);
					char reply[500] = {0};
					sprintf(reply, ret200, fSize); 
					write(job, reply, strlen(reply));
					sendfile(job, index, NULL, fSize);
				}
			}
		}
		//printf("%s\n", token);
		token = strtok_r(NULL, " ", &savePtr);
		tokenId++;
	}
	//write(job, reply, strlen(reply));
	return;
}
