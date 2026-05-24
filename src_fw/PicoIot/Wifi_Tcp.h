// Copyright © 2024 Shiomachi Software. All rights reserved.
#ifndef WIFI_TCP_H
#define WIFI_TCP_H

// [define]
#define WIFI_DEFAULT_COUNTRY_CODE   "XX"        // カントリーコードのデフォルト値 ※ArduinoのWifiライブラリではカントリーコードは設定できない
#define WIFI_DEFAULT_MY_IP_ADDR     0xC0A80A64  // PicoのIPアドレスのデフォルト値
#define WIFI_DEFAULT_GW             0xC0A80A01  // デフォルトのGW
#define WIFI_DEFAULT_SUBNET         0xFFFFFF00  // デフォルトのサブネットマスク
#define TCP_PORT                    7777        // TCPソケットポート番号

#define NW_CONFIG_COUNTRY_CODE_SIZE 3     // カントリーコードのバッファサイズ(NULL文字含む)
#define NW_CONFIG_IP_ADDR_SIZE      4     // IPアドレス(IPv4)のオクテット数
#define NW_CONFIG_SSID_SIZE         33    // SSIDのバッファサイズ(最大32文字+NULL文字)
#define NW_CONFIG_PASSWORD_SIZE     65    // Wi-Fiパスワードのバッファサイズ(最大64文字+NULL文字)
#define NW_CONFIG_DEVICE_NAME_SIZE  33    // デバイス識別名のバッファサイズ(最大32文字+NULL文字)
#define NW_CONFIG_EMAIL_ADDR_SIZE   256   // メールアドレス(宛先/送信元)のバッファサイズ(NULL文字含む)
#define NW_CONFIG_SMTP_PASSWORD_SIZE 128  // SMTPパスワードのバッファサイズ(NULL文字含む)
#define NW_CONFIG_SMTP_HOST_SIZE    256   // SMTPホスト名のバッファサイズ(NULL文字含む)

// [列挙体]
// フェーズ
typedef enum _E_WIFI_TCP_PHASE {
    E_WIFI_TCP_PHASE_WIFI_NOT_INIT,   // WiFi未初期化
    E_WIFI_TCP_PHASE_WIFI_INITED,     // WiFi初期化済み
    E_WIFI_TCP_PHASE_AP_CONNECTING,   // APへの接続処理を実行中
    E_WIFI_TCP_PHASE_AP_CONNECTED,    // APに接続済み
    E_WIFI_TCP_PHASE_TCP_CONNECTED    // TCP接続済み
} E_WIFI_TCP_PHASE;

#pragma pack(1)

// ネットワーク設定
typedef struct _ST_NW_CONFIG3 {
    UCHAR isWifi;                                       // 通信モードはWiFiモードかBLEモードか
    char  szCountryCode[NW_CONFIG_COUNTRY_CODE_SIZE];   // カントリーコード ※未使用
    UCHAR aMyIpAddr[NW_CONFIG_IP_ADDR_SIZE];            // PicoのIPアドレス
    UCHAR aSubnet[NW_CONFIG_IP_ADDR_SIZE];              // サブネットマスク
    UCHAR aGateway[NW_CONFIG_IP_ADDR_SIZE];             // ゲートウェイ
    char  szSsid[NW_CONFIG_SSID_SIZE];                  // APのSSID
    char  szPassword[NW_CONFIG_PASSWORD_SIZE];          // APのパスワード
} ST_NW_CONFIG3;

// ネットワーク設定(オプション)
typedef struct _ST_NW_CONFIG3_OPTION {
    char  szDeviceName[NW_CONFIG_DEVICE_NAME_SIZE];         // 識別名 
    UCHAR isEmailEnable;                                    // Email機能の有効/無効 (0:Disable, 1:Enable)
    char  szSmtpHostName[NW_CONFIG_SMTP_HOST_SIZE];         // SMTPホスト名 (例: smtp.gmail.com)
    USHORT smtpPort;                                        // SMTPポート番号 (例: 465)
    char  szSmtpUser[NW_CONFIG_EMAIL_ADDR_SIZE];            // SMTPユーザー名(Emailアドレス等)
    char  szSmtpPassword[NW_CONFIG_SMTP_PASSWORD_SIZE];     // SMTPパスワード(Gmailの場合はアプリパスワード)
    char  szRecipientEmail[NW_CONFIG_EMAIL_ADDR_SIZE];      // 宛先メールアドレス
    UCHAR mailIntervalHour;                                 // 定期メール送信間隔(単位:hour)
#ifdef ENABLE_MQTT // MQTT機能は作成中
    UCHAR isMqttEnable;                                     // MQTT機能の有効/無効 (0:Disable, 1:Enable)
    UCHAR aMqttBrokerIpAddr[NW_CONFIG_IP_ADDR_SIZE];        // MQTTブローカーのIPアドレス
#endif
    ST_ALERT_CONFIG astAlertConfig[ALT_MAX];                // アラート設定の配列  
} ST_NW_CONFIG3_OPTION;

#pragma pack()

// [関数プロトタイプ宣言]
void WIFI_GetWifiDefaultConfig(ST_NW_CONFIG3 *pstConfig, ST_NW_CONFIG3_OPTION *pstConfigOption);
void WIFI_Init();
bool WIFI_IsApConnected();
void WIFI_TCP_Main();
void TCP_SendJsonData(char* pszJson);

#endif