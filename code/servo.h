#ifndef __SERVO_H__
#define __SERVO_H__
#include "zf_common_headfile.h"

#define SMOTOR_PWM_PIN (ATOM1_CH1_P33_9)
#define SERVO_FREQ     (333) //舵机中值
#define SMOTOR_CENTER  (6400)    //舵机中值(实测占空比, 脉宽≈1922us)
#define SMOTOR_RATE    (2.4)    //舵机角度的减速比
#define SMOTOR_LIMIT   (14.5f)   //舵机角度限幅
#define SMOTOR_DUTY(x) ((float)PWM_DUTY_MAX / (1000.0f / (float)SERVO_FREQ) * ((float)(x) / 90.0f) + SMOTOR_CENTER)

void servo_init(void);//舵机初始化
void servo_set(float angle_deg);
#endif
