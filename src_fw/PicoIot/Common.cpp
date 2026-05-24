// Copyright © 2024 Shiomachi Software. All rights reserved.
#include "Common.h" 

// [ファイルスコープ変数]
static volatile ULONG f_errorBits = 0; // FWエラー
static critical_section_t f_stSpinLock = {0};   // スピンロック

// スピンロックを獲得
// スピンロックはCPU間排他をしつつ割り込みを禁止にする場合に使用する。
// CPU間排他だけならミューテックスを使用すること。
// Picoのcritical_section(spin lock)とmutexの定義は下記。
// https://www.raspberrypi.com/documentation/pico-sdk/high_level.html#pico_sync
// critical_section(spin lock):
// Critical Section API for short-lived mutual exclusion safe for IRQ and multi-core. 
// mutex:
// Mutex API for non IRQ mutual exclusion between cores. 
void CMN_EnterSpinLock()
{
	critical_section_enter_blocking(&f_stSpinLock);
}

// スピンロックを解放
void CMN_ExitSpinLock()
{
	critical_section_exit(&f_stSpinLock);
}

// チェックサム検査を実行
bool CMN_Checksum(PVOID pBuf, USHORT expect, ULONG size)
{
	bool bRet;

	bRet = (CMN_CalcChecksum(pBuf, size) == expect) ? true : false;
	return bRet;
}

// チェックサムを計算
USHORT CMN_CalcChecksum(PVOID pBuf, ULONG size)
{
	UCHAR* pDataAry = (UCHAR*)pBuf;
	USHORT checksum = 0;
	ULONG i;

	for (i = 0; i < size; i++) {
		checksum += pDataAry[i];			
	}

	return checksum;
}

// FWエラーを設定
void CMN_SetErrorBits(ULONG errorBit, bool bSpinLock)
{
	if (bSpinLock) {
		CMN_EnterSpinLock();
	}

	f_errorBits |= errorBit; // OR演算はアトミックではないので排他する

	if (bSpinLock) {
		CMN_ExitSpinLock();
	}
}

// FWエラーを取得
ULONG CMN_GetFwErrorBits()
{
	return f_errorBits;
}

// FWエラーをクリア
void CMN_ClearFwErrorBits(bool bSpinLock)
{
	if (bSpinLock) {
		CMN_EnterSpinLock();
	}

	f_errorBits = 0;

	if (bSpinLock) {
		CMN_ExitSpinLock();
	}	
}

// watchdog_enable()を使用して即WDTタイムアウトで再起動する
void CMN_WdtEnableReboot()
{
    watchdog_enable(1, true);
    while (1) {
        tight_loop_contents();
    }
}

// watchdog_enable()を使用しないで即WDTタイムアウトで再起動する
void CMN_WdtRebootWithoutEnable()
{
    watchdog_reboot(0, 0, 1);
    while (1) {
        tight_loop_contents();
    }
}

// strncpyを実行した後に終端NULL文字を必ず付与する
char* CMN_Strncpy(char* dest, const char* src, size_t n)
{
	if (dest == NULL || src == NULL || n == 0) {
		return dest;
	}

	char* ret = strncpy(dest, src, n);
	dest[n - 1] = '\0';
	
	return ret;
}

// 共通ライブラリを初期化
void CMN_Init()
{
	// [変数を初期化]
	critical_section_init(&f_stSpinLock);
}