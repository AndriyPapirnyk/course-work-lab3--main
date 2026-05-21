#include <Arduino.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WebSocketsClient.h> 

const char* ssid = "TP-Link_8F0C";
const char* password = "71402024";
const char* serverIp = "192.168.0.195"; 
const int serverPort = 3000;

String serverUrl = "http://" + String(serverIp) + ":" + String(serverPort) + "/api/access?uid=";

#define BUZZER_PIN    26
#define LED_R_PIN     14
#define LED_G_PIN     12
#define BUTTON_PIN    32  

#define RFID_SS_PIN   5
#define RFID_RST_PIN  17  

MFRC522 rfid(RFID_SS_PIN, RFID_RST_PIN);
LiquidCrystal_I2C lcd(0x27, 16, 2);
WebSocketsClient webSocket;

bool lastButtonState = LOW;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50; 
bool triggerRemoteUnlock = false; 

void terminalPrint(String line1, String line2) {
  while (line1.length() < 16) line1 += " ";
  while (line2.length() < 16) line2 += " ";
  lcd.setCursor(0, 0); lcd.print(line1);
  lcd.setCursor(0, 1); lcd.print(line2);
}

void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.println("[WS] З'єднання розірвано з сервером.");
      break;
    case WStype_CONNECTED:
      Serial.println("[WS] Успішно підключено до WebSocket сервера!");
      break;
    case WStype_TEXT:
      Serial.printf("[WS] Отримано команду: %s\n", payload);
      
      if (strcmp((char*)payload, "REMOTE_UNLOCK") == 0) {
        triggerRemoteUnlock = true; 
      }
      break;
  }
}

void executeUnlockAnimation(String line1, String line2) {
  terminalPrint(line1, line2);
  digitalWrite(LED_G_PIN, HIGH);
  digitalWrite(BUZZER_PIN, HIGH);
  delay(300);
  digitalWrite(BUZZER_PIN, LOW);
  delay(2000); 
  digitalWrite(LED_G_PIN, LOW);
  terminalPrint("> IR-22 TERMINAL", "AWAITING INPUT_ ");
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_R_PIN, OUTPUT);
  pinMode(LED_G_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLDOWN); 

  Wire.begin(21, 22);
  lcd.init(); lcd.backlight();
  
  terminalPrint("BOOTING_SYSTEM..", "INIT_NETWORK_   ");
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\n[WIFI] Connected!");

  webSocket.begin(serverIp, serverPort, "/");
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(5000); 

  SPI.begin();
  rfid.PCD_Init();
  
  terminalPrint("> IR-22 TERMINAL", "AWAITING INPUT_ ");
}

void loop() {
  webSocket.loop(); 

  if (triggerRemoteUnlock) {
    triggerRemoteUnlock = false; 
    executeUnlockAnimation("> REMOTE OVERRIDE", "ACCESS GRANTED_");
  }

  bool currentButtonState = digitalRead(BUTTON_PIN);
  if (currentButtonState != lastButtonState) {
    if ((millis() - lastDebounceTime) > debounceDelay) {
      lastDebounceTime = millis();
      lastButtonState = currentButtonState;

      if (currentButtonState == HIGH) {
        Serial.println("[MANUAL] Кнопка дзвінка натиснута!");
        terminalPrint("> CALLING ADMIN_", "PLEASE WAIT...  ");
        
        webSocket.sendTXT("BELL_PRESSED");
        
        digitalWrite(BUZZER_PIN, HIGH); delay(70); digitalWrite(BUZZER_PIN, LOW);
        delay(2000); 
        terminalPrint("> IR-22 TERMINAL", "AWAITING INPUT_ ");
      }
    }
  }

  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) { return; }

  String uidDisplay = "ID: "; String uidServer = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) { uidDisplay += "0"; uidServer += "0"; }
    uidDisplay += String(rfid.uid.uidByte[i], HEX); uidServer += String(rfid.uid.uidByte[i], HEX);
    uidDisplay += " ";
  }
  uidDisplay.toUpperCase(); uidServer.toUpperCase();
  
  terminalPrint("> CONNECTING DB_", uidDisplay);

  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(serverUrl + uidServer);
    int httpResponseCode = http.GET();
    
    if (httpResponseCode > 0) {
      String payload = http.getString();
      JsonDocument doc;
      deserializeJson(doc, payload);
      
      bool access = doc["access"];
      const char* msg = doc["message"];
      
      if (access) {
        executeUnlockAnimation("> ACCESS GRANTED", msg);
      } else {
        terminalPrint("> ACCESS DENIED!", msg);
        for (int i = 0; i < 3; i++) {
          digitalWrite(LED_R_PIN, HIGH); digitalWrite(BUZZER_PIN, HIGH); delay(120);
          digitalWrite(BUZZER_PIN, LOW); digitalWrite(LED_R_PIN, LOW); delay(100);
        }
        delay(1000);
        terminalPrint("> IR-22 TERMINAL", "AWAITING INPUT_ ");
      }
    }
    http.end();
  }
  rfid.PICC_HaltA(); rfid.PCD_StopCrypto1();
}
