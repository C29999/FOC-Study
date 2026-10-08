# TC264D + SimpleFOC Mini / DRV8313 开环电机调试

本次交付直接修改现有逐飞 TC264 开源库工程，沿用 AURIX Development Studio、TASKING、Infineon iLLD、双核启动和逐飞 GPIO/UART/PIT 驱动。STM32 命令词是功能参考；没有创建 STM32CubeIDE/HAL 工程。目标是拆桨的 1104 / 4300KV 电机空载起转验证。

控制实现为**电压开环三相正弦 PWM**。没有编码器、转子观测器、电流闭环、速度闭环或相电流采样；不会假定极对数为 7。`FREQ` 和 `STATUS` 中的频率都是电频率 Hz。开环给定频率不能证明转子已经跟随，更不能直接当作机械转速。

## 工程检查与变更

原工程已经有 `code/foc.c/.h`，但初始化曾将 EN 拉高、停止仅清零 PWM、固定极对数为 7，并且 nFAULT 没有停机处理。原来三次独立 `pwm_init/pwm_set_duty` 的实现不能保证一个共同计数器和一组三相同时更新。本次替换这一模块，移除预留但未实现的传感器/闭环代码。

| 文件 | 作用 |
| --- | --- |
| `code/foc.c`, `code/foc.h` | 正弦调制、复位/唤醒/定位/斜坡、停止、故障锁存和显式清故障 |
| `user/foc_config.h` | 所有启动参数、限幅和 TC264 引脚映射 |
| `user/foc_port.c`, `user/foc_port.h` | CCU61 同步中心对齐 3PWM、GPIO、ERU 故障和临界区 |
| `user/foc_console.c`, `user/foc_console.h` | 串口命令、有限长度解析、接收错误/溢出整行丢弃 |
| `user/cpu0_main.c` | 先将 EN 设为低，随后初始化；主循环处理串口，无自动启动 |
| `user/isr.c`, `user/isr_config.h` | 100 μs 控制定时中断、优先级 255 故障 ISR、串口 RX/错误 ISR |
| `.cproject`, `build.ps1` | 排除主机测试/构建目录、官方 ADS 完整构建和固件导出 |
| `tests/foc_host_test.c`, `tests/run_host_tests.py`, `foc_sim_check.py` | 编译执行实际控制/串口 C 代码，模拟硬件接口验证软件行为 |

CPU0 独占控制；CPU1 沿用原工程空闲循环。原有 `motor_init()`、摄像头、编码器、IMU/TFT/ToF/CCD 初始化不在本程序中调用。P00.1/.3/.5 原推荐给摄像头数据；P33.6/.7 原推荐给编码器；P33.6/.7 还被旧 motor PWM/方向使用；P15.4 被 IMU660RC/TFT 使用；P33.5 被 ToF I2C 使用。**这些功能和对应外设连接不能同时占用本次引脚。**项目列出的 boot 引脚没有用于本次驱动。

## 接线

已核对用户提供的 `C:/Users/10503/Downloads/Schematic_simplefocmini.pdf`（Mini REV 1.0，2022-02-26）。板上三相 EN 共用，nRESET/nSLEEP/nFAULT 各有 10kΩ 上拉到 DRV 内部 3.3V，R4 是 H1-9 到三相 EN 的 **10kΩ 串联电阻**。板上没有电流采样，也没有 nFAULT 直接关闭 EN 的逻辑门。工作区另一份 STM32 HTML 原理图有额外采样/门电路，不能当作这块原版 Mini 的实物接线依据。

以下映射已按 TC264 V3.1 通用主板原理图核对，信号均可从主板现有插座引出。插座原来标注的摄像头、无刷电机、ToF 和姿态模块在本固件中停用，不要同时连接。STM32 的 PA/PB 引脚名称不适用于 TC264。

| 功能 | TC264 引脚 | TC264 V3.1 主板插座 | SimpleFOC Mini |
| --- | --- | --- | --- |
| PWM A | P00.1 / CCU61 CC60，ALT7 | P3-3 | H1-3 / IN1 |
| PWM B | P00.3 / CCU61 CC61，ALT7 | P3-7 | H1-5 / IN2 |
| PWM C | P00.5 / CCU61 CC62，ALT7 | P3-11 | H1-7 / IN3 |
| nSLEEP | P33.6 | P18-2（主板串联 100Ω） | H1-8 |
| nRESET | P33.7 | P18-6（主板串联 100Ω） | H1-6 |
| 三相共用 EN | P33.5 | P20-4 | H1-9 |
| nFAULT | P15.4 / ERU0 REQ0 | P4-8 | H1-10，必须连接 |
| 共地 | GND | P18-3 或 P18-4（也可用其他 GND） | H1-1 或 H1-4 |
| 禁止连接 | — | — | H1-2：DRV 内部 3.3V 输出，不给主板供电 |

主板电源从 P1 XT60 输入 7.2～24V。首次测试建议用 9V 限流台式电源接 P1，并从主板 DCOUT1/2 的 VCCBAT/GND 给 SimpleFOC Mini 的 VM/GND 供电，使两板天然共地。Mini 的 VM 推荐不低于 8V，不能用主板 VCC5V 给 VM 供电。若主控和驱动分开供电，仍必须连接两者 GND。

相对旧 FOC 配置，EN 保留 P33.5；三相 PWM 从 P22.0/.2/.1 移到 P00.1/.3/.5；nFAULT 从 P21.0 移到 P15.4；增加 nSLEEP/nRESET。旧线不能直接沿用。

TC264 核心板使用其规定的独立稳压电源，与 Mini 共地；不要将 STM32 裸芯片的“接 3.3V”要求机械套用到整块 TC264 核心板。H1-2 不给主控供电。P15.4 是 MP/VEXT 输入，本固件将它设为无 MCU 内部上拉的 TTL 滞回输入，使用 Mini 原有的 3.3V 上拉。按 TC26x 数据手册 TTL 滞回输入公式，在 VEXT 不高于 5.5V 时保证 VIH 最大为 2.03V，因此 3.3V 高电平满足规格；不要擅自把 nFAULT 上拉到另一电源导致反向供电。仍应按实际核心板 I/O 电源核对 PWM、EN、RESET、SLEEP 输出电平。

DRV8313 数据手册说明 ENx 内部有下拉，原图没有单独的外部 EN 下拉。固件会先写低输出锁存器再设 GPIO 输出，但**不能保证 MCU 复位、未供电或尚未进入 main 时 EN 一定为低**。应在断开电机时测量 EN 的启动/复位波形，按实物需要增加下拉或门电路。增加电阻时要考虑 R4 串联 10kΩ、三个 EN 输入的并联负载和 DRV 的 VIH，避免简单增加 10kΩ 下拉形成分压后无法使能。

## PWM 与控制顺序

三路 PWM 使用 CCU61 T12 的一个共享上/下计数器，中心对齐，目标 20kHz。程序从 `IfxScuCcu_getSpbFrequency()` 获取外设时钟，选择分频并计算 `T12PR = round(f_timer / (2 * 20000)) - 1`；实际配置频率由 `STATUS` 的 `PWM_HZ` 报告。只有 CC60/61/62 输出启用，COUT 互补输出和死区关闭，DRV8313 自行完成半桥驱动。

更新先取消旧影子传输请求，写完 CC60SR/CC61SR/CC62SR，再发一次 T12 影子传输请求；三相在同一个 PWM 边界生效。`MODCTR=0x15` 仅打开三路 CC 输出。

控制使用 CCU60_CH0 PIT，约每 100 μs 更新：

```text
theta_e += 2*pi*f_e*dt
A = 0.5 + 0.5*m*sin(theta_e)
B = 0.5 + 0.5*m*sin(theta_e - 2*pi/3)
C = 0.5 + 0.5*m*sin(theta_e + 2*pi/3)
```

角度归一到 `[0,2π)`，占空比防御限幅。正常开机停在 STOPPED，EN=0，nRESET/nSLEEP=0，PWM 关闭。只有显式 START 才执行：

1. EN=0，复位/睡眠保持 2 ms。
2. 释放 nRESET、nSLEEP，等待 5 ms（TI 数据手册唤醒约 1 ms，此处留余量）。
3. 确认 nFAULT 高，启动 PWM 并写入低幅固定角度，占空比同步更新；EN 仍为低。
4. 再等待 1 ms 确保 PWM/影子值稳定；复核 nFAULT 电平及 ERU 挂起事件，才使 EN=1。
5. 固定电角度、低幅定位 200 ms，然后同时缓慢增加电频率和幅值。

STOP 首先 EN=0，随后关闭 CCU61 PWM 调制/计数器，并拉低 nRESET/nSLEEP。仅清零占空比不能代表高阻：EN=1、IN=0 会使 DRV 下管导通。

本芯片没有 STM32 TIM1/BKIN。本版把低有效 nFAULT 接到 ERU0 的下降沿中断，优先级 255；ISR 第一项清 EN，然后锁存故障、关闭 PWM。运行期间控制中断还检查 nFAULT 电平。**本版没有使用 CCU6 CTRAP 硬件关 PWM，也没有 nFAULT 硬件门直接清 EN；关断依赖固件响应，延迟需要实测。**短临界区保护启停/三相提交，三角函数计算允许故障中断抢占。

复位/睡眠/唤醒等待期间 EN 始终低，预期的 nFAULT 变化不当作运行故障；唤醒结束仍低则锁存 FAULT。故障信号恢复高不会自动恢复，STOP 不清除锁存。CLEAR_FAULT 在 EN=0 时重新执行复位/唤醒并复核 nFAULT，成功后只回 STOPPED，必须再发 START。

## 参数和串口

参数集中在 `user/foc_config.h`：

| 参数 | 默认值 |
| --- | --- |
| PWM | 20,000 Hz，中心对齐 |
| 控制周期 | 100 μs |
| 目标电频率 | 5 Hz |
| 目标调制系数 | 0.05 |
| 定位调制系数/时间 | 不超过 0.03 / 200 ms |
| 电频率变化斜率 | 2 Hz/s |
| 调制系数变化斜率 | 0.02/s |
| 串口电频率范围 | 0.1～100 Hz，正向 |
| 串口调制系数范围 | 0～0.15；AMP=0 时拒绝 START |
| 占空比防御范围 | 0.05～0.95 |
| 复位/唤醒/PWM 稳定等待 | 2 / 5 / 1 ms |

运行中 FREQ/AMP 的变化也采用相同斜率。AMP=0 表示幅值逐渐降到零，不替代高阻 STOP。无负载/低频时仍可能有较大相电流，默认参数是初始调试值，不是某台 1104 电机的实测保证。

调试命令使用 UART3（ASCLIN3），115200、8N1，并使用 V3.1 主板 P9 已引出的第一组串口：P9-7=P15.6/TX 接 USB 转串口 RX，P9-5=P15.7/RX 接 USB 转串口 TX，P9-3=GND 接 USB 转串口 GND。P9-1 是主板 VCC5V，不接 USB-TTL 的信号电源脚；适配器信号电平应为 3.3V。终端可发送 CR、LF 或 CRLF。P14.0/P14.1 未从该主板外设插座引出，因此不再使用原 UART0 配置。

```text
STATUS
AMP 0.03
FREQ 2
START
STATUS
STOP
CLEAR_FAULT
HELP
```

命令不区分大小写；FREQ 单位为电 Hz，AMP 无量纲。START/CLEAR_FAULT 返回 accepted 仅代表流程开始，使用 STATUS 检查最终状态。STATUS 包含状态、EN、锁存标志、故障计数、PWM 配置频率、实际/目标电频率、实际/目标幅值及三相给定占空比；PWM_HZ 是配置值，PWM 停止时也保留。

串口 ISR 只搬运接收数据、标记错误，不解析或打印。主循环完成解析/响应；数字 NaN/Inf、超范围、额外参数均拒绝。超过 79 字符的行、256 字节 RX 环形缓存溢出和 UART 错误都会丢弃输入直到换行，避免截断命令触发 START。

## 编译与交付固件

在本项目目录执行：

```powershell
.\build.ps1 -Clean
python .\tests\run_host_tests.py
```

`build.ps1` 自动查找 AURIX Studio，或用 `-StudioRoot 'D:\Infineon\AURIX-Studio-1.10.10'` 指定安装路径。它使用官方 ADS 无界面构建，将源码和原始 linker 脚本复制到 `build/ads-project`，在构建副本移除依赖 GUI 的 Project Booster/Auto Discovery builder，重新生成当前路径的 Makefile；原工程框架保留。官方 ADS 捆绑 TASKING 许可要求通过 IDE 构建，不能直接把独立调用 cctc 的许可错误当作编译结果。

最终目标构建：AURIX Studio 1.10.10 / TASKING 1.1r8，**0 errors / 0 warnings**。日志 `build/build.log`。ROM/RAM 大小以最终日志为准。本次固件为：

- `build/FOC_TC264.elf`：调试/烧录用 ELF。
- `build/FOC_TC264.hex`：Intel HEX 烧录文件。
- `build/FOC_TC264.map`：链接映射。

**原先 `Debug/` 的 ELF/HEX 是旧版本，不要用它们烧录本次程序。**重新编译后只使用新的 `build/FOC_TC264.*`。

主机测试用 C99 编译器编译实际 `foc.c` 和 `foc_console.c`，只模拟 `foc_port`，覆盖精确等待时间、定位/斜坡/正弦限幅、各启动阶段 STOP、故障中断抢占、低故障信号、显式清故障、禁止自动重启及串口输入/缓存异常。测试脚本支持 GCC/Clang/Zig，或用 `FOC_HOST_CC` 指定 C99 编译器。本机使用 `D:/code/FOC/tmp/host_toolchain/ziglang/zig.exe`。软件测试不证明寄存器输出波形、真实中断耗时或电机转动。

## 实物验证顺序

1. **拆桨**并固定电机。先断开电机三相，9V 台式限流电源给 Mini 供电；主控独立供电并共地。初始电流限制可从约 0.3A 起步，按温升/母线情况调整；不要为了克服堵转直接加大幅值/电流。DRV8313 推荐 VM 最低 8V，直接 2S 电池不能保证全程满足。台式电源限流不能当成相电流闭环保护。
2. 不发 START，测上电、复位、下载期间的 DRV 侧 EN 波形，应保持安全低电平；核对 MCU 复位期间硬件默认状态。确认 nRESET/nSLEEP 和 nFAULT 实测电平符合主控/DRV 阈值。
3. 断开电机时发送 `AMP 0.03`、`FREQ 2`、`START`。示波器看三路 PWM：约 20kHz，同步中心对齐；EN 应在唤醒和 PWM 等待结束后才高，先固定角度定位、再慢变。发送 STOP，确认 EN 先低，再停 PWM；不能仅看 IN 都为低。
4. PWM/STOP 正常后接三相，仍拆桨、9V 限流，用上述低幅参数 START；观察起转、声音、母线和温升。只在确认同步跟随后小步调整 FREQ/AMP。只抖动或堵转时立即 STOP；开环无法判断失步，不能保证所有 1104 电机都能以同一组参数起转。
5. 用合适的开漏晶体管或受控接地测试 nFAULT（不要向其强灌高电平）：运行中将 H1-10 拉低，测 nFAULT→EN 关断延迟、EN 先低、PWM 停止，STATUS=FAULT。此测试验证输入停机链路，不等同于验证 DRV 内部过流/过温机制。
6. 恢复 nFAULT 高，不发指令，确认不会自启；直接 START 应拒绝。发 CLEAR_FAULT，确认等待结束后 STOPPED 且 EN=0，只有新的 START 才允许启动。nFAULT 仍低时 CLEAR_FAULT 必须仍停在 FAULT；STOP 也不能清锁存。

目前仅完成目标编译和实际 C 软件测试，**未连接实物，未验证波形、故障响应时间、相电流或电机转动成功**。
