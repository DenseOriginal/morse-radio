#include <RadioLib.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define OLED_SDA 17
#define OLED_SCL 18
#define OLED_RST 21
#define VEXT_PIN 36 

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

// Standard input timings (unchanged)
const int DEBOUNCE_DELAY = 20;
const int DIT_DAH_THRESHOLD = 200; 
const int LETTER_TIMEOUT = 500;

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
    for(;;); 
  }
  
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 20);
  display.print("STARTER SYSTEM...");
  display.display();

  int state = radio.begin(868.0);
  if (state != RADIOLIB_ERR_NONE) {
    display.print("\nRADIO ERR");
    display.display();
    for(;;);
  }

  radio.setDio1Action(setFlag);
  radio.startReceive();
  
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  
  updateDisplay();
}

void loop() {
  if (receivedFlag) {
    receivedFlag = false;
    String str;
    int state = radio.readData(str);
    
    if (state == RADIOLIB_ERR_NONE) {
      int firstColon = str.indexOf(':');
      int secondColon = str.indexOf(':', firstColon + 1);
      int thirdColon = str.indexOf(':', secondColon + 1);

      String senderID = "";
      String payload = str;
      int rxDitTime = DIT_DAH_THRESHOLD;
      int rxLetterTime = LETTER_TIMEOUT;

      if (thirdColon != -1) {
        // Format: ID:DIT:LETTER:MSG (From PC)
        senderID = str.substring(0, firstColon);
        rxDitTime = str.substring(firstColon + 1, secondColon).toInt();
        rxLetterTime = str.substring(secondColon + 1, thirdColon).toInt();
        payload = str.substring(thirdColon + 1);
      } else if (firstColon != -1) {
        // Format: ID:MSG (From another remote)
        senderID = str.substring(0, firstColon);
        payload = str.substring(firstColon + 1);
      }

      if (senderID != myID) {
        playMorseString(payload, rxDitTime, rxLetterTime);
      }
    }
    radio.startReceive();
  }

  if (isReceiving) return;
  
  bool buttonState = digitalRead(BUTTON_PIN);
  unsigned long currentTime = millis();

  if (buttonState == LOW && lastButtonState == HIGH) {
    delay(DEBOUNCE_DELAY);
    pressTime = currentTime;
    digitalWrite(LED_PIN, HIGH);

    if (receivedMessage.length() > 0) {
      receivedMessage = "";
      updateDisplay();
    }
  } 
  else if (buttonState == HIGH && lastButtonState == LOW) {
    delay(DEBOUNCE_DELAY);
    releaseTime = currentTime;
    digitalWrite(LED_PIN, LOW);
    
    if ((releaseTime - pressTime) < DIT_DAH_THRESHOLD) {
      currentSequence += ".";
    } else {
      currentSequence += "-";
    }
    updateDisplay();
  }

  if (buttonState == HIGH && currentSequence.length() > 0 && (currentTime - releaseTime > LETTER_TIMEOUT)) {
    lastChar = translateMorse(currentSequence);
    currentSequence = "";
    
    if (lastChar != '?') {
      String msgStr = String(lastChar);
      String txPayload = myID + ":" + msgStr;
      radio.transmit(txPayload);
      radio.startReceive(); 
    }
    updateDisplay();
  }
  lastButtonState = buttonState;
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
  updateDisplay();
}

void updateDisplay() {
  display.clearDisplay();
  
  // Tactical Top Banner (Inverted)
  display.fillRect(0, 0, 128, 12, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setTextSize(1);
  display.setCursor(32, 2);
  display.print("MORSE RADIO");

  // Status Text & Divider
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

  // Main Text Area
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