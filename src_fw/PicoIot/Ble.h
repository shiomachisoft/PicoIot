// Copyright © 2025 Shiomachi Software. All rights reserved.
#ifndef BLE_H
#define BLE_H

// [構造体]
// 通知データ
typedef struct _ST_NOTIFY_DATA {
    uint8_t *buffer;
    uint16_t size;
} ST_NOTIFY_DATA;

// [関数プロトタイプ宣言]
void BLE_Init();
void BLE_SendJsonData(char* pszJson);
bool BLE_ReqToNotify(uint8_t* pBuf, uint16_t size);
bool BLE_IsConnected();

#endif