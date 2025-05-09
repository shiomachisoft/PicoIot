# PicoIot
## 1. 概要
- (1) マイコン基板はRaspberry Pi Pico Wを使用します。  
- (2) Pico Wが、下記の①～④の送信方法で、センサデータの送信を行います。  
      
    **<Pico Wのセンサデータの送信方法>**    
    **■BLEモード**  
    ①BLEで送信  
     Pico Wは、BLEのペリフェラルになります。  

    **■Wi-Fiモード**  
    ②Webブラウザに送信  
    ③TCPソケット通信で送信   
     Pico WはTCPサーバー/TCPクライアントのどちらにもなることができます。  
    ④メール送信    
     Gmailアカウントを使用します。Gmailが送信元になります。    

    **<センサデータの種類>**  
    ・Pico WのGPIO入力値  
    ・Pico WのADC値(電圧値, 温度センサ値)  
    ・BoschのBME280(温度・湿度・気圧センサ)のデータ(※1)   
        
    ※1: Pico WにBME280が接続されていない場合、本データは0になります。  
 
- (3) Pico Wに対するモード設定・Wi-Fi設定は、専用PCアプリを使用してPico WのFlashメモリに保存します。 

## 2. システム構成  
  ![システム構成 - コピー](https://github.com/user-attachments/assets/52b8d7f1-4fa5-49df-b479-bfb482fc7beb)

## 3. 画面  
- (1) スマホへBELでセンサデータを送信

     ![iphone](https://github.com/user-attachments/assets/2ae37a95-5677-477c-b7a7-ba2a14705271)


- (2) Webブラウザへセンサデータを送信

     ![image](https://github.com/user-attachments/assets/71e27e84-6caf-4cd0-81f6-465259255e8c)
  
- (3) TCPソケット通信でセンサデータを送信
  
     ![image](https://github.com/user-attachments/assets/1559c24e-8774-4bae-8a01-713edf9ea340)

- (4) メールでセンサデータを送信
  
     ![image](https://github.com/user-attachments/assets/aea04a1b-ad59-40a5-b4d6-c8a71dbab6e4)  
  
## 4. 使い方  
マニュアルを参照。




