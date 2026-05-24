// Copyright © 2026 Shiomachi Software. All rights reserved.
#ifndef IOT_H
#define IOT_H

// [define]
#define IOT_JSON_MAX_PAIRS 32 // JSONデータから抽出するKey-Valueの最大ペア数
#define IOT_SYSTEM_DATA_NUM 4 // JSONに含まれるシステム管理情報(FW_Name, FW_Ver, BoardID, DeviceName)の項目数

// 共通バッファサイズ・定数
#define IOT_JSON_BUF_SIZE           (NW_CONFIG_DEVICE_NAME_SIZE + 4096) // JSON文字列のバッファサイズ(byte)
#define IOT_DEFAULT_IS_WIFI         1    // デフォルトの通信モード 0:BLE 1:WiFi
#define IOT_SENSOR_NAME_SIZE        32   // センサー名(JSONキー)の最大サイズ(byte)
#define IOT_DISPLAY_NAME_BUF_SIZE   (NW_CONFIG_DEVICE_NAME_SIZE + FW_NAME_BUF_SIZE + 16) // ホスト名のバッファサイズ
#define IOT_JSON_KEY_BUF_SIZE       IOT_SENSOR_NAME_SIZE // JSONから抽出したキーのバッファサイズ
#define IOT_JSON_VAL_BUF_SIZE       64                           // JSONから抽出した値のバッファサイズ
#define IOT_SENSOR_GET_PERIOD_US    2500000ULL                   // センサデータの取得周期(2.5秒)
#define IOT_NETWORK_SEND_PERIOD_US  (IOT_SENSOR_GET_PERIOD_US * 2) // Push送信周期(取得周期の整数倍)

// [列挙体]
typedef enum _E_IOT_COND {
    IOT_COND_EQ = 0,    // アラート監視の比較条件: ==
    IOT_COND_GE = 1,    // アラート監視の比較条件: >=
    IOT_COND_LE = 2     // アラート監視の比較条件: <=
} E_IOT_COND;

#endif