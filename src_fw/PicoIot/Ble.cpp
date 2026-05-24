// Copyright © 2025 Shiomachi Software. All rights reserved.
#include "Common.h"

// [define]
#define NUS_SERVICE_UUID    "6E400001-B5A3-F393-E0A9-E50E24DCCA9E" // Service UUID
#define NUS_RX_CHAR_UUID    "6E400002-B5A3-F393-E0A9-E50E24DCCA9E" // Central -> Peripheral
#define NUS_TX_CHAR_UUID    "6E400003-B5A3-F393-E0A9-E50E24DCCA9E" // Peripheral -> Central
#define BLE_TX_BUF_SIZE     1024 // BLE送信中のデータ競合を防ぐための退避バッファのサイズ(byte)

// [ファイルスコープ変数]
static BLEDevice* f_pDevice = NULL; // 接続デバイス
static hci_con_handle_t f_conHandle; // 接続ハンドル
static uint16_t f_characteristicNotifyHandle;  // Characteristic通知のハンドル
static btstack_context_callback_registration_t f_stNotifiableCallbackContext; // 通知可能コールバックのコンテキスト 
static ST_NOTIFY_DATA f_stNotifyData; // 通知データ
static volatile bool f_isNotifying = false; // 通知処理中か否か
static volatile bool f_isNotifyEnabled = false; // セントラルがNotify(CCCD)を有効にしたか
static uint8_t f_bleTxBuffer[BLE_TX_BUF_SIZE]; // BLE送信中のデータ競合を防ぐための退避バッファ

// アドバタイズデータ
static const uint8_t f_advertisingData[] = {
    0x02, 0x01, 0x06, // Flags: LE General Discoverable Mode, BR/EDR Not Supported
    0x11, 0x07, // Complete List of 128-bit Service Class UUIDs
    // NUS UUID: 6E400001-B5A3-F393-E0A9-E50E24DCCA9E (リトルエンディアンで配置)
    0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, 
    0x93, 0xF3, 0xA3, 0xB5, 0x01, 0x00, 0x40, 0x6E,
    8,                                // Length:「Type + Value」の長さ
    0x09,                             // Type:    Complete Local Name
    'P', 'i', 'c', 'o', 'I', 'o', 't' // Value:   local_name  
};

// [関数プロトタイプ宣言]
static void BLE_DeviceConnectedCallback(BLEStatus status, BLEDevice* pDevice);
static void BLE_DeviceDisconnectedCallback(BLEDevice* pDevice);
static int BLE_CharacteristicWriteCallback(uint16_t value_handle, uint8_t* pBuf, uint16_t size);
static bool BLE_IsNotifying();
static void BLE_CharacteristicNotifiableCallback(void *context);

// BLEのメイン処理
void BLE_SendJsonData(char* pszJson)
{
    if (pszJson != NULL && pszJson[0] != '\0') {
        if (BLE_IsConnected()) { // セントラルと接続済みかつNotify有効
            if (!BLE_IsNotifying()) { // 現在通知処理中でない場合のみ送信(呼び出し元での論理チェック)
                // センサデータをBLE送信
                if (!BLE_ReqToNotify((uint8_t*)pszJson, strlen(pszJson))) {
                    // 送信キューに入らなかった場合でもスキップするのみとする
                } 
            }       
        }
    }

    // BTstackのメイン処理
    BTstack.loop();
}

// BLEを初期化
void BLE_Init()
{
    // コールバック関数の設定
    BTstack.setBLEDeviceConnectedCallback(BLE_DeviceConnectedCallback);
    BTstack.setBLEDeviceDisconnectedCallback(BLE_DeviceDisconnectedCallback);
    BTstack.setGATTCharacteristicWrite(BLE_CharacteristicWriteCallback);

    // GATTデータベースのセットアップ
    // Nordic UART Service (NUS)
    BTstack.addGATTService(new UUID(NUS_SERVICE_UUID));
    BTstack.addGATTCharacteristicDynamic(new UUID(NUS_RX_CHAR_UUID), ATT_PROPERTY_WRITE | ATT_PROPERTY_WRITE_WITHOUT_RESPONSE, 0); // RXは受信処理しないためハンドルの保存は不要
    f_characteristicNotifyHandle = BTstack.addGATTCharacteristicDynamic(new UUID(NUS_TX_CHAR_UUID), ATT_PROPERTY_NOTIFY, 0);

    // Bluetoothの起動とアドバタイズの開始
    BTstack.setup();
    BTstack.setAdvData(sizeof(f_advertisingData), f_advertisingData);
    BTstack.startAdvertising();
}

// セントラルと接続済みかつNotify有効か否か
bool BLE_IsConnected()
{
    return ((f_pDevice != NULL) && f_isNotifyEnabled) ? true : false;
}

// 通知処理中か否かを取得
static bool BLE_IsNotifying()
{
    return f_isNotifying;    
}

// 通知の要求
bool BLE_ReqToNotify(uint8_t *buffer, uint16_t size) 
{
    bool bRet = true;

    // 既に通知要求中の場合は、BTstackのリンクリスト破壊を防ぐためブロックする
    if (f_isNotifying) {
        return false;
    }

    if ((size > 0) && BLE_IsConnected()) {
        // 送信中に元のバッファが書き換えられるのを防ぐため、退避バッファにコピーする
        uint16_t copySize = (size > sizeof(f_bleTxBuffer)) ? sizeof(f_bleTxBuffer) : size;
        memcpy(f_bleTxBuffer, buffer, copySize);

        f_isNotifying = true; // 通知処理中
        memset(&f_stNotifiableCallbackContext, 0, sizeof(f_stNotifiableCallbackContext));
        // コンテキストに通知データを指定
        f_stNotifyData.buffer = f_bleTxBuffer;
        f_stNotifyData.size = copySize;    
        f_stNotifiableCallbackContext.context = (void*)&f_stNotifyData;  
        // 通知可能になると呼ばれるコールバックを指定
        f_stNotifiableCallbackContext.callback = BLE_CharacteristicNotifiableCallback;
        // 通知の要求
        if (0 == att_server_request_to_send_notification(&f_stNotifiableCallbackContext, f_conHandle)) {
            // 本関数は成功を返す 
        }
        else {
            // 本関数は失敗を返す
            f_isNotifying = false;
            bRet = false;
        }
    }
    else {
        // 破棄(本関数は成功を返す) 
    }

    return bRet;
} 

// デバイス接続通知のコールバック
static void BLE_DeviceConnectedCallback(BLEStatus status, BLEDevice* pDevice) 
{
    switch (status) {
    case BLE_STATUS_OK:
        if (pDevice != NULL) {
            f_conHandle = pDevice->getHandle();
        }
        f_pDevice = pDevice;
        break;
    default:
        break;
    }
}
  
// デバイス切断通知のコールバック
static void BLE_DeviceDisconnectedCallback(BLEDevice* pDevice) 
{
    f_pDevice = NULL;
    f_isNotifying = false;
    f_isNotifyEnabled = false;
}

// Characteristic書き込みコールバック
static int BLE_CharacteristicWriteCallback(uint16_t value_handle, uint8_t* pBuf, uint16_t size) 
{
    // データの書き込み先がTX CharacteristicのCCCD(Client Characteristic Configuration Descriptor)の場合
    if (value_handle == f_characteristicNotifyHandle + 1) {
        if (size >= 2) {
            uint16_t cccd = pBuf[0] | (pBuf[1] << 8);
            f_isNotifyEnabled = (cccd & 0x0001) != 0;
        }
    }
    return 0;
}

// 通知可能になると呼ばれるコールバック
static void BLE_CharacteristicNotifiableCallback(void *context)
{
    ST_NOTIFY_DATA* pstNotifyData = (ST_NOTIFY_DATA*)context;

    if (BLE_IsConnected()) { // セントラルと接続済みかつNotify有効
        // 固定の20バイトに制限せず、セントラルとネゴシエーション済みの実際のMTUサイズを最大限活用する。
        // ※att_server_get_mtu() が万が一 3 未満を返した際の uint16_t のアンダーフロー(65535等になるバグ)を防ぐ
        uint16_t actualMtu = att_server_get_mtu(f_conHandle);
        uint16_t currentMtu = (actualMtu > 23) ? (actualMtu - 3) : 20;
        
        uint16_t chunkSize = (pstNotifyData->size > currentMtu) ? currentMtu : pstNotifyData->size;

        // Characteristic通知の実行
        if (0 != att_server_notify(f_conHandle, f_characteristicNotifyHandle, pstNotifyData->buffer, chunkSize)) {
            // 一時的なバッファ不足や不意な切断はBLEで日常的に発生するため、FWエラーにはせず中断する
            f_isNotifying = false;
            return;
        }  

        // 送信済み分だけポインタと残りサイズを更新
        pstNotifyData->buffer += chunkSize;
        pstNotifyData->size -= chunkSize;

        // まだ送信すべきデータが残っている場合は、次のチャンク送信を要求(キューイング)する
        if (pstNotifyData->size > 0) {
            memset(&f_stNotifiableCallbackContext, 0, sizeof(f_stNotifiableCallbackContext));
            f_stNotifiableCallbackContext.context = context;
            f_stNotifiableCallbackContext.callback = BLE_CharacteristicNotifiableCallback;
            if (0 != att_server_request_to_send_notification(&f_stNotifiableCallbackContext, f_conHandle)) {
                f_isNotifying = false;
            }
        } else {
            f_isNotifying = false; // 全ての送信完了
        }
    } else {
        f_isNotifying = false; // 切断時は終了
    }
}
