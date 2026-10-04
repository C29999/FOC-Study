#include "pid.h"
#include "imu.h"

/**
 * 方向环：线性P + 平方P + 滤波D + 陀螺阻尼
 * 转角 = KP*error + KP2*error*|error| + KD*低通(diff) - gyro_z*GKD
 * 线性P：小误差时温和纠偏
 * 平方P：大误差时自动加力（过弯），且保留符号不丢方向
 * D：一阶低通微分，抑制超调
 * 陀螺：车身角速度反馈，转向过快时反向拉
 */
float quadradic_pid_solve(pid_param_t *pid, float error)
{
    pid->out_p = pid->kp * error + pid->kp2 * error * fabsf(error);

    float diff = error - pid->pre_error;
    pid->out_d = diff * pid->low_pass + pid->out_d * (1.f - pid->low_pass);

    pid->pre_pre_error = pid->pre_error;
    pid->pre_error = error;

    return MINMAX(pid->out_p, -pid->p_max, pid->p_max)
         + MINMAX(pid->kd * pid->out_d, -pid->d_max, pid->d_max)
         - gyro_z * pid->kgyro;
}

/**
 * 速度环：增量式 PID
 * delta = kp*(e-e1) + ki*e + kd*(e-2e1+e2)，输出累加到占空比
 * pre_output 保护：上次输出已顶到±10000还同向 → 强制归零，防电机堵转烧管
 * 输入输出单位：编码器误差 → PWM占空比
 */
float increment_pid_solve(pid_param_t *pid, float error)
{
    pid->out_d = MINMAX(pid->kd * (error - 2 * pid->pre_error + pid->pre_pre_error), -pid->d_max, pid->d_max);
    pid->out_p = MINMAX(pid->kp * (error - pid->pre_error), -pid->p_max, pid->p_max);
    pid->out_i = MINMAX(pid->ki * error, -pid->i_max, pid->i_max);

    pid->pre_pre_error = pid->pre_error;
    pid->pre_error = error;

    pid->output = pid->out_p + pid->out_i + pid->out_d;

    if (pid->pre_output > 10000)
        if (pid->output > 0)
            pid->output = 0.;
    if (pid->pre_output < -10000)
        if (pid->output < 0)
            pid->output = 0.;

    pid->pre_output = pid->output;
    return pid->output;
}

/**
 * 备用：变积分增量式。误差大时 ki 按 S 曲线衰减（积分快退），防大偏差积饱和
 * 
 */
float changable_pid_solve(pid_param_t *pid, float error)
{
    pid->out_d = MINMAX(pid->kd * (error - 2 * pid->pre_error + pid->pre_pre_error), -pid->d_max, pid->d_max);
    pid->out_p = MINMAX(pid->kp * (error - pid->pre_error), -pid->p_max, pid->p_max);

    float ki_index = pid->ki;
    if (error + pid->pre_error > 0)
        ki_index = MAX((pid->ki) - (pid->ki) / (1.f + expf(100.f - 0.2f * fabsf(error))), 0.);

    pid->out_i = MINMAX(ki_index * error, -pid->i_max, pid->i_max);

    pid->pre_pre_error = pid->pre_error;
    pid->pre_error = error;

    pid->output = pid->out_p + pid->out_i + pid->out_d;

    if (pid->pre_output > 10000)
        if (pid->output > 0)
            pid->output = 0.;
    if (pid->pre_output < -10000)
        if (pid->output < 0)
            pid->output = 0.;

    pid->pre_output = pid->output;
    return pid->output;
}

/**
 * 备用：BangBang。误差>8°直接全速打（15000），否则退化成普通增量式
 * 配合直道冲刺变速用，先抄了备用
 */
float bangbang_pid_solve(pid_param_t *pid, float error)
{
    float out;
    pid->error = error;

    if (error > 8 || error < -8)
    {
        out = (error > 0) ? 15000.f : -15000.f;
    }
    else
    {
        pid->out_d = pid->kd * (error - 2 * pid->pre_error + pid->pre_pre_error);
        pid->out_p = pid->kp * (error - pid->pre_error);
        pid->out_i = pid->ki * error;
        out = MINMAX(pid->out_p, -pid->p_max, pid->p_max)
            + MINMAX(pid->out_i, -pid->i_max, pid->i_max)
            + MINMAX(pid->out_d, -pid->d_max, pid->d_max);
    }
    pid->pre_pre_error = pid->pre_error;
    pid->pre_error = error;
    return out;
}
