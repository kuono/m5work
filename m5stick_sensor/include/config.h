#ifndef _CONFIG_STICK_H_
#define _CONFIG_STICK_H_

#include <stdio.h>
#include <stdint.h>

#define WANT_STDIO   0
#define WANT_FLOAT32 1
#define WANT_FLOAT64 0
#define WANT_MATH    0
#define WANT_MD5     0
#define WANT_TICK    0
#define WANT_ARGS    0

#define GCRED        0
#define INTTABLE     1
#define SANITY       1
#define MARK_PFQ     0
#define STACKOVL     1

#define HEAP_CELLS   4000
#define STACK_SIZE   500

#undef MAIN
#define INITIALIZATION

void mhs_error_die(const char *fmt, ...) __attribute__((noreturn));

#undef ERR
#undef ERR1
#define ERR(s)      mhs_error_die("%s", (s))
#define ERR1(s, a)  mhs_error_die((s), (a))

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

int   c_get_millis(void);
float c_get_accel_delta(void);
float c_get_threshold(void);
void  c_send_packet(int status, float delta);
void  c_delay_ms(int ms);

#ifdef __cplusplus
}
#endif

#endif /* _CONFIG_STICK_H_ */
