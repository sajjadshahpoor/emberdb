#include "ember_eventloop.h"
#include "ember_common.h"
#include "test_common.h"

#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static char g_buf[64];
static ssize_t g_received;

static void on_readable(ember_eventloop *loop, int fd, void *user_data) {
    EMBER_UNUSED(user_data);
    g_received = read(fd, g_buf, sizeof(g_buf));
    ember_eventloop_stop(loop);
}

static void test_fd_readable_triggers_callback(void) {
    int fds[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

    ember_eventloop *loop = ember_eventloop_create();
    ASSERT_NOT_NULL(loop);
    ASSERT_EQ(ember_eventloop_add_fd(loop, fds[0], on_readable, NULL, NULL), EMBER_OK);

    ssize_t written = write(fds[1], "hello", 5);
    ASSERT_EQ(written, 5);

    ember_eventloop_run(loop); /* on_readable stops the loop */

    ASSERT_EQ(g_received, 5);
    ASSERT_EQ(memcmp(g_buf, "hello", 5), 0);

    ember_eventloop_destroy(loop);
    close(fds[0]);
    close(fds[1]);
}

static int g_ticks;

static void on_tick(ember_eventloop *loop, void *user_data) {
    EMBER_UNUSED(user_data);
    g_ticks++;
    if (g_ticks >= 3) ember_eventloop_stop(loop);
}

static void test_timer_fires_repeatedly(void) {
    g_ticks = 0;
    ember_eventloop *loop = ember_eventloop_create();

    int handle = ember_eventloop_add_timer(loop, 5, on_tick, NULL);
    ASSERT_TRUE(handle >= 0);

    ember_eventloop_run(loop);
    ASSERT_TRUE(g_ticks >= 3);

    ember_eventloop_remove_timer(loop, handle);
    ember_eventloop_destroy(loop);
}

static void test_add_fd_rejects_negative_fd(void) {
    ember_eventloop *loop = ember_eventloop_create();
    ASSERT_EQ(ember_eventloop_add_fd(loop, -1, on_readable, NULL, NULL), EMBER_ERR_INVALID);
    ember_eventloop_destroy(loop);
}

static int g_remove_call_count;

static void on_readable_then_remove(ember_eventloop *loop, int fd, void *user_data) {
    EMBER_UNUSED(user_data);
    char buf[16];
    read(fd, buf, sizeof(buf));
    g_remove_call_count++;
    ASSERT_EQ(ember_eventloop_remove_fd(loop, fd), EMBER_OK);
    ember_eventloop_stop(loop);
}

static void test_remove_fd_during_callback_is_safe(void) {
    g_remove_call_count = 0;
    int fds[2];
    socketpair(AF_UNIX, SOCK_STREAM, 0, fds);

    ember_eventloop *loop = ember_eventloop_create();
    ember_eventloop_add_fd(loop, fds[0], on_readable_then_remove, NULL, NULL);
    write(fds[1], "x", 1);

    ember_eventloop_run(loop);
    ASSERT_EQ(g_remove_call_count, 1);

    ember_eventloop_destroy(loop);
    close(fds[0]);
    close(fds[1]);
}

int main(void) {
    RUN_TEST(test_fd_readable_triggers_callback);
    RUN_TEST(test_timer_fires_repeatedly);
    RUN_TEST(test_add_fd_rejects_negative_fd);
    RUN_TEST(test_remove_fd_during_callback_is_safe);
    TEST_REPORT_AND_EXIT();
}
