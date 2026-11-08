#include "unity.h"
#include "logger_channel.h"
#include "pico/util/queue.h"

#include <string.h>

static unsigned int calls, fail_on, frees;
void queue_init(queue_t *q, unsigned int size, unsigned int count)
{
    TEST_ASSERT_EQUAL_UINT(sizeof(uint32_t), size);
    TEST_ASSERT_EQUAL_UINT(8, count);
    memset(q, 0, sizeof(*q));
    if (++calls != fail_on) q->data = (uint8_t *)q->values;
}
void queue_free(queue_t *q) { ++frees; q->data = NULL; }
bool queue_try_add(queue_t *q, const void *data)
{
    if (q->count == 8) return false;
    q->values[q->head++ % 8] = *(const uint32_t *)data;
    ++q->count;
    return true;
}
bool queue_try_remove(queue_t *q, void *data)
{
    if (!q->count) return false;
    *(uint32_t *)data = q->values[q->tail++ % 8];
    --q->count;
    return true;
}
void queue_add_blocking(queue_t *q, const void *data) { TEST_ASSERT_TRUE(queue_try_add(q, data)); }
void setUp(void) {}
void tearDown(void) {}

static void handles_allocation_failure_and_preserves_queue_order(void)
{
    fail_on = 1;
    TEST_ASSERT_FALSE(logger_channel_init());
    TEST_ASSERT_EQUAL_UINT(1, calls);
    calls = 0; fail_on = 2;
    TEST_ASSERT_FALSE(logger_channel_init());
    TEST_ASSERT_EQUAL_UINT(1, frees);
    calls = 0; fail_on = 0;
    TEST_ASSERT_TRUE(logger_channel_init());
    for (uint32_t i = 0; i < 8; ++i) TEST_ASSERT_TRUE(logger_channel_to_worker(i));
    TEST_ASSERT_FALSE(logger_channel_to_worker(9));
    for (uint32_t i = 0; i < 8; ++i) {
        uint32_t value;
        TEST_ASSERT_TRUE(logger_channel_from_main(&value));
        TEST_ASSERT_EQUAL_UINT32(i, value);
    }
    uint32_t value;
    TEST_ASSERT_FALSE(logger_channel_from_main(&value));
    logger_channel_to_main(42);
    TEST_ASSERT_TRUE(logger_channel_from_worker(&value));
    TEST_ASSERT_EQUAL_UINT32(42, value);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(handles_allocation_failure_and_preserves_queue_order);
    return UNITY_END();
}
