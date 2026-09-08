#include "ember_threadpool.h"
#include "test_common.h"

#include <stdatomic.h>

static void increment_job(void *arg) {
    atomic_int *counter = arg;
    atomic_fetch_add(counter, 1);
}

static void test_destroy_drains_all_queued_jobs(void) {
    atomic_int counter;
    atomic_init(&counter, 0);

    ember_threadpool *pool = ember_threadpool_create(4);
    const int N = 2000;
    for (int i = 0; i < N; i++) {
        ASSERT_TRUE(ember_threadpool_submit(pool, increment_job, &counter));
    }

    /* destroy() must not return until every queued job has actually run,
     * even though workers are still racing to drain the queue. */
    ember_threadpool_destroy(pool);
    ASSERT_EQ(atomic_load(&counter), N);
}

static void set_flag_job(void *arg) {
    atomic_int *flag = arg;
    atomic_store(flag, 1);
}

static void test_each_job_gets_its_own_argument(void) {
    ember_threadpool *pool = ember_threadpool_create(3);

    atomic_int flags[64];
    for (int i = 0; i < 64; i++) atomic_init(&flags[i], 0);
    for (int i = 0; i < 64; i++) {
        ember_threadpool_submit(pool, set_flag_job, &flags[i]);
    }
    ember_threadpool_destroy(pool);

    for (int i = 0; i < 64; i++) {
        ASSERT_EQ(atomic_load(&flags[i]), 1);
    }
}

static void test_create_and_destroy_empty_pool(void) {
    ember_threadpool *pool = ember_threadpool_create(2);
    ASSERT_EQ(ember_threadpool_pending(pool), 0u);
    ember_threadpool_destroy(pool);
}

int main(void) {
    RUN_TEST(test_destroy_drains_all_queued_jobs);
    RUN_TEST(test_each_job_gets_its_own_argument);
    RUN_TEST(test_create_and_destroy_empty_pool);
    TEST_REPORT_AND_EXIT();
}
