#include "ember_object.h"

#include <stdlib.h>
#include <time.h>

ember_object *ember_object_create(const void *data, size_t len) {
    ember_object *obj = malloc(sizeof(ember_object));
    if (!obj) return NULL;

    obj->value = sds_new_len(data, len);
    if (!obj->value) {
        free(obj);
        return NULL;
    }
    obj->expire_at_ms = 0;
    return obj;
}

void ember_object_free(void *obj_ptr) {
    if (!obj_ptr) return;
    ember_object *obj = obj_ptr;
    sds_free(obj->value);
    free(obj);
}

bool ember_object_is_expired(const ember_object *obj, int64_t now_ms) {
    return obj->expire_at_ms != 0 && obj->expire_at_ms <= now_ms;
}

int64_t ember_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
