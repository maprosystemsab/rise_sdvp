#pragma once
#include "datatypes.h"
can_status_msg *comm_can_get_status_msg_id(int id);
void comm_can_transmit_eid(uint32_t id, uint8_t *data, uint8_t len);
void comm_can_transmit_sid(uint32_t id, uint8_t *data, uint8_t len);
