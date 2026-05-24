// Copyright © 2024 Shiomachi Software. All rights reserved.
#include "Common.h"

// [define]
#define TCP_CONNECT_AP_INTERVAL 1000000ULL   // us 1秒 APとの接続失敗が確定した場合、この時間を待ってからフェーズをE_WIFI_TCP_PHASE_WIFI_INITEDに戻す
#define TCP_CONNECT_AP_TIMEOUT  10000000ULL // us 10秒 APとの接続試行中、この時間が経過しても接続できない場合、フェーズをE_WIFI_TCP_PHASE_WIFI_INITEDに戻す
#define TCP_CONNECT_AP_KEEP     5000000ULL  // us 5秒 この時間の間、APとの接続が維持できていれば、接続成功とみなす ※WiFi.begin直後に未接続状態でもWiFi.status()が一時的にWL_CONNECTEDを返す場合がある問題への対策。

// [ファイルスコープ変数]
static WiFiClient f_tcpClient;           // TCPクライアント
static WiFiServer f_tcpServer(TCP_PORT); // TCPサーバー
static E_WIFI_TCP_PHASE f_ePhase = E_WIFI_TCP_PHASE_WIFI_INITED; // フェーズ
static volatile uint64_t f_startUs_tryConnectToAp = 0;
static volatile uint64_t f_startUs_keepConnectWithAp = 0;
static bool f_isTcpServerBegun = false; // TCPサーバーを開始済みか否か
static char f_hostNameBuf[IOT_DISPLAY_NAME_BUF_SIZE]; // ホスト名一時バッファ

// [関数プロトタイプ宣言]
static void TCP_TryConnectAp(ST_FLASH_DATA* pstFlashData);
static bool TCP_IsApConnectCompleted();
static void TCP_CheckApDisconnected();
static void TCP_Close(bool bInited);
static bool TCP_IsConnected();
static bool TCP_SendData(uint8_t* buffer, uint32_t size);

// WiFiとTCPソケット通信のメイン処理
void WIFI_TCP_Main()
{
    WiFiClient client;
    ST_FLASH_DATA* pstFlashData = FLASH_GetDataAtPowerOn(); // 電源起動時のFLASHデータを取得

    // APとの接続が切れていないかを確認する
    TCP_CheckApDisconnected();  

    // フェーズ
    switch (f_ePhase)
    {
        case E_WIFI_TCP_PHASE_WIFI_INITED: // WiFi初期化済み
            // APへの接続を実行
            TCP_TryConnectAp(pstFlashData);
            f_ePhase = E_WIFI_TCP_PHASE_AP_CONNECTING; // APへの接続処理を実行中        
            break;
        case E_WIFI_TCP_PHASE_AP_CONNECTING: // APへの接続処理を実行中 
            // APとの接続が成功したかを確認する
            if (TCP_IsApConnectCompleted()) {
                f_ePhase = E_WIFI_TCP_PHASE_AP_CONNECTED; // APに接続済み
                
                // HTTPサーバーを開始
                HTTP_Begin(); 
            }         
            break;
        case E_WIFI_TCP_PHASE_AP_CONNECTED: // APに接続済み
            // [TCPソケット通信の接続処理]          
            // TCPサーバーを開始
            if (!f_isTcpServerBegun) {
                f_tcpServer.begin();   
                f_isTcpServerBegun = true;
            }                    
            // アクセプト
            client = f_tcpServer.accept();
            if (client) {
                f_tcpClient = client;  
                f_tcpClient.setTimeout(1000); // 悪意のある/遅延するクライアントによる送信時のブロッキング(フリーズ)を防ぐためのタイムアウト設定
                f_ePhase = E_WIFI_TCP_PHASE_TCP_CONNECTED; // TCP接続済み          
            } 
            break;
        case E_WIFI_TCP_PHASE_TCP_CONNECTED: // TCP接続済み
            if (!f_tcpClient.connected()) { // TCP接続が切れている場合
                TCP_Close(false); // フェーズをE_WIFI_TCP_PHASE_AP_CONNECTEDに戻す
            }
            else {
                // [LwIPの受信バッファ枯渇対策]
                // TCPクライアントから予期せぬデータが送られてきた場合、読み捨てないと受信バッファが詰まりフリーズや切断の原因となる
                while (f_tcpClient.available()) {
                    f_tcpClient.read();
                }
                
                // すでに1台のクライアントと接続中に別の新規接続要求が来た場合、
                // 同時に複数台接続させないために新しい接続を即座に切断(拒否)する
                client = f_tcpServer.accept();
                if (client) {
                    client.stop();
                    client = WiFiClient(); // オブジェクトを確実にクリアしてソケットリソースの枯渇を防ぐ
                }
            }
            break;
        default:
            break;
    }   
}

// JSONデータのTCPソケット送信のメイン処理
void TCP_SendJsonData(char* pszJson)
{
    if (pszJson != NULL && pszJson[0] != '\0') {
        if (WIFI_IsApConnected()) {  // APと接続済みの場合   
            // JSONデータをTCPソケット送信
            if (TCP_IsConnected()) { 
                if (!TCP_SendData((uint8_t*)pszJson, strlen(pszJson))) {
                    // FWエラーを設定
                    CMN_SetErrorBits(CMN_ERR_BIT_WL_SEND_ERR, true);
                } 
            }
        }
    }
}

// APへの接続を実行
static void TCP_TryConnectAp(ST_FLASH_DATA* pstFlashData)
{
    if (pstFlashData->stNwConfig.szSsid[0] != '\0') {
        // SSIDが空白ではない場合

        // WPA/WPA2ネットワークに接続する
        WiFi.begin(pstFlashData->stNwConfig.szSsid, pstFlashData->stNwConfig.szPassword);
        f_startUs_tryConnectToAp = time_us_64();   
        f_startUs_keepConnectWithAp = 0; 
    }
}

// APとの接続が成功したかを確認する
static bool TCP_IsApConnectCompleted() 
{
    int status;
    bool bRet = false;
    volatile uint64_t currentUs;
    volatile uint64_t diffUs;
    volatile uint64_t threshold;

    currentUs = time_us_64();

    status = WiFi.status();
    if (WL_CONNECTED == status) {
        // ※WiFi.begin直後に未接続状態でもWiFi.status()が一時的にWL_CONNECTEDを返す場合がある問題への対策。
        threshold = TCP_CONNECT_AP_KEEP; 
        if (0 == f_startUs_keepConnectWithAp) {
            f_startUs_keepConnectWithAp = currentUs;
        }
        diffUs = currentUs - f_startUs_keepConnectWithAp; 
        if (diffUs >= threshold) { 
            bRet = true; // APとの接続は成功している
        }    
    }  
    else { // APとまだ接続できていない

        f_startUs_keepConnectWithAp = 0;

        switch (status) {
            // 接続失敗が確定
            case WL_CONNECT_FAILED:
            case WL_NO_SSID_AVAIL:
            case WL_CONNECTION_LOST:
            //case WL_DISCONNECTED:
            case WL_NO_SHIELD:
                threshold = TCP_CONNECT_AP_INTERVAL;
                break;
            // 接続を試み中  
            default:
                threshold = TCP_CONNECT_AP_TIMEOUT;
                break;
        }
        diffUs = currentUs - f_startUs_tryConnectToAp;    
        if (diffUs >= threshold) {
            TCP_Close(true); // フェーズをE_WIFI_TCP_PHASE_WIFI_INITEDに戻す
        }   
    }

    return bRet;
}

// APと接続済みか否かを取得
bool WIFI_IsApConnected()
{
    return (f_ePhase >= E_WIFI_TCP_PHASE_AP_CONNECTED) ? true : false;
}

// APとの接続が切れていないかを確認する
static void TCP_CheckApDisconnected()
{
    int status;

    if (f_ePhase >=  E_WIFI_TCP_PHASE_AP_CONNECTED) {
        status = WiFi.status();
        if (status != WL_CONNECTED) { // APとの接続が切れている場合
            TCP_Close(true); // フェーズをE_WIFI_TCP_PHASE_WIFI_INITEDに戻す
        }
    }
}

// TCP接続済みか否かを取得
static bool TCP_IsConnected()
{
    return (E_WIFI_TCP_PHASE_TCP_CONNECTED == f_ePhase) ? true : false;
}

// TCPソケットを切断
static void TCP_Close(bool bInit) 
{
    f_tcpClient.stop();
    f_tcpClient = WiFiClient(); // オブジェクトをクリアしてリソースを確実に解放する
    
    if (bInit) {
        f_tcpServer.stop();
        f_isTcpServerBegun = false;
        f_ePhase = E_WIFI_TCP_PHASE_WIFI_INITED; // フェーズをE_WIFI_TCP_PHASE_WIFI_INITEDに戻す
    }
    else {
        f_ePhase = E_WIFI_TCP_PHASE_AP_CONNECTED; // フェーズをE_WIFI_TCP_PHASE_AP_CONNECTEDに戻す
    }
}

// TCPソケット送信
static bool TCP_SendData(uint8_t* buffer, uint32_t size)
{
    bool bRet = true;
    size_t written = 0;
    uint32_t startMs = millis();

    if ((size > 0) && TCP_IsConnected()) {
        // [送信バッファ溢れ対策と分割送信]
        // マイコンの送信バッファ空き容量以上のデータを一括でwrite()しようとすると、
        // 送信しきれなかった分が破棄されて通信不良となるため、空きを確認しながら分割送信する。
        while (written < size) {
            if (!f_tcpClient.connected() || (millis() - startMs > 1000)) {
                bRet = false;
                break;
            }
            
            size_t chunk = f_tcpClient.availableForWrite();
            if (chunk > 0) {
                if (chunk > (size - written)) chunk = size - written;
                size_t ret = f_tcpClient.write(buffer + written, chunk);
                if (ret == 0) {
                    bRet = false;
                    break;
                }
                written += ret;
                startMs = millis(); // 送信が進めばタイムアウトを延長
            } else {
                delay(1); // 空きができるまで微小待機
            }
        }

        if (!bRet) { // タイムアウト等で全データ送信できなかった場合は異常とみなして切断する
            TCP_Close(false);
        }
    }
    else {
        // 破棄(本関数は成功を返す)
    }
        
    return bRet;
}

// ST_NW_CONFIG3構造体とST_NW_CONFIG3_OPTION構造体にデフォルト値を格納
void WIFI_GetWifiDefaultConfig(ST_NW_CONFIG3 *pstConfig, ST_NW_CONFIG3_OPTION *pstConfigOption)
{
    // ST_NW_CONFIG3構造体
    memset(pstConfig, 0, sizeof(ST_NW_CONFIG3));

    pstConfig->isWifi = IOT_DEFAULT_IS_WIFI;
    strcpy(pstConfig->szCountryCode, WIFI_DEFAULT_COUNTRY_CODE);
    pstConfig->aMyIpAddr[0] = (WIFI_DEFAULT_MY_IP_ADDR >> 24) & 0xFF;
    pstConfig->aMyIpAddr[1] = (WIFI_DEFAULT_MY_IP_ADDR >> 16) & 0xFF;
    pstConfig->aMyIpAddr[2] = (WIFI_DEFAULT_MY_IP_ADDR >> 8) & 0xFF;
    pstConfig->aMyIpAddr[3] = (WIFI_DEFAULT_MY_IP_ADDR) & 0xFF;
    pstConfig->aSubnet[0] = (WIFI_DEFAULT_SUBNET >> 24) & 0xFF;
    pstConfig->aSubnet[1] = (WIFI_DEFAULT_SUBNET >> 16) & 0xFF;
    pstConfig->aSubnet[2] = (WIFI_DEFAULT_SUBNET >> 8) & 0xFF;
    pstConfig->aSubnet[3] = (WIFI_DEFAULT_SUBNET) & 0xFF;
    pstConfig->aGateway[0] = (WIFI_DEFAULT_GW >> 24) & 0xFF;
    pstConfig->aGateway[1] = (WIFI_DEFAULT_GW >> 16) & 0xFF;
    pstConfig->aGateway[2] = (WIFI_DEFAULT_GW >> 8) & 0xFF;
    pstConfig->aGateway[3] = (WIFI_DEFAULT_GW) & 0xFF;
    memset(pstConfig->szSsid, 0, sizeof(pstConfig->szSsid));
    memset(pstConfig->szPassword, 0, sizeof(pstConfig->szPassword));
    
    // ST_NW_CONFIG3_OPTION構造体
    memset(pstConfigOption, 0, sizeof(ST_NW_CONFIG3_OPTION));
    
    strcpy(pstConfigOption->szSmtpHostName, EMAIL_GMAIL_SMTP_HOST);
    pstConfigOption->smtpPort = EMAIL_GMAIL_SMTP_PORT;
    memset(pstConfigOption->szSmtpUser, 0, sizeof(pstConfigOption->szSmtpUser));
    memset(pstConfigOption->szSmtpPassword, 0, sizeof(pstConfigOption->szSmtpPassword));
    memset(pstConfigOption->szRecipientEmail, 0, sizeof(pstConfigOption->szRecipientEmail));
    pstConfigOption->mailIntervalHour = EMAIL_DEFAULT_INTERVAL_HOUR;
    pstConfigOption->isEmailEnable = 0; // デフォルトは 0:Disable
#ifdef ENABLE_MQTT // MQTT機能は作成中
    pstConfigOption->isMqttEnable = 0;  // デフォルトは 0:Disable
    pstConfigOption->aMqttBrokerIpAddr[0] = 0;
    pstConfigOption->aMqttBrokerIpAddr[1] = 0;
    pstConfigOption->aMqttBrokerIpAddr[2] = 0;
    pstConfigOption->aMqttBrokerIpAddr[3] = 0;
#endif
    memset(pstConfigOption->astAlertConfig, 0, sizeof(pstConfigOption->astAlertConfig));
}  

// WiFiを初期化
void WIFI_Init()
{
    ST_FLASH_DATA* pstFlashData = FLASH_GetDataAtPowerOn();
    pico_unique_board_id_t board_id;
    static String s_wifiHostName;

    WiFi.mode(WIFI_STA);
    
    pico_get_unique_board_id(&board_id);
    snprintf(f_hostNameBuf, sizeof(f_hostNameBuf), "%s-%02X%02X%02X", FW_NAME, board_id.id[5], board_id.id[6], board_id.id[7]);
    s_wifiHostName = f_hostNameBuf;
    s_wifiHostName.toLowerCase(); // ホスト名のトラブル防止のため全て小文字に統一
    
    WiFi.setHostname(s_wifiHostName.c_str());

    // 静的IPアドレスを設定
    IPAddress ip(pstFlashData->stNwConfig.aMyIpAddr[0],
                 pstFlashData->stNwConfig.aMyIpAddr[1], 
                 pstFlashData->stNwConfig.aMyIpAddr[2],
                 pstFlashData->stNwConfig.aMyIpAddr[3]); 

    IPAddress subnet(pstFlashData->stNwConfig.aSubnet[0],
                     pstFlashData->stNwConfig.aSubnet[1],
                     pstFlashData->stNwConfig.aSubnet[2],
                     pstFlashData->stNwConfig.aSubnet[3]);
                     
    IPAddress gateway(pstFlashData->stNwConfig.aGateway[0],
                      pstFlashData->stNwConfig.aGateway[1],
                      pstFlashData->stNwConfig.aGateway[2],
                      pstFlashData->stNwConfig.aGateway[3]);

    // ネットワーク設定を適用 (Arduino標準/Pico Wの引数順序: IP, DNS, Gateway, Subnet)
    WiFi.config(ip, gateway/*DNS*/, gateway, subnet);
}
