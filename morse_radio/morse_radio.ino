#include <RadioLib.h>
#include <U8g2lib.h>
#include <Wire.h>

// Heltec V3 Pins
#define NSS 8
#define DIO1 14
#define NRST 12
#define BUSY 13
#define BUTTON_PIN 0
#define LED_PIN 35

// Heltec V3 OLED Pins
#define SDA_OLED 17
#define SCL_OLED 18
#define RST_OLED 21

// Heltec V3 Battery Pins
#define VBAT_PIN 1
#define VBAT_CTRL 37

const char DEVICE_ID = '3';

SX1262 radio = new Module(NSS, DIO1, NRST, BUSY);
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, RST_OLED, SCL_OLED, SDA_OLED);

// Input Timing
const int DOT_DUR = 250;
const int CHAR_GAP = 600;

// Playback Timing
const int PLAY_DOT_DUR = 100;
const int PLAY_CHAR_GAP = 300;

const int MAX_CHARS_PER_LINE = 21;
const int MAX_LOG_LEN = 42;

unsigned long btnPressTime = 0;
unsigned long btnReleaseTime = 0;
bool isPressed = false;
String morseBuffer = "";
String textLog = "";
String txQueue = "";
String radioState = "RDY";
String serialBuffer = "";
bool displayNeedsUpdate = false;
bool isTransmitting = false;
bool bridgeMode = false;
bool bridgeAllowed = false;

// Signal Quality Metrics
float lastRSSI = 0;
float lastSNR = 0;
bool hasSignalInfo = false;

// RX Playback Variables
String rxCharQueue = "";
String currentMorse = "";
char currentChar = 0;
int morseIndex = 0;
bool isPlayingSymbol = false;
unsigned long playTimer = 0;
int playDuration = 0;
String displayMorseBuffer = "";

enum DataDirection { DIR_NONE, DIR_TX, DIR_RX };
DataDirection lastDirection = DIR_NONE;

const char* morseAlphabet[] = {
  ".-", "-...", "-.-.", "-..", ".", "..-.", "--.", "....", "..", ".---",
  "-.-", ".-..", "--", "-.", "---", ".--.", "--.-", ".-.", "...", "-",
  "..-", "...-", ".--", "-..-", "-.--", "--..",
  "-----", ".----", "..---", "...--", "....-", ".....", "-....", "--...", "---..", "----."
};

char decodeMorse(String m) {
  for (int i = 0; i < 36; i++) {
    if (m == morseAlphabet[i]) {
      if (i < 26) return (char)('A' + i);
      else return (char)('0' + (i - 26));
    }
  }
  return '?'; 
}

String encodeMorse(char c) {
  c = toupper(c);
  if (c >= 'A' && c <= 'Z') return morseAlphabet[c - 'A'];
  if (c >= '0' && c <= '9') return morseAlphabet[c - '0' + 26];
  return "";
}

String makePacket(bool fromBridge, char id, const String& message) {
  uint8_t header = (fromBridge ? 0x08 : 0x00) | (id - '0');
  String packet;
  packet += (char)header;
  packet += message;
  return packet;
}

int getBatteryPct() {
  digitalWrite(VBAT_CTRL, LOW);
  delay(5);
  float voltage = (analogRead(VBAT_PIN) / 4095.0) * 3.3 * 4.9;
  digitalWrite(VBAT_CTRL, HIGH);
  
  int pct = (voltage - 3.2) / (4.2 - 3.2) * 100;
  if (pct > 100) return 100;
  if (pct < 0) return 0;
  return pct;
}

void updateDisplay() {
  u8g2.clearBuffer();
  
  u8g2.setCursor(0, 10);
  u8g2.print(radioState);
  
  String rightText = "";
  if (hasSignalInfo) {
    rightText += String(lastRSSI, 0) + "dB ";
    if (lastSNR > 0) rightText += "+";
    rightText += String(lastSNR, 1) + " ";
  }
  rightText += String(getBatteryPct()) + "%";
  
  int textWidth = u8g2.getStrWidth(rightText.c_str());
  u8g2.setCursor(128 - textWidth, 10);
  u8g2.print(rightText);
  
  if (textLog.length() > MAX_LOG_LEN) {
    textLog = textLog.substring(textLog.length() - MAX_LOG_LEN);
  }
  
  String line1 = textLog.substring(0, min((int)textLog.length(), MAX_CHARS_PER_LINE));
  String line2 = "";
  if (textLog.length() > MAX_CHARS_PER_LINE) {
    line2 = textLog.substring(MAX_CHARS_PER_LINE);
  }
  
  u8g2.setCursor(0, 25);
  u8g2.print(line1);
  u8g2.setCursor(0, 37);
  u8g2.print(line2);
  
  u8g2.setCursor(0, 57);
  if (rxCharQueue.length() > 0 || currentMorse.length() > 0) {
    u8g2.print(displayMorseBuffer);
  } else {
    u8g2.print(morseBuffer);
  }
  
  u8g2.sendBuffer();
}

void handlePlayback() {
  if (rxCharQueue.length() == 0 && currentMorse.length() == 0) return;

  unsigned long now = millis();
  if (now - playTimer < playDuration) return;

  if (isPlayingSymbol) {
    digitalWrite(LED_PIN, LOW);
    isPlayingSymbol = false;
    playTimer = now;
    morseIndex++;
    
    if (morseIndex < currentMorse.length()) {
      playDuration = PLAY_DOT_DUR; 
    } else {
      textLog += currentChar;
      displayMorseBuffer = "";
      currentMorse = "";
      displayNeedsUpdate = true;
      playDuration = PLAY_CHAR_GAP; 
    }
  } else {
    if (currentMorse.length() == 0) {
      if (rxCharQueue.length() > 0) {
        currentChar = rxCharQueue.charAt(0);
        rxCharQueue.remove(0, 1);
        currentMorse = encodeMorse(currentChar);
        
        if (currentMorse == "") {
          textLog += currentChar;
          displayNeedsUpdate = true;
          return; 
        }
        
        morseIndex = 0;
        displayMorseBuffer = "";
        playDuration = 0; 
      }
    } else {
      char symbol = currentMorse.charAt(morseIndex);
      displayMorseBuffer += symbol;
      displayNeedsUpdate = true;
      
      digitalWrite(LED_PIN, HIGH);
      isPlayingSymbol = true;
      playTimer = now;
      playDuration = (symbol == '-') ? (PLAY_DOT_DUR * 3) : PLAY_DOT_DUR;
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial.setTimeout(10);
  
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  pinMode(VBAT_CTRL, OUTPUT);

  digitalWrite(VBAT_CTRL, HIGH);
  analogReadResolution(12);
  
  bridgeMode = false;
  radioState = "RDY";
  
  u8g2.begin();
  u8g2.setFont(u8g2_font_6x10_tf); 
  
  int state = radio.begin(868.0, 125.0, 12, 5, 0x42, 22);
  
  if (state == RADIOLIB_ERR_NONE) {
    radio.startReceive();
    updateDisplay();
  } else {
    u8g2.clearBuffer();
    u8g2.drawStr(0, 10, "Radio Error");
    u8g2.sendBuffer();
    while (true);
  }

  updateDisplay();

  delay(200);
  bridgeAllowed = (digitalRead(BUTTON_PIN) == LOW);
}

void loop() {
  bool btnState = (digitalRead(BUTTON_PIN) == LOW);
  unsigned long now = millis();

  // Process serial input for Bridge Mode commands or TX queue
  if(bridgeAllowed) {
    if (Serial.available()) {
      String incoming = Serial.readString();
      String cleanStr = "";
      
      // Filter for printable ASCII
      for (int i = 0; i < incoming.length(); i++) {
        char c = incoming.charAt(i);
        if (c >= 32 && c <= 126) cleanStr += c;
      }
      
      if (cleanStr.indexOf("BRG_ON") >= 0) {
        bridgeMode = true;
        radioState = "BRG";
        displayNeedsUpdate = true;
      } else if (cleanStr.indexOf("BRG_OFF") >= 0) {
        bridgeMode = false;
        radioState = "RDY";
        displayNeedsUpdate = true;
      } else if (bridgeMode && cleanStr.startsWith("BRG_SEND:")) {
        int separator = cleanStr.indexOf(':', 9);
        if (separator > 9) {
          char destination = cleanStr.charAt(9);
          String message = cleanStr.substring(separator + 1);
          if (destination >= '1' && destination <= '7' && message.length() > 0) {
            txQueue += makePacket(true, destination, message);
            displayNeedsUpdate = true;
          }
        }
      } else if (bridgeMode && cleanStr.length() > 0) {
        txQueue += makePacket(true, '1', cleanStr);
        displayNeedsUpdate = true;
      }
    }
  }

  static unsigned long lastBatUpdate = 0;
  if (now - lastBatUpdate > 30000) {
    lastBatUpdate = now;
    displayNeedsUpdate = true;
  }

  // Handle Button Input
  if (btnState && !isPressed) {
    isPressed = true;
    btnPressTime = now;
    digitalWrite(LED_PIN, HIGH);
  }
  else if (!btnState && isPressed) {
    isPressed = false;
    btnReleaseTime = now;
    digitalWrite(LED_PIN, LOW);
    unsigned long dur = now - btnPressTime;
    
    if (dur > 20) {
      morseBuffer += (dur > DOT_DUR) ? "-" : ".";
      displayNeedsUpdate = true;
    }
  }

  // Decode Character and add to TX Queue
  if (!isPressed && morseBuffer.length() > 0 && (now - btnReleaseTime) > CHAR_GAP) {
    char decoded = decodeMorse(morseBuffer);
    morseBuffer = "";
    txQueue += String(decoded);
    displayNeedsUpdate = true;
  }

  // Async Transmit Logic
  if (!isTransmitting && txQueue.length() > 0) {
    if (lastDirection == DIR_RX) textLog = ""; 
    lastDirection = DIR_TX;
    hasSignalInfo = false;
    
    textLog += txQueue;
    radioState = "TX";
    updateDisplay();
    
    String packet = bridgeMode ? txQueue : makePacket(false, DEVICE_ID, txQueue);
    radio.startTransmit(packet);
    txQueue = ""; 
    isTransmitting = true;
  }

  // Handle Radio Interrupts (DIO1)
  if (digitalRead(DIO1)) {
    if (isTransmitting) {
      isTransmitting = false;
      radioState = bridgeMode ? "BRG" : "RDY";
      radio.standby();
      radio.startReceive();
      displayNeedsUpdate = true;
    } else {
      String rxData;
      int state = radio.readData(rxData);
      
      if (state == RADIOLIB_ERR_NONE && rxData.length() >= 1) {
        uint8_t header = (uint8_t)rxData.charAt(0);
        bool validHeader = (header & 0xF0) == 0;
        bool packetFromBridge = (header & 0x08) != 0;
        char packetId = (char)('0' + (header & 0x07));
        bool isForThisDevice = bridgeMode
          ? validHeader && !packetFromBridge && packetId != '0'
          : validHeader && packetFromBridge && packetId == DEVICE_ID;

        if (!isForThisDevice) {
          radio.standby();
          radio.startReceive();
          return;
        }

        rxData.remove(0, 1);
        if (lastDirection == DIR_TX) textLog = ""; 
        lastDirection = DIR_RX;
        
        // Filter incoming LoRa packets for printable ASCII
        for (int i = 0; i < rxData.length(); i++) {
          char c = rxData.charAt(i);
          if (c >= 32 && c <= 126) { 
            rxCharQueue += c; 
          }
        }
        
        lastRSSI = radio.getRSSI();
        lastSNR = radio.getSNR();
        hasSignalInfo = true;
        
        if (bridgeMode) {
          Serial.print("BRG_RX:");
          Serial.print(packetId);
          Serial.print(":");
          Serial.println(rxData);
        }
      }
      
      radio.standby(); 
      radio.startReceive();
    }
  }

  handlePlayback();

  if (displayNeedsUpdate) {
    updateDisplay();
    displayNeedsUpdate = false;
  }
}