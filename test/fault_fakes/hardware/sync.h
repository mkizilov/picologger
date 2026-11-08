#ifndef TEST_FAULT_SYNC_H
#define TEST_FAULT_SYNC_H
#if defined(__arm__)
#define __dmb() __asm volatile("dmb" ::: "memory")
#else
#define __dmb() __asm volatile("" ::: "memory")
#endif
#endif
