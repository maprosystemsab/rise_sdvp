#pragma once
#include <stdint.h>
typedef struct { int id; uint32_t rx_time; float rpm, current, duty, v_in; } can_status_msg;
enum { CAN_PACKET_SET_CURRENT=1, CAN_PACKET_SET_CURRENT_BRAKE=2 };
#define CMD_GENERATOR 152
