#include <stdint.h>

#define PL011_BASE  0x3F201000
#define UART_DR     (*(volatile uint32_t*)(PL011_BASE + 0x00))
#define UART_FR     (*(volatile uint32_t*)(PL011_BASE + 0x18))
#define UART_IBRD   (*(volatile uint32_t*)(PL011_BASE + 0x24))
#define UART_FBRD   (*(volatile uint32_t*)(PL011_BASE + 0x28))
#define UART_LCRH   (*(volatile uint32_t*)(PL011_BASE + 0x2C))
#define UART_CR     (*(volatile uint32_t*)(PL011_BASE + 0x30))

void uart_init(void) {
    UART_CR   = 0;
    UART_IBRD = 26;
    UART_FBRD = 3;
    UART_LCRH = (3 << 5);
    UART_CR   = (1<<0)|(1<<8)|(1<<9);
}

void uart_putc(char c) {
    while (UART_FR & (1 << 5));
    UART_DR = c;
}

char uart_getc(void) {
    while (UART_FR & (1 << 4));
    return UART_DR & 0xFF;
}

void uart_puts(const char *s) {
    while (*s) {
        if (*s == '\n') uart_putc('\r');
        uart_putc(*s++);
    }
}
