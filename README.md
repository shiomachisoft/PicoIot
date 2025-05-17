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
![システム構成2](https://github.com/user-attachments/assets/bd1f3872-290d-43fc-abdd-6f6824f8ec77)


## 3. 画面  
- (1) BLEでスマホへセンサデータを送信

     ![aaa](https://github.com/user-attachments/assets/25d75e21-08b5-4cd6-b222-466bac17a6db)


- (2) Webブラウザへセンサデータを送信  
  
     ![web](https://github.com/user-attachments/assets/058d2783-8015-45db-8436-0c00014cbed0)
  
- (3) TCPソケット通信でセンサデータを送信
  
     ![image](https://github.com/user-attachments/assets/1559c24e-8774-4bae-8a01-713edf9ea340)

- (4) メールでセンサデータを送信
  
     ![image](https://github.com/user-attachments/assets/aea04a1b-ad59-40a5-b4d6-c8a71dbab6e4)  
  
## 4. 使い方  
マニュアルを参照。

## 5. 不具合報告や要望
イシューを作成して頂いて構いません。  
ただし、対応できるか分かりません。   

