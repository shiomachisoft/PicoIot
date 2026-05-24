// Copyright © 2026 Shiomachi Software. All rights reserved.
#include "../Common.h"

// [define]
#define ALT_HOUR_US (1000000ULL * 3600ULL) // 1時間あたりのマイクロ秒数(us)
#define ALT_ALERT_COOLDOWN_TIME (1000000ULL * 300ULL) // アラートの連続送信を防止するクールダウン時間(us)。5分 = 300秒。

// [ファイルスコープ変数]
static char f_reason[ALT_REASON_SIZE];                          // 個別のアラート条件ごとのトリガー理由を生成する際の一時バッファ
static char f_emailReasonBuf[ALT_REASON_BUF_SIZE];              // Email通知のトリガーとなった具体的な理由をまとめた文字列バッファ
#ifdef ENABLE_MQTT
static char f_mqttReasonBuf[ALT_REASON_BUF_SIZE];               // MQTT通知のトリガーとなった具体的な理由をまとめた文字列バッファ
#endif
static char f_keys[IOT_JSON_MAX_PAIRS][IOT_JSON_KEY_BUF_SIZE];  // JSONデータからパースした各キーの配列
static char f_vals[IOT_JSON_MAX_PAIRS][IOT_JSON_VAL_BUF_SIZE];  // JSONデータからパースした各センサー測定値の配列
static char f_jsonBuf[IOT_JSON_BUF_SIZE];                       // JSONデータのパースのための一時バッファ

static bool f_isAlertConditionMet[ALT_MAX] = {false};           // 各アラート設定における、現在の条件合致状態（エッジ検出用）
static volatile uint64_t f_previousUs_emailAlertSend[ALT_MAX] = {0}; // Email用：前回の通知時刻(us)。クールダウン判定に使用
static bool f_isEmailAlertPending[ALT_MAX] = {false};                // Email用：アラート送信保留（待ち）状態
#ifdef ENABLE_MQTT
static volatile uint64_t f_previousUs_mqttAlertSend[ALT_MAX] = {0};  // MQTT用：前回の通知時刻(us)
static bool f_isMqttAlertPending[ALT_MAX] = {false};                 // MQTT用：アラート送信保留（待ち）状態
#endif

// [関数プロトタイプ宣言]
static int ALT_ParseJson(const char* pszJson);
static void ALT_CheckAlert(ST_FLASH_DATA* pstFlashData, volatile uint64_t currentUs, int pairCount, char keys[][IOT_JSON_KEY_BUF_SIZE], char vals[][IOT_JSON_VAL_BUF_SIZE], char* emailReasonBuf, size_t emailReasonBufSize, bool canSendEmail, bool* pIsTriggerEmail
#ifdef ENABLE_MQTT
    , char* mqttReasonBuf, size_t mqttReasonBufSize, bool canSendMqtt, bool* pIsTriggerMqtt
#endif
);

// -----------------------------------------------------------------------------
// トリガー監視のメイン処理
// センサデータの更新ごとに呼び出され、アラート条件の判定とEメール通知、
// およびEメール定期送信のタイミング制御と実行を行う。
// -----------------------------------------------------------------------------
void ALT_Main(char* pszJson)
{
    volatile uint64_t currentUs = time_us_64();              // 現在のシステム時刻(マイコン起動からの経過マイクロ秒)
    volatile uint64_t diffUs_interval;                       // 前回の定期送信からの経過時間(マイクロ秒)
    ST_FLASH_DATA* pstFlashData = FLASH_GetDataAtPowerOn();  // 電源起動時のFLASHデータ
    static bool s_isFirstRegularSent = false;                // 起動後、1回目の定期メール送信が完了したかを示すフラグ
    static volatile uint64_t s_previousUs_send = 0;          // 前回の定期メール送信完了時のシステム時刻(マイクロ秒)

    // -----------------------------------------------------------------------------
    // アラート監視 判定
    // -----------------------------------------------------------------------------    
    // センサデータをJSONから抽出し、各アラート設定の条件に合致するか判定する
    bool isTriggerEmail = false; // Emailアラート送信が必要か否か
#ifdef ENABLE_MQTT
    bool isTriggerMqtt = false;  // MQTTアラート送信が必要か否か
#endif
    int pairCount = 0;                 

    memset(f_emailReasonBuf, 0, sizeof(f_emailReasonBuf));
#ifdef ENABLE_MQTT
    memset(f_mqttReasonBuf, 0, sizeof(f_mqttReasonBuf));
#endif
    memset(f_keys, 0, sizeof(f_keys));
    memset(f_vals, 0, sizeof(f_vals));
    
    // -----------------------------------------------------------------------------
    // 通知機能の準備状態確認
    // -----------------------------------------------------------------------------    
    // Eメール機能が有効であり、かつ必須設定項目が入力されているかを確認する
    bool isEmailReady = pstFlashData->stNwConfigOption.isEmailEnable &&
                        (pstFlashData->stNwConfigOption.szSmtpHostName[0] != '\0') &&
                        (pstFlashData->stNwConfigOption.szRecipientEmail[0] != '\0') && 
                        (WIFI_IsApConnected() == true); // APと接続済み

    bool canSendEmail = (isEmailReady && EMAIL_IsSendable());
#ifdef ENABLE_MQTT
    bool canSendMqtt = (pstFlashData->stNwConfigOption.isMqttEnable && 
                        (pstFlashData->stNwConfigOption.aMqttBrokerIpAddr[0] != 0) &&
                        (WIFI_IsApConnected() == true));
#endif

    // ネットワーク状態に関わらず、常にセンサデータをパースしてアラート状態を監視する
    // （オフライン時もセンサの状態変化を正確に追従させるため）
    pairCount = ALT_ParseJson(pszJson);
    if (pairCount > 0) {
        ALT_CheckAlert(pstFlashData, currentUs, pairCount, f_keys, f_vals, f_emailReasonBuf, sizeof(f_emailReasonBuf), canSendEmail, &isTriggerEmail
#ifdef ENABLE_MQTT
            , f_mqttReasonBuf, sizeof(f_mqttReasonBuf), canSendMqtt, &isTriggerMqtt
#endif
        );
    }
    
    // -----------------------------------------------------------------------------
    // アラート送信
    // -----------------------------------------------------------------------------    

    if (isTriggerEmail) { // Emailアラート送信が必要な場合
        EMAIL_SendMail(pstFlashData, true/*アラート送信*/, f_emailReasonBuf, pairCount, f_keys, f_vals);
    }
#ifdef ENABLE_MQTT
    if (isTriggerMqtt) {
        // MQTT_SendAlert(pstFlashData, true, f_mqttReasonBuf, pairCount, f_keys, f_vals); // 将来実装用
    }
#endif

    // -----------------------------------------------------------------------------
    // Eメールの定期送信
    // -----------------------------------------------------------------------------     
    // --- 定期送信の判定 ---
    diffUs_interval = currentUs - s_previousUs_send;  
    // 以下の4つの条件をすべて満たした場合に定期メール送信のトリガーをONにする:
    // 1. isEmailReady     : メール機能が有効化されており、ネットワーク設定などの送信準備が完了している
    // 2. intervalHour > 0 : ユーザー設定の送信インターバルが0(無効)以外に設定されている
    // 3. pairCount > 0    : センサから取得したデータ(キーと値のペア)が少なくとも1つ以上存在する
    // 4. タイミング条件   : マイコン起動直後の初回送信であるか、または前回送信から設定時間(intervalHour)が経過している
    bool isRegularTriggered = (isEmailReady && (pstFlashData->stNwConfigOption.mailIntervalHour > 0) && (pairCount > 0) &&
         (!s_isFirstRegularSent || (diffUs_interval >= (unsigned long long)pstFlashData->stNwConfigOption.mailIntervalHour * ALT_HOUR_US)));

    if (!isTriggerEmail) {  
        if (isRegularTriggered) { // 定期送信が必要な場合
            CMN_Strncpy(f_emailReasonBuf, "Regular Interval", sizeof(f_emailReasonBuf));
            
            if (isEmailReady && EMAIL_IsSendable()) {
                // Eメール送信
                EMAIL_SendMail(pstFlashData, false/*定期送信*/, f_emailReasonBuf, pairCount, f_keys, f_vals);
                s_isFirstRegularSent = true;    
                s_previousUs_send = currentUs;
            }
        }
    }
}

// -----------------------------------------------------------------------------
// JSONパース処理
// 受信したJSON形式のセンサデータを解析し、キー(センサ名)とバリュー(値)のペアに分割する。
// -----------------------------------------------------------------------------
static int ALT_ParseJson(const char* pszJson)
{
    int pairCount = 0;
    
    if (pszJson == NULL || pszJson[0] == '\0') {
        return 0;
    }

    CMN_Strncpy(f_jsonBuf, pszJson, sizeof(f_jsonBuf));
    
    // JSON文字列内の不要な記号（括弧、改行、ダブルクォーテーション）を空白に置換し、
    // strtok_r等で処理しやすくする（破壊的パースのための前処理）
    char* p = f_jsonBuf;
    while (*p) {
        if (*p == '{' || *p == '}' || *p == '\r' || *p == '\n' || *p == '"') {
            *p = ' ';
        }
        p++;
    }

    // JSONの各要素をカンマ ',' で分割しながら処理する
    char* saveptr1 = NULL;
    char* token = strtok_r(f_jsonBuf, ",", &saveptr1);
    while (token != NULL && pairCount < IOT_JSON_MAX_PAIRS) {
        // コロン ':' でキーとバリューに分割
        char* colonPtr = strchr(token, ':');
        if (colonPtr != NULL) {
            *colonPtr = '\0';
            char* keyStr = token;
            char* valStr = colonPtr + 1;
            
            // キーの前後の空白を取り除く(トリム処理)
            while (*keyStr == ' ') keyStr++;
            size_t keyLen = strlen(keyStr);
            if (keyLen > 0) {
                char* end = keyStr + keyLen - 1;
                while (end > keyStr && *end == ' ') { *end = '\0'; end--; }
            }
            
            // バリューの前後の空白を取り除く(トリム処理)
            while (*valStr == ' ') valStr++;
            size_t valLen = strlen(valStr);
            if (valLen > 0) {
                char* end = valStr + valLen - 1;
                while (end > valStr && *end == ' ') { *end = '\0'; end--; }
            }
            
            // 抽出したキーとバリューをグローバル配列に保存
            CMN_Strncpy(f_keys[pairCount], keyStr, sizeof(f_keys[pairCount]));
            CMN_Strncpy(f_vals[pairCount], valStr, sizeof(f_vals[pairCount]));
            pairCount++;
        }
        token = strtok_r(NULL, ",", &saveptr1);
    }
    return pairCount;
}

// -----------------------------------------------------------------------------
// アラート監視判定処理
// ユーザーが設定したアラート条件（閾値や条件式）と、現在のセンサ値を比較して
// アラートを発報すべきかどうかを判定する。
// -----------------------------------------------------------------------------
static void ALT_CheckAlert(ST_FLASH_DATA* pstFlashData, volatile uint64_t currentUs, int pairCount, char keys[][IOT_JSON_KEY_BUF_SIZE], char vals[][IOT_JSON_VAL_BUF_SIZE], char* emailReasonBuf, size_t emailReasonBufSize, bool canSendEmail, bool* pIsTriggerEmail
#ifdef ENABLE_MQTT
    , char* mqttReasonBuf, size_t mqttReasonBufSize, bool canSendMqtt, bool* pIsTriggerMqtt
#endif
)
{
    if (pIsTriggerEmail) *pIsTriggerEmail = false;
#ifdef ENABLE_MQTT
    if (pIsTriggerMqtt) *pIsTriggerMqtt = false;
#endif
    
    // 最大 ALT_MAX 個のアラート設定を一つずつチェックする
    for (int i = 0; i < ALT_MAX; i++) {
        // アラート設定が無効(Disable)に変更された場合は、すべての状態をリセットして評価をスキップする
        if (!pstFlashData->stNwConfigOption.astAlertConfig[i].isEnable) {
            f_isAlertConditionMet[i] = false;
            f_isEmailAlertPending[i] = false;
            f_previousUs_emailAlertSend[i] = 0; // クールダウン時間もリセット
#ifdef ENABLE_MQTT
            f_isMqttAlertPending[i] = false;
            f_previousUs_mqttAlertSend[i] = 0;
#endif
            continue;
        }

        bool curMet = false;
        
        for (int j = 0; j < pairCount; j++) {
            if (strcmp(keys[j], pstFlashData->stNwConfigOption.astAlertConfig[i].szSensorName) == 0) {
                
                // JSONから取得した文字列の値を浮動小数点数(float)に変換
                float fVal = atof(vals[j]);
                float th = pstFlashData->stNwConfigOption.astAlertConfig[i].threshold;
                
                // 設定された条件式（以上：GE、以下：LE）に従って閾値と比較
                if (pstFlashData->stNwConfigOption.astAlertConfig[i].condition == IOT_COND_GE && fVal >= th) curMet = true;
                else if (pstFlashData->stNwConfigOption.astAlertConfig[i].condition == IOT_COND_LE && fVal <= th) curMet = true;
                
                break;
            }
        }
        
        // センサ値のアラート条件状態を更新し、エッジ検出時に送信保留フラグをセットする
        if (curMet) {
            // 条件を満たしていない状態から満たした状態になった場合（エッジ検出）
            if (!f_isAlertConditionMet[i]) {
                f_isAlertConditionMet[i] = true;
                f_isEmailAlertPending[i] = true; // Emailの送信要求をセット
#ifdef ENABLE_MQTT
                f_isMqttAlertPending[i] = true;  // MQTTの送信要求をセット
#endif
            }
        } else {
            // センサ値が正常値に戻った場合、状態をリセットし送信要求もキャンセル
            // （チャタリング等による一時的な異常で、送信前に正常復帰した場合は発報しない）
            f_isAlertConditionMet[i] = false;
            f_isEmailAlertPending[i] = false;
#ifdef ENABLE_MQTT
            f_isMqttAlertPending[i] = false;
#endif
        }
        
        bool triggerEmailThisAlert = false;
#ifdef ENABLE_MQTT
        bool triggerMqttThisAlert = false;
#endif

        // Emailの送信要求（保留）があり、かつ送信条件（クールダウン完了＆送信可能）が揃っている場合
        if (f_isEmailAlertPending[i]) {
            if ((f_previousUs_emailAlertSend[i] == 0 || currentUs - f_previousUs_emailAlertSend[i] >= ALT_ALERT_COOLDOWN_TIME) && canSendEmail) {
                triggerEmailThisAlert = true;
                f_previousUs_emailAlertSend[i] = currentUs;
                f_isEmailAlertPending[i] = false; // 送信済みとして保留を解除
            }
        }

#ifdef ENABLE_MQTT
        // MQTTの送信判定とクールダウン制御（将来用）
        if (f_isMqttAlertPending[i]) {
            if ((f_previousUs_mqttAlertSend[i] == 0 || currentUs - f_previousUs_mqttAlertSend[i] >= ALT_ALERT_COOLDOWN_TIME) && canSendMqtt) {
                triggerMqttThisAlert = true;
                f_previousUs_mqttAlertSend[i] = currentUs;
                f_isMqttAlertPending[i] = false; // 送信済みとして保留を解除
            }
        }
#endif

        if (triggerEmailThisAlert) {
            if (pIsTriggerEmail) *pIsTriggerEmail = true;
            const char* condStr = (pstFlashData->stNwConfigOption.astAlertConfig[i].condition == IOT_COND_GE) ? ">=" : "<=";
            
            if (emailReasonBufSize > strlen(emailReasonBuf) + 2) {
                strncat(emailReasonBuf, "\r\n", emailReasonBufSize - strlen(emailReasonBuf) - 1);
            }
            // アラート発報の理由となる文字列を組み立てる
            snprintf(f_reason, sizeof(f_reason), "Alert %d: %s %s %.3f", 
                     i + 1, pstFlashData->stNwConfigOption.astAlertConfig[i].szSensorName, condStr, pstFlashData->stNwConfigOption.astAlertConfig[i].threshold);
            if (emailReasonBufSize > strlen(emailReasonBuf) + 1) {
                strncat(emailReasonBuf, f_reason, emailReasonBufSize - strlen(emailReasonBuf) - 1);
            }
        }

#ifdef ENABLE_MQTT
        if (triggerMqttThisAlert) {
            if (pIsTriggerMqtt) *pIsTriggerMqtt = true;
            const char* condStr = (pstFlashData->stNwConfigOption.astAlertConfig[i].condition == IOT_COND_GE) ? ">=" : "<=";
            
            if (mqttReasonBuf[0] != '\0') {
                if (mqttReasonBufSize > strlen(mqttReasonBuf) + 2) {
                    strncat(mqttReasonBuf, "\r\n", mqttReasonBufSize - strlen(mqttReasonBuf) - 1);
                }
            }
            snprintf(f_reason, sizeof(f_reason), "Alert %d: %s %s %.3f", 
                     i + 1, pstFlashData->stNwConfigOption.astAlertConfig[i].szSensorName, condStr, pstFlashData->stNwConfigOption.astAlertConfig[i].threshold);
            if (mqttReasonBufSize > strlen(mqttReasonBuf) + 1) {
                strncat(mqttReasonBuf, f_reason, mqttReasonBufSize - strlen(mqttReasonBuf) - 1);
            }
        }
#endif
    }
}