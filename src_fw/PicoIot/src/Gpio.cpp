// Copyright © 2024 Shiomachi Software. All rights reserved.
#include "../Common.h"

// [ファイルスコープ変数]
// GPIOのGP番号と方向
static const ST_GPIO_PIN f_castGpioPin[] = {
    // false:入力 true:出力
    { GP_10, false },      // 入力 
    { GP_11, false },      // 入力
    { GP_12, false },      // 入力
    { GP_13, false },      // 入力
    { GP_14, false },      // 入力
    { GP_15, false },      // 入力
};

// ST_GPIO_CONFIG構造体にデフォルト値を格納
void GPIO_GetDefaultConfig(ST_GPIO_CONFIG *pstConfig)
{
    pstConfig->pullDownBits   = 0; // 全てのGPIO入力はプルアップ
    pstConfig->initialValBits = 0; // 全てのGPIO出力の電源ON時出力値=OFF 
}

// GPIOを初期化
void GPIO_Init(ST_GPIO_CONFIG *pstConfig)
{
    bool dir;        // 入出力方向
    bool initialVal; // 電源ON時出力値
    ULONG gp;        // GP番号
    ULONG i;

    // [GPIOピンの設定]
    for (i = 0; i < sizeof(f_castGpioPin) / sizeof(ST_GPIO_PIN); i++) {
        gp  = f_castGpioPin[i].gp;  // GP番号
        dir = f_castGpioPin[i].dir; // 方向
        if (dir) { 
            // 出力の場合    

            // 電源ON時出力値を決定
            if (pstConfig->initialValBits & (1UL << gp)) {
                initialVal = true;
            }
            else {
                initialVal = false;
            }
            
            // GPIOの初期化
            gpio_init(gp);         
            // 電源ON時出力値を出力 
            gpio_put(gp, initialVal);
            // 出力方向に設定
            gpio_set_dir(gp, dir); 
        }
        else { 
            // 入力の場合
            
            // GPIOの初期化
            gpio_init(gp);
            // 入力方向に設定
            gpio_set_dir(gp, dir);          
            // プルダウン/プルアップの設定
            if (pstConfig->pullDownBits & (1UL << gp)) {
                gpio_pull_down(gp); // プルダウン
            }
            else {
                gpio_pull_up(gp); // プルアップ
            }              
        }
    }
}
