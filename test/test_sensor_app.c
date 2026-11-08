#include "unity.h"
#include "sensor.h"
#include "i2c_bus.h"
#include "sampling_timer.h"

#include <string.h>

static uint8_t registers[256];
static uint64_t clock_us;
static bool connected, fail_data;
static unsigned int probes, data_reads, writes, pulse_starts, pulse_ends;
static uint32_t timer_rate, sequence;
static bool tick_pending, pin_high;
static sampling_tick_t pending_tick;

void i2c_bus_init(void) {}
int i2c_bus_probe(uint8_t address)
{
    ++probes;
    return address == 0x20 || address == 0x76;
}
uint64_t time_us_64(void) { return clock_us; }
bool stdio_usb_connected(void) { return connected; }
bool sampling_timer_start(uint32_t rate_hz) { timer_rate = rate_hz; return true; }
bool sampling_timer_take(sampling_tick_t *tick)
{
    if (!tick_pending) return false;
    *tick = pending_tick;
    tick_pending = false;
    return true;
}
void sampling_pulse_begin(void) { TEST_ASSERT_FALSE(pin_high); pin_high = true; ++pulse_starts; }
void sampling_pulse_end(void) { TEST_ASSERT_TRUE(pin_high); pin_high = false; ++pulse_ends; }
static void post_tick(void)
{
    pending_tick.sequence = ++sequence;
    pending_tick.timestamp_us = clock_us;
    tick_pending = true;
}

static int read_reg(void *ctx, uint8_t address, uint8_t reg, uint8_t *data, size_t length)
{
    (void)ctx;
    if (address != 0x76 || length > 256u - reg) return -1;
    if (reg == 0xf7) {
        ++data_reads;
        if (fail_data) return -1;
    }
    memcpy(data, registers + reg, length);
    return 0;
}
static int write_reg(void *ctx, uint8_t address, uint8_t reg, uint8_t value)
{
    (void)ctx;
    if (address != 0x76) return -1;
    ++writes;
    registers[reg] = value;
    return 0;
}
bme280_bus_t i2c_bus_interface(void)
{
    return (bme280_bus_t){NULL, read_reg, write_reg};
}
static void set16(uint8_t address, int32_t value)
{
    uint16_t encoded = (uint16_t)value;
    registers[address] = (uint8_t)encoded;
    registers[address + 1] = (uint8_t)(encoded >> 8);
}
void setUp(void) {}
void tearDown(void) {}

static void boot_scan_reset_and_timed_sample(void)
{
    registers[0xd0] = 0x60;
    const int32_t calibration[] = {27504, 26435, -1000, 36477, -10685, 3024,
                                   2855, 140, -7, 15500, -14600, 6000};
    for (unsigned int i = 0; i < 12; ++i) set16((uint8_t)(0x88 + 2 * i), calibration[i]);
    registers[0xa1] = 75; set16(0xe1, 375);
    registers[0xe4] = 18; registers[0xe5] = 0x21; registers[0xe6] = 3; registers[0xe7] = 30;
    const uint8_t raw[] = {0x65, 0x5a, 0xc0, 0x7e, 0xed, 0x00, 0x75, 0x30};
    memcpy(registers + 0xf7, raw, sizeof(raw));
    sensor_start();
    TEST_ASSERT_EQUAL_UINT(112, probes);
    TEST_ASSERT_EQUAL_UINT(1, writes);
    TEST_ASSERT_EQUAL_UINT(0xb6, registers[0xe0]);
    sensor_report_boot();
    clock_us = 1999;
    sensor_poll();
    TEST_ASSERT_EQUAL_UINT(1, writes);
    clock_us = 2000;
    sensor_poll();
    TEST_ASSERT_EQUAL_UINT(4, writes);
    TEST_ASSERT_EQUAL_UINT(0x27, registers[0xf4]);
    TEST_ASSERT_EQUAL_UINT(PICOLOGGER_SAMPLE_RATE_HZ, timer_rate);
    clock_us = 22000;
    post_tick();
    sensor_poll();
    TEST_ASSERT_EQUAL_UINT(1, data_reads);
    TEST_ASSERT_EQUAL_UINT(1, pulse_starts);
    TEST_ASSERT_EQUAL_UINT(1, pulse_ends);
    connected = true;
    clock_us = 42000;
    post_tick();
    clock_us = 62000;
    post_tick();
    sensor_poll();
    TEST_ASSERT_EQUAL_UINT(2, data_reads);
    sensor_poll();
    TEST_ASSERT_EQUAL_UINT(2, data_reads);
    for (unsigned int i = 4; i <= 500; ++i) {
        clock_us = 2000 + i * UINT64_C(20000);
        post_tick();
        sensor_poll();
    }
    TEST_ASSERT_EQUAL_UINT(499, data_reads);
    TEST_ASSERT_EQUAL_UINT(data_reads, pulse_starts);
    TEST_ASSERT_EQUAL_UINT(data_reads, pulse_ends);
    TEST_ASSERT_FALSE(pin_high);
    fail_data = true;
    clock_us += 20000;
    post_tick();
    sensor_poll();
    TEST_ASSERT_EQUAL_UINT(500, data_reads);
    TEST_ASSERT_EQUAL_UINT(data_reads, pulse_starts);
    TEST_ASSERT_EQUAL_UINT(data_reads, pulse_ends);
    TEST_ASSERT_FALSE(pin_high);
    clock_us = 11002000;
    sensor_poll();
    clock_us = 20002000;
    sensor_poll();
    clock_us = 30002000;
    sensor_poll();
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(boot_scan_reset_and_timed_sample);
    return UNITY_END();
}
