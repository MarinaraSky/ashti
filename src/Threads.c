#include "Threads.h"
#include <pthread.h>
#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <semaphore.h>
#include <unistd.h>

typedef void   *(
    *func_w)        (
    void *);

typedef struct args
{
    t_pool         *pool;
    uint64_t        id;
} args;

static void     Threads_working(
    args * create);

static          queue_t * Threads_initJobs(
    void);

static          uint64_t Threads_getJob(
    queue_t * jobs);

void            parseHTTP(
    uint64_t job);

t_pool         *
Threads_initThreadPool(
    uint64_t numOfThreads)
{
    if (numOfThreads == 0)
    {
        fprintf(stderr, "Cannot initialize zero threads\n");
        return NULL;
    }
    t_pool         *pool = calloc(1, sizeof(*pool));

    if (pool == NULL)
    {
        fprintf(stderr, "Pool calloc failed\n");
        exit(1);
    }
    pool->threads = calloc(numOfThreads, sizeof(*pool->threads));
    if (pool->threads == NULL)
    {
        fprintf(stderr, "Threads calloc failed\n");
        exit(2);
    }
    sem_init(&pool->mySem, 0, 0);
    if (pthread_mutex_init(&pool->lock, NULL) != 0)
    {
        fprintf(stderr, "Failed mutex initialization\n");
        exit(7);
    }
    pool->queue = Threads_initJobs();
    for (uint64_t i = 0; i < numOfThreads; i++)
    {
        args           *create = calloc(1, sizeof(*create));

        if (create == NULL)
        {
            fprintf(stderr, "Args calloc failed\n");
            exit(3);
        }
        create->pool = pool;
        create->id = i;
        int             ret =
            pthread_create(&pool->threads[i].thread, NULL,
                           (func_w) Threads_working, create);
        if (ret != 0)
        {
            fprintf(stderr, "Cannot create thread\n");
            exit(4);
        }
    }
    return pool;
}


/* Planned to loop and sleep */
void
Threads_working(
    args * create)
{
    mypthread_t     curThread = create->pool->threads[create->id];

    curThread.id = create->id;
    pthread_cleanup_push(free, create);
    while (1)
    {
        printf("%lu: Waiting turn\n", curThread.id);
        sem_wait(&create->pool->mySem);
        create->pool->working++;
        pthread_mutex_lock(&create->pool->lock);
        uint64_t        job = Threads_getJob(create->pool->queue);

        pthread_mutex_unlock(&create->pool->lock);
        parseHTTP(job);
        create->pool->working--;
        close(job);
    }
    pthread_cleanup_pop(1);
    return;
}

void
Threads_reapThreadPool(
    t_pool * pool,
    uint64_t num)
{
    for (uint64_t i = 0; i < num; i++)
    {
        printf("reaping %lu\n", i);
        pthread_cancel(pool->threads[i].thread);
        pthread_join(pool->threads[i].thread, NULL);
    }
    return;

}

void
Threads_destroyThreadPool(
    t_pool * pool)
{
    sem_destroy(&pool->mySem);
    pthread_mutex_destroy(&pool->lock);
    free(pool->threads);
    free(pool->queue);
    free(pool);
}

queue_t        *
Threads_initJobs(
    void)
{
    queue_t        *jobs = calloc(1, sizeof(*jobs));

    if (jobs == NULL)
    {
        fprintf(stderr, "Cannot create jobs list\n");
        exit(6);
    }
    jobs->head = NULL;
    jobs->tail = NULL;
    return jobs;
}

void
Threads_addJob(
    t_pool * pool,
    uint64_t num)
{
    pthread_mutex_lock(&pool->lock);
    queuelist_t    *newJob = calloc(1, sizeof(*newJob));

    newJob->num = num;
    newJob->next = NULL;
    if (pool->queue->head == NULL)
    {
        pool->queue->head = newJob;
        pthread_mutex_unlock(&pool->lock);
        sem_post(&pool->mySem);
        return;
    }
    queuelist_t    *index = pool->queue->head;

    while (index->next != NULL)
    {
        index = index->next;
    }
    index->next = newJob;
    pthread_mutex_unlock(&pool->lock);
    sem_post(&pool->mySem);
    return;
}

uint64_t
Threads_getJob(
    queue_t * jobs)
{
    uint64_t        ret = jobs->head->num;
    queuelist_t    *old = jobs->head;

    jobs->head = jobs->head->next;
    free(old);
    return ret;
}
