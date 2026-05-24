// Copyright © 2026 Shiomachi Software. All rights reserved.
#include "Common.h"

// [define]
#define HTTP_REFRESH ((uint32_t)(IOT_NETWORK_SEND_PERIOD_US / 1000000ULL)) // HTMLメタタグによるルート画面の自動リフレッシュ間隔(秒) 
#define HTTP_RECV_TIMEOUT 3000 // HTTPクライアントからのデータ受信待ちタイムアウト時間(ms)
#define HTTP_DELAY_RECV_DATA 10 // HTTPクライアントがレスポンスデータを確実に受信し終えるための確保時間(ms)
#define HTTP_DELAY_WAIT_RES  1000 // FLASH書き込み等の重い処理の前に、ブラウザへの応答送信が完了するのを待つ時間(ms)
#define HTTP_REQ_LINE_BUF_SIZE (NW_CONFIG_SMTP_HOST_SIZE + NW_CONFIG_EMAIL_ADDR_SIZE + NW_CONFIG_SMTP_PASSWORD_SIZE + NW_CONFIG_EMAIL_ADDR_SIZE + NW_CONFIG_DEVICE_NAME_SIZE + ((IOT_JSON_KEY_BUF_SIZE + IOT_JSON_VAL_BUF_SIZE) * ALT_MAX) + 512) // HTTPリクエストの1行目(URLやGETパラメータ)を保存するバッファの最大サイズ(byte)
#define HTTP_DEC_BUF_SIZE      (NW_CONFIG_SMTP_HOST_SIZE + NW_CONFIG_EMAIL_ADDR_SIZE + NW_CONFIG_SMTP_PASSWORD_SIZE + NW_CONFIG_EMAIL_ADDR_SIZE + NW_CONFIG_DEVICE_NAME_SIZE + IOT_JSON_KEY_BUF_SIZE + IOT_JSON_VAL_BUF_SIZE) // URLデコード・文字列加工用の一時バッファのサイズ(byte)
#define HTTP_URL_DECODE_TEMP_SIZE 3 // URLデコード処理(%XX)の解析に用いる一時バッファのサイズ(byte)

// [ファイルスコープ変数]
static WiFiServer f_httpServer(HTTP_PORT); // ポート80で待ち受けるTCP/HTTPサーバーオブジェクト
static bool f_isHttpServerBegun = false; // HTTPサーバーの begin() が実行済みか否かの状態フラグ
static WiFiClient f_httpClient; // 現在接続して通信処理中のHTTPクライアント(ブラウザ等)オブジェクト
static uint32_t f_clientStartMs = 0; // クライアント接続時のタイムスタンプ(無通信タイムアウト判定用)
static bool f_isLineEnd = true; // 受信したHTTPリクエストにおいて、LF(\n)による行末を検出したかのフラグ
static bool f_isRootAccess = false; // リクエストのURLがルートパス ("/") へのアクセスか否か
static bool f_isConfigAccess = false; // リクエストのURLが設定画面 ("/config") へのアクセスか否か
static char f_reqLineBuf[HTTP_REQ_LINE_BUF_SIZE]; // クライアントから送信されたHTTPリクエストの1行目を保存するバッファ
static uint16_t f_reqLineLen = 0; // 現在バッファ(f_reqLineBuf)に格納されている文字列の長さ
static bool f_isFirstLine = true; // 現在受信中のデータがHTTPリクエストの1行目か否かのフラグ
static char f_urlDecodeTemp[HTTP_URL_DECODE_TEMP_SIZE]; // %XX形式のURLエンコードを復元する際の一時バッファ
static int f_newAlertEn[ALT_MAX]; // フォームから受信した、変更後のアラート有効/無効状態 (0:無効, 1:有効, -1:未変更)
static char f_newAlertSensor[ALT_MAX][IOT_JSON_KEY_BUF_SIZE]; // フォームから受信した、変更後のアラート対象センサー名称
static int f_newAlertCond[ALT_MAX]; // フォームから受信した、変更後のアラート条件 (等しい, 以上, 以下 など)
static float f_newAlertThresh[ALT_MAX]; // フォームから受信した、変更後のアラート発報の閾値(数値)
#ifdef ENABLE_MQTT // MQTT機能は作成中
static int f_newMqttEn; // フォームから受信した、変更後のMQTT有効/無効状態 (0:無効, 1:有効, -1:未変更)
static int f_newMqttBrokerIp[NW_CONFIG_IP_ADDR_SIZE]; // フォームから受信した、変更後のMQTTブローカーIPアドレスの各オクテット
#endif
static int f_newEmailEn; // フォームから受信した、変更後のEmail通知有効/無効状態 (0:無効, 1:有効, -1:未変更)
static char f_szSmtpHostName[NW_CONFIG_SMTP_HOST_SIZE]; // フォームから受信した、変更後のSMTPホスト名
static int f_newSmtpPort; // フォームから受信した、変更後のSMTPポート番号
static char f_szSmtpUser[NW_CONFIG_EMAIL_ADDR_SIZE]; // フォームから受信した、変更後のSMTPユーザー名(Emailアドレス等)
static char f_szSmtpPassword[NW_CONFIG_SMTP_PASSWORD_SIZE]; // フォームから受信した、変更後のSMTPパスワード
static char f_szRecipientEmail[NW_CONFIG_EMAIL_ADDR_SIZE]; // フォームから受信した、変更後の宛先メールアドレス
static char f_szDeviceName[NW_CONFIG_DEVICE_NAME_SIZE]; // フォームから受信した、変更後のデバイス識別名
static char f_decBuf[HTTP_DEC_BUF_SIZE]; // URLデコード等の文字列処理の際に作業用として用いる一時バッファ
static char f_jsonKeys[IOT_JSON_MAX_PAIRS][IOT_JSON_KEY_BUF_SIZE]; // JSONパース結果から抽出されたセンサーのキー(名前)の配列
static int f_jsonKeyCount = 0; // JSONから正常に抽出できたキー(センサー数)の合計カウント
static char f_jsonCopy[IOT_JSON_BUF_SIZE]; // JSON文字列を非破壊でパース・解析するための複製バッファ
static char f_titleBuf[IOT_DISPLAY_NAME_BUF_SIZE]; // HTML出力時にブラウザのタイトルタブ等へ表示する文字列のバッファ
static ST_FLASH_DATA f_stFlashData; // 不揮発性メモリ(FLASH)へ読み書きするための設定データ一時保持構造体
static int f_newInterval = -1; // フォームから受信した、定期メール送信間隔の値 (-1:未変更)
#ifdef ENABLE_MQTT // MQTT機能は作成中
static bool f_bMqttUpdate = false; // URLパラメータ内にMQTT設定に関する項目が含まれていたかのフラグ
#endif
static bool f_bEmailUpdate = false; // URLパラメータ内にEmail設定に関する項目が含まれていたかのフラグ
static bool f_bClearPw = false; // ユーザーからパスワードの消去(クリア)要求が送信されたかのフラグ
static bool f_bNameUpdate = false; // URLパラメータ内にデバイス名設定に関する項目が含まれていたかのフラグ
static bool f_bIntervalChanged = false; // 現在のFLASH設定値と比較し、送信間隔に実際の変更があったかのフラグ
static bool f_bAlertChanged = false; // 現在のFLASH設定値と比較し、アラート設定に実際の変更があったかのフラグ
#ifdef ENABLE_MQTT // MQTT機能は作成中
static bool f_bMqttChanged = false; // 現在のFLASH設定値と比較し、MQTT設定に実際の変更があったかのフラグ
#endif
static bool f_bEmailChanged = false; // 現在のFLASH設定値と比較し、Email設定に実際の変更があったかのフラグ
static bool f_bNameChanged = false; // 現在のFLASH設定値と比較し、デバイス名に実際の変更があったかのフラグ

// [関数プロトタイプ宣言]
static void HTTP_UrlDecode(const char* src, char* dest, size_t destSize);
static void HTTP_SanitizeString(char* str);
static void HTTP_InitNewConfig(ST_FLASH_DATA* pstFlashData);
static void HTTP_ParseQueryString();
static void HTTP_CheckConfigChanges(ST_FLASH_DATA* pstFlashData);
static void HTTP_SendResponse(ST_FLASH_DATA* pstFlashData, const char* pszJson);
static void HTTP_SaveConfigAndReboot();

// -----------------------------------------------------------------------------
// HTTPサーバーを開始 
// 既存のサーバーやクライアント接続があれば一旦停止し、新たにリッスンを開始する。
// ネットワークの再接続時や設定変更後の再起動時など、クリーンな状態で
// サーバーを立ち上げるために、古いソケットリソースを確実に解放してから begin() する。
// -----------------------------------------------------------------------------
void HTTP_Begin()
{
    // [既存のサーバーソケットの解放]
    // 既にサーバーが稼働中の場合は、一旦停止してポート(TCP_PORT/HTTP_PORT)を解放する。
    if (f_isHttpServerBegun) {
        f_httpServer.stop(); // 内部のリッスンソケットを閉じる    
    }    
    
    // [既存のクライアント接続の切断]
    // 前回のセッションで接続状態のまま放置されたクライアント(半死半生のソケットなど)があれば強制切断する。
    if (f_httpClient) {
        f_httpClient.stop(); // TCP接続の切断処理を実行
        f_httpClient = WiFiClient(); // オブジェクトをクリアしてメモリリークを防ぐ
    }

    // [サーバーの起動]
    // 新たにポートのリッスン(接続待ち)を開始する。
    f_httpServer.begin(); // ソケットをバインドし、リッスン状態へ移行
    f_isHttpServerBegun = true; // 稼働中フラグをセット
}

// -----------------------------------------------------------------------------
// HTTPサーバーのメイン処理 
// クライアントからの接続待ち、HTTPリクエストの受信と各サブ関数の呼び出しを行う。
// -----------------------------------------------------------------------------
void HTTP_Main(char* pszJson) 
{
    ST_FLASH_DATA* pstFlashData = FLASH_GetDataAtPowerOn(); // 電源起動時のFLASHデータを取得(設定の参照用)
    char c;                      // ソケットから1バイト読み取るための一時変数

    if (!WIFI_IsApConnected()) { // APと接続済みでない場合
        return;
    }

    // サーバーが正常に起動している場合のみ処理を進行する
    if (f_isHttpServerBegun) {
        // 現在接続中のクライアントが存在しない、あるいは接続が切れている場合
        if (!f_httpClient || !f_httpClient.connected()) {
            if (f_httpClient) {
                // 不完全なソケットが残っている場合は確実に閉じてクリアする
                f_httpClient.stop();
                f_httpClient = WiFiClient();
            }
            // 新規のTCP接続要求を受け付ける(ブロッキングはしない)
            WiFiClient newClient = f_httpServer.accept();
            if (newClient) {
                // クライアントからの接続が確立した場合、各種状態変数を初期化
                f_httpClient = newClient;
                f_clientStartMs = millis(); // 接続開始時刻を記録
                f_isLineEnd = true;
                f_isRootAccess = false;
                f_isConfigAccess = false;
                f_reqLineLen = 0;
                f_isFirstLine = true;
                memset(f_reqLineBuf, 0, sizeof(f_reqLineBuf)); // リクエスト解析バッファをゼロクリア
            }
        }

        // クライアントと現在通信中の場合
        if (f_httpClient) {
            if (f_httpClient.connected()) {
                // ブラウザのプレコネクト機能や通信途絶に対処するため、一定時間でタイムアウトさせる
                if (millis() - f_clientStartMs >= HTTP_RECV_TIMEOUT) {
                    f_httpClient.stop(); // 切断
                    f_httpClient = WiFiClient();
                    return; // 処理を抜ける
                }

                // ソケットの受信バッファにデータがある限りループで読み出す
                while (f_httpClient.available()) {
                    c = f_httpClient.read(); // 1バイト読み出し
                    
                    // データを受信したらタイムアウトのカウントをリセット(延長)する
                    f_clientStartMs = millis(); 
                    
                    // HTTPリクエストの1行目 (例: "GET /config?param=1 HTTP/1.1") を抽出する
                    if (f_isFirstLine) {
                        if (c == '\r' || c == '\n') {
                            f_isFirstLine = false; // 1行目の終端を検知
                        } else if (f_reqLineLen < sizeof(f_reqLineBuf) - 1) {
                            f_reqLineBuf[f_reqLineLen++] = c; // バッファに文字を蓄積
                        }
                    }

                    // 空行 (LFのみ) を検出した場合、HTTPリクエストヘッダの終了とみなす
                    if ((c == '\n') && f_isLineEnd) {
                        
                        // 設定の整合性チェック等に用いるため、先に最新のFLASHデータを読み込む
                        FLASH_Read(&f_stFlashData);
                        
                        // 受信パラメータのパース結果を格納する各種変数を初期化する
                        HTTP_InitNewConfig(&f_stFlashData);
                        
                        // URLのクエリストリング(GETパラメータ)を解析する
                        HTTP_ParseQueryString();
                        
                        // 変更要求と現在の設定を比較し、実際に変更が必要か判定する
                        HTTP_CheckConfigChanges(&f_stFlashData);
                        
                        // Webブラウザへ返すHTMLレスポンスを生成・送信する
                        HTTP_SendResponse(&f_stFlashData, pszJson);
                        
                        // 送信バッファのデータがネットワークへ完全に送出されるのを待機する
                        f_httpClient.flush();
                        // HTTPクライアントがレスポンスデータを確実に受信し終えるための確保時間
                        delay(HTTP_DELAY_RECV_DATA);
                        
                        // HTTPの通信規約に基づき、レスポンス送信後は接続を閉じる
                        f_httpClient.stop();
                        f_httpClient = WiFiClient();
                        
                        // 設定に変更があった場合はFLASHへ書き込みデバイスを再起動する
                        HTTP_SaveConfigAndReboot();
                        break; // 受信ループを終了
                    } 
                    // 改行コードの判定処理
                    if (c == '\n') {
                        f_isLineEnd = true; // 次の文字が行の先頭になる
                    }
                    else if (c != '\r') {
                        f_isLineEnd = false; // CR以外の文字が来たら行の途中と判定
                    } 
                }
            } else {
                f_httpClient.stop();
                f_httpClient = WiFiClient();
            }
        }
    }
}

// -----------------------------------------------------------------------------
// URLデコード関数 (C文字列版) 
// URLに含まれる %XX 形式のエンコード文字列や '+' を元の文字にデコードする
// -----------------------------------------------------------------------------
static void HTTP_UrlDecode(const char* src, char* dest, size_t destSize) {
    size_t i = 0, j = 0;
    // 解析文字が終端に達するか、出力先バッファの限界まで繰り返す
    while (src[i] != '\0' && j < destSize - 1) {
        if (src[i] == '%' && src[i + 1] != '\0' && src[i + 2] != '\0') {
            // 16進数文字をバッファに詰めてデコードする
            f_urlDecodeTemp[0] = src[i + 1];
            f_urlDecodeTemp[1] = src[i + 2];
            f_urlDecodeTemp[2] = '\0';
            long val = strtol(f_urlDecodeTemp, NULL, 16);
            if (val > 0) {
                dest[j] = (char)val;
            } else {
                dest[j] = '?'; // デコード失敗やNULL文字(%00)の混入を防ぐための代替文字
            }
            i += 3;
            j++;
        } else if (src[i] == '+') {
            // URLエンコードにおける '+' は空白(スペース)に復元する
            dest[j] = ' ';
            i++;
            j++;
        } else {
            // それ以外の通常の文字はそのままコピーする
            dest[j] = src[i];
            i++;
            j++;
        }
    }
    dest[j] = '\0'; // 最後に確実にNULL文字で終端させる
}

// -----------------------------------------------------------------------------
// HTML特殊文字のサニタイズ関数
// フォーム入力値によるHTML構造の破壊やXSSを防ぐため、危険な記号を置換する
// -----------------------------------------------------------------------------
static void HTTP_SanitizeString(char* str) {
    while (*str) {
        if (*str == '"' || *str == '<' || *str == '>' || *str == '&' || *str == '\'' || *str == '\\') {
            *str = '_'; // HTMLを破壊する文字を安全な文字(アンダースコア)に置換
        }
        str++;
    }
}

// -----------------------------------------------------------------------------
// リクエスト受信時の設定変数初期化
// -----------------------------------------------------------------------------
static void HTTP_InitNewConfig(ST_FLASH_DATA* pstFlashData) 
{
    // 設定画面の送信内容を保持する変数を、変更なしの初期値(-1やfalse)にリセットする
    f_newInterval = -1;
#ifdef ENABLE_MQTT // MQTT機能は作成中
    f_bMqttUpdate = false;
#endif
    f_bEmailUpdate = false;
    f_bClearPw = false;
    f_bNameUpdate = false;
    f_bIntervalChanged = false;
    f_bAlertChanged = false;
#ifdef ENABLE_MQTT // MQTT機能は作成中
    f_bMqttChanged = false;
#endif
    f_bEmailChanged = false;
    f_bNameChanged = false;

    // 各アラートの設定を「変更なし」の状態で初期化
    for (int i = 0; i < ALT_MAX; i++) {
        f_newAlertEn[i] = -1;
        memset(f_newAlertSensor[i], 0, sizeof(f_newAlertSensor[i]));
        f_newAlertCond[i] = -1;
        f_newAlertThresh[i] = 0.0f;
    }
#ifdef ENABLE_MQTT // MQTT機能は作成中
    // MQTT関連の変数を初期化
    f_newMqttEn = -1;
    for (int i = 0; i < NW_CONFIG_IP_ADDR_SIZE; i++) {
        f_newMqttBrokerIp[i] = -1;
    }
#endif
    // Email、その他の文字列設定項目は現在の設定値をコピーしておく
    f_newEmailEn = -1;
    CMN_Strncpy(f_szSmtpHostName, pstFlashData->stNwConfigOption.szSmtpHostName, sizeof(f_szSmtpHostName));
    f_newSmtpPort = -1;
    CMN_Strncpy(f_szSmtpUser, pstFlashData->stNwConfigOption.szSmtpUser, sizeof(f_szSmtpUser));
    CMN_Strncpy(f_szSmtpPassword, pstFlashData->stNwConfigOption.szSmtpPassword, sizeof(f_szSmtpPassword));
    CMN_Strncpy(f_szRecipientEmail, pstFlashData->stNwConfigOption.szRecipientEmail, sizeof(f_szRecipientEmail));
    CMN_Strncpy(f_szDeviceName, pstFlashData->stNwConfigOption.szDeviceName, sizeof(f_szDeviceName));
}

// -----------------------------------------------------------------------------
// URLパラメータ(クエリストリング)の解析
// -----------------------------------------------------------------------------
static void HTTP_ParseQueryString()
{
    // HTTPリクエストのパスとメソッドに応じてアクセス種類を判定する
    if (strncmp(f_reqLineBuf, "GET / ", 6) == 0 || strncmp(f_reqLineBuf, "GET / HTTP", 10) == 0) {
        // ルート画面 (データ閲覧用)
        f_isRootAccess = true;
    } else if (strncmp(f_reqLineBuf, "GET /config ", 12) == 0 || strncmp(f_reqLineBuf, "GET /config HTTP", 16) == 0) {
        // 設定画面 (クエリなし)
        f_isConfigAccess = true;
    } else if (strncmp(f_reqLineBuf, "GET /config?", 12) == 0) {
        // 設定画面への保存要求 (クエリパラメータあり)
        f_isConfigAccess = true;
        char* httpPtr = strstr(f_reqLineBuf, " HTTP");
        if (httpPtr != NULL) *httpPtr = '\0';
        
        // クエリストリング(?)の開始位置を探す
        char* qPtr = strchr(f_reqLineBuf, '?');
        if (qPtr != NULL) {
            // strtok_r() を用いて "key=value" のペアごとに分割していく
            char* query = qPtr + 1;
            char* saveptr = NULL;
            char* token = strtok_r(query, "&", &saveptr);
            while (token != NULL) {
                char* eqPtr = strchr(token, '=');
                // '=' を区切りとして Key と Value に分ける
                if (eqPtr != NULL) {
                    *eqPtr = '\0';
                    char* key = token;
                    char* val = eqPtr + 1;
                    
                    if (strcmp(key, "interval") == 0) {
                        // メール送信間隔の更新
                        f_newInterval = atoi(val);
                    } else if (strcmp(key, "name") == 0) {
                        f_bNameUpdate = true;
                        HTTP_UrlDecode(val, f_decBuf, sizeof(f_decBuf)); // パーセントエンコードを復元
                        HTTP_SanitizeString(f_decBuf); // HTML破壊を防ぐためサニタイズ
                        
                        // 未入力の場合はボード固有のIDで名前を補完する
                        if (f_decBuf[0] == '\0') {
                            pico_unique_board_id_t board_id;
                            pico_get_unique_board_id(&board_id);
                            snprintf(f_decBuf, sizeof(f_decBuf),
                                     "%02X%02X%02X%02X%02X%02X%02X%02X",
                                     board_id.id[0], board_id.id[1], board_id.id[2], board_id.id[3], board_id.id[4], board_id.id[5], board_id.id[6], board_id.id[7]);
                        }
                        CMN_Strncpy(f_szDeviceName, f_decBuf, sizeof(f_szDeviceName));
#ifdef ENABLE_MQTT // MQTT機能は作成中
                    } else if (strcmp(key, "mqtt_en") == 0) {
                        f_bMqttUpdate = true;
                        f_newMqttEn = atoi(val);
                    } else if (strncmp(key, "mip", 3) == 0 && key[3] >= '0' && key[3] <= '9') {
                        // MQTTブローカーIP(mip0 〜 mip3)
                        int idx = atoi(key + 3);
                        if (idx >= 0 && idx < NW_CONFIG_IP_ADDR_SIZE) {
                            f_bMqttUpdate = true;
                            f_newMqttBrokerIp[idx] = atoi(val);
                        }
#endif
                    } else if (strcmp(key, "email_en") == 0) {
                        f_bEmailUpdate = true;
                        f_newEmailEn = atoi(val);
                    } else if (strcmp(key, "smtp_host") == 0) {
                        // SMTPホスト名
                        f_bEmailUpdate = true;
                        HTTP_UrlDecode(val, f_decBuf, sizeof(f_decBuf));
                        HTTP_SanitizeString(f_decBuf);
                        CMN_Strncpy(f_szSmtpHostName, f_decBuf, sizeof(f_szSmtpHostName));
                    } else if (strcmp(key, "smtp_port") == 0) {
                        // SMTPポート番号
                        f_bEmailUpdate = true;
                        f_newSmtpPort = atoi(val);
                    } else if (strcmp(key, "smtp_user") == 0) {
                        // SMTP送信元アドレス(ユーザー)
                        f_bEmailUpdate = true;
                        HTTP_UrlDecode(val, f_decBuf, sizeof(f_decBuf));
                        HTTP_SanitizeString(f_decBuf);
                        CMN_Strncpy(f_szSmtpUser, f_decBuf, sizeof(f_szSmtpUser));
                    } else if (strcmp(key, "smtp_pw") == 0) {
                        // SMTPパスワード
                        f_bEmailUpdate = true;
                        HTTP_UrlDecode(val, f_decBuf, sizeof(f_decBuf));
                        // パスワードは未入力(空文字)の場合は現在の値を維持するため更新しない
                        if (f_decBuf[0] != '\0') {
                            CMN_Strncpy(f_szSmtpPassword, f_decBuf, sizeof(f_szSmtpPassword));
                        }
                    } else if (strcmp(key, "clr_pw") == 0) {
                        // パスワードのクリア要求フラグ
                        f_bEmailUpdate = true;
                        f_bClearPw = true;
                    } else if (strcmp(key, "recipient_email") == 0) {
                        // 宛先アドレス
                        f_bEmailUpdate = true;
                        HTTP_UrlDecode(val, f_decBuf, sizeof(f_decBuf));
                        HTTP_SanitizeString(f_decBuf);
                        CMN_Strncpy(f_szRecipientEmail, f_decBuf, sizeof(f_szRecipientEmail));
                    } else if (strncmp(key, "en", 2) == 0 && key[2] >= '0' && key[2] <= '9') {
                        // アラートの有効化設定
                        int idx = atoi(key + 2);
                        if (idx >= 0 && idx < ALT_MAX) {
                            f_newAlertEn[idx] = atoi(val);
                        }
                    } else if (strncmp(key, "sensor", 6) == 0 && key[6] >= '0' && key[6] <= '9') {
                        // アラートの対象センサー
                        int idx = atoi(key + 6);
                        if (idx >= 0 && idx < ALT_MAX) {
                            HTTP_UrlDecode(val, f_decBuf, sizeof(f_decBuf));
                            HTTP_SanitizeString(f_decBuf);
                            CMN_Strncpy(f_newAlertSensor[idx], f_decBuf, sizeof(f_newAlertSensor[idx]));
                        }
                    } else if (strncmp(key, "ope", 3) == 0 && key[3] >= '0' && key[3] <= '9') {
                        // アラートの条件式
                        int idx = atoi(key + 3);
                        if (idx >= 0 && idx < ALT_MAX) {
                            f_newAlertCond[idx] = atoi(val);
                        }
                    } else if (strncmp(key, "th", 2) == 0 && key[2] >= '0' && key[2] <= '9') {
                        // アラートの閾値
                        int idx = atoi(key + 2);
                        if (idx >= 0 && idx < ALT_MAX) {
                            f_newAlertThresh[idx] = atof(val);
                        }
                    }
                }
                token = strtok_r(NULL, "&", &saveptr);
            }
        }
    }

    // パスワードのクリアチェックが設定されていた場合は、ここで文字列を空にする
    if (f_bClearPw) {
        f_szSmtpPassword[0] = '\0';
    }
}

// -----------------------------------------------------------------------------
// 設定の変更有無を判定
// -----------------------------------------------------------------------------
static void HTTP_CheckConfigChanges(ST_FLASH_DATA* pstFlashData) 
{
    // メールの送信間隔の変更確認
    if (f_newInterval >= 0 && f_newInterval <= 24) {
        if (pstFlashData->stNwConfigOption.mailIntervalHour != f_newInterval) f_bIntervalChanged = true;
    }
    // デバイス名の変更確認
    if (f_bNameUpdate) {
        if (strncmp(pstFlashData->stNwConfigOption.szDeviceName, f_szDeviceName, sizeof(pstFlashData->stNwConfigOption.szDeviceName)) != 0) {
            f_bNameChanged = true;
        }
    }
#ifdef ENABLE_MQTT // MQTT機能は作成中
    // MQTT設定の変更確認。すべてのIPオクテットが正しい範囲かどうかも併せて検証する。
    if (f_bMqttUpdate) {
        bool allValid = true;
        if (f_newMqttEn >= 0 && f_newMqttEn <= 1) {
            if (pstFlashData->stNwConfigOption.isMqttEnable != f_newMqttEn) f_bMqttChanged = true;
        }
        for (int i = 0; i < NW_CONFIG_IP_ADDR_SIZE; i++) {
            if (f_newMqttBrokerIp[i] < 0 || f_newMqttBrokerIp[i] > 255) allValid = false;
        }
        if (allValid) {
            for (int i = 0; i < NW_CONFIG_IP_ADDR_SIZE; i++) {
                if (pstFlashData->stNwConfigOption.aMqttBrokerIpAddr[i] != f_newMqttBrokerIp[i]) f_bMqttChanged = true;
            }
        } else {
            // URLパラメータが不完全な場合(IPのどれか1つでも欠損等)は更新を無効化する
            f_bMqttUpdate = false;
            f_bMqttChanged = false;
        }
    }
#endif
    if (f_bEmailUpdate) {
        // 各Email設定項目が現在の設定値と異なるかを比較する
        if (f_newEmailEn >= 0 && f_newEmailEn <= 1) {
            if (pstFlashData->stNwConfigOption.isEmailEnable != f_newEmailEn) f_bEmailChanged = true;
        }
        if (strncmp(pstFlashData->stNwConfigOption.szSmtpHostName, f_szSmtpHostName, sizeof(pstFlashData->stNwConfigOption.szSmtpHostName)) != 0) f_bEmailChanged = true;
        if (f_newSmtpPort >= 1 && f_newSmtpPort <= 65535 && pstFlashData->stNwConfigOption.smtpPort != f_newSmtpPort) f_bEmailChanged = true;
        if (strncmp(pstFlashData->stNwConfigOption.szSmtpUser, f_szSmtpUser, sizeof(pstFlashData->stNwConfigOption.szSmtpUser)) != 0) f_bEmailChanged = true;
        if (strncmp(pstFlashData->stNwConfigOption.szSmtpPassword, f_szSmtpPassword, sizeof(pstFlashData->stNwConfigOption.szSmtpPassword)) != 0) f_bEmailChanged = true;
        if (strncmp(pstFlashData->stNwConfigOption.szRecipientEmail, f_szRecipientEmail, sizeof(pstFlashData->stNwConfigOption.szRecipientEmail)) != 0) f_bEmailChanged = true;
    }
    if (f_newInterval >= 0) { // フォーム送信時は常に差分チェックを行う(全削除やゾンビ消去を反映させるため)
        ST_ALERT_CONFIG astTempAlert[ALT_MAX];
        memset(astTempAlert, 0, sizeof(astTempAlert));
        int saveIdx = 0;
        
        for (int i = 0; i < ALT_MAX; i++) {
            if (f_newAlertEn[i] >= 0 && f_newAlertSensor[i][0] != '\0' && (f_newAlertCond[i] == IOT_COND_GE || f_newAlertCond[i] == IOT_COND_LE)) {
                astTempAlert[saveIdx].isEnable = f_newAlertEn[i];
                CMN_Strncpy(astTempAlert[saveIdx].szSensorName, f_newAlertSensor[i], sizeof(astTempAlert[saveIdx].szSensorName));
                astTempAlert[saveIdx].condition = f_newAlertCond[i];
                astTempAlert[saveIdx].threshold = f_newAlertThresh[i];
                saveIdx++;
            }
        }
        
        // 構築した新しいアラート配列と現在の設定を比較する
        for (int i = 0; i < ALT_MAX; i++) {
            if (strncmp(pstFlashData->stNwConfigOption.astAlertConfig[i].szSensorName, astTempAlert[i].szSensorName, sizeof(astTempAlert[i].szSensorName)) != 0) {
                f_bAlertChanged = true;
                break;
            }
            // センサー名が一致している場合のみ、他の設定項目を比較する
            if (astTempAlert[i].szSensorName[0] != '\0') {
                if (pstFlashData->stNwConfigOption.astAlertConfig[i].isEnable != astTempAlert[i].isEnable ||
                    pstFlashData->stNwConfigOption.astAlertConfig[i].condition != astTempAlert[i].condition ||
                    fabs(pstFlashData->stNwConfigOption.astAlertConfig[i].threshold - astTempAlert[i].threshold) >= 0.001f) {
                    f_bAlertChanged = true;
                    break;
                }
            }
        }
    }
}

// -----------------------------------------------------------------------------
// HTTPレスポンス(HTML)の生成と送信
// -----------------------------------------------------------------------------
static void HTTP_SendResponse(ST_FLASH_DATA* pstFlashData, const char* pszJson) 
{
    // ルートアクセス、または設定画面アクセスの場合は適切なHTTPレスポンスを生成する
    if (f_isRootAccess || f_isConfigAccess) {
        f_httpClient.print(
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html; charset=utf-8\r\n"
            "Cache-Control: no-store, no-cache, must-revalidate\r\n"
            "Connection: close\r\n"
        );
        if (f_isRootAccess) {
            // ルート画面は定期的に自動リロードさせるためのRefreshヘッダを付与
            f_httpClient.print("Refresh: ");
            f_httpClient.println(HTTP_REFRESH);
        }
        f_httpClient.print("\r\n");
        
        // 設定画面へのアクセスであり、かつ何らかのパラメータ(保存要求)が存在する場合
#ifdef ENABLE_MQTT // MQTT機能は作成中
        if (f_isConfigAccess && (f_newInterval >= 0 || f_bMqttUpdate || f_bEmailUpdate || f_bNameUpdate)) {
            if (f_bIntervalChanged || f_bAlertChanged || f_bMqttChanged || f_bEmailChanged || f_bNameChanged) {
#else
        if (f_isConfigAccess && (f_newInterval >= 0 || f_bEmailUpdate || f_bNameUpdate)) {
            if (f_bIntervalChanged || f_bAlertChanged || f_bEmailChanged || f_bNameChanged) {
#endif
                // いずれかの設定に変更があった場合は「保存と再起動」を通知する画面を出力
                f_httpClient.print("<!DOCTYPE html><html><body><h2 style=\"text-align:center;\">Settings saved. Rebooting...</h2>"
                                   "<script>setTimeout(function(){window.location.href='/';}, 6000);</script>"
                                   "</body></html>");
            } else {
                // 変更点が一切無かった場合は「変更なし」と表示して設定画面に戻る
                f_httpClient.print("<!DOCTYPE html><html><body><h2 style=\"text-align:center;\">No changes.</h2>"
                                   "<script>setTimeout(function(){window.location.href='/config';}, 1500);</script>"
                                   "</body></html>");
            }
        } else if (f_isConfigAccess) {
            
            // 以降は設定入力フォームの生成
            // 各アラートのラベル表示用に、最新のJSONキャッシュからセンサーのキー名(温度等)を抽出する
            f_jsonKeyCount = 0;
            if (pszJson != NULL && pszJson[0] != '\0') {
                CMN_Strncpy(f_jsonCopy, pszJson, sizeof(f_jsonCopy));
                char* p = f_jsonCopy;
                while (*p) {
                    // パースしやすくするために不要な記号を空白に置換
                    if (*p == '{' || *p == '}' || *p == '\r' || *p == '\n' || *p == '"') {
                        *p = ' ';
                    }
                    p++;
                }
                char* saveptr1 = NULL;
                char* token = strtok_r(f_jsonCopy, ",", &saveptr1);
                while (token != NULL && f_jsonKeyCount < IOT_JSON_MAX_PAIRS) {
                    // ":" で分割してキー部分のみを取り出す
                    char* colonPtr = strchr(token, ':');
                    if (colonPtr != NULL) {
                        *colonPtr = '\0';
                        char* keyStr = token;
                        while (*keyStr == ' ') keyStr++;
                        size_t keyLen = strlen(keyStr);
                        if (keyLen > 0) {
                            char* end = keyStr + keyLen - 1;
                            while (end > keyStr && *end == ' ') { *end = '\0'; end--; }
                        }
                        // システム系の予約語はアラート対象から除外してリストに加える
                        if (strcmp(keyStr, "FW_Name") != 0 && strcmp(keyStr, "FW_Ver") != 0 &&
                            strcmp(keyStr, "BoardID") != 0 && strcmp(keyStr, "DeviceName") != 0) {
                            CMN_Strncpy(f_jsonKeys[f_jsonKeyCount], keyStr, sizeof(f_jsonKeys[f_jsonKeyCount]));
                            f_jsonKeyCount++;
                        }
                    }
                    token = strtok_r(NULL, ",", &saveptr1);
                }
            }
            
            snprintf(f_titleBuf, sizeof(f_titleBuf), "%s[%s] Settings", FW_NAME, pstFlashData->stNwConfigOption.szDeviceName);
            
            // HTMLのひな形とCSSスタイルを出力
            // HTMLヘッダと基本スタイルの出力
            f_httpClient.print(
                "<!DOCTYPE html><html><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
                "<title>"
            );
            f_httpClient.print(f_titleBuf);
            f_httpClient.print(
                "</title>"
                "<style>body{font-family:sans-serif;background:#f0f2f5;padding:20px;color:#333;}"
                "h2{text-align:center;}a.btn{display:block;width:fit-content;margin:10px auto;padding:10px 20px;background:#6c757d;color:#fff;text-decoration:none;border-radius:5px;text-align:center;}</style>"
                "</head><body><h2>"
            );
            f_httpClient.print(f_titleBuf);
            f_httpClient.print(
                "</h2>"
                "<a href=\"/\" class=\"btn\">Back to Sensor Data</a>"
            );
            
            // 設定フォームの開始
            f_httpClient.print("<form method=\"GET\" action=\"/config\" autocomplete=\"off\" style=\"max-width:500px;margin:20px auto;text-align:center;background:#fff;padding:15px;box-shadow:0 2px 4px rgba(0,0,0,0.1);\">");
            
            // デバイス名の設定項目
            f_httpClient.print("<h3>Network Settings</h3>");
            f_httpClient.print("<label>Device Name: <input type=\"text\" name=\"name\" maxlength=\"");
            f_httpClient.print(NW_CONFIG_DEVICE_NAME_SIZE - 1);
            f_httpClient.print("\" pattern=\"[ -~]*\" title=\"Alphanumeric characters and symbols only\" placeholder=\""); f_httpClient.print(FW_NAME); f_httpClient.print("\" value=\""); f_httpClient.print(pstFlashData->stNwConfigOption.szDeviceName); f_httpClient.print("\"></label><hr style=\"border:1px solid #ddd;margin:15px 0;\">");
            
#ifdef ENABLE_MQTT // MQTT機能は作成中
            // MQTT設定項目
            f_httpClient.print("<h3>MQTT Settings</h3>");
            f_httpClient.print("<label>Status: <select name=\"mqtt_en\">");
            // 現在の設定値に基づいて selected 属性を付与する
            f_httpClient.print("<option value=\"0\""); if(pstFlashData->stNwConfigOption.isMqttEnable == 0) f_httpClient.print(" selected"); f_httpClient.print(">Disabled</option>");
            f_httpClient.print("<option value=\"1\""); if(pstFlashData->stNwConfigOption.isMqttEnable == 1) f_httpClient.print(" selected"); f_httpClient.print(">Enabled</option>");
            f_httpClient.print("</select></label><br><br>");
            f_httpClient.print("<label>Broker IP: ");
            for (int i=0; i<NW_CONFIG_IP_ADDR_SIZE; i++) {
                f_httpClient.print("<input type=\"number\" name=\"mip"); f_httpClient.print(i);
                f_httpClient.print("\" min=\"0\" max=\"255\" style=\"width:50px;\" value=\"");
                f_httpClient.print(pstFlashData->stNwConfigOption.aMqttBrokerIpAddr[i]); f_httpClient.print("\">");
                if(i<NW_CONFIG_IP_ADDR_SIZE-1) f_httpClient.print(" . ");
            }
            f_httpClient.print("</label><hr style=\"border:1px solid #ddd;margin:15px 0;\">");
#endif
            
            // Email(SMTP, 宛先, パスワード, 送信間隔)の設定項目
            f_httpClient.print("<h3>Email Settings</h3>");
            f_httpClient.print("<label>Status: <select name=\"email_en\">");
            f_httpClient.print("<option value=\"0\""); if(pstFlashData->stNwConfigOption.isEmailEnable == 0) f_httpClient.print(" selected"); f_httpClient.print(">Disabled</option>");
            f_httpClient.print("<option value=\"1\""); if(pstFlashData->stNwConfigOption.isEmailEnable == 1) f_httpClient.print(" selected"); f_httpClient.print(">Enabled</option>");
            f_httpClient.print("</select></label><br><br>");
            
            f_httpClient.print("<label>SMTP Host: <input type=\"text\" name=\"smtp_host\" maxlength=\"");
            f_httpClient.print(NW_CONFIG_SMTP_HOST_SIZE - 1);
            f_httpClient.print("\" value=\""); f_httpClient.print(pstFlashData->stNwConfigOption.szSmtpHostName); f_httpClient.print("\"></label><br><br>");
            f_httpClient.print("<label>SMTP Port: <input type=\"number\" name=\"smtp_port\" min=\"1\" max=\"65535\" value=\"");
            f_httpClient.print(pstFlashData->stNwConfigOption.smtpPort); f_httpClient.print("\"></label><br><br>");
            f_httpClient.print("<label>SMTP Username: <input type=\"text\" name=\"smtp_user\" maxlength=\"");
            f_httpClient.print(NW_CONFIG_EMAIL_ADDR_SIZE - 1);
            f_httpClient.print("\" value=\""); f_httpClient.print(pstFlashData->stNwConfigOption.szSmtpUser); f_httpClient.print("\"></label><br><br>");
            f_httpClient.print("<label>SMTP Password: <input type=\"password\" name=\"smtp_pw\" maxlength=\"");
            f_httpClient.print(NW_CONFIG_SMTP_PASSWORD_SIZE - 1);
            
            // パスワードの入力状態に応じてプレースホルダ(********)の表示を切り替える
            if (pstFlashData->stNwConfigOption.szSmtpPassword[0] != '\0') {
                f_httpClient.print("\" value=\"\" placeholder=\"********\" autocomplete=\"new-password\"></label><br>");
                f_httpClient.print("<label><input type=\"checkbox\" name=\"clr_pw\" value=\"1\"> Clear Password</label><br><br>");
            } else {
                f_httpClient.print("\" value=\"\" placeholder=\"(Not set)\" autocomplete=\"new-password\"></label><br><br>");
            }
            f_httpClient.print("<label>Recipient Email: <input type=\"text\" name=\"recipient_email\" maxlength=\"");
            f_httpClient.print(NW_CONFIG_EMAIL_ADDR_SIZE - 1);
            f_httpClient.print("\" value=\""); f_httpClient.print(pstFlashData->stNwConfigOption.szRecipientEmail); f_httpClient.print("\"></label><br><br>");
            
            f_httpClient.print("<label>Interval (hours): <input type=\"number\" name=\"interval\" min=\"0\" max=\"24\" value=\"");
            f_httpClient.print(pstFlashData->stNwConfigOption.mailIntervalHour);
            f_httpClient.print("\"> <span style=\"font-size:0.85em;color:#666;\">(0 to disable)</span></label><hr style=\"border:1px solid #ddd;margin:15px 0;\">");
            
            // アラート(Alert Settings)の設定項目 (ALT_MAX個分ループして出力)
            f_httpClient.print("<h3>Alert Settings</h3>");
            
            const char* displayedSensors[ALT_MAX] = {0};
            int displayedCount = 0;

            for (int i = 0; i < ALT_MAX; i++) {
                // 表示するセンサー名を決定
                const char* targetSensor = "";
                
                // まずは既存の設定値(FLASH)から、まだ表示していないものを探す
                for (int j = 0; j < ALT_MAX; j++) {
                    if (pstFlashData->stNwConfigOption.astAlertConfig[j].szSensorName[0] != '\0') {
                        // 不要な亡霊(ゾンビ)センサーの除外判定
                        // Disableであり、かつ最新のJSONキーにも存在しない場合は、表示対象から除外する
                        if (pstFlashData->stNwConfigOption.astAlertConfig[j].isEnable == 0) {
                            bool foundInJson = false;
                            for (int m = 0; m < f_jsonKeyCount; m++) {
                                if (strcmp(pstFlashData->stNwConfigOption.astAlertConfig[j].szSensorName, f_jsonKeys[m]) == 0) {
                                    foundInJson = true;
                                    break;
                                }
                            }
                            if (!foundInJson) {
                                continue; // 画面に出さずにスキップすることで、Save時にFLASHから完全消去させる
                            }
                        }

                        bool already = false;
                        for (int k = 0; k < displayedCount; k++) {
                            if (strcmp(pstFlashData->stNwConfigOption.astAlertConfig[j].szSensorName, displayedSensors[k]) == 0) {
                                already = true;
                                break;
                            }
                        }
                        if (!already) {
                            targetSensor = pstFlashData->stNwConfigOption.astAlertConfig[j].szSensorName;
                            break;
                        }
                    }
                }

                // FLASHから見つからなければ、最新のJSONキーから探す
                if (targetSensor[0] == '\0') {
                    for (int j = 0; j < f_jsonKeyCount; j++) {
                        bool already = false;
                        for (int k = 0; k < displayedCount; k++) {
                            if (strcmp(f_jsonKeys[j], displayedSensors[k]) == 0) {
                                already = true;
                                break;
                            }
                        }
                        if (!already) {
                            targetSensor = f_jsonKeys[j];
                            break;
                        }
                    }
                }

                // 割り当てるセンサーがなく、既存の設定も空の場合は、このアラート設定枠を非表示にする
                if (targetSensor[0] == '\0') {
                    continue;
                }
                
                displayedSensors[displayedCount++] = targetSensor;

                // 表示するセンサー名に紐づく過去の設定を検索する
                ST_ALERT_CONFIG stAlertToShow;
                memset(&stAlertToShow, 0, sizeof(stAlertToShow));
                stAlertToShow.condition = IOT_COND_GE; // 万が一のための初期値
                
                for (int j = 0; j < ALT_MAX; j++) {
                    if (pstFlashData->stNwConfigOption.astAlertConfig[j].szSensorName[0] != '\0' &&
                        strcmp(pstFlashData->stNwConfigOption.astAlertConfig[j].szSensorName, targetSensor) == 0) {
                        stAlertToShow = pstFlashData->stNwConfigOption.astAlertConfig[j];
                        break;
                    }
                }

                f_httpClient.print("<h4>Alert "); f_httpClient.print(i + 1); f_httpClient.print("</h4>");
                f_httpClient.print("<label>Status: <select name=\"en"); f_httpClient.print(i); f_httpClient.print("\">");
                f_httpClient.print("<option value=\"0\""); if(stAlertToShow.isEnable == 0) f_httpClient.print(" selected"); f_httpClient.print(">Disabled</option>");
                f_httpClient.print("<option value=\"1\""); if(stAlertToShow.isEnable == 1) f_httpClient.print(" selected"); f_httpClient.print(">Enabled</option>");
                f_httpClient.print("</select></label><br><br>");
                
                f_httpClient.print("<label>Sensor: <b>");
                f_httpClient.print(targetSensor);
                f_httpClient.print("</b></label>");
                f_httpClient.print("<input type=\"hidden\" name=\"sensor"); f_httpClient.print(i); f_httpClient.print("\" value=\"");
                f_httpClient.print(targetSensor); f_httpClient.print("\"><br><br>");
                
                f_httpClient.print("<label>Condition: <select name=\"ope"); f_httpClient.print(i); f_httpClient.print("\">");
                // 比較条件の展開
                f_httpClient.print("<option value=\""); f_httpClient.print(IOT_COND_GE); f_httpClient.print("\""); if(stAlertToShow.condition == IOT_COND_GE) f_httpClient.print(" selected"); f_httpClient.print(">&gt;=</option>");
                f_httpClient.print("<option value=\""); f_httpClient.print(IOT_COND_LE); f_httpClient.print("\""); if(stAlertToShow.condition == IOT_COND_LE) f_httpClient.print(" selected"); f_httpClient.print(">&lt;=</option>");
                f_httpClient.print("</select></label><br><br>");
                f_httpClient.print("<label>Threshold: <input type=\"number\" step=\"any\" name=\"th"); f_httpClient.print(i); f_httpClient.print("\" value=\"");
                f_httpClient.print(stAlertToShow.threshold, 3);
                f_httpClient.print("\"></label><hr style=\"border:1px solid #ddd;margin:15px 0;\">");
            }
            
            f_httpClient.print("<input type=\"submit\" value=\"Save\" style=\"padding:10px 25px;background:#007bff;color:#fff;border:none;border-radius:5px;font-size:16px;cursor:pointer;\"></form>");
            f_httpClient.print("</body></html>");
        } else if (f_isRootAccess && pszJson != NULL && pszJson[0] != '\0') {
            
            // ルート画面 (センサー閲覧画面) の生成
            snprintf(f_titleBuf, sizeof(f_titleBuf), "%s[%s]", FW_NAME, pstFlashData->stNwConfigOption.szDeviceName);
            
            // キャッシュしているJSONデータをJavaScriptの変数として直接埋め込み、
            // クライアント(ブラウザ)側で動的に表の行を生成させる
            f_httpClient.print(
                "<!DOCTYPE html><html><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
                "<title>"
            );
            f_httpClient.print(f_titleBuf);
            f_httpClient.print(
                "</title>"
                "<style>body{font-family:sans-serif;background:#f0f2f5;padding:20px;color:#333;}"
                "h2{text-align:center;}table{width:100%;max-width:500px;margin:0 auto;border-collapse:collapse;background:#fff;box-shadow:0 2px 4px rgba(0,0,0,0.1);}"
                "th,td{padding:12px;border-bottom:1px solid #ddd;text-align:left;}th{background:#007bff;color:#fff;}"
                "a.btn{display:block;width:fit-content;margin:20px auto;padding:10px 20px;background:#6c757d;color:#fff;text-decoration:none;border-radius:5px;text-align:center;}</style>"
                "</head><body><h2>"
            );
            f_httpClient.print(f_titleBuf);
            f_httpClient.print(
                "</h2>"
                "<a href=\"/config\" class=\"btn\">Settings</a>"
                "<table><tr><th>Sensor</th><th>Value</th></tr>"
                "<tbody id=\"data\"></tbody></table>"
            );
            
            f_httpClient.print(
                "<script>const jsonData = "
            );
            f_httpClient.print(pszJson);
            f_httpClient.print(
                ";const tbody = document.getElementById('data');"
                "for(const [key, value] of Object.entries(jsonData)){"
                "tbody.innerHTML += `<tr><td>${key}</td><td>${value}</td></tr>`;"
                "}</script></body></html>"
            );
        } else if (f_isRootAccess) {
            // JSONデータがまだ到着していない起動直後等の表示
            f_httpClient.print("<!DOCTYPE html><html><body><h2 style=\"text-align:center;\">No Data Yet</h2></body></html>");
        }
    } else {
        // 定義されていないパスへのアクセスには 404 エラーを返す
        f_httpClient.print(
            "HTTP/1.1 404 Not Found\r\n"
            "Connection: close\r\n"
            "\r\n"
        );
    }
}

// -----------------------------------------------------------------------------
// 設定の保存と再起動
// -----------------------------------------------------------------------------
static void HTTP_SaveConfigAndReboot() 
{
    // 設定に変更があった場合のみ、FLASHデータ構造体の各項目を新しい値で上書きする
#ifdef ENABLE_MQTT // MQTT機能は作成中
    if (f_bMqttChanged) {
        if (f_newMqttEn >= 0) {
            f_stFlashData.stNwConfigOption.isMqttEnable = f_newMqttEn;
        }
        for (int i = 0; i < NW_CONFIG_IP_ADDR_SIZE; i++) {
            if (f_newMqttBrokerIp[i] >= 0 && f_newMqttBrokerIp[i] <= 255) {
                f_stFlashData.stNwConfigOption.aMqttBrokerIpAddr[i] = f_newMqttBrokerIp[i];
            }
        }
    }
#endif
    if (f_bEmailChanged) {
        if (f_newEmailEn >= 0) {
            f_stFlashData.stNwConfigOption.isEmailEnable = f_newEmailEn;
        }
        CMN_Strncpy(f_stFlashData.stNwConfigOption.szSmtpHostName, f_szSmtpHostName, sizeof(f_stFlashData.stNwConfigOption.szSmtpHostName));
        if (f_newSmtpPort >= 1 && f_newSmtpPort <= 65535) {
            f_stFlashData.stNwConfigOption.smtpPort = f_newSmtpPort;
        }
        CMN_Strncpy(f_stFlashData.stNwConfigOption.szSmtpUser, f_szSmtpUser, sizeof(f_stFlashData.stNwConfigOption.szSmtpUser));
        CMN_Strncpy(f_stFlashData.stNwConfigOption.szSmtpPassword, f_szSmtpPassword, sizeof(f_stFlashData.stNwConfigOption.szSmtpPassword));
        CMN_Strncpy(f_stFlashData.stNwConfigOption.szRecipientEmail, f_szRecipientEmail, sizeof(f_stFlashData.stNwConfigOption.szRecipientEmail));
    }
    if (f_bNameChanged) {
        CMN_Strncpy(f_stFlashData.stNwConfigOption.szDeviceName, f_szDeviceName, sizeof(f_stFlashData.stNwConfigOption.szDeviceName));
    }
    if (f_bIntervalChanged) {
        f_stFlashData.stNwConfigOption.mailIntervalHour = f_newInterval;
    }
    if (f_bAlertChanged) {
        // 現在のアラート設定を一旦すべてクリアする(不要になったゾンビ設定を消去するため)
        memset(f_stFlashData.stNwConfigOption.astAlertConfig, 0, sizeof(f_stFlashData.stNwConfigOption.astAlertConfig));
        
        // フォームから送信された有効なアラート設定を先頭から順に詰め直す
        int saveIdx = 0;
        for (int i = 0; i < ALT_MAX; i++) {
            if (f_newAlertEn[i] >= 0 && f_newAlertSensor[i][0] != '\0' && (f_newAlertCond[i] == IOT_COND_GE || f_newAlertCond[i] == IOT_COND_LE)) {
                f_stFlashData.stNwConfigOption.astAlertConfig[saveIdx].isEnable = f_newAlertEn[i];
                CMN_Strncpy(f_stFlashData.stNwConfigOption.astAlertConfig[saveIdx].szSensorName, f_newAlertSensor[i], sizeof(f_stFlashData.stNwConfigOption.astAlertConfig[saveIdx].szSensorName));
                f_stFlashData.stNwConfigOption.astAlertConfig[saveIdx].condition = f_newAlertCond[i];
                f_stFlashData.stNwConfigOption.astAlertConfig[saveIdx].threshold = f_newAlertThresh[i];
                saveIdx++;
            }
        }
    }
    
    // 最終的にいずれか一つでも変更があった場合のみ、不揮発性メモリ(FLASH)へ保存処理を行う
#ifdef ENABLE_MQTT // MQTT機能は作成中
    if (f_bIntervalChanged || f_bAlertChanged || f_bMqttChanged || f_bEmailChanged || f_bNameChanged) {
#else
    if (f_bIntervalChanged || f_bAlertChanged || f_bEmailChanged || f_bNameChanged) {
#endif
        delay(HTTP_DELAY_WAIT_RES);
        FLASH_Write(&f_stFlashData); // 内部でシステム再起動(リセット)が実行される
    }
}
