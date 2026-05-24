// Copyright © 2024 Shiomachi Software. All rights reserved.
#ifndef EMAIL_H
#define EMAIL_H

#include "Common.h"

// [define]
#define EMAIL_DEFAULT_INTERVAL_HOUR 1           // メール送信間隔(時間)
#define EMAIL_GMAIL_SMTP_HOST "smtp.gmail.com"  // デフォルトのSMTPホスト名(Gmail)
#define EMAIL_GMAIL_SMTP_PORT 465               // デフォルトのSMTPポート番号(Gmail)

// [関数プロトタイプ宣言]
bool EMAIL_IsSendable();
bool EMAIL_SendMail(ST_FLASH_DATA* pstFlashData, bool triggerAlert, const char* triggerReasonBuf, int pairCount, char keys[][IOT_JSON_KEY_BUF_SIZE], char vals[][IOT_JSON_VAL_BUF_SIZE]);

#endif