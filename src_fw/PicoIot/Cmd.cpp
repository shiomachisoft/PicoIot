// Copyright © 2024 Shiomachi Software. All rights reserved.
#include "Common.h"

// [マクロ定義]
#define CMD_WAIT_SEND_END 1000 // FLASHに設定データを書き込んでリセットする前の応答フレームの送信完了待ち時間(ms)

// [ファイルスコープ変数]
static ST_FW_INFO f_stFwInfo;               // FW情報保持用
static ST_NW_CONFIG3 f_stNwConfig;          // ネットワーク設定保持用
static ST_FLASH_DATA f_stFlashData;         // FLASHデータ保持用

// [関数プロトタイプ宣言]
static void CMD_ExecReqCmd_GetFwError(ST_FRM_REQ_FRAME *pstReqFrm);
static void CMD_ExecReqCmd_ClearFwError(ST_FRM_REQ_FRAME *pstReqFrm);
static void CMD_ExecReqCmd_EraseFlash(ST_FRM_REQ_FRAME *pstReqFrm);
static void CMD_ExecReqCmd_GetFwInfo(ST_FRM_REQ_FRAME *pstReqFrm);
static void CMD_ExecReqCmd_SetNwConfig3(ST_FRM_REQ_FRAME *pstReqFrm);
static void CMD_ExecReqCmd_GetNwConfig3(ST_FRM_REQ_FRAME *pstReqFrm);

// 要求コマンドの実行
void CMD_ExecReqCmd(ST_FRM_REQ_FRAME *pstReqFrm)
{   
    switch (pstReqFrm->cmd) 
    {
        // FW情報取得コマンド
        case CMD_GET_FW_INFO:
            CMD_ExecReqCmd_GetFwInfo(pstReqFrm);
            break;                           
        // FWエラー取得コマンド
        case CMD_GET_FW_ERR:
            CMD_ExecReqCmd_GetFwError(pstReqFrm);
            break;    
        // FWエラークリアコマンド
        case CMD_CLEAR_FW_ERR:
            CMD_ExecReqCmd_ClearFwError(pstReqFrm);
            break; 
        // FLASH消去コマンド
        case CMD_ERASE_FLASH: 
            CMD_ExecReqCmd_EraseFlash(pstReqFrm);
            break;             
        // ネットワーク設定変更コマンド3
        case CMD_SET_NW_CONFIG3:
            CMD_ExecReqCmd_SetNwConfig3(pstReqFrm);
            break;    
        // ネットワーク設定取得コマンド3
        case CMD_GET_NW_CONFIG3:
            CMD_ExecReqCmd_GetNwConfig3(pstReqFrm);
            break;                  
        default:
            break;       
    }
}

// FW情報取得コマンドの実行
static void CMD_ExecReqCmd_GetFwInfo(ST_FRM_REQ_FRAME *pstReqFrm)
{
    USHORT expectedSize = 0;            // 要求フレームのデータサイズの期待値
    USHORT dataSize = 0;                // 応答フレームのデータサイズ
    USHORT errCode = FRM_ERR_SUCCESS;   // エラーコード
    PVOID pBuf = NULL;                  // 応答フレームのデータ
    
    // データサイズをチェック
    if (pstReqFrm->dataSize != expectedSize) {
        errCode = FRM_ERR_DATA_SIZE; // データサイズが不正
    }
    else { // 正常系
        memset(&f_stFwInfo, 0, sizeof(f_stFwInfo));
        strcpy(f_stFwInfo.szMakerName, MAKER_NAME);     // メーカー名
        strcpy(f_stFwInfo.szFwName, FW_NAME);           // FW名
        f_stFwInfo.fwVer = FW_VER;                      // FWバージョン
        pico_get_unique_board_id(&f_stFwInfo.board_id); // ユニークボードID サイズ = PICO_UNIQUE_BOARD_ID_SIZE_BYTES

        dataSize = sizeof(f_stFwInfo); // 応答フレームのデータサイズ
        pBuf = (PVOID)&f_stFwInfo;     // 応答フレームのデータ
    }

    // 応答フレームを送信
    FRM_SendResFrm(pstReqFrm->seqNo, pstReqFrm->cmd, errCode, dataSize, pBuf);
}

// FWエラー取得コマンドの実行
static void CMD_ExecReqCmd_GetFwError(ST_FRM_REQ_FRAME *pstReqFrm)
{
    USHORT expectedSize = 0;            // 要求フレームのデータサイズの期待値
    USHORT dataSize = 0;                // 応答フレームのデータサイズ
    USHORT errCode = FRM_ERR_SUCCESS;   // エラーコード
    ULONG errorBits = 0;                // FWエラー
    PVOID pBuf = NULL;                  // 応答フレームのデータ部

    // データサイズをチェック
    if (pstReqFrm->dataSize != expectedSize) {
        errCode = FRM_ERR_DATA_SIZE; // データサイズが不正
    }
    else { // 正常系
        // FWエラーを取得
        errorBits = CMN_GetFwErrorBits();

        dataSize = sizeof(errorBits); // 応答フレームのデータサイズ
        pBuf = (PVOID)&errorBits;     // 応答フレームのデータ
    }

    // 応答フレームを送信
    FRM_SendResFrm(pstReqFrm->seqNo, pstReqFrm->cmd, errCode, dataSize, pBuf);
}

// FWエラークリアコマンドの実行
static void CMD_ExecReqCmd_ClearFwError(ST_FRM_REQ_FRAME *pstReqFrm)
{
    USHORT expectedSize = 0;            // 要求フレームのデータサイズの期待値
    USHORT errCode = FRM_ERR_SUCCESS;   // エラーコード

    // データサイズをチェック
    if (pstReqFrm->dataSize != expectedSize) {
        errCode = FRM_ERR_DATA_SIZE; // データサイズが不正
    }
    else { // 正常系
        // FWエラークリア
        CMN_ClearFwErrorBits(true);
    }

    // 応答フレームを送信
    FRM_SendResFrm(pstReqFrm->seqNo, pstReqFrm->cmd, errCode, 0, NULL); 
}

// FLASH消去コマンドの実行
static void CMD_ExecReqCmd_EraseFlash(ST_FRM_REQ_FRAME *pstReqFrm)
{
    USHORT expectedSize = 0;            // 要求フレームのデータサイズの期待値
    USHORT errCode = FRM_ERR_SUCCESS;   // エラーコード

    // データサイズをチェック
    if (pstReqFrm->dataSize != expectedSize) {
        errCode = FRM_ERR_DATA_SIZE;    // データサイズが不正
    }

    // 成功・失敗に関わらず応答フレームを送信
    FRM_SendResFrm(pstReqFrm->seqNo, pstReqFrm->cmd, errCode, 0, NULL);

    if (errCode == FRM_ERR_SUCCESS) { // 正常系
        // USBの応答フレーム送信完了(ホスト側からの読み取り完了)を待つ
        delay(CMD_WAIT_SEND_END);
        // FLASH消去
        FLASH_Erase(); // マイコンはリセットされる
    }
}

// ネットワーク設定変更コマンドの実行
static void CMD_ExecReqCmd_SetNwConfig3(ST_FRM_REQ_FRAME *pstReqFrm)
{
    USHORT expectedSize = 0;            // 要求フレームのデータサイズの期待値
    USHORT errCode = FRM_ERR_SUCCESS;   // エラーコード

    // データサイズをチェック
    expectedSize = sizeof(ST_NW_CONFIG3);
    if (pstReqFrm->dataSize != expectedSize) {
        errCode = FRM_ERR_DATA_SIZE; // データサイズが不正
    }
    else { // 正常系
        // 引数を取得
        memcpy(&f_stNwConfig, &pstReqFrm->aData[0], sizeof(f_stNwConfig)); // ネットワーク設定
    }

    // 成功・失敗に関わらず応答フレームを送信
    FRM_SendResFrm(pstReqFrm->seqNo, pstReqFrm->cmd, errCode, 0, NULL);

    if (errCode == FRM_ERR_SUCCESS) { // 正常系
        // USBの応答フレーム送信完了(ホスト側からの読み取り完了)を待つ
        delay(CMD_WAIT_SEND_END);
        // [FLASHへ書き込み]
        // FLASHデータ読み込み
        FLASH_Read(&f_stFlashData);
        // FLASHへ書き込み
        memcpy(&f_stFlashData.stNwConfig, &f_stNwConfig, sizeof(f_stNwConfig));
        FLASH_Write(&f_stFlashData); // この関数の中でリセットされる
    }
}

// ネットワーク設定取得コマンドの実行
static void CMD_ExecReqCmd_GetNwConfig3(ST_FRM_REQ_FRAME *pstReqFrm)
{
    USHORT expectedSize = 0;            // 要求フレームのデータサイズの期待値
    USHORT dataSize = 0;                // 応答フレームのデータサイズ
    USHORT errCode = FRM_ERR_SUCCESS;   // エラーコード
    ST_FLASH_DATA *pstFlashData = NULL; // 電源起動時のFLASHデータ
    PVOID pBuf = NULL;                  // 応答フレームのデータ部
    
    // データサイズをチェック
    if (pstReqFrm->dataSize != expectedSize) {
        errCode = FRM_ERR_DATA_SIZE; // データサイズが不正
    }
    else { // 正常系
        // 電源起動時のFLASHデータを取得
        pstFlashData = FLASH_GetDataAtPowerOn();

        dataSize = sizeof(pstFlashData->stNwConfig); // 応答フレームのデータサイズ
        pBuf = (PVOID)&pstFlashData->stNwConfig;     // 応答フレームのデータ
    }

    // 応答フレームを送信
    FRM_SendResFrm(pstReqFrm->seqNo, pstReqFrm->cmd, errCode, dataSize, pBuf);
}