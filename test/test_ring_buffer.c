#include "unity.h"
#include "ring_buffer.h"

#include <pthread.h>
#include <stdint.h>

typedef struct {
    uint32_t sequence;
    uint32_t inverse;
} record_t;

void setUp(void) {}
void tearDown(void) {}

static void rejects_invalid_configuration(void)
{
    ring_buffer_t ring;
    record_t storage[4];
    TEST_ASSERT_FALSE(ring_buffer_init(NULL, storage, sizeof(storage), 4, sizeof(record_t)));
    TEST_ASSERT_FALSE(ring_buffer_init(&ring, NULL, sizeof(storage), 4, sizeof(record_t)));
    TEST_ASSERT_FALSE(ring_buffer_init(&ring, storage, sizeof(storage), 0, sizeof(record_t)));
    TEST_ASSERT_FALSE(ring_buffer_init(&ring, storage, sizeof(storage), 3, sizeof(record_t)));
    TEST_ASSERT_FALSE(ring_buffer_init(&ring, storage, sizeof(storage), 4, 0));
    TEST_ASSERT_FALSE(ring_buffer_init(&ring, storage, sizeof(storage) - 1, 4, sizeof(record_t)));
    TEST_ASSERT_FALSE(ring_buffer_init(&ring, storage, SIZE_MAX, 4, SIZE_MAX));
}

static void empty_full_and_overflow(void)
{
    ring_buffer_t ring;
    record_t storage[4], out;
    TEST_ASSERT_TRUE(ring_buffer_init(&ring, storage, sizeof(storage), 4, sizeof(record_t)));
    TEST_ASSERT_FALSE(ring_buffer_pop(&ring, &out));
    for (uint32_t i = 0; i < 4; ++i) {
        record_t in = {i, ~i};
        TEST_ASSERT_TRUE(ring_buffer_push(&ring, &in));
    }
    record_t rejected = {99, 0};
    TEST_ASSERT_FALSE(ring_buffer_push(&ring, &rejected));
    TEST_ASSERT_FALSE(ring_buffer_push(&ring, &rejected));
    TEST_ASSERT_EQUAL_UINT32(2, ring_buffer_overflows(&ring));
    for (uint32_t i = 0; i < 4; ++i) {
        TEST_ASSERT_TRUE(ring_buffer_pop(&ring, &out));
        TEST_ASSERT_EQUAL_UINT32(i, out.sequence);
        TEST_ASSERT_EQUAL_UINT32(~i, out.inverse);
    }
    TEST_ASSERT_FALSE(ring_buffer_pop(&ring, &out));
    TEST_ASSERT_TRUE(ring_buffer_push(&ring, &rejected));
    TEST_ASSERT_TRUE(ring_buffer_pop(&ring, &out));
    TEST_ASSERT_EQUAL_UINT32(99, out.sequence);
}

static void wraparound_and_counter_wrap(void)
{
    ring_buffer_t ring;
    record_t storage[4], out = {0};
    TEST_ASSERT_TRUE(ring_buffer_init(&ring, storage, sizeof(storage), 4, sizeof(record_t)));
    atomic_store_explicit(&ring.head, UINT32_MAX - 2u, memory_order_relaxed);
    atomic_store_explicit(&ring.tail, UINT32_MAX - 2u, memory_order_relaxed);
    for (uint32_t i = 0; i < 4; ++i) {
        record_t in = {i, ~i};
        TEST_ASSERT_TRUE(ring_buffer_push(&ring, &in));
    }
    TEST_ASSERT_FALSE(ring_buffer_push(&ring, &out));
    for (uint32_t i = 0; i < 4; ++i) {
        TEST_ASSERT_TRUE(ring_buffer_pop(&ring, &out));
        TEST_ASSERT_EQUAL_UINT32(i, out.sequence);
        TEST_ASSERT_EQUAL_UINT32(~i, out.inverse);
    }
    for (uint32_t i = 0; i < 100000; ++i) {
        record_t in = {i, ~i};
        TEST_ASSERT_TRUE(ring_buffer_push(&ring, &in));
        TEST_ASSERT_TRUE(ring_buffer_pop(&ring, &out));
        TEST_ASSERT_EQUAL_UINT32(i, out.sequence);
        TEST_ASSERT_EQUAL_UINT32(~i, out.inverse);
    }
    for (unsigned int i = 0; i < 4; ++i) {
        record_t in = {i, ~i};
        TEST_ASSERT_TRUE(ring_buffer_push(&ring, &in));
    }
    atomic_store_explicit(&ring.overflows, UINT32_MAX, memory_order_relaxed);
    TEST_ASSERT_FALSE(ring_buffer_push(&ring, &out));
    TEST_ASSERT_EQUAL_UINT32(0, ring_buffer_overflows(&ring));
}

static void single_slot_capacity(void)
{
    ring_buffer_t ring;
    record_t storage[1], in = {17, ~UINT32_C(17)}, out;
    TEST_ASSERT_TRUE(ring_buffer_init(&ring, storage, sizeof(storage), 1, sizeof(in)));
    TEST_ASSERT_TRUE(ring_buffer_push(&ring, &in));
    TEST_ASSERT_FALSE(ring_buffer_push(&ring, &in));
    TEST_ASSERT_TRUE(ring_buffer_pop(&ring, &out));
    TEST_ASSERT_EQUAL_UINT32(in.sequence, out.sequence);
    TEST_ASSERT_EQUAL_UINT32(in.inverse, out.inverse);
    TEST_ASSERT_FALSE(ring_buffer_pop(&ring, &out));
}

static void single_context_ring_preserves_records_and_counts_overflow(void)
{
    ring_local_t ring;
    record_t storage[2], out, one = {1, ~UINT32_C(1)}, two = {2, ~UINT32_C(2)};
    TEST_ASSERT_FALSE(ring_local_init(&ring, storage, sizeof(storage), 3, sizeof(one)));
    TEST_ASSERT_TRUE(ring_local_init(&ring, storage, sizeof(storage), 2, sizeof(one)));
    TEST_ASSERT_FALSE(ring_local_pop(&ring, &out));
    ring.head = ring.tail = UINT32_MAX;
    TEST_ASSERT_TRUE(ring_local_push(&ring, &one));
    TEST_ASSERT_TRUE(ring_local_push(&ring, &two));
    TEST_ASSERT_FALSE(ring_local_push(&ring, &one));
    TEST_ASSERT_EQUAL_UINT32(1, ring.overflows);
    TEST_ASSERT_TRUE(ring_local_pop(&ring, &out));
    TEST_ASSERT_EQUAL_UINT32(1, out.sequence);
    TEST_ASSERT_TRUE(ring_local_pop(&ring, &out));
    TEST_ASSERT_EQUAL_UINT32(2, out.sequence);
    TEST_ASSERT_FALSE(ring_local_pop(&ring, &out));
}

enum { STRESS_RECORDS = 10000000, STRESS_CAPACITY = 1024 };
typedef struct {
    ring_buffer_t ring;
    record_t storage[STRESS_CAPACITY];
    atomic_bool cancelled;
    bool mismatch;
} stress_t;

static void *produce(void *arg)
{
    stress_t *s = arg;
    for (uint32_t i = 0; i < STRESS_RECORDS; ++i) {
        record_t record = {i, ~i};
        while (!ring_buffer_push(&s->ring, &record)) {
            if (atomic_load_explicit(&s->cancelled, memory_order_relaxed)) return NULL;
        }
    }
    return NULL;
}

static void *consume(void *arg)
{
    stress_t *s = arg;
    for (uint32_t i = 0; i < STRESS_RECORDS; ++i) {
        record_t record;
        while (!ring_buffer_pop(&s->ring, &record)) {
            if (atomic_load_explicit(&s->cancelled, memory_order_relaxed)) return NULL;
        }
        if (record.sequence != i || record.inverse != ~i) {
            s->mismatch = true;
            atomic_store_explicit(&s->cancelled, true, memory_order_relaxed);
            return NULL;
        }
    }
    return NULL;
}

static void pthread_stress_ten_million_records(void)
{
    stress_t s = {0};
    pthread_t producer, consumer;
    TEST_ASSERT_TRUE(ring_buffer_init(&s.ring, s.storage, sizeof(s.storage),
                                      STRESS_CAPACITY, sizeof(record_t)));
    atomic_init(&s.cancelled, false);
    TEST_ASSERT_EQUAL_INT(0, pthread_create(&consumer, NULL, consume, &s));
    int result = pthread_create(&producer, NULL, produce, &s);
    if (result != 0) {
        atomic_store_explicit(&s.cancelled, true, memory_order_relaxed);
        (void)pthread_join(consumer, NULL);
        TEST_FAIL_MESSAGE("Could not create producer thread");
    }
    TEST_ASSERT_EQUAL_INT(0, pthread_join(producer, NULL));
    TEST_ASSERT_EQUAL_INT(0, pthread_join(consumer, NULL));
    TEST_ASSERT_FALSE(s.mismatch);
    record_t record;
    TEST_ASSERT_FALSE(ring_buffer_pop(&s.ring, &record));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(rejects_invalid_configuration);
    RUN_TEST(empty_full_and_overflow);
    RUN_TEST(wraparound_and_counter_wrap);
    RUN_TEST(single_slot_capacity);
    RUN_TEST(single_context_ring_preserves_records_and_counts_overflow);
    RUN_TEST(pthread_stress_ten_million_records);
    return UNITY_END();
}
