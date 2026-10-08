// process.h - MSVC _beginthreadex / _endthreadex (over CreateThread, win_sync.cpp).
#pragma once
#include "bo1_web_prelude.h"
BO1_EXTERN_C_BEGIN
uintptr_t _beginthreadex(void *security, unsigned stackSize, unsigned (*start)(void *), void *arg, unsigned flags, unsigned *threadId);
void _endthreadex(unsigned code);
uintptr_t _beginthread(void (*start)(void *), unsigned stackSize, void *arg);
BO1_EXTERN_C_END
