#include "threads.h"
#include <sys/socket.h>
#include <stdlib.h>
#include <semaphore.h>
#include <stdio.h>
#include <netinet/in.h>

#define PORTNUM 9001

typedef struct socketStruct
{
    int             socketFd;
    struct sockaddr *address;
    int             sockaddrlen;
} socketStruct;

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
		pthread_mutex_lock(&myPool->lock);
		add_job(myPool->queue, socketNum);
		pthread_mutex_unlock(&myPool->lock);
		sem_post(&myPool->mySem);
	}

	//sleep(1);
	destroy_t_pool(myPool);
	return 0;
}
