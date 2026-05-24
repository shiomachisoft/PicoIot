// Copyright © 2024 Shiomachi Software. All rights reserved.
#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <time.h>
#include <math.h>

#include <pico/stdlib.h>
#include <pico/binary_info.h>
#include <hardware/gpio.h>
#include <hardware/adc.h>
#include <hardware/uart.h>
#include <hardware/spi.h>
#include <hardware/dma.h>
#include <hardware/i2c.h>
#include <hardware/pwm.h>
#include <pico/multicore.h>
#include <hardware/flash.h>
#include <class/cdc/cdc_device.h>
#include <pico/unique_id.h>
#include <hardware/pll.h>
#include <hardware/clocks.h>
#include <hardware/structs/pll.h>
#include <hardware/structs/clocks.h>
#include <hardware/watchdog.h>
#include <hardware/resets.h>
#include <pico/bootrom.h>
#include <hardware/exception.h>

#include <Arduino.h>
#include <WiFi.h>
#include <ESP_Mail_Client.h>
#include <BTstackLib.h>
#include <ble/att_server.h>
#ifdef ENABLE_MQTT // MQTT機能は作成中
#include <PubSubClient.h>
#endif

#include "Type.h"
#include "Ver.h"
#include "Timer.h"
#include "Frame.h"
#include "src/Gpio.h"
#include "Cmd.h"
#include "src/Sensor2Json.h"
#include "Ble.h"
#include "Iot.h"
#include "src/Alert.h"
#include "Wifi_Tcp.h"
#include "Flash.h"
#include "Http.h"
#include "Email.h"
#include "src/Bme280/bme280_pico.h"
#include "Mqtt.h"

// [define]

// FWエラービット
#define CMN_ERR_BIT_WDT_RESET                       (1 << 0)  // WDTタイムアウトでマイコンがリセットした
#define CMN_ERR_BIT_UART_FRAMING_ERR                (1 << 1)  // UART: Framing error / UART:Framing error     
#define CMN_ERR_BIT_UART_PARITY_ERR                 (1 << 2)  // UART: Parity error / UART:Parity error
#define CMN_ERR_BIT_UART_BREAK_ERR                  (1 << 3)  // UART: Break error / UART:Break error
#define CMN_ERR_BIT_UART_OVERRUN_ERR                (1 << 4)  // UART: Overrun error / UART:Overrun error
#define CMN_ERR_BIT_BUF_SIZE_NOT_ENOUGH_USB_WL_SEND (1 << 7)  // バッファに空きがないので要求データを破棄した(USB/無線送信)
#define CMN_ERR_BIT_BUF_SIZE_NOT_ENOUGH_UART_SEND   (1 << 8)  // バッファに空きがないので要求データを破棄した(UART送信)
#define CMN_ERR_BIT_BUF_SIZE_NOT_ENOUGH_UART_RECV   (1 << 9)  // バッファに空きがないので要求データを破棄した(UART受信)
#define CMN_ERR_BIT_BUF_SIZE_NOT_ENOUGH_WL_RECV     (1 << 11) // バッファに空きがないので要求データを破棄した(無線受信)
#define CMN_ERR_BIT_WL_SEND_ERR                     (1 << 12) // 無線送信が失敗した

// [関数プロトタイプ宣言]
void CMN_WdtEnableReboot();
void CMN_WdtRebootWithoutEnable();
void CMN_EnterSpinLock();
void CMN_ExitSpinLock();
void CMN_SetErrorBits(ULONG errorBit, bool bSpinLock);
ULONG CMN_GetFwErrorBits();
void CMN_ClearFwErrorBits(bool bSpinLock);
bool CMN_Checksum(PVOID pBuf, USHORT expect, ULONG size);
USHORT CMN_CalcChecksum(PVOID pBuf, ULONG size);
char* CMN_Strncpy(char* dest, const char* src, size_t n);
void CMN_Init();

#endif
