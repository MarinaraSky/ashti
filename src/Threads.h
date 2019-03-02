#ifndef THREADS_H
#define THREADS_H

#include <pthread.h>
#include <stdint.h>
#include <semaphore.h>

typedef struct mypthread_t
{
    pthread_t       thread;
    uint64_t        id;
} mypthread_t;

typedef struct queuelist_t
{
    uint64_t        num;
    struct queuelist_t *next;
} queuelist_t;

typedef struct queue_t
{
    queuelist_t    *head;
    queuelist_t    *tail;
} queue_t;

typedef struct t_pool
{
    mypthread_t    *threads;
    uint64_t        working;
    uint64_t        total;
    sem_t           mySem;
    queue_t        *queue;
    pthread_mutex_t lock;
} t_pool;


/* Thread pool that will spawn numOfThreads */
t_pool         *Threads_initThreadPool(
    uint64_t numOfThreads);

/* Frees up memory for thread pool */
void            Threads_destroyThreadPool(
    t_pool * pool);

/* Adds job for thread to do, can be customized */
void            Threads_addJob(
    t_pool * pool,
    uint64_t num);

/* NOT IMPLEMENTED */
void            Threads_clearJobs(
    queue_t * jobs);

/* Joins threads and terminates */
void            Threads_reapThreadPool(
    t_pool * pool,
    uint64_t num);

#endif
