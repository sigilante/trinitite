#include "uart.h"

void main(void) {
    uart_init();
    uart_puts("Hello, Fock\r\n");

    // Hang — nothing else exists yet
    while (1) {
        char c = uart_getc();
        uart_putc(c);  // echo everything back
    }
}
