#pragma once
#include "noun.h"

/*
 * Phase 6 — Kernel Loop helpers.
 *
 * uart_recv_noun: read a length-framed cue-decoded noun from UART.
 *   Wire format: [8-byte LE length][raw jam bytes]
 *
 * uart_send_noun: jam a noun and write it length-framed to UART.
 *
 * dispatch_effects: walk a Nock effect list [[tag data] rest] and
 *   dispatch known tags to UART.  Unknown tags are silently ignored.
 *   Phase 6 tags:
 *     %out  (7632239)   — uart output of data atom as raw bytes
 *     %blit (1953066082) — same
 *
 * arvo_loop / shrine_loop: enter the respective kernel event loop.
 *   Both never return.  On nock crash: print error, continue loop.
 */

noun uart_recv_noun(void);
void uart_send_noun(noun n);
void dispatch_effects(noun effects);
void arvo_loop(noun kernel);     /* never returns */
void shrine_loop(noun kernel);   /* never returns */
