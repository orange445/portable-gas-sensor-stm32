# GasSensor STM32CubeMX 配置

工程入口：

- `GasSensor_STM32G030F6.ioc`
- MCU：STM32G030F6P6，TSSOP-20
- 目标 IDE：Keil MDK-ARM
- 目标编译器：Arm Compiler 6.22
- Keil工程：`MDK-ARM/GasSensor_STM32G030F6.uvprojx`
- 兼容版本：STM32CubeMX 6.12.x、STM32CubeG0 1.6.2

## 导入与生成

1. 用 STM32CubeMX 打开 `.ioc`。
2. 如果提示迁移数据库，选择 `Migrate`，但不要让 CubeMX 自动替换引脚。
3. 在 Pinout 页面确认 PA11/PA12 显示为 `I2C2_SCL/I2C2_SDA`，不是 PA9/PA10 重映射。
4. 在 Project Manager 的 Toolchain/IDE 中选择 `MDK-ARM`，生成后在 Keil Target Options 中固定选择 `V6.22`。
5. 生成后先检查 `MX_GPIO_Init()` 的输出预置顺序：
   - `ANA_EN` 必须先写低，再配置成输出。
   - `BLE_WAKE` 必须先写高，再配置成输出。

## 固化参数

| 模块 | 配置 |
|---|---|
| 系统时钟 | HSI16 直连，SYSCLK/HCLK/PCLK = 16 MHz，不启用 PLL |
| ADC | 12 bit，PCLK/4，160.5 cycles，64×过采样，右移6位 |
| ADC触发 | TIM3 TRGO Update，上升沿，20 Hz |
| ADC DMA | DMA1 Channel 1，外设到内存，16 bit，循环，高优先级 |
| ADC序列 | CH0、CH1、CH4、CH5、CH6、VREFINT |
| USART2 | PA2/PA3，115200，8-N-1，UART中断开启 |
| I2C2 | PA11/PA12默认映射，100 kHz，外部4.7 kΩ上拉 |
| SWD | PA13 SWDIO、PA14 SWCLK，保持启用 |
| CHG_STAT_N | PA7普通输入、无内部上下拉 |
| ANA_EN | PB0推挽输出，上电默认低 |
| BLE_WAKE | PB6推挽输出，上电默认高，下降沿脉冲唤醒 |

## ADC DMA 缓冲区

DMA每次触发写入6个 `uint16_t`：

```c
enum {
    ADC_I_TIA = 0,
    ADC_I_VCM,
    ADC_I_VFORCE,
    ADC_I_BAT,
    ADC_I_VBUS,
    ADC_I_VREFINT,
    ADC_FRAME_WORDS
};

static uint16_t adc_raw[ADC_FRAME_WORDS];
```

启动顺序必须是：

```c
HAL_ADCEx_Calibration_Start(&hadc1);
HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc_raw, ADC_FRAME_WORDS);
HAL_TIM_Base_Start(&htim3);
```

先启动ADC和DMA，最后启动TIM3，避免第一个触发丢失。DMA传输完成回调每50 ms触发一次，只做拷贝/置标志，不在中断中发送蓝牙数据或做浮点运算。

## 上电状态机

1. `MX_GPIO_Init()` 后确认 `ANA_EN=0`、`BLE_WAKE=1`。
2. 初始化DMA、I2C、USART2、TIM3和ADC。
3. 执行ADC自校准，启动DMA和TIM3。
4. 用 `BAT_SENSE`、`VBUS_SENSE` 和 VREFINT 检查电源。
5. 电池满足阈值后置 `ANA_EN=1`。
6. 等模拟前端稳定至少100 ms，丢弃前2帧，再允许输出测量值。
7. 电池低于3.25 V时立即置 `ANA_EN=0`。

固件0.2.1已从任务历史中的原始补丁恢复：默认输出10 Hz；`GAS,START`后立即打开模拟电源并进入150 ms `WARMUP`，随后进入`RUNNING`。本版没有后来加入的ARMING延时、USB输出限速、ALIVE心跳、提前UART诊断和初始化故障保活；保留12 kΩ TIA参数、ADC换算与安全关断。

首版调试阶段不在CubeMX中启用IWDG，避免尚未加入喂狗代码时反复复位。量产配置应在主循环、ADC超时和UART超时策略完成后启用约2 s IWDG。

## 后续代码存放

- 应用源文件：`Core/Src`
- 应用头文件：`Core/Inc`
- Keil工程入口：`MDK-ARM/GasSensor_STM32G030F6.uvprojx`
- CubeMX配置源：`GasSensor_STM32G030F6.ioc`

对CubeMX生成的文件进行修改时，代码必须放在对应的 `USER CODE BEGIN/END` 区域。独立的应用模块可以直接新建在 `Core/Src` 和 `Core/Inc`，并在Keil工程中加入相应文件组。

当前应用入口为 `Core/Src/gas_sensor.c`。Keil 工程使用
`Core/Startup/startup_stm32g030xx_arm.s`；同目录的
`startup_stm32g030f6px.s` 是原 CubeIDE/GCC 语法文件，不要加入 MDK Target。
