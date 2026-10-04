#include "control.h"
#include "data.h"
#include "imu.h"
#include "motor.h"
#include "servo.h"

void control_init(void)
{
    data_init();
    imu_init();
    encoder_init();
    servo_init();
    motor_init();
}

void control_update_5ms(void)
{
    imu_update();
}

void control_update_2ms(float steering_error)
{
    angle = quadradic_pid_solve(&servo_pid, steering_error);
    servo_set(angle);
}

void control_update_10ms(void)
{
    encoder_update();
    speed_control();
}
