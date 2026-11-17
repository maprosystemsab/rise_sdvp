#pragma once
#include <stdint.h>
#include <stdbool.h>
typedef uint32_t systime_t;
extern systime_t sim_now;
#define ST2MS(x) (x)
#define MS2ST(x) (x)
#define THD_WORKING_AREA(n,s) int n[1]
#define THD_FUNCTION(n,a) void n(void *a)
#define NORMALPRIO 0
static inline systime_t chVTGetSystemTimeX(void){return sim_now;}
static inline systime_t chVTGetSystemTime(void){return sim_now;}
static inline systime_t chVTTimeElapsedSinceX(systime_t t){return sim_now-t;}
void chThdSleepMilliseconds(int ms);
static inline void chRegSetThreadName(const char*n){(void)n;}
static inline void *chThdCreateStatic(void*w,int s,int p,void(*f)(void*),void*a){(void)w;(void)s;(void)p;(void)f;(void)a;return 0;}
