#ifndef _CONFIG_CORE2_H_
#define _CONFIG_CORE2_H_

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

#ifdef __cplusplus
extern "C" {
#endif

int   c_get_millis(void);
int   c_poll_has_packet(void);
int   c_get_packet_status(void);
float c_get_packet_delta(void);
int   c_get_packet_battery(void);
int   c_get_core2_battery(void);
int   c_poll_btn_b_pressed(void);
// 旧: void c_update_display(int status, int muteMode, float delta, int stickBat, int core2Bat);
// 新:
void c_render_template(int status, int muteMode, float delta, int stickBat, int core2Bat);void  c_set_buzzer(int buzzerCmd);
void  c_delay_ms(int ms);

#ifdef __cplusplus
}
#endif

#endif /* _CONFIG_CORE2_H_ */
