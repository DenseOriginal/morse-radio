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

SX1262 radio = new Module(NSS, DIO1, NRST, BUSY);
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, RST_OLED, SCL_OLED, SDA_OLED);

int counter = 0;
unsigned long lastTxTime = 0;
const unsigned long TX_INTERVAL = 5000; // Transmit every 5 seconds

// State variables for toggling
bool isRunning = false;
bool isPressed = false;

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

void updateDisplay(String msg) {
  u8g2.clearBuffer();
  
  u8g2.setCursor(0, 10);
  u8g2.print(isRunning ? "Mode: TX RUN" : "Mode: TX PAUSED");
  u8g2.setCursor(85, 10);
  u8g2.print(getBatteryPct());
  u8g2.print("%");
  
  u8g2.setCursor(0, 35);
  u8g2.print("Sent: ");
  u8g2.print(msg);
  
  u8g2.sendBuffer();
}

void setup() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  pinMode(VBAT_CTRL, OUTPUT);
  digitalWrite(VBAT_CTRL, HIGH);
  analogReadResolution(12);
  
  u8g2.begin();
  u8g2.setFont(u8g2_font_6x10_tf); 
  
  // Maximum range settings: 868 MHz, 125 kHz bandwidth, SF12, CR 4/5, 22 dBm power
  int state = radio.begin(868.0, 125.0, 12, 5, 0x12, 22);
  
  if (state != RADIOLIB_ERR_NONE) {
    u8g2.clearBuffer();
    u8g2.drawStr(0, 10, "Radio Error");
    u8g2.sendBuffer();
    while (true);
  }
  
  updateDisplay("Ready");
}

void loop() {
  bool btnState = (digitalRead(BUTTON_PIN) == LOW);

  // Button toggle logic
  if (btnState && !isPressed) {
    isPressed = true;
    isRunning = !isRunning;
    lastTxTime = millis() - TX_INTERVAL; // Trigger immediate transmit on resume
    updateDisplay(isRunning ? "Starting..." : "Paused");
    delay(50); // Basic debounce
  } else if (!btnState) {
    isPressed = false;
  }

  // Transmit loop
  if (isRunning && (millis() - lastTxTime >= TX_INTERVAL)) {
    lastTxTime = millis();
    
    String txStr = String(counter);
    
    updateDisplay(txStr);
    digitalWrite(LED_PIN, HIGH);
    
    // Blocking transmit is used here to ensure the radio completes
    // sending before attempting to do anything else.
    radio.transmit(txStr);
    
    digitalWrite(LED_PIN, LOW);
    
    counter++;
    if (counter > 9) {
      counter = 0;
    }
  }
}