#ifndef TEST_FAULT_STDLIB_H
#define TEST_FAULT_STDLIB_H
#define __not_in_flash_func(name) __attribute__((section(".time_critical." #name))) name
#endif
