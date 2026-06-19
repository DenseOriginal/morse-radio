#include <RadioLib.h>

#define VEXT_PIN 36 
#define LORA_NSS 8
#define LORA_DIO1 14
#define LORA_NRST 12
#define LORA_BUSY 13

SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_NRST, LORA_BUSY);

volatile bool receivedFlag = false;

#if defined(ESP8266) || defined(ESP32)
  ICACHE_RAM_ATTR
#endif
void setFlag(void) {
  receivedFlag = true;
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(VEXT_PIN, OUTPUT);
  digitalWrite(VEXT_PIN, LOW); 
  delay(50);

  if (radio.begin(868.0) != RADIOLIB_ERR_NONE) {
    Serial.println("BRIDGE_LOG: LoRa Init Failed!");
    while (true); 
  }
  
  Serial.println("BRIDGE_LOG: Boot successful. Listening...");
  radio.setDio1Action(setFlag);
  radio.startReceive();
}

void loop() {
  if (receivedFlag) {
    receivedFlag = false;
    String str;
    int state = radio.readData(str);
    if (state == RADIOLIB_ERR_NONE) {
      Serial.println(str); 
    } else {
      Serial.println("BRIDGE_LOG: RX Error " + String(state));
    }
    radio.startReceive();
  }

  if (Serial.available()) {
    String tx = Serial.readStringUntil('\n');
    tx.trim();
    
    // Respond to Python's discovery ping
    if (tx == "PING") {
      Serial.println("BRIDGE_ACK");
      return; 
    }
    
    if (tx.length() > 0) {
      Serial.println("BRIDGE_LOG: Attempting to broadcast: " + tx);
      int state = radio.transmit(tx);
      
      if (state == RADIOLIB_ERR_NONE) {
        Serial.println("BRIDGE_LOG: TX Success.");
      } else {
        Serial.println("BRIDGE_LOG: TX Failed with code " + String(state));
      }
      radio.startReceive(); 
    }
  }
}