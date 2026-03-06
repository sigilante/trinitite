#pragma once
#include "noun.h"

/*
 * Nock 4K evaluator — Phase 3.
 *
 * nock(subject, formula) → product   (crashes on ill-formed input)
 * slot(axis, subject)    → noun      (Nock / operator)
 *
 * Crash behaviour: nock_crash() prints to UART and halts the CPU.
 * A longjmp-based recovery path (back to QUIT) is deferred to Phase 3b.
 */

noun nock(noun subject, noun formula);
noun slot(noun axis, noun subject);
