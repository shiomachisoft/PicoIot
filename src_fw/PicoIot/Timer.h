// Copyright © 2024 Shiomachi Software. All rights reserved.
#ifndef TIMER_H
#define TIMER_H

#include "Common.h"

// [define]
#define TMR_CALLBACK_PERIOD         50 // 定期タイマコールバックの周期(ms)
#define TMR_STABILIZATION_WAIT_TIME (200 / TMR_CALLBACK_PERIOD)   // 起動してからの安定待ち時間(ms)

// [関数プロトタイプ宣言] 
void TMR_WdtClear();
bool TMR_IsStabilizationWaitTimePassed();
void TMR_ClearRecvTimeout();
bool TMR_IsRecvTimeout();
void TMR_Init();

#endif