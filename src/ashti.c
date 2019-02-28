#include "threads.h"
#include <sys/socket.h>
#include <stdlib.h>
#include <semaphore.h>
#include <stdio.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/sendfile.h>
#include <fcntl.h>
#include <setjmp.h>

#define PORTNUM 9001

typedef struct socketStruct
{
    int             socketFd;
    struct sockaddr *address;
    int             sockaddrlen;
} socketStruct;

void parseHTML(uint64_t job);
char *getBanner(uint64_t type, uint64_t size, char *fLoc);

uint8_t RUNNING = 1;
jmp_buf sigExit;

void
ignoreSIGINT(
    __attribute__ ((unused))
    int sig_num)
{
	RUNNING = 0;
	longjmp(sigExit, 0);
}

char *wwwDir = NULL;

int main(int argc, char **argv)
{
	if(argc != 2)
	{
		fprintf(stderr, "Requires directory.\n");
		return 1;
	}
    struct sigaction ignore = {
        .sa_handler = &ignoreSIGINT,
        .sa_flags = SA_RESTART
    };
    sigaction(SIGINT, &ignore, NULL);
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
	setjmp(sigExit);
	while(RUNNING)
	{
		socketNum = accept(mySock.socketFd, 
				mySock.address, 
				(socklen_t *)&mySock.sockaddrlen);
		add_job(myPool, socketNum);
	}
	free(wwwDir);
	reap_t_pool(myPool, 8);
	destroy_t_pool(myPool);
	return 0;
}

void parseHTML(uint64_t job)
{
	uint64_t size = 256;
	char *buff = calloc(size, sizeof(*buff)+1);
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
					char *error = getBanner(1, 0, NULL);
					write(job, error, strlen(error));
					free(error);
					free(buff);
					free(savePtr);
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
					banner = getBanner(0, fSize, fileLoc);
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
						char *cgiQuery = strchr(testDir, '?');
						if(cgiQuery != NULL)
						{
							*cgiQuery = '\0';
							cgiQuery++;
							setenv("QUERY_STRING", cgiQuery, 1);
						}	
						int64_t index = open(testDir, O_RDONLY);
						if(index == -1)
						{
							printf("ERROR 404\n");
							banner = getBanner(2, 0, NULL);
							write(job, banner, strlen(banner));
							free(fileLoc);
							free(banner);
							free(buff);
							return;
						}
						/* Script */
						printf("Trying %s\n", testDir);
						testDir = realloc(testDir, strlen(testDir) + strlen(" 2>/dev/null") + 1);
						strcat(testDir, " 2>/dev/null");
						FILE *script = popen(testDir, "r");
						char *results = NULL;
						char *fileBuff = NULL;
						if(script != NULL)
						{
							results = calloc(257, sizeof(*results));
							fileBuff = calloc(257, sizeof(*fileBuff));
							if(results == NULL || fileBuff == NULL)
							{
								fprintf(stderr, "Cgi buffs calloc failed\n");
								exit(10);
							}
							int numRead = 0;
							while((numRead = fread(fileBuff, 1, 256, script)) == 256)
							{
								results = realloc(results, strlen(results) + numRead + 2);
								strncat(results, fileBuff, numRead);
							}
							results = realloc(results, strlen(results) + numRead + 2);
							strncat(results, fileBuff, numRead);
						}
						if(pclose(script) != 0)
						{
							fprintf(stderr, "ERROR 500\n");
							banner = getBanner(3, 0, NULL);
							write(job, banner, strlen(banner));
							free(banner);
							free(buff);
							free(fileLoc);
							free(testDir);
							return;
						}
						banner = getBanner(0, strlen(results), testDir);
						write(job, banner, strlen(banner));
						write(job, results, strlen(results));
						free(testDir);
						free(results);
						free(fileBuff);
						free(fileLoc);
						free(banner);
						break;
					}
					uint64_t fSize = lseek(index, 0, SEEK_END);
					lseek(index, 0, SEEK_SET);
					char *banner = getBanner(0, fSize, fileLoc);
					write(job, banner, strlen(banner));
					sendfile(job, index, NULL, fSize);
					free(banner);
					free(fileLoc);
				}
				break;
			}
		}
		token = strtok_r(NULL, " ", &savePtr);
		tokenId++;
	}
	free(buff);
	return;
}

char *getBanner(uint64_t type, uint64_t size, char *fLoc)
{
	char httpBanner[] = "HTTP/1.1 %d %s\r\n"
						"Content-Type:%s\r\n"
						"Content-Length:%d\r\n\r\n";

	char cgiBanner[] = "HTTP/1.1 %d %s\r\n"
					   "Content-Length:%d\r\n";
	
	char *retString = NULL;
	switch(type)
	{
		case(0): 	/* OK */
			{
				char *fExt = strrchr(fLoc, '.');
				if(strcmp(fExt, ".css") == 0)
				{
					asprintf(&retString, httpBanner, 200, "OK", "text/css", size);
				}
				else if(strcmp(fExt, ".txt") == 0)
				{
					asprintf(&retString, httpBanner, 200, "OK", "text/plain", size);
				}
				else if(strcmp(fExt, ".jpeg") == 0)
				{
					asprintf(&retString, httpBanner, 200, "OK", "image/jpeg", size);
				}
				else if(strcmp(fExt, ".png") == 0)
				{
					asprintf(&retString, httpBanner, 200, "OK", "image/png", size);
				}
				else if(strcmp(fExt, ".gif") == 0)
				{
					asprintf(&retString, httpBanner, 200, "OK", "image/gif", size);
				}
				else if(strcmp(fExt, ".html") == 0)
				{
					asprintf(&retString, httpBanner, 200, "OK", "text/html", size);
				}
				else
				{
					asprintf(&retString, cgiBanner, 200, "OK", size);
				}
				break;
			}
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
			{
				char err500[] = "<!doctype html>\n"
					"<html lang=\"en\">\n"
					"<head>\n"
					"<title>Internal Server Error</title>\n"
					"</head>\n"
					"<body>\n"
					"<h2>Error 500</h2>\n"
					"</body>\n"
					"</html>\n";
				asprintf(&retString, httpBanner, 500, "Internal Server Error", "text/html", strlen(err500));
				retString = realloc(retString, strlen(retString) + strlen(err500) + 1);
				strcat(retString, err500);
				break;
			}
			break;
	}
	return retString;
}
