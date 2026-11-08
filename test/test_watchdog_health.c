#include "unity.h"
#include "watchdog_health.h"

static watchdog_health_t health;
static watchdog_observation_t observation;

void setUp(void)
{
    watchdog_health_init(&health);
    observation = (watchdog_observation_t){.bme_required = true, .adc_required = true,
                                           .writer_required = true, .ring_required = true, .rate_hz = 50};
}
void tearDown(void) {}

static void requires_fresh_progress_from_every_active_subsystem(void)
{
    TEST_ASSERT_EQUAL_UINT32(63, watchdog_health_observe(&health, 0, &observation));
    ++observation.bme_samples; ++observation.adc_samples; ++observation.worker_progress; ++observation.writer_progress;
    ++observation.ring_progress;
    ++observation.physical_progress; observation.physical_observed = true;
    TEST_ASSERT_EQUAL_UINT32(0, watchdog_health_observe(&health, 100000, &observation));
    ++observation.bme_samples; ++observation.adc_samples; ++observation.writer_progress;
    ++observation.ring_progress;
    TEST_ASSERT_EQUAL_UINT32(WATCHDOG_MISSING_WORKER, watchdog_health_observe(&health, 1700001, &observation));
    ++observation.worker_progress;
    TEST_ASSERT_EQUAL_UINT32(0, watchdog_health_observe(&health, 1700002, &observation));
    ++observation.adc_samples; ++observation.worker_progress; ++observation.writer_progress;
    ++observation.ring_progress;
    TEST_ASSERT_EQUAL_UINT32(WATCHDOG_MISSING_BME, watchdog_health_observe(&health, 3200002, &observation));
    ++observation.bme_samples; ++observation.adc_samples; ++observation.worker_progress;
    ++observation.ring_progress;
    TEST_ASSERT_EQUAL_UINT32(WATCHDOG_MISSING_WRITER, watchdog_health_observe(&health, 4700003, &observation));
    ++observation.bme_samples; ++observation.adc_samples; ++observation.worker_progress; ++observation.writer_progress;
    ++observation.ring_progress;
    TEST_ASSERT_EQUAL_UINT32(WATCHDOG_MISSING_PHYSICAL, watchdog_health_observe(&health, 5200000, &observation));
}

static void stopped_disabled_and_offline_states_are_explicit(void)
{
    observation.bme_required = observation.adc_required = observation.writer_required = observation.ring_required = false;
    TEST_ASSERT_EQUAL_UINT32(WATCHDOG_MISSING_WORKER, watchdog_health_observe(&health, 0, &observation));
    ++observation.worker_progress;
    TEST_ASSERT_EQUAL_UINT32(0, watchdog_health_observe(&health, 1, &observation));
    observation.bme_required = true;
    TEST_ASSERT_EQUAL_UINT32(WATCHDOG_MISSING_BME, watchdog_health_observe(&health, 2, &observation));
    ++observation.bme_samples;
    TEST_ASSERT_EQUAL_UINT32(0, watchdog_health_observe(&health, 3, &observation));
    observation.physical_observed = true;
    observation.physical_progress = 1;
    observation.writer_required = true;
    TEST_ASSERT_EQUAL_UINT32(WATCHDOG_MISSING_WRITER, watchdog_health_observe(&health, 4, &observation));
    ++observation.writer_progress;
    TEST_ASSERT_EQUAL_UINT32(0, watchdog_health_observe(&health, 5, &observation));
    observation.writer_required = false;
    TEST_ASSERT_EQUAL_UINT32(0, watchdog_health_observe(&health, 6, &observation));
}

static void slow_sampling_allows_a_full_chunk_interval(void)
{
    observation.rate_hz = 1;
    observation.physical_observed = true;
    observation.physical_progress = 1;
    (void)watchdog_health_observe(&health, 0, &observation);
    for (unsigned int second = 1; second <= 130; ++second) {
        ++observation.bme_samples; ++observation.adc_samples;
        ++observation.worker_progress; ++observation.writer_progress;
        ++observation.ring_progress;
        TEST_ASSERT_EQUAL_UINT32(0, watchdog_health_observe(&health, (uint64_t)second * 1000000, &observation));
    }
    ++observation.bme_samples; ++observation.adc_samples; ++observation.worker_progress; ++observation.writer_progress;
    ++observation.ring_progress;
    TEST_ASSERT_EQUAL_UINT32(WATCHDOG_MISSING_PHYSICAL, watchdog_health_observe(&health, UINT64_C(131000000), &observation));
}

static void stalled_adc_blocks_feeding_even_if_bme_and_storage_advance(void)
{
    observation.physical_observed = true;
    observation.physical_progress = 1;
    (void)watchdog_health_observe(&health, 0, &observation);
    ++observation.bme_samples; ++observation.adc_samples; ++observation.worker_progress; ++observation.writer_progress;
    ++observation.ring_progress;
    TEST_ASSERT_EQUAL_UINT32(0, watchdog_health_observe(&health, 1, &observation));
    ++observation.bme_samples; ++observation.worker_progress; ++observation.writer_progress; ++observation.physical_progress;
    ++observation.ring_progress;
    TEST_ASSERT_EQUAL_UINT32(WATCHDOG_MISSING_ADC, watchdog_health_observe(&health, 1500002, &observation));
    ++observation.adc_samples;
    TEST_ASSERT_EQUAL_UINT32(0, watchdog_health_observe(&health, 1500003, &observation));
}

static void old_physical_write_does_not_become_fresh_on_reconnect(void)
{
    observation.bme_required = observation.adc_required = observation.writer_required = observation.ring_required = false;
    observation.physical_observed = true;
    observation.physical_progress = 1;
    observation.physical_at_us = 0;
    (void)watchdog_health_observe(&health, 0, &observation);
    ++observation.worker_progress;
    observation.writer_required = true;
    TEST_ASSERT_EQUAL_UINT32(WATCHDOG_MISSING_WRITER | WATCHDOG_MISSING_PHYSICAL,
                             watchdog_health_observe(&health, UINT64_C(10000000), &observation));
    ++observation.worker_progress; ++observation.writer_progress;
    TEST_ASSERT_EQUAL_UINT32(WATCHDOG_MISSING_PHYSICAL,
                             watchdog_health_observe(&health, UINT64_C(10000001), &observation));
    ++observation.worker_progress; ++observation.physical_progress;
    TEST_ASSERT_EQUAL_UINT32(0, watchdog_health_observe(&health, UINT64_C(10000002), &observation));
}

static void wrapped_tokens_still_count_as_progress(void)
{
    observation.bme_required = observation.adc_required = observation.writer_required = observation.ring_required = false;
    observation.worker_progress = UINT32_MAX;
    (void)watchdog_health_observe(&health, 0, &observation);
    observation.worker_progress = 0;
    TEST_ASSERT_EQUAL_UINT32(0, watchdog_health_observe(&health, 1, &observation));
}

static void stalled_ring_consumer_is_detected_even_when_sd_is_offline(void)
{
    observation.adc_required = false;
    observation.writer_required = false;
    (void)watchdog_health_observe(&health, 0, &observation);
    ++observation.bme_samples; ++observation.worker_progress; ++observation.ring_progress;
    TEST_ASSERT_EQUAL_UINT32(0, watchdog_health_observe(&health, 1, &observation));
    ++observation.bme_samples; ++observation.worker_progress;
    TEST_ASSERT_EQUAL_UINT32(WATCHDOG_MISSING_RING, watchdog_health_observe(&health, 1500002, &observation));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(requires_fresh_progress_from_every_active_subsystem);
    RUN_TEST(stopped_disabled_and_offline_states_are_explicit);
    RUN_TEST(wrapped_tokens_still_count_as_progress);
    RUN_TEST(slow_sampling_allows_a_full_chunk_interval);
    RUN_TEST(stalled_adc_blocks_feeding_even_if_bme_and_storage_advance);
    RUN_TEST(old_physical_write_does_not_become_fresh_on_reconnect);
    RUN_TEST(stalled_ring_consumer_is_detected_even_when_sd_is_offline);
    return UNITY_END();
}
