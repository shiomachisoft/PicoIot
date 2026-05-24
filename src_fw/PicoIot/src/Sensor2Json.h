// Copyright © 2024 Shiomachi Software. All rights reserved.
#ifndef SENSOR_H
#define SENSOR_H

#include "../Common.h"

// [define]
#define S2J_ADC_CH_NUM 4        // ADCのチャンネル数(温度センサ含む)
#define S2J_BME280_DATA_NUM 3   // BME280のデータの数(温度・気圧・湿度)
#define S2J_GP_NUM 6            // GP10～GP15の数

// [構造体]
// 全センサデータ
typedef struct _ST_SENSOR_DATA {
    float aAdcData[S2J_ADC_CH_NUM];         // ADC0・ADC1・ADC2・ADC4
    float aBme280Data[S2J_BME280_DATA_NUM]; // BME280の温度・気圧・湿度
    uint8_t aLvl_gp[S2J_GP_NUM];            // GP10～GP15のレベル: 0 or 1
} ST_SENSOR_DATA;

// [関数プロトタイプ宣言]
void S2J_Init();
char* S2J_CreateJsonData();

#endif