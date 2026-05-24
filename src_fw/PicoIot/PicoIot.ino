// Copyright © 2024 Shiomachi Software. All rights reserved.
#include "Common.h"

// [define]              
//#define MN_WDT_TIMEOUT 0x7FFFFF // WDTタイムアウト時間(ms) 最大の約8.3秒に設定

// 接続時(ダブルフラッシュ)のLED点滅に関する時間定義(ms)
#define MN_LED_DBL_PERIOD       2000 // ダブルフラッシュの周期(2000ms)
#define MN_LED_DBL_ON1_END      50   // 1回目の点灯終了時間(50ms)
#define MN_LED_DBL_ON2_START    150  // 2回目の点灯開始時間(150ms)
#define MN_LED_DBL_ON2_END      200  // 2回目の点灯終了時間(200ms)

// 未接続時(シングルフラッシュ)のLED点滅に関する時間定義(ms)
#define MN_LED_SGL_PERIOD       1000 // シングルフラッシュの周期(1000ms)
#define MN_LED_SGL_ON_END       100  // 点灯終了時間(100ms)

#define MN_DELAY_CORE0_LOOP     10 // CPUコア0のメインループの遅延(ms)
#define MN_DELAY_CORE1_LOOP     10 // CPUコア1のメインループの遅延(ms)

// [列挙体]
// LEDの点滅状態(ステートマシン用)
typedef enum _E_MN_LED_STATE {
    E_MN_LED_STATE_ON_1 = 0, // 1回目の点灯中 (シングル/ダブルフラッシュ共通)
    E_MN_LED_STATE_OFF_1,    // 1回目の消灯中 (シングル/ダブルフラッシュ共通)
    E_MN_LED_STATE_ON_2,     // 2回目の点灯中 (ダブルフラッシュ専用)
    E_MN_LED_STATE_OFF_2     // 2回目の消灯中 (ダブルフラッシュ専用)
} E_MN_LED_STATE;

// [ファイルスコープ変数]
static volatile bool f_isCpu0SetupCompleted = false; // CPUコア0のセットアップが完了済みか否か
static volatile bool f_isCore1WdtClearTurn = false; // CPUコア1によってWDTをクリアする番か否か

// [関数プロトタイプ宣言]
static void MN_ControlLed(bool isConnected);
static void MN_RegisterExceptionHandler();
static void MN_ExceptionHandler();

// CPUコア0のセットアップ
void setup() 
{
	ST_GPIO_CONFIG stGpioConfig; // GPIO設定

	// WDTを有効に設定
	//watchdog_enable(MN_WDT_TIMEOUT, true);
	// 例外ハンドラを登録
	MN_RegisterExceptionHandler();

	// 共通ライブラリを初期化
	CMN_Init();	

	// FLASHライブラリを初期化
	FLASH_Init();

	// GPIOを初期化
	GPIO_GetDefaultConfig(&stGpioConfig);
	GPIO_Init(&stGpioConfig);

	// センサを初期化
	S2J_Init();

	// USB通信を初期化
	FRM_Init();

	// タイマーを初期化
	TMR_Init();

	// 起動してからの安定待ち時間を待つ
	while (!TMR_IsStabilizationWaitTimePassed()) {
		tight_loop_contents();
	}

	if (watchdog_enable_caused_reboot()) { // watchdog_reboot()ではなくwatchdog_enable()のWDTタイムアウトで再起動していた場合
		// FWエラーを設定
		CMN_SetErrorBits(CMN_ERR_BIT_WDT_RESET, true);
	}	
	
	f_isCpu0SetupCompleted = true;
}

// CPUコア0のメインループ
void loop()
{
	if (!f_isCore1WdtClearTurn) { // CPUコア0によってWDTをクリアする番の場合
		// WDTをクリア
		//watchdog_update();
		// WDTタイマをクリア
		TMR_WdtClear();
		f_isCore1WdtClearTurn = true;
	}

	// USB受信データ取り出し⇒コマンド解析・実行
	FRM_Main();	

	if (!tud_cdc_connected()) { // CDC未接続の場合のみ
		volatile uint64_t startUs = time_us_64();
		while (!tud_cdc_connected() && ((time_us_64() - startUs) < (MN_DELAY_CORE0_LOOP * 1000ULL))) {
			__wfi(); // 何らかの割り込み(USB接続やタイマー等)が発生するまでCPUをスリープ
		}
	}
}

// CPUコア1のセットアップ
void setup1() 
{
	// CPUコア0のセットアップが完了するまで待機する
	while (!f_isCpu0SetupCompleted) {
		tight_loop_contents();
	}

	// 電源起動時のFLASHデータを取得
	ST_FLASH_DATA* pstFlashData = FLASH_GetDataAtPowerOn(); 

	if (pstFlashData->stNwConfig.isWifi != 0) { // Wi-Fiモードの場合
		// WiFiを初期化
		WIFI_Init();
	}
	else { // BLEモードの場合
		// BLEを初期化
		BLE_Init();
	}

	// Pico Wの内蔵LEDは無線チップ(CYW43439)経由で制御されるため、初期化完了後に設定する
	pinMode(LED_BUILTIN, OUTPUT); 		
}

// CPUコア1のメインループ
void loop1()
{	
	char* pszJson = NULL; // JSONデータ
	char* pszJsonOnPush = NULL;    // ネットワークPush送信の瞬間(設定された送信周期)のみ値が入るポインタ
	bool isConnected = false;
	volatile uint64_t currentUs = time_us_64();
	ST_FLASH_DATA* pstFlashData = FLASH_GetDataAtPowerOn(); // 電源起動時のFLASHデータ
	static bool s_isFirstCall = true;
	static char* s_pszJson = NULL;  // JSONデータ(キャッシュ)
	static volatile uint64_t s_lastSensorGetUs = 0;
	static uint8_t s_pushSendCounter = 0; // Push送信のタイミングを制御するカウンタ

	if (f_isCore1WdtClearTurn) { // CPUコア1によってWDTをクリアする番の場合	
		// WDTをクリア
		//watchdog_update();
		// WDTタイマをクリア
		TMR_WdtClear();		
		f_isCore1WdtClearTurn = false;
	}

	// 現在の通信モードに応じた接続状態を取得
	if (pstFlashData->stNwConfig.isWifi != 0) { // Wi-Fiモードの場合
		isConnected = WIFI_IsApConnected(); // APと接続済みの場合
	} else { // BLEモードの場合
		isConnected = BLE_IsConnected();
	}

	// LEDを制御する
	MN_ControlLed(isConnected); 
	
	// センサデータ取得のタイミング判定
	if ((true == s_isFirstCall) || (currentUs - s_lastSensorGetUs >= IOT_SENSOR_GET_PERIOD_US)) { 
		s_lastSensorGetUs = currentUs;

		// センサデータを取得してJSONデータを作成する
		pszJson = S2J_CreateJsonData();
		s_pszJson = pszJson;

		// Push制通信のための送信タイミング判定
		s_pushSendCounter++;
		if ((true == s_isFirstCall) || (s_pushSendCounter >= (IOT_NETWORK_SEND_PERIOD_US / IOT_SENSOR_GET_PERIOD_US))) {
			s_pushSendCounter = 0;
			pszJsonOnPush = s_pszJson; // 送信タイミングが来たら最新データを渡す
		}
		
		s_isFirstCall = false;
	}
	
	if (pstFlashData->stNwConfig.isWifi != 0) { // Wi-Fiモードの場合
		// WiFiとTCPソケット通信のメイン処理
		WIFI_TCP_Main(); 

		// JSONデータのTCPソケット送信処理
		if (pszJsonOnPush != NULL) {
			TCP_SendJsonData(pszJsonOnPush);
		}

		// HTTPサーバーのメイン処理
		HTTP_Main(s_pszJson);
		
		// アラート監視のメイン処理
		if (pszJson != NULL) {
			ALT_Main(pszJson);
		}
	}
	else { // BLEモードの場合
		// JSONデータのBLE送信処理
		BLE_SendJsonData(pszJsonOnPush); 
	}

	// CPU負荷を軽減し、消費電力を削減するために遅延を入れる
	delay(MN_DELAY_CORE1_LOOP);
}

// LEDを制御する
static void MN_ControlLed(bool isConnected)
{
	static volatile uint64_t s_lastChangeUs = 0;
	static E_MN_LED_STATE s_state = E_MN_LED_STATE_ON_1;
	static int8_t s_wasConnected = -1; // 初期値を-1にし、初回呼び出し時に必ず初期化処理を通す
	volatile uint64_t currentUs = time_us_64();

	// 接続状態が変わった場合（または初回起動時）は状態をリセットして最初から
	if ((int8_t)isConnected != s_wasConnected) {
		s_state = E_MN_LED_STATE_ON_1;
		s_lastChangeUs = currentUs;
		s_wasConnected = isConnected;
		digitalWrite(LED_BUILTIN, true); // 初期状態は必ずONから始まる
	}

	volatile uint64_t elapsed = (currentUs - s_lastChangeUs) / 1000ULL;

	if (isConnected) { 
		// 接続済み: ダブルフラッシュ
		switch (s_state) {
			case E_MN_LED_STATE_ON_1: // 1回目のON
				if (elapsed >= MN_LED_DBL_ON1_END) {
					s_state = E_MN_LED_STATE_OFF_1;
					s_lastChangeUs = currentUs;
					digitalWrite(LED_BUILTIN, false);
				}
				break;
			case E_MN_LED_STATE_OFF_1: // 1回目のOFF
				if (elapsed >= (MN_LED_DBL_ON2_START - MN_LED_DBL_ON1_END)) {
					s_state = E_MN_LED_STATE_ON_2;
					s_lastChangeUs = currentUs;
					digitalWrite(LED_BUILTIN, true);
				}
				break;
			case E_MN_LED_STATE_ON_2: // 2回目のON
				if (elapsed >= (MN_LED_DBL_ON2_END - MN_LED_DBL_ON2_START)) {
					s_state = E_MN_LED_STATE_OFF_2;
					s_lastChangeUs = currentUs;
					digitalWrite(LED_BUILTIN, false);
				}
				break;
			case E_MN_LED_STATE_OFF_2: // 2回目のOFF (残り時間)
				if (elapsed >= (MN_LED_DBL_PERIOD - MN_LED_DBL_ON2_END)) {
					s_state = E_MN_LED_STATE_ON_1;
					s_lastChangeUs = currentUs;
					digitalWrite(LED_BUILTIN, true);
				}
				break;
			default:
				// 万が一メモリ破壊等で不正な状態になった場合の自己修復
				s_state = E_MN_LED_STATE_ON_1;
				s_lastChangeUs = currentUs;
				digitalWrite(LED_BUILTIN, true);
				break;
		}
	} 
	else {
		// 未接続: シングルフラッシュ
		switch (s_state) {
			case E_MN_LED_STATE_ON_1: // ON
				if (elapsed >= MN_LED_SGL_ON_END) {
					s_state = E_MN_LED_STATE_OFF_1;
					s_lastChangeUs = currentUs;
					digitalWrite(LED_BUILTIN, false);
				}
				break;
			case E_MN_LED_STATE_OFF_1: // OFF (残り時間)
				if (elapsed >= (MN_LED_SGL_PERIOD - MN_LED_SGL_ON_END)) {
					s_state = E_MN_LED_STATE_ON_1;
					s_lastChangeUs = currentUs;
					digitalWrite(LED_BUILTIN, true);
				}
				break;
			default:
				// 万が一メモリ破壊等で不正な状態になった場合の自己修復
				s_state = E_MN_LED_STATE_ON_1;
				s_lastChangeUs = currentUs;
				digitalWrite(LED_BUILTIN, true);
				break;
		}
	}
}

// 例外ハンドラを登録
static void MN_RegisterExceptionHandler()
{
	exception_set_exclusive_handler(NMI_EXCEPTION, MN_ExceptionHandler);
	exception_set_exclusive_handler(HARDFAULT_EXCEPTION, MN_ExceptionHandler);
	//exception_set_exclusive_handler(SVCALL_EXCEPTION, MN_ExceptionHandler);
	//exception_set_exclusive_handler(PENDSV_EXCEPTION, MN_ExceptionHandler);
	//exception_set_exclusive_handler(SYSTICK_EXCEPTION, MN_ExceptionHandler);	
}

// 例外ハンドラ
static void MN_ExceptionHandler()
{
	// watchdog_enable()を使用して即WDTタイムアウトで再起動する
	CMN_WdtEnableReboot();
}