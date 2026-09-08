#include "ember_threadpool.h"

#include <pthread.h>
#include <stdlib.h>

typedef struct ember_job {
    void (*fn)(void *);
    void *arg;
    struct ember_job *next;
} ember_job;

struct ember_threadpool {
    pthread_t *threads;
    size_t num_threads;

    pthread_mutex_t mutex;
    pthread_cond_t cond;

    ember_job *head;
    ember_job *tail;
    size_t pending;

    bool shutting_down;
};

static void *worker_main(void *arg) {
    ember_threadpool *pool = arg;

    for (;;) {
        pthread_mutex_lock(&pool->mutex);
        while (!pool->head && !pool->shutting_down) {
            pthread_cond_wait(&pool->cond, &pool->mutex);
        }

        if (!pool->head && pool->shutting_down) {
            pthread_mutex_unlock(&pool->mutex);
            break;
        }

        ember_job *job = pool->head;
        pool->head = job->next;
        if (!pool->head) pool->tail = NULL;
        pool->pending--;
        pthread_mutex_unlock(&pool->mutex);

        job->fn(job->arg);
        free(job);
    }
    return NULL;
}

ember_threadpool *ember_threadpool_create(size_t num_threads) {
    if (num_threads == 0) num_threads = 1;

    ember_threadpool *pool = malloc(sizeof(ember_threadpool));
    if (!pool) return NULL;

    pool->threads = malloc(sizeof(pthread_t) * num_threads);
    if (!pool->threads) {
        free(pool);
        return NULL;
    }

    pool->num_threads = num_threads;
    pool->head = pool->tail = NULL;
    pool->pending = 0;
    pool->shutting_down = false;
    pthread_mutex_init(&pool->mutex, NULL);
    pthread_cond_init(&pool->cond, NULL);

    for (size_t i = 0; i < num_threads; i++) {
        if (pthread_create(&pool->threads[i], NULL, worker_main, pool) != 0) {
            /* Best-effort: shrink to however many threads actually started
             * rather than tearing everything down over one failed spawn. */
            pool->num_threads = i;
            break;
        }
    }

    return pool;
}

bool ember_threadpool_submit(ember_threadpool *pool, void (*fn)(void *), void *arg) {
    pthread_mutex_lock(&pool->mutex);
    if (pool->shutting_down) {
        pthread_mutex_unlock(&pool->mutex);
        return false;
    }

    ember_job *job = malloc(sizeof(ember_job));
    if (!job) {
        pthread_mutex_unlock(&pool->mutex);
        return false;
    }
    job->fn = fn;
    job->arg = arg;
    job->next = NULL;

    if (pool->tail) {
        pool->tail->next = job;
    } else {
        pool->head = job;
    }
    pool->tail = job;
    pool->pending++;

    pthread_cond_signal(&pool->cond);
    pthread_mutex_unlock(&pool->mutex);
    return true;
}

size_t ember_threadpool_pending(ember_threadpool *pool) {
    pthread_mutex_lock(&pool->mutex);
    size_t pending = pool->pending;
    pthread_mutex_unlock(&pool->mutex);
    return pending;
}

void ember_threadpool_destroy(ember_threadpool *pool) {
    if (!pool) return;

    pthread_mutex_lock(&pool->mutex);
    pool->shutting_down = true;
    pthread_cond_broadcast(&pool->cond);
    pthread_mutex_unlock(&pool->mutex);

    for (size_t i = 0; i < pool->num_threads; i++) {
        pthread_join(pool->threads[i], NULL);
    }

    pthread_mutex_destroy(&pool->mutex);
    pthread_cond_destroy(&pool->cond);
    free(pool->threads);
    free(pool);
}
