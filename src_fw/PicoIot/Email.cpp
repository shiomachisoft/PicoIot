// Copyright © 2024 Shiomachi Software. All rights reserved.
#include "Common.h"

// [define]
#define EMAIL_SUBJECT_BUF_SIZE        (IOT_DISPLAY_NAME_BUF_SIZE + 64)  // メールの件名(Subject)を構築するためのバッファサイズ(デバイス表示名 ＋ 固定文字列等)
#define EMAIL_ROW_BUF_SIZE            (IOT_JSON_KEY_BUF_SIZE + IOT_JSON_VAL_BUF_SIZE + 64)  // テキストメール本文の1行分のデータを構築するバッファサイズ
#define EMAIL_MESSAGE_BUF_SIZE        (EMAIL_ROW_BUF_SIZE * IOT_JSON_MAX_PAIRS + ALT_REASON_BUF_SIZE + 256) // メールのテキスト本文全体を構築するためのバッファサイズ(固定文字列等の余裕分を含む)

// [ファイルスコープ変数]
static SMTPSession f_smtp;                              // ESP Mail ClientライブラリのSMTPセッションオブジェクト(接続状態やタイムアウトを管理)
static Session_Config f_stConfig;                       // SMTPサーバーへの接続設定情報(ホスト名、ポート番号、ログイン情報など)
static SMTP_Message f_email_msg;                        // メールの実体となるメッセージ情報(送信者、宛先、件名、本文など)
static volatile bool f_isEmailSending = false;          // 現在バックグラウンドでメール送信処理が実行中か否かを示すフラグ(多重起動防止用)
static char f_senderName[IOT_DISPLAY_NAME_BUF_SIZE];    // 送信元名として表示する文字列(FW名とデバイス名を組み合わせたもの)のバッファ
static char f_subjectBuf[EMAIL_SUBJECT_BUF_SIZE];       // 構築されたメールの件名(Subject)を保持するバッファ
static char f_messageBuf[EMAIL_MESSAGE_BUF_SIZE];       // 構築されたテキスト形式のメール本文全体を保持するバッファ
static char f_rowBuf[EMAIL_ROW_BUF_SIZE];               // センサー1つ分の測定結果を生成する際の一時バッファ

// [関数プロトタイプ宣言]
static void EMAIL_SmtpCallback(SMTP_Status status);

// -----------------------------------------------------------------------------
// メール送信可能判定
// -----------------------------------------------------------------------------
bool EMAIL_IsSendable()
{
    ST_FLASH_DATA* pstFlashData = FLASH_GetDataAtPowerOn();
    if (!pstFlashData->stNwConfigOption.isEmailEnable) {
        return false;
    }
    if (f_isEmailSending) {
        return false;
    }
    return true;
}

// -----------------------------------------------------------------------------
// SMTPコールバック関数
// 非同期メール送信のステータス変化時(完了やエラー発生時)にライブラリから呼び出される。
// -----------------------------------------------------------------------------
static void EMAIL_SmtpCallback(SMTP_Status status)
{
    // [WDTクラッシュ(フリーズからの再起動)防止]
    // SMTP通信はTLSハンドシェイクやサーバー応答待ちで数秒〜数十秒のブロッキングが発生する。
    // 同期送信中はメインループが停止しWDTタイマがクリアされなくなるため、
    // ステータスが進行するこのコールバック内でこまめにWDTをクリア(延長)し、システムのクラッシュを防ぐ。
    TMR_WdtClear();
}

// -----------------------------------------------------------------------------
// メール送信実行処理
// -----------------------------------------------------------------------------
bool EMAIL_SendMail(ST_FLASH_DATA* pstFlashData, bool isTriggerAlert, const char* triggerReasonBuf, int pairCount, char keys[][IOT_JSON_KEY_BUF_SIZE], char vals[][IOT_JSON_VAL_BUF_SIZE])
{
    bool bRet = false;

    if (!EMAIL_IsSendable()) {
        return false;
    }

    // 送信者(Sender)名として、FW名とデバイス名を組み合わせた文字列を直接構築する
    snprintf(f_senderName, sizeof(f_senderName), "%s[%s]", FW_NAME, pstFlashData->stNwConfigOption.szDeviceName);

    // メールの件名を作成
    if (isTriggerAlert) {
        snprintf(f_subjectBuf, sizeof(f_subjectBuf), "[Alert] %s - Sensor Alert Triggered", f_senderName);
    } else {
        snprintf(f_subjectBuf, sizeof(f_subjectBuf), "[Report] %s - Periodic Sensor Data", f_senderName);
    }

    // メール送信(SMTP)設定
    f_stConfig.server.host_name = pstFlashData->stNwConfigOption.szSmtpHostName;
    f_stConfig.server.port = pstFlashData->stNwConfigOption.smtpPort;
    f_stConfig.login.email = pstFlashData->stNwConfigOption.szSmtpUser;
    f_stConfig.login.password = pstFlashData->stNwConfigOption.szSmtpPassword;

    f_email_msg.clear();
    f_email_msg.sender.name = f_senderName;
    // 認証不要のSMTPサーバに対応するため、SMTP Userが空文字の場合は送信元アドレスに宛先アドレスを代用する
    f_email_msg.sender.email = pstFlashData->stNwConfigOption.szSmtpUser[0] != '\0' ? pstFlashData->stNwConfigOption.szSmtpUser : pstFlashData->stNwConfigOption.szRecipientEmail;
    f_email_msg.subject = f_subjectBuf;
    f_email_msg.addRecipient("", pstFlashData->stNwConfigOption.szRecipientEmail);

    // メール本文の作成 (テキスト形式)
    snprintf(f_messageBuf, sizeof(f_messageBuf), 
             "Sensor Data\n\n"
             "Trigger Reason: %s\n\n"
             "--- Sensor Values ---\n",
             (triggerReasonBuf != NULL) ? triggerReasonBuf : "N/A");
    
    // データ行を動的に追加
    for (int i = 0; i < pairCount; i++) {
        snprintf(f_rowBuf, sizeof(f_rowBuf), "%s: %s\n", keys[i], vals[i]);
        if (sizeof(f_messageBuf) > strlen(f_messageBuf) + 1) {
            strncat(f_messageBuf, f_rowBuf, sizeof(f_messageBuf) - strlen(f_messageBuf) - 1);
        }
    }

    f_email_msg.text.content = f_messageBuf;
    f_email_msg.text.charSet = "utf-8";

    // 同期(ブロッキング)メール送信の実行
    f_isEmailSending = true;
    f_smtp.callback(EMAIL_SmtpCallback);
    if (f_smtp.connect(&f_stConfig)) {
        if (MailClient.sendMail(&f_smtp, &f_email_msg, true/* 送信後にSMTPセッションを閉じる */)) {
            bRet = true;
        }
    }

    // ESP Mail Clientの内部メモリ解放(リーク防止)は、送信関数が完全にリターンしたこのタイミングで行う
    f_smtp.sendingResult.clear();

    // 次回の送信まで無駄なヒープメモリ(約5KBの本文データなど)を占有し続けないよう、メッセージオブジェクトも即座にクリアする
    f_email_msg.clear();

    // 同期送信が完了したため、確実にフラグを下ろしてスタックを防止する
    f_isEmailSending = false;
    return bRet;
}
