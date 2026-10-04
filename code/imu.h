#ifndef __IMU_H__
#define __IMU_H__

#include "zf_common_headfile.h"

extern float z_angle;       //z轴积分角(度)，2ms积分一次
extern float gyro_z;        //z轴角速度(度/秒)

void imu_init(void);
void imu_update(void);

#endif