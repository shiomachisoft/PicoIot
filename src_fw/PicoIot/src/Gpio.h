// Copyright © 2024 Shiomachi Software. All rights reserved.
#ifndef GPIO_H
#define GPIO_H

#include "../Common.h"

// [define]
// -----------------------------------------------------------------------------
// GP番号
// -----------------------------------------------------------------------------
// [I2C通信用ピン (BME280センサー用)]
// ※bme280_pico.h内で定義・使用されるため、ここでは定義を省略(コメントアウト)している
//#define GP_8    8   // I2C0 SDA
//#define GP_9    9   // I2C0 SCL

// [汎用デジタル入出力ピン]
#define GP_10   10  // デジタル入出力
#define GP_11   11  // デジタル入出力
#define GP_12   12  // デジタル入出力
#define GP_13   13  // デジタル入出力
#define GP_14   14  // デジタル入出力
#define GP_15   15  // デジタル入出力

// [アナログ入力ピン (ADC)]
#define GP_26   26  // アナログ入力 (ADC0)
#define GP_27   27  // アナログ入力 (ADC1)
#define GP_28   28  // アナログ入力 (ADC2)

// [構造体]
// GPIOのGP番号と方向
typedef struct _ST_GPIO_PIN{
    ULONG gp;  // GP番号
    bool  dir; // true:出力 false:入力
} ST_GPIO_PIN;

// GPIO設定
typedef struct _ST_GPIO_CONFIG {
   ULONG pullDownBits;   // プルダウンかプルアップか 
   ULONG initialValBits; // 電源ON時出力値
} ST_GPIO_CONFIG;

// [関数プロトタイプ宣言]
void GPIO_GetDefaultConfig(ST_GPIO_CONFIG *pstConfig);
void GPIO_Init(ST_GPIO_CONFIG *pstConfig);

#endif