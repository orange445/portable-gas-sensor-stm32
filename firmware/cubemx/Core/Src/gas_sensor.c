#include "gas_sensor.h"

#include "adc.h"
#include "main.h"
#include "tim.h"
#include "usart.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GAS_FW_VERSION              "0.2.1"
#define GAS_PROTOCOL_VERSION        1U

#define GAS_ADC_CHANNEL_COUNT       6U
#define GAS_ADC_FULL_SCALE          4095U
#define GAS_ADC_SAT_LOW             4U
#define GAS_ADC_SAT_HIGH            4091U
#define GAS_ACQUISITION_RATE_HZ     20U
#define GAS_DEFAULT_OUTPUT_RATE_HZ  10U
#define GAS_DEFAULT_RF_OHM          12000UL
#define GAS_CURRENT_WARN_MIN_NA     75L
#define GAS_CURRENT_WARN_MAX_NA     20000L
#define GAS_CURRENT_TRIP_NA         25000L
#define GAS_SENSOR_OPEN_NA          50L

#define GAS_BAT_START_MIN_MV        3250UL
#define GAS_BAT_CRITICAL_MV         3100UL
#define GAS_VBUS_PRESENT_MV         4000UL
#define GAS_WARMUP_TIME_MS          150UL
#define GAS_WARMUP_FRAME_COUNT      3U

#define GAS_RX_LINE_LENGTH          96U
#define GAS_TX_LINE_LENGTH          256U

#define GAS_FLAG_ADC_SAT            (1UL << 0)
#define GAS_FLAG_BIAS_OOR           (1UL << 1)
#define GAS_FLAG_CURRENT_OOR        (1UL << 2)
#define GAS_FLAG_SENSOR_OPEN        (1UL << 3)
#define GAS_FLAG_LOW_BAT            (1UL << 4)
#define GAS_FLAG_USB_PRESENT        (1UL << 5)
#define GAS_FLAG_CHARGING           (1UL << 6)
#define GAS_FLAG_WARMUP             (1UL << 7)
#define GAS_FLAG_VREF_INVALID       (1UL << 8)
#define GAS_FLAG_FRAME_DROPPED      (1UL << 9)

typedef enum
{
  GAS_STATE_OFF = 0,
  GAS_STATE_WARMUP,
  GAS_STATE_RUNNING,
  GAS_STATE_LOW_BAT,
  GAS_STATE_FAULT
} GasState;

typedef struct
{
  uint16_t raw[GAS_ADC_CHANNEL_COUNT];
  uint32_t vdda_mV;
  int32_t tia_uV;
  int32_t vcm_uV;
  int32_t vforce_uV;
  int32_t sensor_uV;
  int32_t current_nA;
  uint32_t resistance_ohm;
  uint32_t battery_mV;
  uint32_t vbus_mV;
  uint32_t flags;
} GasMeasurement;

static uint16_t s_adcDma[GAS_ADC_CHANNEL_COUNT];
static volatile uint16_t s_latestRaw[GAS_ADC_CHANNEL_COUNT];
static volatile uint8_t s_framePending;
static volatile uint32_t s_droppedFrames;
static volatile uint32_t s_totalDroppedFrames;
static volatile uint8_t s_adcErrorPending;

static uint8_t s_uartRxByte;
static char s_rxBuild[GAS_RX_LINE_LENGTH];
static char s_rxCommand[GAS_RX_LINE_LENGTH];
static volatile uint16_t s_rxLength;
static volatile uint8_t s_commandPending;
static volatile uint8_t s_uartErrorPending;
static volatile uint8_t s_commandOverflow;
static volatile uint32_t s_bleModuleLogLines;

static char s_txLine[GAS_TX_LINE_LENGTH];
static GasMeasurement s_lastMeasurement;
static GasState s_state = GAS_STATE_OFF;
static uint32_t s_rfOhm = GAS_DEFAULT_RF_OHM;
static int32_t s_zero_uV;
static uint32_t s_outputRateHz = GAS_DEFAULT_OUTPUT_RATE_HZ;
static uint32_t s_outputAccumulator;
static uint32_t s_runStartMs;
static uint32_t s_warmupDeadlineMs;
static uint8_t s_warmupFrames;
static uint8_t s_haveMeasurement;
static uint8_t s_saturationFrames;
static uint8_t s_biasFaultFrames;
static uint8_t s_overcurrentFrames;
static uint32_t s_outputSequence;
static uint32_t s_lastValidVdda_mV = 3300UL;
static const char *s_resetReason = "UNKNOWN";

static const char *Gas_CaptureResetReason(void)
{
  const char *reason = "UNKNOWN";

  if (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST) != RESET)
  {
    reason = "IWDG";
  }
  else if (__HAL_RCC_GET_FLAG(RCC_FLAG_WWDGRST) != RESET)
  {
    reason = "WWDG";
  }
  else if (__HAL_RCC_GET_FLAG(RCC_FLAG_SFTRST) != RESET)
  {
    reason = "SOFTWARE";
  }
  else if (__HAL_RCC_GET_FLAG(RCC_FLAG_PWRRST) != RESET)
  {
    reason = "POWER_BOR";
  }
  else if (__HAL_RCC_GET_FLAG(RCC_FLAG_PINRST) != RESET)
  {
    reason = "NRST";
  }
  else if (__HAL_RCC_GET_FLAG(RCC_FLAG_OBLRST) != RESET)
  {
    reason = "OPTION_BYTE";
  }
  else if (__HAL_RCC_GET_FLAG(RCC_FLAG_LPWRRST) != RESET)
  {
    reason = "LOW_POWER";
  }

  __HAL_RCC_CLEAR_RESET_FLAGS();
  return reason;
}

static const char *Gas_StateName(GasState state)
{
  switch (state)
  {
    case GAS_STATE_OFF:
      return "OFF";
    case GAS_STATE_WARMUP:
      return "WARMUP";
    case GAS_STATE_RUNNING:
      return "RUNNING";
    case GAS_STATE_LOW_BAT:
      return "LOW_BAT";
    case GAS_STATE_FAULT:
      return "FAULT";
    default:
      return "UNKNOWN";
  }
}

static void Gas_SendLine(const char *line)
{
  uint16_t length = (uint16_t)strlen(line);
  (void)HAL_UART_Transmit(&huart2, (uint8_t *)line, length, 100U);
}

static void Gas_SendInfo(void)
{
  (void)snprintf(s_txLine, sizeof(s_txLine),
                 "GAS,INFO,proto=%lu,fw=%s,acq_hz=%lu,out_hz=%lu,rf_ohm=%lu,zero_uV=%ld,cal=ram,reset=%s,uptime_ms=%lu,ble_log=%lu\r\n",
                 (unsigned long)GAS_PROTOCOL_VERSION,
                 GAS_FW_VERSION,
                 (unsigned long)GAS_ACQUISITION_RATE_HZ,
                 (unsigned long)s_outputRateHz,
                 (unsigned long)s_rfOhm,
                 (long)s_zero_uV,
                 s_resetReason,
                 (unsigned long)HAL_GetTick(),
                 (unsigned long)s_bleModuleLogLines);
  Gas_SendLine(s_txLine);
}

static void Gas_SendState(const char *reason)
{
  (void)snprintf(s_txLine, sizeof(s_txLine),
                 "GAS,STATE,%s,%s\r\n",
                 Gas_StateName(s_state), reason);
  Gas_SendLine(s_txLine);
}

static void Gas_SetState(GasState state, const char *reason)
{
  if (s_state != state)
  {
    s_state = state;
    Gas_SendState(reason);
  }
}

static void Gas_SetAnalogPower(uint8_t enabled)
{
  HAL_GPIO_WritePin(ANA_EN_GPIO_Port, ANA_EN_Pin,
                   (enabled != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void GasSensor_ForceSafeOff(void)
{
  Gas_SetAnalogPower(0U);
}

static uint8_t Gas_TickReached(uint32_t now, uint32_t deadline)
{
  return ((int32_t)(now - deadline) >= 0) ? 1U : 0U;
}

static int32_t Gas_AbsI32(int32_t value)
{
  if (value == INT32_MIN)
  {
    return INT32_MAX;
  }
  return (value < 0) ? -value : value;
}

static int32_t Gas_AdcToMicrovolts(uint16_t raw, uint32_t vdda_mV)
{
  uint64_t numerator = (uint64_t)raw * (uint64_t)vdda_mV * 1000ULL;
  return (int32_t)((numerator + (GAS_ADC_FULL_SCALE / 2U)) / GAS_ADC_FULL_SCALE);
}

static uint8_t Gas_IsAdcSaturated(uint16_t raw)
{
  return ((raw <= GAS_ADC_SAT_LOW) || (raw >= GAS_ADC_SAT_HIGH)) ? 1U : 0U;
}

static void Gas_UpdateMeasurement(const uint16_t raw[GAS_ADC_CHANNEL_COUNT],
                                  uint32_t droppedFrames)
{
  uint32_t vdda_mV;
  int32_t correctedDiff_uV;
  int32_t absoluteCurrent_nA;
  uint8_t chargeActive;
  uint32_t flags = 0U;
  uint32_t index;

  for (index = 0U; index < GAS_ADC_CHANNEL_COUNT; ++index)
  {
    s_lastMeasurement.raw[index] = raw[index];
  }

  if (raw[5] != 0U)
  {
    vdda_mV = __HAL_ADC_CALC_VREFANALOG_VOLTAGE(raw[5], ADC_RESOLUTION_12B);
  }
  else
  {
    vdda_mV = 0U;
  }

  if ((vdda_mV >= 2500UL) && (vdda_mV <= 3800UL))
  {
    s_lastValidVdda_mV = vdda_mV;
  }
  else
  {
    vdda_mV = s_lastValidVdda_mV;
    flags |= GAS_FLAG_VREF_INVALID;
  }

  s_lastMeasurement.vdda_mV = vdda_mV;
  s_lastMeasurement.tia_uV = Gas_AdcToMicrovolts(raw[0], vdda_mV);
  s_lastMeasurement.vcm_uV = Gas_AdcToMicrovolts(raw[1], vdda_mV);
  s_lastMeasurement.vforce_uV = Gas_AdcToMicrovolts(raw[2], vdda_mV);
  s_lastMeasurement.sensor_uV =
      s_lastMeasurement.vcm_uV - s_lastMeasurement.vforce_uV;
  s_lastMeasurement.battery_mV =
      (uint32_t)((Gas_AdcToMicrovolts(raw[3], vdda_mV) * 2LL) / 1000LL);
  s_lastMeasurement.vbus_mV =
      (uint32_t)((Gas_AdcToMicrovolts(raw[4], vdda_mV) * 2LL) / 1000LL);

  correctedDiff_uV =
      (s_lastMeasurement.tia_uV - s_lastMeasurement.vcm_uV) - s_zero_uV;
  s_lastMeasurement.current_nA =
      (int32_t)(((int64_t)correctedDiff_uV * 1000LL) / (int64_t)s_rfOhm);
  absoluteCurrent_nA = Gas_AbsI32(s_lastMeasurement.current_nA);

  if ((s_lastMeasurement.current_nA > 0) &&
      (s_lastMeasurement.sensor_uV > 0))
  {
    uint64_t resistance =
        ((uint64_t)(uint32_t)s_lastMeasurement.sensor_uV * 1000ULL) /
        (uint32_t)s_lastMeasurement.current_nA;
    s_lastMeasurement.resistance_ohm =
        (resistance <= UINT32_MAX) ? (uint32_t)resistance : UINT32_MAX;
  }
  else
  {
    s_lastMeasurement.resistance_ohm = 0U;
  }

  if (Gas_IsAdcSaturated(raw[0]) || Gas_IsAdcSaturated(raw[1]) ||
      Gas_IsAdcSaturated(raw[2]))
  {
    flags |= GAS_FLAG_ADC_SAT;
  }

  if ((s_state == GAS_STATE_WARMUP) || (s_state == GAS_STATE_RUNNING))
  {
    if ((s_lastMeasurement.vcm_uV < 2200000L) ||
        (s_lastMeasurement.vcm_uV > 2800000L) ||
        (s_lastMeasurement.vforce_uV < 250000L) ||
        (s_lastMeasurement.vforce_uV > 750000L) ||
        (s_lastMeasurement.sensor_uV < 1750000L) ||
        (s_lastMeasurement.sensor_uV > 2250000L))
    {
      flags |= GAS_FLAG_BIAS_OOR;
    }

    if ((s_lastMeasurement.current_nA < GAS_CURRENT_WARN_MIN_NA) ||
        (s_lastMeasurement.current_nA > GAS_CURRENT_WARN_MAX_NA))
    {
      flags |= GAS_FLAG_CURRENT_OOR;
    }

    if (absoluteCurrent_nA < GAS_SENSOR_OPEN_NA)
    {
      flags |= GAS_FLAG_SENSOR_OPEN;
    }
  }

  if (s_lastMeasurement.battery_mV < GAS_BAT_START_MIN_MV)
  {
    flags |= GAS_FLAG_LOW_BAT;
  }
  if (s_lastMeasurement.vbus_mV >= GAS_VBUS_PRESENT_MV)
  {
    flags |= GAS_FLAG_USB_PRESENT;
  }

  chargeActive =
      (HAL_GPIO_ReadPin(CHG_STAT_N_GPIO_Port, CHG_STAT_N_Pin) == GPIO_PIN_RESET)
          ? 1U
          : 0U;
  if (chargeActive != 0U)
  {
    flags |= GAS_FLAG_CHARGING;
  }
  if (s_state == GAS_STATE_WARMUP)
  {
    flags |= GAS_FLAG_WARMUP;
  }
  if (droppedFrames != 0U)
  {
    flags |= GAS_FLAG_FRAME_DROPPED;
  }

  s_lastMeasurement.flags = flags;
  s_haveMeasurement = 1U;
}

static void Gas_EnterSafeState(GasState state, const char *reason)
{
  Gas_SetAnalogPower(0U);
  s_warmupFrames = 0U;
  s_saturationFrames = 0U;
  s_biasFaultFrames = 0U;
  s_overcurrentFrames = 0U;
  Gas_SetState(state, reason);
}

static void Gas_CheckSafety(void)
{
  uint32_t flags = s_lastMeasurement.flags;
  int32_t absoluteCurrent_nA = Gas_AbsI32(s_lastMeasurement.current_nA);

  if ((s_state != GAS_STATE_WARMUP) &&
      (s_state != GAS_STATE_RUNNING))
  {
    return;
  }

  if (s_lastMeasurement.battery_mV < GAS_BAT_CRITICAL_MV)
  {
    Gas_EnterSafeState(GAS_STATE_LOW_BAT, "BAT_CRITICAL");
    return;
  }

  if (s_state == GAS_STATE_WARMUP)
  {
    return;
  }

  if ((flags & GAS_FLAG_ADC_SAT) != 0U)
  {
    if (++s_saturationFrames >= 3U)
    {
      Gas_EnterSafeState(GAS_STATE_FAULT, "ADC_SAT");
      return;
    }
  }
  else
  {
    s_saturationFrames = 0U;
  }

  if ((s_lastMeasurement.vcm_uV < 2000000L) ||
      (s_lastMeasurement.vcm_uV > 3000000L) ||
      (s_lastMeasurement.sensor_uV < 1500000L) ||
      (s_lastMeasurement.sensor_uV > 2500000L))
  {
    if (++s_biasFaultFrames >= 5U)
    {
      Gas_EnterSafeState(GAS_STATE_FAULT, "BIAS_FAULT");
      return;
    }
  }
  else
  {
    s_biasFaultFrames = 0U;
  }

  if (absoluteCurrent_nA > GAS_CURRENT_TRIP_NA)
  {
    if (++s_overcurrentFrames >= 5U)
    {
      Gas_EnterSafeState(GAS_STATE_FAULT, "CURRENT_HIGH");
    }
  }
  else
  {
    s_overcurrentFrames = 0U;
  }
}

static void Gas_SendDataFrame(void)
{
  uint32_t timestampMs = HAL_GetTick() - s_runStartMs;

  (void)snprintf(
      s_txLine, sizeof(s_txLine),
      "GAS,D,%lu,%lu,%ld,%lu,%ld,%ld,%ld,%ld,%lu,%lu,0x%04lX,%u,%u,%u,%u,%u,%u\r\n",
      (unsigned long)s_outputSequence++,
      (unsigned long)timestampMs,
      (long)s_lastMeasurement.current_nA,
      (unsigned long)s_lastMeasurement.resistance_ohm,
      (long)s_lastMeasurement.sensor_uV,
      (long)s_lastMeasurement.tia_uV,
      (long)s_lastMeasurement.vcm_uV,
      (long)s_lastMeasurement.vforce_uV,
      (unsigned long)s_lastMeasurement.battery_mV,
      (unsigned long)s_lastMeasurement.vbus_mV,
      (unsigned long)s_lastMeasurement.flags,
      (unsigned int)s_lastMeasurement.raw[0],
      (unsigned int)s_lastMeasurement.raw[1],
      (unsigned int)s_lastMeasurement.raw[2],
      (unsigned int)s_lastMeasurement.raw[3],
      (unsigned int)s_lastMeasurement.raw[4],
      (unsigned int)s_lastMeasurement.raw[5]);
  Gas_SendLine(s_txLine);
}

static void Gas_SendStatus(void)
{
  (void)snprintf(
      s_txLine, sizeof(s_txLine),
      "GAS,S,%s,%lu,%lu,%lu,%lu,%ld,0x%04lX,%lu,%lu\r\n",
      Gas_StateName(s_state),
      (unsigned long)s_lastMeasurement.battery_mV,
      (unsigned long)s_lastMeasurement.vbus_mV,
      (unsigned long)s_outputRateHz,
      (unsigned long)s_rfOhm,
      (long)s_zero_uV,
      (unsigned long)s_lastMeasurement.flags,
      (unsigned long)s_totalDroppedFrames,
      (unsigned long)s_bleModuleLogLines);
  Gas_SendLine(s_txLine);
}

static uint8_t Gas_ParseI32(const char *text, int32_t *value)
{
  char *end;
  long parsed = strtol(text, &end, 10);
  if ((end == text) || (*end != '\0'))
  {
    return 0U;
  }
  *value = (int32_t)parsed;
  return 1U;
}

static uint8_t Gas_ParseU32(const char *text, uint32_t *value)
{
  char *end;
  unsigned long parsed = strtoul(text, &end, 10);
  if ((end == text) || (*end != '\0'))
  {
    return 0U;
  }
  *value = (uint32_t)parsed;
  return 1U;
}

static void Gas_CommandStart(void)
{
  if (s_haveMeasurement == 0U)
  {
    Gas_SendLine("GAS,ERR,NO_ADC_SAMPLE\r\n");
    return;
  }
  if (s_lastMeasurement.battery_mV < GAS_BAT_START_MIN_MV)
  {
    Gas_EnterSafeState(GAS_STATE_LOW_BAT, "BAT_TOO_LOW");
    Gas_SendLine("GAS,ERR,BAT_TOO_LOW\r\n");
    return;
  }

  Gas_SetAnalogPower(1U);
  s_warmupFrames = 0U;
  s_saturationFrames = 0U;
  s_biasFaultFrames = 0U;
  s_overcurrentFrames = 0U;
  s_outputAccumulator = 0U;
  s_outputSequence = 0U;
  s_warmupDeadlineMs = HAL_GetTick() + GAS_WARMUP_TIME_MS;
  Gas_SetState(GAS_STATE_WARMUP, "START");
  Gas_SendLine("GAS,ACK,START\r\n");
}

static void Gas_CommandStop(void)
{
  Gas_EnterSafeState(GAS_STATE_OFF, "HOST_STOP");
  Gas_SendLine("GAS,ACK,STOP\r\n");
}

static void Gas_HandleCommand(char *command)
{
  if ((strcmp(command, "HELLO") == 0) ||
      (strcmp(command, "GAS,HELLO") == 0))
  {
    Gas_SendInfo();
    Gas_SendStatus();
    return;
  }
  if (strcmp(command, "GAS,START") == 0)
  {
    Gas_CommandStart();
    return;
  }
  if (strcmp(command, "GAS,STOP") == 0)
  {
    Gas_CommandStop();
    return;
  }
  if (strcmp(command, "GAS,STATUS") == 0)
  {
    Gas_SendStatus();
    return;
  }
  if (strncmp(command, "GAS,RATE,", 9U) == 0)
  {
    uint32_t rate;
    if ((Gas_ParseU32(command + 9U, &rate) == 0U) ||
        (rate < 1U) || (rate > GAS_ACQUISITION_RATE_HZ))
    {
      Gas_SendLine("GAS,ERR,RATE_RANGE_1_20\r\n");
      return;
    }
    s_outputRateHz = rate;
    s_outputAccumulator = 0U;
    (void)snprintf(s_txLine, sizeof(s_txLine),
                   "GAS,ACK,RATE,%lu\r\n", (unsigned long)rate);
    Gas_SendLine(s_txLine);
    return;
  }
  if (strncmp(command, "GAS,CAL,", 8U) == 0)
  {
    char *separator = strchr(command + 8U, ',');
    int32_t zero_uV;
    uint32_t rfOhm;
    if (separator == NULL)
    {
      Gas_SendLine("GAS,ERR,CAL_FORMAT\r\n");
      return;
    }
    *separator = '\0';
    if ((Gas_ParseI32(command + 8U, &zero_uV) == 0U) ||
        (Gas_ParseU32(separator + 1U, &rfOhm) == 0U) ||
        (zero_uV < -500000L) || (zero_uV > 500000L) ||
        (rfOhm < 1000UL) || (rfOhm > 10000000UL))
    {
      Gas_SendLine("GAS,ERR,CAL_RANGE\r\n");
      return;
    }
    s_zero_uV = zero_uV;
    s_rfOhm = rfOhm;
    (void)snprintf(s_txLine, sizeof(s_txLine),
                   "GAS,ACK,CAL,%ld,%lu\r\n",
                   (long)s_zero_uV, (unsigned long)s_rfOhm);
    Gas_SendLine(s_txLine);
    return;
  }

  Gas_SendLine("GAS,ERR,UNKNOWN_COMMAND\r\n");
}

static void Gas_ProcessPendingCommand(void)
{
  if (s_commandPending != 0U)
  {
    char command[GAS_RX_LINE_LENGTH];
    __disable_irq();
    memcpy(command, s_rxCommand, sizeof(command));
    s_commandPending = 0U;
    __enable_irq();
    Gas_HandleCommand(command);
  }

  if (s_commandOverflow != 0U)
  {
    __disable_irq();
    s_commandOverflow = 0U;
    __enable_irq();
    Gas_SendLine("GAS,ERR,RX_OVERFLOW\r\n");
  }

  if (s_uartErrorPending != 0U)
  {
    __disable_irq();
    s_uartErrorPending = 0U;
    __enable_irq();
    Gas_SendLine("GAS,ERR,UART_RECOVERED\r\n");
  }
}

static void Gas_ProcessPendingFrame(void)
{
  uint16_t raw[GAS_ADC_CHANNEL_COUNT];
  uint32_t droppedFrames;
  uint32_t index;
  uint8_t haveFrame = 0U;

  __disable_irq();
  if (s_framePending != 0U)
  {
    for (index = 0U; index < GAS_ADC_CHANNEL_COUNT; ++index)
    {
      raw[index] = s_latestRaw[index];
    }
    droppedFrames = s_droppedFrames;
    s_droppedFrames = 0U;
    s_framePending = 0U;
    haveFrame = 1U;
  }
  __enable_irq();

  if (haveFrame == 0U)
  {
    return;
  }

  Gas_UpdateMeasurement(raw, droppedFrames);
  Gas_CheckSafety();

  if (s_state == GAS_STATE_WARMUP)
  {
    ++s_warmupFrames;
    if ((s_warmupFrames >= GAS_WARMUP_FRAME_COUNT) &&
        (Gas_TickReached(HAL_GetTick(), s_warmupDeadlineMs) != 0U))
    {
      s_runStartMs = HAL_GetTick();
      s_outputAccumulator = 0U;
      s_outputSequence = 0U;
      Gas_SetState(GAS_STATE_RUNNING, "WARMUP_DONE");
    }
  }

  if (s_state == GAS_STATE_RUNNING)
  {
    s_outputAccumulator += s_outputRateHz;
    if (s_outputAccumulator >= GAS_ACQUISITION_RATE_HZ)
    {
      s_outputAccumulator -= GAS_ACQUISITION_RATE_HZ;
      Gas_SendDataFrame();
    }
  }
}

HAL_StatusTypeDef GasSensor_Init(void)
{
  s_resetReason = Gas_CaptureResetReason();
  Gas_SetAnalogPower(0U);
  HAL_GPIO_WritePin(BLE_WAKE_GPIO_Port, BLE_WAKE_Pin, GPIO_PIN_SET);

  if (HAL_UART_Receive_IT(&huart2, &s_uartRxByte, 1U) != HAL_OK)
  {
    return HAL_ERROR;
  }
  if (HAL_ADCEx_Calibration_Start(&hadc1) != HAL_OK)
  {
    return HAL_ERROR;
  }
  if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)s_adcDma,
                        GAS_ADC_CHANNEL_COUNT) != HAL_OK)
  {
    return HAL_ERROR;
  }
  __HAL_DMA_DISABLE_IT(hadc1.DMA_Handle, DMA_IT_HT);
  if (HAL_TIM_Base_Start(&htim3) != HAL_OK)
  {
    (void)HAL_ADC_Stop_DMA(&hadc1);
    return HAL_ERROR;
  }

  s_state = GAS_STATE_OFF;
  Gas_SendInfo();
  Gas_SendState("BOOT");
  return HAL_OK;
}

void GasSensor_Process(void)
{
  Gas_ProcessPendingCommand();

  if (s_adcErrorPending != 0U)
  {
    __disable_irq();
    s_adcErrorPending = 0U;
    __enable_irq();
    Gas_EnterSafeState(GAS_STATE_FAULT, "ADC_ERROR");
  }

  Gas_ProcessPendingFrame();
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
  uint32_t index;
  if (hadc->Instance != ADC1)
  {
    return;
  }

  if (s_framePending != 0U)
  {
    ++s_droppedFrames;
    ++s_totalDroppedFrames;
  }
  for (index = 0U; index < GAS_ADC_CHANNEL_COUNT; ++index)
  {
    s_latestRaw[index] = s_adcDma[index];
  }
  s_framePending = 1U;
}

void HAL_ADC_ErrorCallback(ADC_HandleTypeDef *hadc)
{
  if (hadc->Instance == ADC1)
  {
    s_adcErrorPending = 1U;
  }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance != USART2)
  {
    return;
  }

  if ((s_uartRxByte == '\r') || (s_uartRxByte == '\n'))
  {
    if (s_rxLength != 0U)
    {
      if ((s_rxBuild[0] == '+') ||
          ((s_rxLength >= 4U) &&
           (memcmp(s_rxBuild, "MSG_", 4U) == 0)))
      {
        ++s_bleModuleLogLines;
      }
      else if (s_commandPending == 0U)
      {
        memcpy(s_rxCommand, s_rxBuild, s_rxLength);
        s_rxCommand[s_rxLength] = '\0';
        s_commandPending = 1U;
      }
      else
      {
        s_commandOverflow = 1U;
      }
      s_rxLength = 0U;
    }
  }
  else if ((s_uartRxByte >= 0x20U) && (s_uartRxByte <= 0x7EU))
  {
    if (s_rxLength < (GAS_RX_LINE_LENGTH - 1U))
    {
      s_rxBuild[s_rxLength++] = (char)s_uartRxByte;
    }
    else
    {
      s_rxLength = 0U;
      s_commandOverflow = 1U;
    }
  }

  (void)HAL_UART_Receive_IT(&huart2, &s_uartRxByte, 1U);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART2)
  {
    s_uartErrorPending = 1U;
    s_rxLength = 0U;
    __HAL_UART_CLEAR_OREFLAG(huart);
    (void)HAL_UART_Receive_IT(&huart2, &s_uartRxByte, 1U);
  }
}
