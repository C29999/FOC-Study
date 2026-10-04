#include "imu.h"

float z_angle = 0;
float gyro_z = 0;

#define GYRO_CALIB_COUNT   100
static float  gyro_z_offset    = 0.f;
static float  gyro_z_accum     = 0.f;
static int16  gyro_calib_cnt   = 0;
static uint8  gyro_calib_done  = 0;

#define GYRO_LPF_ALPHA   0.3f
static float  gyro_z_filtered  = 0.f;

void imu_init(void)
{
    (void)imu660rb_init();
}

void imu_update(void)
{
    imu660rb_get_gyro();
    float raw = imu660rb_gyro_transition(imu660rb_gyro_z);

    if (!gyro_calib_done)
    {
        gyro_z_accum += raw;
        gyro_calib_cnt++;
        if (gyro_calib_cnt >= GYRO_CALIB_COUNT)
        {
            gyro_z_offset = gyro_z_accum / GYRO_CALIB_COUNT;
            gyro_calib_done = 1;
        }
        gyro_z = 0.f;       
        return;
    }

    float calibrated = raw - gyro_z_offset;
    gyro_z_filtered = calibrated * GYRO_LPF_ALPHA + gyro_z_filtered * (1.f - GYRO_LPF_ALPHA);
    gyro_z = gyro_z_filtered;

    z_angle += gyro_z * 0.005f;
}
