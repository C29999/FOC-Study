#ifndef _PID_H_
#define _PID_H_
#include "zf_common_headfile.h"

// 限幅宏
#define MINMAX(x, min, max)  ((x) < (min) ? (min) : ((x) > (max) ? (max) : (x)))
#define MAX(a, b)  ((a) > (b) ? (a) : (b))
// 角度弧度互转（带 PID_ 前缀，避免和 image.h 的 IMG_ANGLE_TO_RAD 冲突）
#define PID_ANGLE_TO_RAD(x)  ((x) * PI / 180.0f)
#define PID_RAD_TO_ANGLE(x)  ((x) * 180.0f / PI)

typedef struct
{
    float kp;               // 线性 P 系数
    float kp2;              // 平方 P 系数（error*|error| 项）
    float ki;               // I 系数（速度环用）
    float kd;               // D 系数
    float i_max;            // 积分限幅
    float p_max;            // P 项限幅
    float d_max;            // D 项限幅
    float low_pass;         // 微分低通系数 0~1，越大滤波越弱
    float kgyro;            // 陀螺抑制系数：方向环专用，其他环填0
    float out_p;            // P 项输出
    float out_i;            // I 项输出
    float out_d;            // D 项输出（滤波后）
    float error;            // 当前误差
    float pre_error;        // 上次误差
    float pre_pre_error;    // 上上次误差
    float output;           // 本次输出
    float pre_output;       // 上次输出（抗反接保护用）
} pid_param_t;

// 初始化宏：PID_CREATE(kp, kp2, ki, kd, low_pass, p_max, i_max, d_max, kgyro)
#define PID_CREATE(kp_, kp2_, ki_, kd_, lp_, pmax_, imax_, dmax_, kgyro_)  \
    { .kp=(kp_), .kp2=(kp2_), .ki=(ki_), .kd=(kd_), .low_pass=(lp_),       \
      .p_max=(pmax_), .i_max=(imax_), .d_max=(dmax_), .kgyro=(kgyro_) }

extern pid_param_t servo_pid;
extern pid_param_t motor_pid_l;
extern pid_param_t motor_pid_r;
extern pid_param_t motor_pid_l_bangbang;
extern pid_param_t motor_pid_r_bangbang;

float quadradic_pid_solve(pid_param_t *pid, float error);
float increment_pid_solve(pid_param_t *pid, float error);
float changable_pid_solve(pid_param_t *pid, float error);
float bangbang_pid_solve(pid_param_t *pid, float error);

#endif
