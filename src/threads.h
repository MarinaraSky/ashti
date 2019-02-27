#include <pthread.h>
#include <stdint.h>
#include <semaphore.h>

typedef struct mypthread_t
{
	pthread_t thread;
	uint64_t id;
}mypthread_t;

typedef struct queuelist_t
{
	uint64_t num;
	struct queuelist_t *next;
}queuelist_t;

typedef struct queue_t
{
	queuelist_t *head;
	queuelist_t *tail;
}queue_t;

typedef struct t_pool
{
	mypthread_t *threads;
	uint64_t working;
	uint64_t total;
	sem_t mySem;
	queue_t *queue;
	pthread_mutex_t lock;
}t_pool;


t_pool *init_t_pool(uint64_t numOfThreads);
void destroy_t_pool(t_pool *pool);

queue_t *init_jobs(void);
void add_job(queue_t *jobs, uint64_t num);
uint64_t get_job(queue_t *jobs);
void clear_jobs(queue_t *jobs);
