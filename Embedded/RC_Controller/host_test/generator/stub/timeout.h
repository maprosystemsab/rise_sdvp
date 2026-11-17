#pragma once
#include <stdbool.h>
extern bool sim_timeout;
static inline bool timeout_has_timeout(void){return sim_timeout;}
