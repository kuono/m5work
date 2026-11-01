#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

/* MicroHs が要求するプラットフォーム初期化スタブ */
void main_setup(void) {
    /* ESP32 Arduino 側ですでに初期化済みのため no-op */
}

/* ランタイム致命的エラーハンドラ */
void mhs_error_die(const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    
    /* C言語標準の printf で UART0 へ出力 */
    printf("\n[MicroHs FATAL ERROR] %s\n", buf);
    fflush(stdout);
    
    while (1) {
        /* 停止 */
    }
}
