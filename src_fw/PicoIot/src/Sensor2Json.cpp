// Copyright © 2024 Shiomachi Software. All rights reserved.
#include "../Common.h"

// [define]
#define S2J_ADC_CH_NUM_WITHOUT_TEMP 3 // ADCのチャンネル数(温度センサ含まない)
#define S2J_BME_BUF_SIZE 1024         // BME280から取得した各種データ(温度、気圧、湿度、計算値)をJSON文字列に整形するための一時バッファサイズ(byte)

// [ファイルスコープ変数]
static volatile bool f_isBme280Inited = false; // BME280センサーの初期化が正常に完了したか否かを示すフラグ
static struct bme280_dev *f_dev = NULL; // BME280センサーを制御するためのデバイス構造体ポインタ(API用)
static char f_jsonBuf[IOT_JSON_BUF_SIZE]; // 最終的に出力するすべてのセンサーデータを含んだJSON文字列を生成・保持するバッファ
static char f_bmeBuf[S2J_BME_BUF_SIZE]; // BME280独自のデータ群をJSONフォーマットの文字列として一時的に組み立てるバッファ
static ST_SENSOR_DATA f_stSensorData; // デバイスから読み取った生のセンサデータ(GPIO状態、ADC電圧、BME280測定値)を一時保持する構造体
static uint32_t f_req_delay_us = 0; // BME280の測定完了までの待機時間(us)を保持するキャッシュ変数

// [関数プロトタイプ宣言]
static void S2J_GetAdc(float *pDataAry);
static bool S2J_GetBme280Data(float *pDataAry);

// センサを初期化
void S2J_Init()
{
    int8_t rslt = BME280_OK;
    struct bme280_settings stSettings;

    // [BME280の設定構造体をゼロクリアして初期化]
    memset(&stSettings, 0, sizeof(struct bme280_settings)); 
  
    // [ADCの初期化]
    adc_init();
    // GP26, GP27, GP28をアナログ入力用ピンとして設定
    adc_gpio_init(GP_26);
    adc_gpio_init(GP_27);
    adc_gpio_init(GP_28);
    // Pico本体に内蔵されている温度センサー(ADC CH4)を有効化
    adc_set_temp_sensor_enabled(true); 

    // [BME280(温湿度・気圧センサー)の初期化]
    do {

        // BME280 API向けにPicoのI2Cなどのハードウェア依存処理を初期化する
        f_dev = bme280_user_init();
        if (f_dev == NULL) {
            break; // デバイスポインタの取得に失敗した場合は初期化を中断
        }

        // センサー内部のソフトリセットやキャリブレーションデータの読み込みを実行
        rslt = bme280_init(f_dev);
        if (rslt != BME280_OK) {
            break; // 初期化に失敗した場合は中断
        }

        // センサーに現在書き込まれている設定を読み出して構造体に格納
        rslt = bme280_get_sensor_settings(&stSettings, f_dev);
        if (rslt != BME280_OK) {
            break;
        }

        // [推奨動作モードの設定]
        // 気象モニタリング用途に適した低消費電力・短時間測定の設定を適用する。
        // フィルタリングは無効化し、各センサーのオーバーサンプリングを1倍に設定。
        stSettings.filter = BME280_FILTER_COEFF_OFF;
        stSettings.osr_h = BME280_OVERSAMPLING_1X;
        stSettings.osr_p = BME280_OVERSAMPLING_1X;
        stSettings.osr_t = BME280_OVERSAMPLING_1X;

        // 構成した設定をセンサーのレジスタに書き込んで適用
        rslt = bme280_set_sensor_settings(BME280_SEL_ALL_SETTINGS, &stSettings, f_dev);
        if (rslt != BME280_OK) {
            break;
        }
        
        // 測定に必要な正確な待機時間(us)を計算してキャッシュしておく
        bme280_cal_meas_delay(&f_req_delay_us, &stSettings);

        // [初期化時はスリープモードに設定]
        // 常に測定し続けるノーマルモードではなく、データ取得時のみフォースモードで起動させることで省電力化を図る。
        rslt = bme280_set_sensor_mode(BME280_POWERMODE_SLEEP, f_dev);
        if (rslt != BME280_OK) {
            break;
        }

        // 初期化が全て成功したことをフラグに記録
        f_isBme280Inited = true;

    } while(0);
}

// 全てのセンサデータを取得してJSONデータを作成する
char* S2J_CreateJsonData()
{
    ULONG gpioValBits = 0; 

    // センサデータ構造体をゼロクリアして初期化
    memset(&f_stSensorData, 0, sizeof(ST_SENSOR_DATA)); 

    // [GPIOデータの取得]
    // 全GPIOピンの状態をビットマスクとして一括取得
    gpioValBits = gpio_get_all();
    // 監視対象の各ピン(GP10〜15)のビット状態をチェックし、1(High)または0(Low)として配列に格納
    f_stSensorData.aLvl_gp[0] = (gpioValBits & (1UL << (ULONG)GP_10)) ? 1 : 0; // GP10  
    f_stSensorData.aLvl_gp[1] = (gpioValBits & (1UL << (ULONG)GP_11)) ? 1 : 0; // GP11
    f_stSensorData.aLvl_gp[2] = (gpioValBits & (1UL << (ULONG)GP_12)) ? 1 : 0; // GP12   
    f_stSensorData.aLvl_gp[3] = (gpioValBits & (1UL << (ULONG)GP_13)) ? 1 : 0; // GP13   
    f_stSensorData.aLvl_gp[4] = (gpioValBits & (1UL << (ULONG)GP_14)) ? 1 : 0; // GP14     
    f_stSensorData.aLvl_gp[5] = (gpioValBits & (1UL << (ULONG)GP_15)) ? 1 : 0; // GP15  

    // [アナログ入力データの取得]
    // ADCピン(GP26,27,28)の電圧とオンボード温度センサーの値を一括取得
    S2J_GetAdc(f_stSensorData.aAdcData);

    // [BME280センサーデータの取得]
    // フォースモードで測定を実行し、温度・気圧・湿度の値を取得
    bool isBme280Success = S2J_GetBme280Data(f_stSensorData.aBme280Data);  

    // FLASHデータを取得
    ST_FLASH_DATA* pstFlashData = FLASH_GetDataAtPowerOn(); 
    
    // [ボード固有IDの取得]
    // Picoのフラッシュメモリから一意な識別IDを取得する(デバイスの個体識別に利用)
    pico_unique_board_id_t board_id;
    pico_get_unique_board_id(&board_id);
    
    // [ベースJSON文字列の構築]
    // FW情報、ボードID、デバイス名、GPIO入力状態、ADC電圧、オンボード温度をJSON形式で組み立てる
    snprintf(f_jsonBuf, sizeof(f_jsonBuf),
        "{\"FW_Name\":\"%s\", \"FW_Ver\":\"0x%08X\", \"BoardID\":\"%02X%02X%02X%02X%02X%02X%02X%02X\", \"DeviceName\":\"%s\", \"GP10\":%d, \"GP11\":%d, \"GP12\":%d, "
        "\"GP13\":%d, \"GP14\":%d, \"GP15\":%d, "
        "\"ADC0[V]\":%.3f, \"ADC1[V]\":%.3f, \"ADC2[V]\":%.3f, "
        "\"ADC4_temp[degC]\":%.3f",
        FW_NAME, (unsigned int)FW_VER,
        board_id.id[0], board_id.id[1], board_id.id[2], board_id.id[3],
        board_id.id[4], board_id.id[5], board_id.id[6], board_id.id[7],
        pstFlashData->stNwConfigOption.szDeviceName,
        (int)f_stSensorData.aLvl_gp[0], (int)f_stSensorData.aLvl_gp[1], (int)f_stSensorData.aLvl_gp[2],
        (int)f_stSensorData.aLvl_gp[3], (int)f_stSensorData.aLvl_gp[4], (int)f_stSensorData.aLvl_gp[5],
        f_stSensorData.aAdcData[0], f_stSensorData.aAdcData[1], f_stSensorData.aAdcData[2],
        f_stSensorData.aAdcData[3]
    );

    // BME280のデータ取得に成功していた場合、追加の計算を行ってJSONに追記する
    if (isBme280Success) {
        float temp = f_stSensorData.aBme280Data[0];
        float press = f_stSensorData.aBme280Data[1];
        float hum = f_stSensorData.aBme280Data[2];
        
        // [環境指標の計算]
        // 1. 不快指数 (Discomfort Index) の計算
        // 気温と湿度から夏の蒸し暑さを数量的に表す指数。
        float di = 0.81f * temp + 0.01f * hum * (0.99f * temp - 14.3f) + 46.3f;
        
        // 2. 露点温度 [degC] の計算 (August-Roche-Magnus式)
        // 結露が発生し始める温度を推測する。
        float alpha = (17.27f * temp) / (temp + 237.3f) + logf(hum / 100.0f);
        float dew_point = (237.3f * alpha) / (17.27f - alpha);
        
        // 3. 絶対湿度(容積絶対湿度) [g/m3] の計算 (Tetensの式)
        // 飽和水蒸気圧を求めた後、相対湿度を掛けて水蒸気圧を計算し、気体の状態方程式から絶対湿度を導出する。
        float e_s = 6.1078f * powf(10.0f, (7.5f * temp) / (temp + 237.3f)); // 飽和水蒸気圧[hPa]
        float e = e_s * (hum / 100.0f); // 水蒸気圧[hPa]
        float abs_hum = 216.7f * (e / (temp + 273.15f));
        
        // 4. 簡易熱中症指数 (室内用 近似WBGT) [degC] の計算
        // 小野らの式(2014)をベースに、日射量(SR=0)と風速(WS=0)とした屋内無風環境での近似式。
        float wbgt = 0.735f * temp + 0.0374f * hum + 0.00292f * temp * hum - 4.064f;

        // 取得したBME280の測定値と計算結果をJSON形式に整形
        snprintf(f_bmeBuf, sizeof(f_bmeBuf),
            ", \"BME280_temp[degC]\":%.3f, \"BME280_hum[%%]\":%.3f, \"BME280_press[hPa]\":%.3f, "
            "\"BME280_di\":%.3f, \"BME280_dew_point[degC]\":%.3f, \"BME280_abs_hum[g/m3]\":%.3f, \"BME280_wbgt[degC]\":%.3f",
            temp, hum, press, di, dew_point, abs_hum, wbgt
        );
        // ベースのJSONバッファの末尾に連結する
        strncat(f_jsonBuf, f_bmeBuf, sizeof(f_jsonBuf) - strlen(f_jsonBuf) - 1);
    }

    // JSONオブジェクトの終端括弧と改行を付与して完成させる
    strncat(f_jsonBuf, "}\r\n", sizeof(f_jsonBuf) - strlen(f_jsonBuf) - 1);

    // 構築済みのJSON文字列が格納された静的バッファの先頭アドレスを返す
    return f_jsonBuf; 
}

// 電圧とオンボードの温度センサの温度を取得
static void S2J_GetAdc(float *pDataAry)
{
    ULONG i;
    float adcVal = 0; // AD変換されたデジタル値(0〜4095)の一時保持変数
    // ADCの分解能(12bit)と基準電圧(3.3V)から、デジタル値を実際の電圧[V]に変換するための係数
    const float conversionFactor = 3.3f / (1 << 12); 

    // [外部アナログ電圧(ADC0〜2)を取得]
    for (i = 0; i < S2J_ADC_CH_NUM_WITHOUT_TEMP; i++) {
        // 読み取るADCチャンネル(0, 1, 2)を選択
        adc_select_input(i);
        // マルチプレクサの切り替え直後はADC入力部のコンデンサに前のチャンネルの電荷が残っているため、
        // 一度読み捨ててクロストーク(ゴースト現象)を防ぐ
        adc_read(); 
        
        // 実際に測定値を取得
        adcVal = (float)adc_read();
        // 電圧[V]に変換して配列に格納
        pDataAry[i] = adcVal * conversionFactor;
    }

    // [オンボードの温度センサ(ADC4)の温度を取得]
    // CH4はRP2040内蔵の温度ダイオードに接続されている
    adc_select_input(4);
    adc_read(); // 同様にクロストーク防止のためのダミーリード
    
    // 電圧値に変換
    adcVal = (float)adc_read() * conversionFactor;
    // データシートに記載された計算式により、電圧を摂氏温度[degC]に変換して格納
    pDataAry[S2J_ADC_CH_NUM_WITHOUT_TEMP] = 27.0f - (adcVal - 0.706f) / 0.001721f;
}

// BME280のデータ(温度・気圧・湿度)を取得
static bool S2J_GetBme280Data(float *pDataAry)
{
    int8_t rslt = BME280_OK;       // BME280 APIの戻り値(エラーコード)を格納する変数
    float temp, press, hum;        // 取得した温度[degC]、気圧[hPa]、湿度[%]を一時保持する変数
    bool bRet = false;             
    struct bme280_data comp_data;  // APIから取得した補正済みセンサデータ(温度、気圧、湿度)を格納する構造体

    do {

        if (!f_isBme280Inited) { // BME280センサーの初期化が完了してない場合
            break;
        }

        // [測定の開始]
        // センサをフォースモードに設定して1回だけ測定を要求する。
        // 測定が完了すると、センサーは自動的に待機電力の少ないスリープモードに戻る。
        rslt = bme280_set_sensor_mode(BME280_POWERMODE_FORCED, f_dev);
        if (rslt == BME280_OK) {
            // キャッシュ済みの正確な測定待機時間(us)だけ無駄なく待機する
            f_dev->delay_us(f_req_delay_us, f_dev->intf_ptr);
        }
        else {
            // フォースモードへの移行に失敗した場合(通信エラー等)は、古いデータを読まないように抜ける
            break;
        }

        // [測定結果の取得]
        // 温度、気圧、湿度の全てのデータをセンサーのレジスタから読み出し、
        // 内部のキャリブレーション係数を用いて実際の物理量に補正する。
        rslt = bme280_get_sensor_data(BME280_ALL, &comp_data, f_dev);
        if (rslt != BME280_OK)
        {
            // 取得エラー時は古いデータがそのまま残らないように、0.0でクリアして異常状態であることを明示する
            pDataAry[0] = 0.0f;
            pDataAry[1] = 0.0f;
            pDataAry[2] = 0.0f;
            break;
        }

        // 取得した補正済みデータを変数に取り出す
        temp = comp_data.temperature; // 温度 [degC]
        press = 0.01 * comp_data.pressure; // 気圧 [Pa] を [hPa] に変換
        hum = comp_data.humidity; // 湿度 [%]

        // 引数の配列に格納して呼び出し元へ渡す
        pDataAry[0] = temp;
        pDataAry[1] = press;
        pDataAry[2] = hum;

        // 取得成功フラグをセット
        bRet = true;

    } while(0);

    return bRet;
}
