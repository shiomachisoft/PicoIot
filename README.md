# 概要
- (1)マイコン基板はRaspberry Pico Pico Wを使用します。  
- (2)Pico Wがセンサデータについて、下記の種類の送信を行います。  
    - Webブラウザに送信  
    - TCPソケット通信で送信  
      ※Pico Wはサーバー/クライアントのどちらにもなることができます。  
    - メール送信  
      ※Gmailアカウントを使用します。Gmailが送信元になります。  
  
    - センサデータ:  
      - Pico WのGPIO入力値  
      - Pico WのADC値(電圧値, 温度センサ値)  
      - BoschのBME280(温度・湿度・気圧センサ)のデータ(※1)   
      ※1:
      Pico WにBME280を接続していなくてもPicoIotは使用できます。
      BME280が接続されていない場合、本データは0になります。
   
- (3)Pico Wに対するWi-Fi設定等は専用PCアプリを使用し、Pico WのFlashメモリに保存します。

# システム構成  
![image](https://github.com/user-attachments/assets/26c433b5-2338-4aa4-8bb6-460408e898f0)


