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
char *getBanner(uint64_t type, uint64_t size);

char *wwwDir = NULL;

int main(int argc, char **argv)
{
	if(argc != 2)
	{
		fprintf(stderr, "Requires directory.\n");
		return 1;
	}
	char *filePath = argv[1];
	wwwDir = calloc(strlen(filePath) + 6, sizeof(*wwwDir));
	strcat(wwwDir, filePath);
	strcat(wwwDir, "/www/");
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
	destroy_t_pool(myPool);
	return 0;
}

void parseHTML(uint64_t job)
{
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
				if(strcmp(token, "GET") != 0)
				{
					char *error = getBanner(1, 0);
					write(job, error, strlen(error));
					free(error);
					return;
				}
				break;
			}
			case(1):
			{
				char *banner = NULL;
				char *fileLoc = NULL;
				if(strcmp(token, "/") == 0)
				{
					fileLoc = calloc(strlen(wwwDir) + strlen("index.html") + 1, sizeof(*fileLoc));
					strcpy(fileLoc, wwwDir); /* Makes local copy of www dir string */
					strcat(fileLoc, "index.html"); /* Adds index to file string */
					int64_t index = open(fileLoc, O_RDONLY);
					uint64_t fSize = lseek(index, 0, SEEK_END);
					lseek(index, 0, SEEK_SET);
					banner = getBanner(0, fSize);
					printf("Testing: %s\n", fileLoc);
					write(job, banner, strlen(banner));
					sendfile(job, index, NULL, fSize);
					
				}
				else
				{
					fileLoc = calloc(strlen(wwwDir) + strlen(token) + 1, sizeof(*fileLoc));
					strcpy(fileLoc, wwwDir); /* Makes local copy of www dir string */
					strcat(fileLoc, token + 1); /* Adds the rest of file path to file string */
					int64_t index = open(fileLoc, O_RDONLY);
					printf("Testing1: %s\n", fileLoc);
					if(index == -1)
					{
						char *testDir = calloc(strlen(wwwDir) + strlen(token) + 1, sizeof(*fileLoc));
						strncpy(testDir, wwwDir, strlen(wwwDir) - 4); /* Takes off www part of dir string */ 
						strcat(testDir, token + 1); /* Adds rest of file path to dir string */
						int64_t index = open(testDir, O_RDONLY);
						if(index == -1)
						{
							printf("ERROR 404\n");
							banner = getBanner(2, 0);
							write(job, banner, strlen(banner));
							free(banner);
							return;
						}
						/* Script */
						printf("Trying %s\n", testDir);
						FILE *script = popen(testDir, "r");
						char *results = calloc(257, sizeof(*results));
						char *fileBuff = calloc(257, sizeof(*fileBuff));
						while(fread(fileBuff, 1, 256, script) == 256)
						{
							results = realloc(results, strlen(results) + 257);
							strncat(results, fileBuff, 256);
						}
						banner = getBanner(0, strlen(results));
						write(job, banner, strlen(banner));
						write(job, results, strlen(results));
						free(results);
						free(fileBuff);
						break;
					}
					uint64_t fSize = lseek(index, 0, SEEK_END);
					lseek(index, 0, SEEK_SET);
					char *banner = getBanner(0, fSize);
					write(job, banner, strlen(banner));
					sendfile(job, index, NULL, fSize);
				}
				free(fileLoc);
				free(banner);
				break;
			}
		}
		token = strtok_r(NULL, " ", &savePtr);
		tokenId++;
	}
	free(buff);
	return;
}

char *getBanner(uint64_t type, uint64_t size)
{
	char httpBanner[] = "HTTP/1.1 %d %s\r\n"
						"Content-type:%s\r\n"
						"Content-Length:%d\r\n\r\n";
	
	char *retString = NULL;
	switch(type)
	{
		case(0): 	/* OK */
			asprintf(&retString, httpBanner, 200, "OK", "text/html", size);
			break;
		case(1):	/* 400 */
			{
				char err400[] = "<!doctype html>\n"
					"<html lang=\"en\">\n"
					"<head>\n"
					"<title>Invalid Request</title>\n"
					"</head>\n"
					"<body>\n"
					"<h2>Error 400</h2>\n"
					"</body>\n"
					"</html>\n";
				asprintf(&retString, httpBanner, 400, "Bad Request", "text/html", strlen(err400));
				retString = realloc(retString, strlen(retString) + strlen(err400) + 1);
				strcat(retString, err400);
				break;
			}
		case(2):	/* 404 */
			{
				char err404[] = "<!doctype html>\n"
					"<html lang=\"en\">\n"
					"<head>\n"
					"<title>Page Not Found</title>\n"
					"</head>\n"
					"<body>\n"
					"<h2>Error 404</h2>\n"
					"</body>\n"
					"</html>\n";
				asprintf(&retString, httpBanner, 404, "Not Found", "text/html", strlen(err404));
				retString = realloc(retString, strlen(retString) + strlen(err404) + 1);
				strcat(retString, err404);
				break;
			}
		case(3):	/* 500 */
			break;
	}
	return retString;
}
