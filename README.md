# FOC-Study

基于逐飞 TC264D 开源库与 Infineon AURIX iLLD 的 DRV8313 / SimpleFOC Mini 开环无刷电机驱动实验工程。

当前版本实现：

- GTM ATOM0 三路同步、中心脉冲对齐 20 kHz PWM；
- 电压开环三相正弦调制，不假定电机极对数；
- 低幅定位以及电频率、调制系数斜坡；
- EN 优先关断、nFAULT 中断和故障锁存；
- `START`、`STOP`、`FREQ`、`AMP`、`STATUS`、`CLEAR_FAULT` 串口命令；
- AURIX Studio / TASKING 命令行构建脚本和主机侧控制流程测试。

详细的接线、参数、编译方法和拆桨测试流程见 [README_DRV8313_TC264.md](README_DRV8313_TC264.md)。

TC264 V3.1 通用主板无需飞线到核心板焊盘：PWM 使用有刷电机接口 2 的 P7-2/P7-6/P7-1，nSLEEP/nRESET 使用 P18-2/P18-6，EN 使用 P20-4，nFAULT 使用 P4-8，调试串口使用 P9-7/P9-5。上述插座原来对应的有刷电机、无刷接口、ToF、姿态和串口模块在本固件中不再使用。

## 编译

```powershell
.\build.ps1 -Clean
python .\tests\run_host_tests.py
```

已提交的 `build/FOC_TC264.hex`、`.elf` 和 `.map` 来自 AURIX Studio 1.10.10 / TASKING 1.1r8，目标工程编译结果为 0 errors、0 warnings。

> 当前只完成编译和软件流程验证，尚未连接实物验证 PWM 波形、故障响应时间或电机实际转动。首次测试必须拆桨并使用限流电源。
