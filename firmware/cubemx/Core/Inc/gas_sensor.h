#ifndef GAS_SENSOR_H
#define GAS_SENSOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32g0xx_hal.h"

HAL_StatusTypeDef GasSensor_Init(void);
void GasSensor_Process(void);
void GasSensor_ForceSafeOff(void);

#ifdef __cplusplus
}
#endif

#endif /* GAS_SENSOR_H */
