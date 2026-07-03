#include <RadioLib.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <esp_sleep.h>

#define OLED_SDA 17
#define OLED_SCL 18
#define OLED_RST 21
#define VEXT_PIN 36 

#define VBAT_ADC 1
#define VBAT_CTRL 37

#define LORA_NSS 8
#define LORA_DIO1 14
#define LORA_NRST 12
#define LORA_BUSY 13

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RST);

SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_NRST, LORA_BUSY);

#define BUTTON_PIN 0
#define LED_PIN 35

const int DEBOUNCE_DELAY = 20;
const int DIT_DAH_THRESHOLD = 200; 
const int LETTER_TIMEOUT = 500;

// Power Management State
bool isBridge = false;
bool isSleeping = false;
unsigned long lastActivityTime = 0;
const unsigned long SLEEP_TIMEOUT = 30000; 
unsigned long lastBlinkTime = 0;
bool sleepIndicatorState = false;
bool ignoreNextRelease = false;

// Battery State
int currentBatteryPct = 100;
unsigned long lastBatteryCheck = 0;

unsigned long pressTime = 0;
unsigned long releaseTime = 0;
bool lastButtonState = HIGH;

String currentSequence = "";
char lastChar = ' ';

// Reception State
bool isReceiving = false;
String receivedMessage = "";
String rxAnimSequence = ""; 
String myID = ""; 

volatile bool receivedFlag = false;

#if defined(ESP8266) || defined(ESP32)
  ICACHE_RAM_ATTR
#endif
void setFlag(void) {
  receivedFlag = true;
}

const char* morseTable[] = {
  ".-", "-...", "-.-.", "-..", ".", "..-.", "--.", "....", "..", ".---",
  "-.-", ".-..", "--", "-.", "---", ".--.", "--.-", ".-.", "...", "-",
  "..-", "...-", ".--", "-..-", "-.--", "--..", ".-.-", "---.", 
  "-----", ".----", "..---", "...--", "....-", ".....", "-....", "--...", "---..", "----." 
};
const char alphaTable[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZÆØ0123456789";

int getBatteryPercentage() {
  pinMode(VBAT_CTRL, OUTPUT);
  digitalWrite(VBAT_CTRL, LOW); // Enable battery reading
  delay(10);
  // Heltec V3 uses a 390k/100k voltage divider (multiplier = 4.9)
  float voltage = analogReadMilliVolts(VBAT_ADC) * 4.9 / 1000.0;
  digitalWrite(VBAT_CTRL, HIGH); // Disable to save power

  int pct = (voltage - 3.2) * 100.0;
  if (pct > 100) pct = 100;
  if (pct < 0) pct = 0;
  return pct;
}

void wakeUp() {
  isSleeping = false;
  lastActivityTime = millis();
  display.ssd1306_command(SSD1306_SETCONTRAST);
  display.ssd1306_command(255); 
  updateDisplay();
}

void setup() {
  Serial.begin(115200);
  delay(1000); 

  uint64_t mac = ESP.getEfuseMac();
  myID = String((uint16_t)(mac >> 32), HEX);
  myID.toUpperCase();

  pinMode(VEXT_PIN, OUTPUT);
  digitalWrite(VEXT_PIN, LOW); 
  delay(50);

  pinMode(OLED_RST, OUTPUT);
  digitalWrite(OLED_RST, LOW);
  delay(20);
  digitalWrite(OLED_RST, HIGH);
  delay(20);

  Wire.begin(OLED_SDA, OLED_SCL);
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("BRIDGE_LOG: OLED Init Failed!");
    for(;;); 
  }
  
  currentBatteryPct = getBatteryPercentage();

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 20);
  display.print("STARTER SYSTEM...");
  display.display();

  int state = radio.begin(868.3); 
  if (state != RADIOLIB_ERR_NONE) {
    display.print("\nRADIO ERR");
    display.display();
    Serial.println("BRIDGE_LOG: LoRa Init Failed!");
    for(;;);
  }

  radio.setSyncWord(0xABCD);

  Serial.println("BRIDGE_LOG: Boot successful. Listening...");
  radio.setDio1Action(setFlag);
  radio.startReceive();
  
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  
  lastActivityTime = millis();
  updateDisplay();
}

void loop() {
  unsigned long currentTime = millis();

  // 0. Check Battery State (Every 10 seconds)
  if (currentTime - lastBatteryCheck > 10000) {
    lastBatteryCheck = currentTime;
    currentBatteryPct = getBatteryPercentage();
    
    // Force deep sleep if battery is depleted
    if (currentBatteryPct <= 0 && !isBridge) {
      display.ssd1306_command(SSD1306_SETCONTRAST);
      display.ssd1306_command(255);
      display.clearDisplay();
      display.setCursor(30, 30);
      display.print("LOW BATTERY");
      display.display();
      radio.sleep();
      esp_deep_sleep_start();
    }
  }

  // 1. Handle Incoming LoRa Data
  if (receivedFlag) {
    receivedFlag = false;
    String str;
    int state = radio.readData(str);
    
    if (state == RADIOLIB_ERR_NONE) {
      Serial.println(str);

      int firstColon = str.indexOf(':');
      int secondColon = str.indexOf(':', firstColon + 1);
      int thirdColon = str.indexOf(':', secondColon + 1);

      String senderID = "";
      String payload = str;
      int rxDitTime = DIT_DAH_THRESHOLD;
      int rxLetterTime = LETTER_TIMEOUT;

      if (thirdColon != -1) {
        senderID = str.substring(0, firstColon);
        rxDitTime = str.substring(firstColon + 1, secondColon).toInt();
        rxLetterTime = str.substring(secondColon + 1, thirdColon).toInt();
        payload = str.substring(thirdColon + 1);
      } else if (firstColon != -1) {
        senderID = str.substring(0, firstColon);
        payload = str.substring(firstColon + 1);
      }

      if (senderID != myID) {
        if (isSleeping) wakeUp();
        lastActivityTime = millis();
        playMorseString(payload, rxDitTime, rxLetterTime);
      }
    } else {
      Serial.println("BRIDGE_LOG: RX Error " + String(state));
    }
    radio.startReceive();
  }

  // 2. Handle USB Serial Data from Computer
  if (Serial.available()) {
    isBridge = true; 
    lastActivityTime = millis();
    if (isSleeping) wakeUp();

    String tx = Serial.readStringUntil('\n');
    tx.trim();
    
    if (tx == "PING") {
      Serial.println("BRIDGE_ACK");
    } 
    else if (tx.length() > 0) {
      Serial.println("BRIDGE_LOG: Attempting to broadcast: " + tx);
      int state = radio.transmit(tx);
      
      if (state == RADIOLIB_ERR_NONE) {
        Serial.println("BRIDGE_LOG: TX Success.");
      } else {
        Serial.println("BRIDGE_LOG: TX Failed with code " + String(state));
      }
      
      receivedFlag = false; 
      radio.startReceive(); 
    }
  }

  // 3. Handle Physical Button Input
  if (!isReceiving) {
    bool buttonState = digitalRead(BUTTON_PIN);

    if (buttonState == LOW && lastButtonState == HIGH) { 
      delay(DEBOUNCE_DELAY);
      
      if (isSleeping) {
        wakeUp();
        ignoreNextRelease = true; 
        lastButtonState = buttonState; 
        return; 
      }
      
      lastActivityTime = millis();
      pressTime = millis();
      digitalWrite(LED_PIN, HIGH);

      if (receivedMessage.length() > 0) {
        receivedMessage = "";
        updateDisplay();
      }
    } 
    else if (!isSleeping && buttonState == HIGH && lastButtonState == LOW) { 
      delay(DEBOUNCE_DELAY);
      lastActivityTime = millis();
      releaseTime = millis();
      digitalWrite(LED_PIN, LOW);
      
      if (ignoreNextRelease) {
        ignoreNextRelease = false;
      } else {
        if ((releaseTime - pressTime) < DIT_DAH_THRESHOLD) {
          currentSequence += ".";
        } else {
          currentSequence += "-";
        }
        updateDisplay();
      }
    }

    if (!isSleeping && buttonState == HIGH && currentSequence.length() > 0 && (millis() - releaseTime > LETTER_TIMEOUT)) {
      lastActivityTime = millis();
      lastChar = translateMorse(currentSequence);
      currentSequence = "";
      
      if (lastChar != '?') {
        String msgStr = String(lastChar);
        String txPayload = myID + ":" + msgStr;
        
        Serial.println(txPayload); 
        radio.transmit(txPayload);
        
        receivedFlag = false; 
        radio.startReceive(); 
      }
      updateDisplay();
    }
    lastButtonState = buttonState;
  }

  // 4. Handle Power Saving Mode
  if (!isBridge && !isSleeping && !isReceiving && (millis() - lastActivityTime > SLEEP_TIMEOUT)) {
    isSleeping = true;
    display.ssd1306_command(SSD1306_SETCONTRAST);
    display.ssd1306_command(0); 
    display.clearDisplay();
    display.display();
  }

  if (isSleeping) {
    currentTime = millis();
    if (currentTime - lastBlinkTime > 2000) {
      lastBlinkTime = currentTime;
      sleepIndicatorState = !sleepIndicatorState;
      display.clearDisplay();
      if (sleepIndicatorState) {
        display.drawPixel(127, 0, SSD1306_WHITE); 
      }
      display.display();
    }
    return; 
  }
}

char translateMorse(String seq) {
  for (int i = 0; i < 38; i++) { 
    if (seq == morseTable[i]) return alphaTable[i];
  }
  return '?'; 
}

void playMorseString(String text, int ditTime, int letterTime) {
  isReceiving = true;
  receivedMessage = "";
  text.toUpperCase();
  
  for (int i = 0; i < text.length(); i++) {
    char c = text[i];
    
    if (c == ' ') {
      receivedMessage += " ";
      updateDisplay();
      delay(letterTime); 
    } else {
      for (int j = 0; j < 38; j++) { 
        if (alphaTable[j] == c) {
          String seq = morseTable[j];
          rxAnimSequence = "";
          
          for (int k = 0; k < seq.length(); k++) {
            rxAnimSequence += seq[k];
            updateDisplay(); 
            
            digitalWrite(LED_PIN, HIGH);
            if (seq[k] == '.') delay(ditTime); 
            else delay(ditTime * 3); 
            
            digitalWrite(LED_PIN, LOW);
            delay(ditTime); 
          }
          
          receivedMessage += c;
          rxAnimSequence = "";
          updateDisplay();
          delay(letterTime); 
          break;
        }
      }
    }
  }
  isReceiving = false;
  lastActivityTime = millis();
  updateDisplay();
}

void updateDisplay() {
  display.clearDisplay();
  
  display.fillRect(0, 0, 128, 12, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setTextSize(1);
  display.setCursor(4, 2);
  display.print("RADIO");

  // Display Battery Percentage
  String batStr = String(currentBatteryPct) + "%";
  display.setCursor(124 - (batStr.length() * 6), 2);
  display.print(batStr);

  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 16);

  if (isReceiving) {
    display.print(">> INGAAENDE SIGNAL");
  } else if (receivedMessage.length() > 0) {
    display.print(">> SIDST MODTAGET");
  } else {
    display.print(">> SIKKER KANAL");
  }
  display.drawLine(0, 26, 128, 26, SSD1306_WHITE);

  display.setCursor(0, 32);
  display.setTextWrap(true);

  if (receivedMessage.length() > 0 || isReceiving) {
    display.setTextSize(1);
    display.print(receivedMessage);
    
    if (isReceiving) {
      display.setCursor(0, 52);
      display.setTextSize(2);
      display.print(rxAnimSequence);
    }
  } else {
    display.setTextSize(2);
    display.print(currentSequence);
    
    display.setTextSize(3);
    display.setCursor(105, 36);
    display.print(lastChar);
  }
  
  display.display();
}