#ifndef __MOTOR_H__
#define __MOTOR_H__

#include "zf_common_headfile.h"
#define MOTOR_L_PWM   ATOM2_CH2_P33_6    // 左电机PWM
#define MOTOR_L_DIR   P33_7              // 左电机方向脚
#define MOTOR_R_PWM   ATOM0_CH4_P02_4    // 右电机PWM
#define MOTOR_R_DIR   P02_5              // 右电机方向脚

#define MOTOR_FREQ      (17000)        // PWM频率17kHz
#define MOTOR_DUTY_MAX  (10000)        // 占空比上限
#define L_DEAD_ZONE     (300)          // 左电机死区补偿
#define R_DEAD_ZONE     (300)          // 右电机死区补偿

extern int16 encoder_left;
extern int16 encoder_right;
void motor_init(void);
void encoder_init(void);
void encoder_update(void);
void go_motor(int16 Left_targht_speed, int16 Right_targht_speed);
void speed_control(void);
int16 differential_add_speed2(int16 aim, float turn);
#endif