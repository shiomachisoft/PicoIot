// Copyright © 2026 Shiomachi Software. All rights reserved.
#ifdef ENABLE_MQTT // MQTT機能は作成中
#include "Common.h"

// [define]
#define MQTT_PORT 1883 // 一般的なMQTTポート
#define MQTT_TOPIC_PREFIX "picoiot/sensor/" // 送信先のトピックのプレフィックス
#define MQTT_RECONNECT_INTERVAL 5000000ULL // us 5秒 再接続試行のインターバル
#define MQTT_BUFFER_SIZE (IOT_JSON_BUF_SIZE + 256) // MQTTの送受信バッファサイズ(生成される最大JSONサイズ + MQTTヘッダ等の余裕分)
#define MQTT_CONNECT_TIMEOUT 2000 // ms ブローカーへの接続タイムアウト
#define MQTT_TOPIC_BUF_SIZE (IOT_DISPLAY_NAME_BUF_SIZE + 64) // トピック名を格納する安全なバッファサイズ

// [ファイルスコープ変数]
static WiFiClient f_mqttWifiClient;                     // MQTT通信用の基盤となるTCPクライアント
static PubSubClient f_mqttClient(f_mqttWifiClient);     // PubSubClientライブラリのインスタンス
static volatile uint64_t f_lastReconnectAttemptUs = 0;  // 最後に再接続を試みた時間(us)
static char f_clientId[IOT_DISPLAY_NAME_BUF_SIZE] = {0};
static char f_topic[MQTT_TOPIC_BUF_SIZE] = {0};

// MQTTの初期化
void MQTT_Init()
{
    ST_FLASH_DATA* pstFlashData = FLASH_GetDataAtPowerOn(); // 電源起動時のFLASHデータを取得
    
    IPAddress brokerIp(pstFlashData->stNwConfigOption.aMqttBrokerIpAddr[0],
                       pstFlashData->stNwConfigOption.aMqttBrokerIpAddr[1], 
                       pstFlashData->stNwConfigOption.aMqttBrokerIpAddr[2],
                       pstFlashData->stNwConfigOption.aMqttBrokerIpAddr[3]);
    
    // 生成されるJSONデータがデフォルトパケットサイズ(256byte)を超える可能性が高いためバッファを拡張
    f_mqttClient.setBufferSize(MQTT_BUFFER_SIZE);

    // 接続時に相手が見つからない場合のブロッキング(フリーズ)時間を最小限にするため、タイムアウトを短縮
    f_mqttWifiClient.setTimeout(MQTT_CONNECT_TIMEOUT);

    // ブローカーとポートを設定
    f_mqttClient.setServer(brokerIp, MQTT_PORT);
}

// MQTTのメイン処理
void MQTT_Main(char* pszJson)
{
    volatile uint64_t currentUs;

    if (!WIFI_IsApConnected()) { // APと接続済みでない場合
        return;
    }

    ST_FLASH_DATA* pstFlashData = FLASH_GetDataAtPowerOn();

    // MQTTが無効な場合、または有効でもIPアドレスが未設定(0.0.0.0)の場合、
    // 無駄な接続タイムアウトによるWebサーバー等の動作遅延(フリーズ)を防ぐため処理をスキップする。
    if (!pstFlashData->stNwConfigOption.isMqttEnable ||
        (pstFlashData->stNwConfigOption.aMqttBrokerIpAddr[0] == 0 && pstFlashData->stNwConfigOption.aMqttBrokerIpAddr[1] == 0 &&
         pstFlashData->stNwConfigOption.aMqttBrokerIpAddr[2] == 0 && pstFlashData->stNwConfigOption.aMqttBrokerIpAddr[3] == 0)) {
        return; 
    }

    // [クライアントIDとトピック名のキャッシュと一意性の保証]
    // 毎ループ(10ms間隔)の文字列評価によるCPU負荷を下げるため、初回のみ生成して保持する。
    if (f_clientId[0] == '\0') {
        // 複数台設置してもMQTTブローカー上でIDが衝突(フラッピング)しないよう、
        // Pico固有のボードIDを使用して "PicoIot_A1B2C3" のような一意な文字列を生成する。
        pico_unique_board_id_t board_id;
        pico_get_unique_board_id(&board_id);
        snprintf(f_clientId, sizeof(f_clientId), "%s_%02X%02X%02X", 
                 FW_NAME, board_id.id[5], board_id.id[6], board_id.id[7]);

        // トピック名も再起動まで不変なので初回のみ生成してキャッシュ
        snprintf(f_topic, sizeof(f_topic), "%s%s", MQTT_TOPIC_PREFIX, f_clientId);
    }

    if (!f_mqttClient.connected()) { // ブローカーに未接続の場合
        // [ノンブロッキング再接続処理]
        // 接続に失敗した直後に連続でconnect()を呼ぶとシステムがフリーズしてしまうため、
        // time_us_64()を使ったタイマー判定でMQTT_RECONNECT_INTERVAL(5秒)ごとに接続を試みる。
        currentUs = time_us_64();
        if (currentUs - f_lastReconnectAttemptUs > MQTT_RECONNECT_INTERVAL || f_lastReconnectAttemptUs == 0) {
            f_lastReconnectAttemptUs = currentUs;
            
            // ブローカーに接続を試みる
            if (f_mqttClient.connect(f_clientId)) {
            }
        }
    } 
    
    if (f_mqttClient.connected()) { // ブローカーに接続中の場合
        // [MQTTの定期処理]
        // これを定期的に呼ばないと、サーバーへのPING(Keep-alive)が送信されずにタイムアウトで切断されてしまう。
        f_mqttClient.loop(); // MQTTの定期処理やキープアライブを維持
        
        if (pszJson != NULL && pszJson[0] != '\0') { // センサデータの送信タイミングの場合
            // JSONデータを指定したトピックにパブリッシュ(送信)する。
            // 第3引数に true (Retainフラグ) を指定し、後から接続したダッシュボード等でも即座に最新値を受信できるようにする。
            if (!f_mqttClient.publish(f_topic, pszJson, true)) {
                // 送信に失敗した場合はFWエラー(無線送信エラー)を設定する
                CMN_SetErrorBits(CMN_ERR_BIT_WL_SEND_ERR, true);
            }
        }
    }
}

#endif // ENABLE_MQTT