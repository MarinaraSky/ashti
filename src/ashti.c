#include "Threads.h"
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
#include <time.h>
#include <syslog.h>
#include <limits.h>

typedef struct socketStruct
{
    int             socketFd;
    struct sockaddr *address;
    int             sockaddrlen;
} socketStruct;

/*	Begins to parse HTTP Banner request */
void parseHTTP(uint64_t job);
/*	Builds response banner for request 
 *	Takes type of banner requested
 *	Takes size of file if its been opened
 *	A file path to be parsed
 *	A int pointer to set like a flag
 */
static char *getBanner(uint64_t type, uint64_t size, char *fLoc, uint64_t *fileType);
/*	Builds response body for request 
 * 	Takes a file path
 * 	An int pointer to set the type
 * 	An int pointer to return file descriptor
 */
static char *buildRequest(char *filePath, uint64_t *type, int64_t *fd);
/*	Writes to syslog */
static void writeLog(char *cmd);

/* Bool for signal handler */
uint8_t RUNNING = 1;
/* Graceful shutdown */
jmp_buf sigExit;

void
ignoreSIGINT(
    __attribute__ ((unused))
    int sig_num)
{
	RUNNING = 0;
	longjmp(sigExit, 0);
}

/* Global to store directory passed on command line */
char *siteDir = NULL;

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
	siteDir = calloc(strlen(filePath) + 6, sizeof(*siteDir));
	strcat(siteDir, filePath);
	/* Start of threads to handle requests */
	t_pool *myPool = Threads_initThreadPool(8);

	/* Settig up socket */
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
	if (setsockopt
		(fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt,
		 sizeof(opt)) != 0)
	{
		perror("Unable to set socket options.\n");
		exit(1);
	}
	uint64_t portNum = getuid();
	if(portNum < 1024)
	{
		portNum = 9001;
	}
	printf("Binding to port: %lu\n", portNum);
	struct sockaddr_in address = {
		.sin_family = AF_INET,
		.sin_addr.s_addr = INADDR_ANY,
		.sin_port = htons(portNum)
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
	/* Work loop for the accepting of connections */
	while(RUNNING)
	{
		socketNum = accept(mySock.socketFd, 
				mySock.address, 
				(socklen_t *)&mySock.sockaddrlen);
		/* Passed file descriptor to threads */
		Threads_addJob(myPool, socketNum);
	}
	/* Will go here with ctrl+c */
	free(siteDir);
	Threads_reapThreadPool(myPool, 8);
	Threads_destroyThreadPool(myPool);
	return 0;
}

void parseHTTP(uint64_t job)
{
	uint64_t size = 256;
	char *buff = calloc(size, sizeof(*buff)+1);
	read(job, buff, size);
	uint64_t tokenId = 0;
	char *savePtr = NULL;
	/* Writing to syslog */
	writeLog(buff);
	/* Tokenizing request */
	char *token = strtok_r(buff, " ", &savePtr);
	char *reply = NULL;
	uint64_t fileType = 0;
	int64_t fileDesc = 0;
	char *cgiCmd = NULL;
	while(token != NULL)
	{
		switch(tokenId)
		{
			case(0): /* Verifying GET request */
			{
				if(strcmp(token, "GET") != 0)
				{
					char *error = getBanner(1, 0, NULL, NULL);
					write(job, error, strlen(error));
					free(error);
					free(buff);
					return;
				}
				break;
			}
			case(1): /* Parsing resource requested */
			{
				reply = buildRequest(token, &fileType, &fileDesc);
				if(reply == NULL)
				{
					reply = getBanner(2, 0, NULL, NULL);
				}
				else if(fileType == 2)
				{
					cgiCmd = reply;
				}
				break;
			}
			case(2): /* Chech HTTP version */
			{
				if(strcmp(token, "HTTP/1.1") != 0)
				{
					fprintf(stderr, "Bad HTTP VERSION: %s\n", token);
					char *error = getBanner(1, 0, NULL, NULL);
					write(job, error, strlen(error));
					free(buff);
					return;
				}
				break;
			}
			case(3):/* Verify Host */
			{
				if(strcmp(token, "Host:") != 0)
				{
					fprintf(stderr, "NO HOST\n");
					char *error = getBanner(1, 0, NULL, NULL);
					write(job, error, strlen(error));
					free(reply);
					free(buff);
					free(error);
					return;
				}
				break;
			}

		}
		/* Will silently ignore any other header */
		token = strtok_r(NULL, " \r\n", &savePtr);
		tokenId++;
	}
	/* File not found or cannot be opend */ 
	if(fileDesc == -1)
	{
		char *error = getBanner(2, 0, NULL, NULL);
		write(job, error, strlen(error));
		free(buff);
		free(reply);
		free(error);
		return;
	}
	if(fileType == 0) /* text based file */
	{
		int64_t fSize = lseek(fileDesc, 0, SEEK_END);
		if(fSize == -1)
		{
			/* 404 */
			char *error = getBanner(2, 0, NULL, NULL);
			write(job, error, strlen(error));
			free(error);
			free(buff);
			free(reply);
			return;
		}
		lseek(fileDesc, 0, SEEK_SET);
		char *body = calloc(1, fSize + 2);
		int64_t byteRead = read(fileDesc, body, fSize);
		if(byteRead != fSize)
		{
			/* 404 */
			char *error = getBanner(2, 0, NULL, NULL);
			write(job, error, strlen(error));
			free(buff);
			free(reply);
			return;
		}
		reply = realloc(reply, strlen(body) + strlen(reply) + 1);
		strcat(reply, body);
		free(body);
		write(job, reply, strlen(reply));
	}
	else if(fileType == 1) /* Data file */
	{
		uint64_t fSize = lseek(fileDesc, 0, SEEK_END);
		lseek(fileDesc, 0, SEEK_SET);
		write(job, reply, strlen(reply));
		sendfile(job, fileDesc, NULL, fSize);
	}
	else if(fileType == 2) /* CGI script */
	{
		/* Checks if query */
		char *cgiQuery = strchr(cgiCmd, '?');
		if(cgiQuery != NULL)
		{
			*cgiQuery = '\0';
			cgiQuery++;
			setenv("QUERY_STRING", cgiQuery, 1);
		}	
		/* Does the fork and exec of cgiCmd */
		FILE *script = popen(cgiCmd, "r");
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
		/* Checks return code of script */
		if(pclose(script) != 0)
		{
			fprintf(stderr, "ERROR 500\n");
			reply = getBanner(3, 0, NULL, NULL);
			write(job, reply, strlen(reply));
			free(buff);
			free(fileBuff);
			free(results);
			free(cgiCmd);
			free(reply);
			return;
		}
		char *banner = getBanner(0, strlen(results), reply, NULL);
		write(job, banner, strlen(banner));
		write(job, results, strlen(results));
		free(fileBuff);
		free(results);
		free(banner);
	}
	free(reply);
	free(buff);
	return;
}

char *buildRequest(char *filePath, uint64_t *type, int64_t *fd)
{
	char basePath[] = "%s%s%s";
	char *retFilePath = NULL;
	if(strcmp(filePath, "/") == 0)
	{
		/* Builds file path */
		asprintf(&retFilePath, basePath, siteDir, "/www/", "index.html");	
		*fd = open(retFilePath, O_RDONLY);
		if(*fd == -1)
		{
			free(retFilePath);
			return NULL;
		}
		uint64_t fSize = lseek(*fd, 0, SEEK_END);
		lseek(*fd, 0, SEEK_SET);
		char *banner = getBanner(0, fSize, retFilePath, type);
		free(retFilePath);
		return banner;
	}
	/* Checking to see if its supposed to be a script */
	else if(strncmp(filePath, "/cgi-bin/", 9) == 0)
	{
		char *file = strrchr(filePath, '/');
		asprintf(&retFilePath, basePath, siteDir, "/cgi-bin/", file++);	
		*type = 2;
		/* Open file and return */
		return retFilePath;
	}
	/* Remaining requests will be looked for in www directory */
	else
	{
		asprintf(&retFilePath, basePath, siteDir, "/www", filePath++);	
		char *fullPath = NULL;
		/* Attempts to resolve directory traversing */
		fullPath = realpath(retFilePath, fullPath);
		if(fullPath == NULL)
		{
			fprintf(stderr, "Failed to find full path.");
			free(fullPath);
			free(retFilePath);
			return NULL;
		}
		char *subStr = strstr(fullPath, "/www/");
		if(subStr == NULL)
		{
			/* 404 */
			free(fullPath);
			free(retFilePath);
			return NULL;
		}
		*fd = open(retFilePath, O_RDONLY);
		if(*fd == -1)
		{
			free(fullPath);
			free(retFilePath);
			return NULL;
		}
		uint64_t fSize = lseek(*fd, 0, SEEK_END);
		lseek(*fd, 0, SEEK_SET);
		char *banner = getBanner(0, fSize, retFilePath, type);
		free(fullPath);
		free(retFilePath);
		return banner;
	}
}

char *getBanner(uint64_t type, uint64_t size, char *fLoc, uint64_t *fileType)
{
	char httpBanner[] = "HTTP/1.1 %d %s\r\n"
						"Date:%s\r\n"
						"Content-Type:%s\r\n"
						"Content-Length:%d\r\n\r\n";

	char cgiBanner[] = "HTTP/1.1 %d %s\r\n"
					   "Date:%s\r\n";
	
	char *retString = NULL;
	struct tm *curTime;
	time_t localTime;
	time(&localTime);
	curTime = localtime(&localTime);
	char *date = asctime(curTime);
	date[strlen(date)-1] = '\0';
	switch(type)
	{
		case(0): 	/* OK */
			{
				char *fExt = strrchr(fLoc, '.');
				if(strcmp(fExt, ".css") == 0)
				{
					asprintf(&retString, httpBanner, 200, "OK", date, "text/css", size);
					*fileType = 0;
				}
				else if(strcmp(fExt, ".txt") == 0)
				{
					asprintf(&retString, httpBanner, 200, "OK", date, "text/plain", size);
					*fileType = 0;
				}
				else if(strcmp(fExt, ".jpeg") == 0)
				{
					asprintf(&retString, httpBanner, 200, "OK", date, "image/jpeg", size);
					*fileType = 1;
				}
				else if(strcmp(fExt, ".ico") == 0)
				{
					asprintf(&retString, httpBanner, 200, "OK", date, "image/vnd.microsoft.icon", size);
					*fileType = 0;
				}
				else if(strcmp(fExt, ".png") == 0)
				{
					asprintf(&retString, httpBanner, 200, "OK", date, "image/png", size);
					*fileType = 1;
				}
				else if(strcmp(fExt, ".gif") == 0)
				{
					asprintf(&retString, httpBanner, 200, "OK", date, "image/gif", size);
					*fileType = 1;
				}
				else if(strcmp(fExt, ".html") == 0)
				{
					asprintf(&retString, httpBanner, 200, "OK", date, "text/html", size);
					*fileType = 0;
				}
				else
				{
					asprintf(&retString, cgiBanner, 200, "OK", date);
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
				asprintf(&retString, httpBanner, 400, "Bad Request", date, "text/html", strlen(err400));
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
				asprintf(&retString, httpBanner, 404, "Not Found", date, "text/html", strlen(err404));
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
				asprintf(&retString, httpBanner, 500, "Internal Server Error", date, "text/html", strlen(err500));
				retString = realloc(retString, strlen(retString) + strlen(err500) + 1);
				strcat(retString, err500);
				break;
			}
			break;
	}
	return retString;
}

void writeLog(char *cmd)
{
	struct tm *curTime;
	time_t localTime;
	time(&localTime);
	curTime = localtime(&localTime);
	char *date = asctime(curTime);
	openlog("jspence_ashti", LOG_PID | LOG_NOWAIT | LOG_CONS, LOG_LOCAL0);
	syslog(LOG_INFO, "Page Requested: %s\nTime: %s", cmd, date);
	closelog();
}
