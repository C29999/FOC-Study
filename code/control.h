#ifndef CONTROL_H
#define CONTROL_H

#include "zf_common_headfile.h"

void control_init(void);
void control_update_5ms(void);
void control_update_2ms(float steering_error);
void control_update_10ms(void);

#endif
