# STM32CubeMX 配置基线

## 1. 配置文件

可直接导入的配置位于：

`firmware/cubemx/GasSensor_STM32G030F6.ioc`

目标器件是 STM32G030F6P6（TSSOP-20）。该配置使用内部16 MHz HSI，不依赖外部晶振，符合当前低成本、小面积和低功耗目标。

文件按STM32CubeMX 6.12.0、数据库DB.6.0.120和STM32CubeG0 1.6.2保存，可由实验室当前的CubeMX 6.12.0直接打开。更高版本打开时允许正常迁移。

## 2. 引脚核对

| MCU脚位 | CubeMX功能 | 原理图网络 | 说明 |
|---|---|---|---|
| PA0 | ADC1_IN0 | AIN_TIA | TIA输出 |
| PA1 | ADC1_IN1 | AIN_VCM | TIA共模/虚拟地 |
| PA2 | USART2_TX | BLE_UART_TX | 到蓝牙RXD |
| PA3 | USART2_RX | BLE_UART_RX | 来自蓝牙TXD |
| PA4 | ADC1_IN4 | AIN_VFORCE | 传感器低端偏置 |
| PA5 | ADC1_IN5 | BAT_SENSE | 1 MΩ/1 MΩ电池分压 |
| PA6 | ADC1_IN6 | VBUS_SENSE | 1 MΩ/1 MΩ USB分压 |
| PA7 | GPIO Input | CHG_STAT_N | TP4054开漏状态，板上已有上拉 |
| PB0 | GPIO Output | ANA_EN | 默认低，控制模拟电源 |
| PA11 | I2C2_SCL | I2C_SCL | 默认映射，不切到PA9 |
| PA12 | I2C2_SDA | I2C_SDA | 默认映射，不切到PA10 |
| PA13 | SYS_SWDIO | SWDIO | 保留调试 |
| PA14 | SYS_SWCLK | SWDCLK | 保留调试/BOOT0共享脚 |
| PB6 | GPIO Output | BLE_WAKE | 默认高，下降沿唤醒 |

TSSOP-20封装的15、19、20脚包含多个内部连接的逻辑别名。配置只允许PB0、PA14和PB6承担输出功能，其同焊盘别名保持未配置/模拟状态，禁止再分配为另一个输出。

## 3. 时钟

```text
HSI16 = 16 MHz
SYSCLK = HSI16
HCLK   = 16 MHz
PCLK   = 16 MHz
TIM3   = 16 MHz
ADC kernel input = 16 MHz
ADC clock = PCLK / 4 = 4 MHz
```

不启用PLL的理由是当前计算、20 Hz采样和115200 UART所需性能很低；16 MHz可减少功耗和数字开关噪声，也不增加晶振面积。

## 4. ADC、DMA与定时器

ADC固定序列：

| DMA索引 | ADC通道 | 网络 |
|---:|---|---|
| 0 | CH0 | AIN_TIA |
| 1 | CH1 | AIN_VCM |
| 2 | CH4 | AIN_VFORCE |
| 3 | CH5 | BAT_SENSE |
| 4 | CH6 | VBUS_SENSE |
| 5 | VREFINT | 内部参考 |

ADC设置：

- 12位、右对齐。
- 同步PCLK/4，即4 MHz ADC时钟。
- 每通道160.5周期采样。
- 硬件过采样64次，右移6位，输出仍为12位平均值。
- TIM3 TRGO Update上升沿触发。
- DMA循环模式，半字到半字，内存地址递增。
- DMA1 Channel 1中断优先级1。

TIM3设置：

```text
16 MHz / (15999 + 1) = 1 kHz
1 kHz / (49 + 1) = 20 Hz
```

一帧最坏转换时间估算：

```text
6通道 × 64次 × (160.5 + 12.5)周期 / 4 MHz
= 16.608 ms
```

该时间小于50 ms触发周期。余下约33.4 ms可供CPU处理、UART分批发送和休眠。

## 5. 为什么加入VREFINT

STM32 ADC以VDDA为参考，而3V0 LDO、BLE发射电流和电池状态会让VDDA发生小幅变化。读取VREFINT可以估算实际VDDA，再将所有ADC码转换为电压，避免固定使用3000 mV造成比例误差。

建议换算顺序：

```text
VDDA     = VREFINT_CAL换算结果
V_TIA    = ADC_TIA    × VDDA / 4095
V_VCM    = ADC_VCM    × VDDA / 4095
V_FORCE  = ADC_VFORCE × VDDA / 4095
V_BAT    = 2 × ADC_BAT  × VDDA / 4095
V_VBUS   = 2 × ADC_VBUS × VDDA / 4095

Vdiff    = V_TIA - V_VCM
Vsensor  = V_VCM - V_FORCE
Isensor  = (Vdiff - Vzero) / Rf_cal
Rsensor  = Vsensor / Isensor
```

`Vzero`和`Rf_cal`必须来自实测校准，不能只使用0和12 kΩ标称值。

## 6. UART与BLE

USART2固定为115200 bit/s、8数据位、无校验、1停止位。使能USART2中断，用接收中断或Receive-to-Idle处理AT响应和控制命令；发送测量数据可先使用短包阻塞发送，确认稳定后再改为DMA队列。

蓝牙发送不允许放在ADC DMA中断中。DMA回调只复制6个原始码并置位，主循环再完成：

```text
原始码快照 -> 电压换算 -> 校准/滤波 -> 打包 -> UART发送
```

## 7. I2C预留

I2C2配置为100 kHz，PA11/PA12默认映射，板上R22/R23提供4.7 kΩ上拉，因此GPIO内部不上拉。当前没有装温湿度器件时可以不调用 `MX_I2C2_Init()` 以节省少量功耗和Flash，但不要改变引脚映射。

## 8. 必须人工检查的生成代码

CubeMX生成代码后检查以下项目：

1. `MX_GPIO_Init()` 在设置GPIO模式前，先把PB0写低、PB6写高。
2. `HAL_ADCEx_Calibration_Start()` 在ADC DMA启动前只执行一次。
3. `HAL_ADC_Start_DMA()` 在 `HAL_TIM_Base_Start()` 之前调用。
4. DMA长度严格为6个 `uint16_t`。
5. `HAL_ADC_ConvCpltCallback()` 不做UART发送、浮点计算或阻塞等待。
6. 启用模拟前端后至少等待100 ms并丢弃2帧。
7. PA13/PA14仍是SWD，PA11/PA12没有发生PA9/PA10重映射。
8. 调试阶段不启用IWDG；量产固件完善喂狗路径后再启用约2 s IWDG。

## 9. 采样率扩展

当前20 Hz适合图示约0.1–1.6 µA呼吸/湿度变化信号。如果需要观察更快的瞬态，可把TIM3周期从49改为19得到50 Hz，但现有64×过采样单帧需要约16.6 ms，50 Hz只剩约3.4 ms余量。更高采样率应先降低过采样倍率或缩短采样时间，并重新验证1 MΩ分压通道的稳定误差。
