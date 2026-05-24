// Copyright © 2024 Shiomachi Software. All rights reserved.
#include "Common.h"

// [define] 
#define TMR_RECV_TIMEOUT    (500 / TMR_CALLBACK_PERIOD)   // 要求フレームのヘッダを受信後、500[ms]経過しても要求フレームの末尾まで受信してない場合はタイムアウトとする
#define TMR_WDT_TIMEOUT     (60000 / TMR_CALLBACK_PERIOD) // WDTタイマクリアのタイムアウト時間(60000ms) ※FLASHの消去・書き込みやメール送信が間に合う時間にすること

// [ファイルスコープ変数の宣言]
static repeating_timer_t f_stTimer = {0};       // 定期タイマコールバック登録時に渡すパラメータ
static volatile ULONG f_timerCnt_wdt = 0;                // WDTタイマのタイマカウント
static volatile ULONG f_timerCnt_stabilizationWait = 0;  // 起動してからの安定待ち時間のタイマカウント
static volatile ULONG f_timerCnt_recvTimeout = 0;        // 受信タイムアウト判定用のタイマカウント(ヘッダ受信後、末尾まで受信しない場合のタイムアウト)

// [関数プロトタイプ宣言]
static bool TMR_PeriodicCallback(repeating_timer_t *pstTimer);

// 定期タイマコールバック
static bool TMR_PeriodicCallback(repeating_timer_t *pstTimer) 
{
    // [WDTタイマのタイマカウント]
    if (f_timerCnt_wdt <  TMR_WDT_TIMEOUT) { // タイムアウトしてない場合
        f_timerCnt_wdt++;
    }
    else { // タイムアウトした場合
        // watchdog_enable()を使用して即WDTタイムアウトで再起動する
        CMN_WdtEnableReboot();
    }

    // [起動してからの安定待ち時間のタイマカウント]
    if (f_timerCnt_stabilizationWait < TMR_STABILIZATION_WAIT_TIME) {
        f_timerCnt_stabilizationWait++;
    }

    // 受信タイムアウト判定用のタイマカウント
    if (f_timerCnt_recvTimeout < TMR_RECV_TIMEOUT) {
        f_timerCnt_recvTimeout++;
    }

    return true; // タイマーを継続(trueを返す)
}

// WDTタイマのタイマカウントをクリア
void TMR_WdtClear()
{
    f_timerCnt_wdt = 0;
}

// 起動してからの安定待ち時間が経過したかどうかを取得
bool TMR_IsStabilizationWaitTimePassed()
{
    return (f_timerCnt_stabilizationWait >=TMR_STABILIZATION_WAIT_TIME) ? true : false;
}

// 受信タイムアウト判定用のタイマカウントをクリア
void TMR_ClearRecvTimeout()
{
    f_timerCnt_recvTimeout = 0;
}

// 受信タイムアウトが発生したか否かを取得
bool TMR_IsRecvTimeout()
{
    return (f_timerCnt_recvTimeout >= TMR_RECV_TIMEOUT) ? true : false;
}

// タイマーを初期化
void TMR_Init()
{
    // 定期タイマコールバックの登録
    add_repeating_timer_ms(TMR_CALLBACK_PERIOD, TMR_PeriodicCallback, NULL, &f_stTimer);
}