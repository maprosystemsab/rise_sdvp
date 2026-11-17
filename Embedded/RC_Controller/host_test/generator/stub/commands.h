#pragma once
#include <stdio.h>
#define commands_printf(...) do{printf("    [%5.2fs] ",sim_now/1000.0);printf(__VA_ARGS__);printf("\n");}while(0)
static inline void commands_send_packet(unsigned char*d,unsigned int l){(void)d;(void)l;}
