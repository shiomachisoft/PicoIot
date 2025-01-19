# 1.概要
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

# 2.システム構成  
![image](https://github.com/user-attachments/assets/26c433b5-2338-4aa4-8bb6-460408e898f0)  
  
# 3.画面  
- (1)Webブラウザの表示

     ![image](https://github.com/user-attachments/assets/71e27e84-6caf-4cd0-81f6-465259255e8c)
  
- (2)TCPソケット通信
  
     ![image](https://github.com/user-attachments/assets/1559c24e-8774-4bae-8a01-713edf9ea340)

- (3)メールメッセージ
  
     ![image](https://github.com/user-attachments/assets/aea04a1b-ad59-40a5-b4d6-c8a71dbab6e4)  





