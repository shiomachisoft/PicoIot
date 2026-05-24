// Copyright © 2026 Shiomachi Software. All rights reserved.
#ifndef ALERT_H
#define ALERT_H

#include "../Common.h"

// [define]
#define ALT_MAX                 (IOT_JSON_MAX_PAIRS - IOT_SYSTEM_DATA_NUM) // アラート機能の最大設定数
#define ALT_REASON_SIZE         (IOT_JSON_KEY_BUF_SIZE + IOT_JSON_VAL_BUF_SIZE + 32) // 各アラートのトリガー理由を格納する文字列バッファのサイズ
#define ALT_REASON_BUF_SIZE     (ALT_REASON_SIZE * ALT_MAX + 128) // 複数のアラートが同時発生した場合に連結して格納する全体の理由文字列バッファサイズ

#pragma pack(1)

// [構造体]
// 個別のアラート設定
typedef struct _ST_ALERT_CONFIG {
    UCHAR isEnable;           // アラート機能の有効/無効 (0:無効, 1:有効)
    char  szSensorName[IOT_SENSOR_NAME_SIZE]; // センサー名 (例: "BME280_temp[degC]")
    UCHAR condition;          // アラート条件 (E_IOT_COND)
    float threshold;          // 閾値
} ST_ALERT_CONFIG;
#pragma pack()

// [関数プロトタイプ宣言]
void ALT_Main(char* pszJson);

#endif