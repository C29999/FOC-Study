#include "motor.h"
#include "servo.h"
#include "data.h"
#include "pid.h"

int16 encoder_left = 0;
int16 encoder_right = 0;

void motor_init(void)
{
    // 左电机PWM初始化
    pwm_init(MOTOR_L_PWM, MOTOR_FREQ, 0);
    gpio_init(MOTOR_L_DIR, GPO, GPIO_LOW, GPO_PUSH_PULL);

    // 右电机PWM初始化
    pwm_init(MOTOR_R_PWM, MOTOR_FREQ, 0);
    gpio_init(MOTOR_R_DIR, GPO, GPIO_LOW, GPO_PUSH_PULL);
}

void encoder_init(void)
{
    encoder_dir_init(TIM6_ENCODER, TIM6_ENCODER_CH1_P20_3, TIM6_ENCODER_CH2_P20_0);
    encoder_dir_init(TIM5_ENCODER, TIM5_ENCODER_CH1_P10_3, TIM5_ENCODER_CH2_P10_1);
}

void encoder_update(void)
{
    encoder_left = encoder_get_count(TIM6_ENCODER);
    encoder_right = encoder_get_count(TIM5_ENCODER);
    encoder_clear_count(TIM6_ENCODER);
    encoder_clear_count(TIM5_ENCODER);
}
void servo_init(void)
{
    pwm_init(SMOTOR_PWM_PIN, SERVO_FREQ, SMOTOR_CENTER);
}

static float last_servo_angle = 9999.0f;

void servo_set(float angle_deg)
{
    angle_deg = MINMAX(angle_deg, -SMOTOR_LIMIT, SMOTOR_LIMIT);
    if (fabsf(angle_deg - last_servo_angle) < 0.01f)
        return;
    last_servo_angle = angle_deg;
    pwm_set_duty(SMOTOR_PWM_PIN, (uint32)SMOTOR_DUTY(angle_deg));
}

void go_motor(int16 l, int16 r)
{
    if (l > 0)
    {
        gpio_set_level(MOTOR_L_DIR, 0);
        pwm_set_duty(MOTOR_L_PWM, l + L_DEAD_ZONE);
    }
    else if (l < 0)
    {
        gpio_set_level(MOTOR_L_DIR, 1);
        pwm_set_duty(MOTOR_L_PWM, -l + L_DEAD_ZONE);
    }
    else
    {
        gpio_set_level(MOTOR_L_DIR, 0);
        pwm_set_duty(MOTOR_L_PWM, 0);
    }
    if (r > 0)
    {
        gpio_set_level(MOTOR_R_DIR, 0);
        pwm_set_duty(MOTOR_R_PWM, r + R_DEAD_ZONE);
    }
    else if (r < 0)
    {
        gpio_set_level(MOTOR_R_DIR, 1);
        pwm_set_duty(MOTOR_R_PWM, -r + R_DEAD_ZONE);
    }
    else
    {
        gpio_set_level(MOTOR_R_DIR, 0);
        pwm_set_duty(MOTOR_R_PWM, 0);
    }
}

#define LDISTANCE   (200)
#define BORDWIDTH   (150)
// 线性差速：turn 是舵机角度(度)，返回内轮需要减掉的速度量
// 每度减 aim*0.06，angle=10° → 减 60%，angle=14.5° → 减 87%
int16 differential_add_speed2(int16 aim, float turn)
{
    float ratio = fabsf(turn) / SMOTOR_LIMIT;   // 0~1
    if (ratio > 1.0f) ratio = 1.0f;
    return (int16)(aim * ratio * turn_diff);      // 内轮减速比例由 data.c 的 turn_diff 控制（原 0.8 常量）
}
void speed_control(void)
{
    int16 dynamic_speed;
    float abs_angle = fabsf(pure_angle);

    if (abs_angle <= 1.0f)
    {
        dynamic_speed = straight_speed;
    }
    else
    {
        // corner_speed 是入弯基础速度，转角继续增大时按斜率进一步降速。
        float factor = 1.0f - abs_angle * corner_speed_slope;
        if (factor < 0.4f) factor = 0.4f;            // 最低不低于 40%
        dynamic_speed = (int16)(corner_speed * factor);
    }
    int16 l_goal = dynamic_speed;
    int16 r_goal = dynamic_speed;
    static int16 l_pwm = 0;
    static int16 r_pwm = 0;
    if (stop_flog)
    {
        base_speed = 0;
        l_goal = 0;
        r_goal = 0;
        if (encoder_left > -10 && encoder_left < 10 &&
            encoder_right > -10 && encoder_right < 10)
        {
            l_pwm = 0;
            r_pwm = 0;
            motor_pid_l.pre_error = 0;
            motor_pid_l.pre_pre_error = 0;
            motor_pid_r.pre_error = 0;
            motor_pid_r.pre_pre_error = 0;
            go_motor(0, 0);
            return;
        }
    }
    else
    {
        // 差速（内减外加结构）：内轮减 diff，外轮加 diff_outer，过弯更凌厉
        int16 diff = differential_add_speed2(dynamic_speed, MINMAX(angle, -12.0f, 12.0f));
        int16 diff_outer = (int16)(diff * turn_diff_outer);
        if (angle > 0)      // 左转：左轮为内轮（减速），右轮为外轮（加速）
        {
            l_goal -= diff;
            r_goal += diff_outer;
        }
        else                // 右转：右轮为内轮（减速），左轮为外轮（加速）
        {
            r_goal -= diff;
            l_goal += diff_outer;
        }
    }
    // 电机目标速度限幅：不允许反向（前进方向为负值，差速后内轮最多减速到 0，不反转）
    if (l_goal > 0) l_goal = 0;
    if (r_goal > 0) r_goal = 0;
    int16 l_delta = (int16)increment_pid_solve(&motor_pid_l, l_goal - encoder_left);
    int16 r_delta = (int16)increment_pid_solve(&motor_pid_r, r_goal - encoder_right);
    l_pwm += l_delta;
    r_pwm += r_delta;
    if (l_pwm > 9000) l_pwm = 9000;
    if (l_pwm < -9000) l_pwm = -9000;
    if (r_pwm > 9000) r_pwm = 9000;
    if (r_pwm < -9000) r_pwm = -9000;
    go_motor(l_pwm, r_pwm);
}
