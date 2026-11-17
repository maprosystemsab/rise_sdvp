#pragma once
#include <stdint.h>
static inline void buffer_append_int32(uint8_t*b,int32_t n,int32_t*i){b[(*i)++]=n>>24;b[(*i)++]=n>>16;b[(*i)++]=n>>8;b[(*i)++]=n;}
static inline int32_t buffer_get_int32(const uint8_t*b,int32_t*i){int32_t r=((uint32_t)b[*i]<<24)|((uint32_t)b[*i+1]<<16)|((uint32_t)b[*i+2]<<8)|b[*i+3];*i+=4;return r;}
static inline void buffer_append_int16(uint8_t*b,int16_t n,int32_t*i){b[(*i)++]=n>>8;b[(*i)++]=n;}
static inline void buffer_append_uint16(uint8_t*b,uint16_t n,int32_t*i){b[(*i)++]=n>>8;b[(*i)++]=n;}
