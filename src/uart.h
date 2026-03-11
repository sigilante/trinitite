#pragma once
#include <stdint.h>
void uart_init(void);
void uart_putc(char c);
char uart_getc(void);
void uart_puts(const char *s);
void uart_read_bytes(uint8_t *buf, uint64_t n);
void uart_write_bytes(const uint8_t *buf, uint64_t n);
