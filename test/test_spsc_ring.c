#include "unity.h"
#include "spsc_ring.h"

#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

typedef struct { uint32_t sequence, inverse; uint64_t pattern; uint8_t bytes[16]; } record_t;
static spsc_ring_t ring;
static record_t slots[1024];
static atomic_bool failed;
static uint32_t total;

void setUp(void)
{
    atomic_store(&failed, false);
    TEST_ASSERT_TRUE(spsc_ring_init(&ring, slots, sizeof(slots), 1024, sizeof(slots[0])));
}
void tearDown(void) {}

static void bounded_ring_handles_full_empty_and_wrap(void)
{
    record_t out, in = {0};
    TEST_ASSERT_TRUE(spsc_ring_empty(&ring));
    TEST_ASSERT_FALSE(spsc_ring_pop(&ring, &out));
    atomic_store(&ring.head, UINT32_MAX - 10u);
    atomic_store(&ring.tail, UINT32_MAX - 10u);
    for (uint32_t i = 0; i < 1024; ++i) {
        in.sequence = i;
        uint32_t level;
        TEST_ASSERT_TRUE(spsc_ring_push(&ring, &in, &level));
        TEST_ASSERT_EQUAL_UINT32(i + 1u, level);
    }
    TEST_ASSERT_EQUAL_UINT32(1024, spsc_ring_producer_level(&ring));
    TEST_ASSERT_FALSE(spsc_ring_push(&ring, &in, NULL));
    for (uint32_t i = 0; i < 1024; ++i) {
        TEST_ASSERT_TRUE(spsc_ring_pop(&ring, &out));
        TEST_ASSERT_EQUAL_UINT32(i, out.sequence);
    }
    TEST_ASSERT_TRUE(spsc_ring_empty(&ring));
    TEST_ASSERT_EQUAL_UINT32(0, spsc_ring_producer_level(&ring));
    TEST_ASSERT_TRUE(spsc_ring_init(&ring, slots, sizeof(slots), 1, sizeof(slots[0])));
    TEST_ASSERT_TRUE(spsc_ring_push(&ring, &in, NULL));
    TEST_ASSERT_FALSE(spsc_ring_push(&ring, &in, NULL));
    TEST_ASSERT_TRUE(spsc_ring_pop(&ring, &out));
    TEST_ASSERT_TRUE(spsc_ring_empty(&ring));
    TEST_ASSERT_FALSE(spsc_ring_init(NULL, slots, sizeof(slots), 1, sizeof(slots[0])));
    TEST_ASSERT_FALSE(spsc_ring_init(&ring, NULL, sizeof(slots), 1, sizeof(slots[0])));
    TEST_ASSERT_FALSE(spsc_ring_init(&ring, slots, sizeof(slots), 0, sizeof(slots[0])));
    TEST_ASSERT_FALSE(spsc_ring_init(&ring, slots, sizeof(slots), 3, sizeof(slots[0])));
    TEST_ASSERT_FALSE(spsc_ring_init(&ring, slots, 1, 1024, sizeof(slots[0])));
}

static record_t make_record(uint32_t sequence)
{
    record_t value = {.sequence = sequence, .inverse = ~sequence,
                      .pattern = (uint64_t)sequence * UINT64_C(0x100000001)};
    for (unsigned int i = 0; i < sizeof(value.bytes); ++i) value.bytes[i] = (uint8_t)(sequence + i * 17u);
    return value;
}

static void *produce(void *context)
{
    (void)context;
    for (uint32_t i = 0; i < total && !atomic_load(&failed); ++i) {
        record_t value = make_record(i);
        while (!spsc_ring_push(&ring, &value, NULL)) {
            if (atomic_load(&failed)) return NULL;
            sched_yield();
        }
    }
    return NULL;
}

static void *consume(void *context)
{
    (void)context;
    for (uint32_t i = 0; i < total; ++i) {
        record_t actual;
        while (!spsc_ring_pop(&ring, &actual)) {
            if (atomic_load(&failed)) return NULL;
            sched_yield();
        }
        record_t expected = make_record(i);
        if (actual.sequence != expected.sequence || actual.inverse != expected.inverse ||
            actual.pattern != expected.pattern || memcmp(actual.bytes, expected.bytes, sizeof(actual.bytes))) {
            atomic_store(&failed, true);
            return NULL;
        }
    }
    return NULL;
}

static void threads_exchange_ordered_records_without_corruption(void)
{
    pthread_t producer, consumer;
    TEST_ASSERT_EQUAL_INT(0, pthread_create(&producer, NULL, produce, NULL));
    TEST_ASSERT_EQUAL_INT(0, pthread_create(&consumer, NULL, consume, NULL));
    TEST_ASSERT_EQUAL_INT(0, pthread_join(producer, NULL));
    TEST_ASSERT_EQUAL_INT(0, pthread_join(consumer, NULL));
    TEST_ASSERT_FALSE(atomic_load(&failed));
    TEST_ASSERT_TRUE(spsc_ring_empty(&ring));
}

int main(int argc, char **argv)
{
    UNITY_BEGIN();
    if (argc == 1) RUN_TEST(bounded_ring_handles_full_empty_and_wrap);
    else if (argc == 3 && !strcmp(argv[1], "stress")) {
        char *end;
        unsigned long n = strtoul(argv[2], &end, 10);
        if (*end || !n || n > 100000000) return 1;
        total = (uint32_t)n;
        RUN_TEST(threads_exchange_ordered_records_without_corruption);
    } else return 1;
    return UNITY_END();
}
