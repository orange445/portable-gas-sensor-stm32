;******************************************************************************
;* File Name          : startup_stm32g030xx.s
;* Author             : MCD Application Team
;* Description        : STM32G030xx vector table for the MDK-ARM toolchain.
;******************************************************************************
;* Copyright (c) 2018-2021 STMicroelectronics.
;* All rights reserved.
;*
;* This software is licensed under terms that can be found in the LICENSE file
;* in the root directory of this software component.
;* If no LICENSE file comes with this software, it is provided AS-IS.
;******************************************************************************

Stack_Size      EQU     0x00000400

                AREA    STACK, NOINIT, READWRITE, ALIGN=3
Stack_Mem       SPACE   Stack_Size
__initial_sp

Heap_Size       EQU     0x00000200

                AREA    HEAP, NOINIT, READWRITE, ALIGN=3
__heap_base
Heap_Mem        SPACE   Heap_Size
__heap_limit

                PRESERVE8
                THUMB

; Vector table mapped to address 0 at reset
                AREA    RESET, DATA, READONLY
                EXPORT  __Vectors
                EXPORT  __Vectors_End
                EXPORT  __Vectors_Size

__Vectors       DCD     __initial_sp
                DCD     Reset_Handler
                DCD     NMI_Handler
                DCD     HardFault_Handler
                DCD     0
                DCD     0
                DCD     0
                DCD     0
                DCD     0
                DCD     0
                DCD     0
                DCD     SVC_Handler
                DCD     0
                DCD     0
                DCD     PendSV_Handler
                DCD     SysTick_Handler

                DCD     WWDG_IRQHandler
                DCD     0
                DCD     RTC_TAMP_IRQHandler
                DCD     FLASH_IRQHandler
                DCD     RCC_IRQHandler
                DCD     EXTI0_1_IRQHandler
                DCD     EXTI2_3_IRQHandler
                DCD     EXTI4_15_IRQHandler
                DCD     0
                DCD     DMA1_Channel1_IRQHandler
                DCD     DMA1_Channel2_3_IRQHandler
                DCD     DMA1_Ch4_5_DMAMUX1_OVR_IRQHandler
                DCD     ADC1_IRQHandler
                DCD     TIM1_BRK_UP_TRG_COM_IRQHandler
                DCD     TIM1_CC_IRQHandler
                DCD     0
                DCD     TIM3_IRQHandler
                DCD     0
                DCD     0
                DCD     TIM14_IRQHandler
                DCD     0
                DCD     TIM16_IRQHandler
                DCD     TIM17_IRQHandler
                DCD     I2C1_IRQHandler
                DCD     I2C2_IRQHandler
                DCD     SPI1_IRQHandler
                DCD     SPI2_IRQHandler
                DCD     USART1_IRQHandler
                DCD     USART2_IRQHandler
                DCD     0
                DCD     0
                DCD     0

__Vectors_End
__Vectors_Size  EQU     __Vectors_End - __Vectors

                AREA    |.text|, CODE, READONLY

Reset_Handler   PROC
                EXPORT  Reset_Handler [WEAK]
                IMPORT  __main
                IMPORT  SystemInit
                LDR     R0, =SystemInit
                BLX     R0
                LDR     R0, =__main
                BX      R0
                ENDP

NMI_Handler     PROC
                EXPORT  NMI_Handler [WEAK]
                B       .
                ENDP

HardFault_Handler PROC
                EXPORT  HardFault_Handler [WEAK]
                B       .
                ENDP

SVC_Handler     PROC
                EXPORT  SVC_Handler [WEAK]
                B       .
                ENDP

PendSV_Handler  PROC
                EXPORT  PendSV_Handler [WEAK]
                B       .
                ENDP

SysTick_Handler PROC
                EXPORT  SysTick_Handler [WEAK]
                B       .
                ENDP

Default_Handler PROC
                EXPORT  WWDG_IRQHandler [WEAK]
                EXPORT  RTC_TAMP_IRQHandler [WEAK]
                EXPORT  FLASH_IRQHandler [WEAK]
                EXPORT  RCC_IRQHandler [WEAK]
                EXPORT  EXTI0_1_IRQHandler [WEAK]
                EXPORT  EXTI2_3_IRQHandler [WEAK]
                EXPORT  EXTI4_15_IRQHandler [WEAK]
                EXPORT  DMA1_Channel1_IRQHandler [WEAK]
                EXPORT  DMA1_Channel2_3_IRQHandler [WEAK]
                EXPORT  DMA1_Ch4_5_DMAMUX1_OVR_IRQHandler [WEAK]
                EXPORT  ADC1_IRQHandler [WEAK]
                EXPORT  TIM1_BRK_UP_TRG_COM_IRQHandler [WEAK]
                EXPORT  TIM1_CC_IRQHandler [WEAK]
                EXPORT  TIM3_IRQHandler [WEAK]
                EXPORT  TIM14_IRQHandler [WEAK]
                EXPORT  TIM16_IRQHandler [WEAK]
                EXPORT  TIM17_IRQHandler [WEAK]
                EXPORT  I2C1_IRQHandler [WEAK]
                EXPORT  I2C2_IRQHandler [WEAK]
                EXPORT  SPI1_IRQHandler [WEAK]
                EXPORT  SPI2_IRQHandler [WEAK]
                EXPORT  USART1_IRQHandler [WEAK]
                EXPORT  USART2_IRQHandler [WEAK]

WWDG_IRQHandler
RTC_TAMP_IRQHandler
FLASH_IRQHandler
RCC_IRQHandler
EXTI0_1_IRQHandler
EXTI2_3_IRQHandler
EXTI4_15_IRQHandler
DMA1_Channel1_IRQHandler
DMA1_Channel2_3_IRQHandler
DMA1_Ch4_5_DMAMUX1_OVR_IRQHandler
ADC1_IRQHandler
TIM1_BRK_UP_TRG_COM_IRQHandler
TIM1_CC_IRQHandler
TIM3_IRQHandler
TIM14_IRQHandler
TIM16_IRQHandler
TIM17_IRQHandler
I2C1_IRQHandler
I2C2_IRQHandler
SPI1_IRQHandler
SPI2_IRQHandler
USART1_IRQHandler
USART2_IRQHandler
                B       .
                ENDP

                ALIGN

                IF      :DEF:__MICROLIB

                EXPORT  __initial_sp
                EXPORT  __heap_base
                EXPORT  __heap_limit

                ELSE

                IMPORT  __use_two_region_memory
                EXPORT  __user_initial_stackheap

__user_initial_stackheap
                LDR     R0, =Heap_Mem
                LDR     R1, =(Stack_Mem + Stack_Size)
                LDR     R2, =(Heap_Mem + Heap_Size)
                LDR     R3, =Stack_Mem
                BX      LR

                ALIGN
                ENDIF

                END
