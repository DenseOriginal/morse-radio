#include <WiFi.h>
#include <WebSocketsClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Network configuration
const char* ssid = "Langengevej";
const char* password = "DDEA10JDFD2";
const char* serverAddress = "192.168.8.146"; 
const int serverPort = 8765;

// Hardware configuration
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#define BUTTON_PIN 3
#define LED_PIN 4

// Timing and State
const int DEBOUNCE_DELAY = 20;
const int DIT_DAH_THRESHOLD = 250; 
const int LETTER_TIMEOUT = 600;

unsigned long pressTime = 0;
unsigned long releaseTime = 0;
bool lastButtonState = HIGH;

String currentSequence = "";
char lastChar = ' ';

// New State Variables for Receiving
bool isReceiving = false;
String receivedMessage = "";

WebSocketsClient webSocket;

const char* morseTable[] = {
  ".-", "-...", "-.-.", "-..", ".", "..-.", "--.", "....", "..", ".---",
  "-.-", ".-..", "--", "-.", "---", ".--.", "--.-", ".-.", "...", "-",
  "..-", "...-", ".--", "-..-", "-.--", "--..", ".-.-", "---.", // A-Z, Æ, Ø
  "-----", ".----", "..---", "...--", "....-", ".....", "-....", "--...", "---..", "----." // 0-9
};
const char alphaTable[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZÆØ0123456789";

void wsDelay(unsigned long ms) {
  unsigned long start = millis();
  while (millis() - start < ms) {
    webSocket.loop();
    delay(1); 
  }
}

void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  if (type == WStype_TEXT) {
    String msg = (char*)payload;
    playMorseString(msg);
  }
}

void setup() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    for(;;); 
  }
  
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextWrap(true); // Ensures text wrapping is enabled
  display.setCursor(0, 0);
  display.print("Connecting WiFi...");
  display.display();

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }

  webSocket.begin(serverAddress, serverPort, "/");
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(5000);
  
  updateDisplay();
}

void loop() {
  webSocket.loop();
  
  // Block user input while receiving and beeping a message
  if (isReceiving) return;
  
  bool buttonState = digitalRead(BUTTON_PIN);
  unsigned long currentTime = millis();

  if (buttonState == LOW && lastButtonState == HIGH) {
    delay(DEBOUNCE_DELAY);
    pressTime = currentTime;
    digitalWrite(LED_PIN, HIGH);

    // Clear received message from screen when user starts typing
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
      String msg = String(lastChar);
      webSocket.sendTXT(msg);
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

void playMorseString(String text) {
  isReceiving = true;
  receivedMessage = "";
  text.toUpperCase();
  
  for (int i = 0; i < text.length(); i++) {
    char c = text[i];
    
    // Play the beep first
    if (c == ' ') {
      wsDelay(LETTER_TIMEOUT); 
    } else {
      for (int j = 0; j < 38; j++) { 
        if (alphaTable[j] == c) {
          blinkSequence(morseTable[j]);
          wsDelay(LETTER_TIMEOUT); 
          break;
        }
      }
    }
    
    // Append character and update display after beeping
    receivedMessage += c;
    updateDisplay();
  }
  
  isReceiving = false;
}

void blinkSequence(String seq) {
  for (int i = 0; i < seq.length(); i++) {
    digitalWrite(LED_PIN, HIGH);
    if (seq[i] == '.') wsDelay(DIT_DAH_THRESHOLD); 
    else wsDelay(DIT_DAH_THRESHOLD * 3); 
    
    digitalWrite(LED_PIN, LOW);
    wsDelay(DIT_DAH_THRESHOLD); 
  }
}

void updateDisplay() {
  display.clearDisplay();
  
  if (receivedMessage.length() > 0) {
    // Show incoming message
    display.setTextSize(2); 
    display.setCursor(0, 0);
    display.print(receivedMessage);
  } else {
    // Show user input
    display.setTextSize(2);
    display.setCursor(0, 0);
    display.print(currentSequence);
    display.setTextSize(4);
    display.setCursor(50, 30);
    display.print(lastChar);
  }
  
  display.display();
}