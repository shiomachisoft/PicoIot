// Copyright © 2024 Shiomachi Software. All rights reserved.
#include "Common.h"

// [define]
// Picoの場合:
// PICO_FLASH_SIZE_BYTES = 0x200000
// FLASH_SECTOR_SIZE = 0x1000
// FLASH_PAGE_SIZE = 256
#define FLASH_OFFSET  (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE) // FLASHの最後のセクタの先頭オフセットアドレス
#define FLASH_WRITE_BUF_SIZE FLASH_SECTOR_SIZE                    // FLASHデータ書き込みサイズ ※ FLASH_PAGE_SIZEの倍数とする

// [ファイルスコープ変数]
static ST_FLASH_DATA f_stFlashData = {0};            // 電源起動時のFLASHデータ
static UCHAR f_writeBuf[FLASH_WRITE_BUF_SIZE] = {0}; // FLASHデータ書き込みバッファ
static volatile bool f_isFlashWriting = false;       // FLASHアクセス処理中フラグ

static_assert(sizeof(ST_FLASH_DATA) <= FLASH_WRITE_BUF_SIZE, "ST_FLASH_DATA size exceeds FLASH_WRITE_BUF_SIZE");

// 電源起動時のFLASHデータを返す
ST_FLASH_DATA* FLASH_GetDataAtPowerOn()
{
    return &f_stFlashData;
}

// FLASHの最終セクタから設定データを読み込む
void FLASH_Read(ST_FLASH_DATA *pstFlashData)
{
    bool bDefault = false; // デフォルトの設定データを採用するか否か
    char szFwName[FW_NAME_BUF_SIZE];
    const PVOID pSrc = (const PVOID) (XIP_BASE + FLASH_OFFSET); // 最終セクタの先頭アドレス。XIP_BASEはフラッシュメモリの先頭アドレス。
    USHORT checksum;       // チェックサム

    // 最終セクタの先頭アドレスからデータを読み込む
    memcpy(pstFlashData, pSrc, sizeof(ST_FLASH_DATA));

    // [FW名、FWバージョン、チェックサムが全て正常な場合、読み込んだデータがそのまま使用される]
    do {
        // FW名のチェック
        memset(szFwName, 0, sizeof(szFwName));
        strcpy(szFwName, FW_NAME);
        // pstFlashData->szFwNameがNULL文字で終わっているとは限らないのでstrcmpは使用しないで比較する
        if (memcmp(pstFlashData->szFwName, szFwName, FW_NAME_BUF_SIZE) != 0) {
            bDefault = true;  // デフォルトの設定データを採用
            break;
        }

        // FWバージョンのチェック
        if (pstFlashData->fwVer != FW_VER) {
            // FWバージョンが不正値の場合
            bDefault = true;  // デフォルトの設定データを採用
            break;
        }      

        // チェックサム検査
        checksum = CMN_CalcChecksum(pstFlashData, sizeof(ST_FLASH_DATA) - sizeof(pstFlashData->checksum));        
        if (pstFlashData->checksum != checksum) {
            // チェックサム検査がNGの場合
            bDefault = true; // デフォルトの設定データを採用
            break;
        }
    } while(0); 

    if (bDefault) {
        // デフォルトの設定データを採用
        
        // 構造体全体を初期化し、FW名やバージョンを設定
        memset(pstFlashData, 0, sizeof(ST_FLASH_DATA));
        strcpy(pstFlashData->szFwName, FW_NAME);
        pstFlashData->fwVer = FW_VER;
        WIFI_GetWifiDefaultConfig(&pstFlashData->stNwConfig, &pstFlashData->stNwConfigOption);
    }

    // szDeviceNameが未設定(空文字)の場合、ユニークボードIDをszDeviceNameに設定する
    if (pstFlashData->stNwConfigOption.szDeviceName[0] == '\0') {
        pico_unique_board_id_t board_id;
        pico_get_unique_board_id(&board_id);
        snprintf(pstFlashData->stNwConfigOption.szDeviceName, sizeof(pstFlashData->stNwConfigOption.szDeviceName),
                 "%02X%02X%02X%02X%02X%02X%02X%02X",
                 board_id.id[0], board_id.id[1], board_id.id[2], board_id.id[3],
                 board_id.id[4], board_id.id[5], board_id.id[6], board_id.id[7]);
    }
}

// FLASHの最終セクタに設定データを書き込む
void FLASH_Write(ST_FLASH_DATA *pstFlashData)
{
    USHORT checksum;   // チェックサム
    
    // すでに別コアでFLASHアクセス処理が進行中の場合は、再起動されるまで待機する
    CMN_EnterSpinLock();
    if (f_isFlashWriting) {
        CMN_ExitSpinLock();
        while (1) { tight_loop_contents(); }
    }
    f_isFlashWriting = true;
    CMN_ExitSpinLock();

    // FLASHデータ書き込みバッファを0xFF(消去状態)で初期化
    memset(f_writeBuf, 0xFF, sizeof(f_writeBuf));
    // FW名を設定
    memset(pstFlashData->szFwName, 0, sizeof(pstFlashData->szFwName));
    strcpy(pstFlashData->szFwName, FW_NAME);
    // FWバージョンを設定
    pstFlashData->fwVer = FW_VER;
    // チェックサムを計算して設定
    checksum = CMN_CalcChecksum(pstFlashData, sizeof(ST_FLASH_DATA) - sizeof(pstFlashData->checksum));
    pstFlashData->checksum = checksum;
    // FLASHデータ書き込みバッファに引数データをコピー
    memcpy(f_writeBuf, pstFlashData, sizeof(ST_FLASH_DATA));
   
    // CPUコア1をブロック
    rp2040.idleOtherCore();
    // 割り込み禁止
    (void)save_and_disable_interrupts();

    // FLASH消去
    // 消去単位はflash.hで定義されている FLASH_SECTOR_SIZE(4096byte)の倍数とする
    flash_range_erase(FLASH_OFFSET, FLASH_SECTOR_SIZE);
    // FLASH書き込み
    // 書込単位はflash.hで定義されている FLASH_PAGE_SIZE(256byte)の倍数とする
    flash_range_program(FLASH_OFFSET, f_writeBuf, sizeof(f_writeBuf)); // f_writeBufのサイズはFLASH_PAGE_SIZEの倍数になっている
    
    // watchdog_enable()を使用しないで即WDTタイムアウトで再起動する
    CMN_WdtRebootWithoutEnable();
}

// FLASHの最終セクタのデータを消去
void FLASH_Erase()
{
    // すでに別コアでFLASHアクセス処理が進行中の場合は、再起動されるまで待機する
    CMN_EnterSpinLock();
    if (f_isFlashWriting) {
        CMN_ExitSpinLock();
        while (1) { tight_loop_contents(); }
    }
    f_isFlashWriting = true;
    CMN_ExitSpinLock();

    // CPUコア1をブロック
    rp2040.idleOtherCore();
    // 割り込み禁止
    (void)save_and_disable_interrupts();

    // FLASH消去
    // 消去単位はflash.hで定義されている FLASH_SECTOR_SIZE(4096byte)の倍数とする
    flash_range_erase(FLASH_OFFSET, FLASH_SECTOR_SIZE);
    
    // watchdog_enable()を使用しないで即WDTタイムアウトで再起動する
    CMN_WdtRebootWithoutEnable();
}

// FLASHライブラリを初期化
void FLASH_Init()
{
    // 電源起動時のFLASHデータを読み込み
    FLASH_Read(&f_stFlashData);
}