#include "data.h"
uint8 wifi_ok_flag;
uint8 wifi_flag = 1;    //鍥惧儚鍙戦�佸紑鍏筹細1=鍙戝浘鍒颁笂浣嶆満 0=涓嶅彂
uint8 wifi_init_flag;
volatile uint8 wifi_stage;
volatile uint8 wifi_result;
uint8 fps;
uint8 fps_count;

int16 image_center=94;
int16 mid;
int16 mid_y = 0;           // 中线前瞻点 y 像素坐标（调试十字用） //璧涢亾涓偣鐨勪綅缃�
int16 image_error=0;
int16 image_error_filter=0;



uint8 stop_flog=1;       // 上电保持停车，双击发车后才清零
int16 base_speed=0;       //鍩虹閫熷害鐩爣(缂栫爜鍣ㄨ鏁�/10ms)
int16 dif_val=0;        //鏂瑰悜涓幆杈撳嚭鐨勫樊閫熼噺

//                kp    kp2    ki   kd   low_pass  p_max  i_max  d_max  kgyro
pid_param_t servo_pid = PID_CREATE(1.2 , 0.03,  0,  .0,  0.3, 14.5,   0,    8.0,  -0.01);
pid_param_t motor_pid_l = PID_CREATE(20.0, 0, 0.3, 0, 0, 3000, 2000, 0, 0.);
pid_param_t motor_pid_r = PID_CREATE(20.0, 0, 0.3, 0, 0, 3000, 2000, 0, 0.);
pid_param_t motor_pid_l_bangbang = PID_CREATE(60.0, 0, 3.0, 0, 0, 10000, 5000, 0, 0.);
pid_param_t motor_pid_r_bangbang = PID_CREATE(60.0, 0, 3.0, 0, 0, 10000, 5000, 0, 0.);

int16 straight_speed      = -330;
int16 long_straight_speed = -330;
int16 corner_speed        = -100;   // 弯道基础速度（-90→-100，弯道整体加快一点）
float corner_speed_slope  = 0.01f;   // 弯道减速斜率（越大减速越快，0.04≈温和 0.06≈激进）

float pure_angle = 0;        // 纯转角度（弧度），0=直道
float pure_rad   = 0;        // 纯转角度（弧度），0=直道
float aim_distance = 0.15f;  // 目标前瞻距离（米）
float angle = 0;             // 目标角度（弧度），0=直道
float turn_diff = 0.5f;       // 内轮最大减速比例50%
float turn_diff_outer = 0.0f; // 外轮不额外加速
float mx_rate_limit = 0.02f;   // 中线单帧最大变化（米/帧≈1.4px）
uint8 launch_direction = 0;    // 0=左线为主，1=右线为主；发车前KEY3切换
uint16 state_flags = 0;         // bit0=L bit1=R bit2=双边中线 bit4=左补线 bit5=右补线 bit6=全丢
void data_init(void)
{
}
void data_debug(void)
{

}
