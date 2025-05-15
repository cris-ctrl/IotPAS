  #include <ESP8266WiFi.h>
#include <espnow.h>

// ============== CONFIGURATION ==============
const char* selfName = "2C"; 
uint8_t controllerMAC[] = {0x2C, 0xF4, 0x32, 0x8C, 0x09, 0xBF}; // ESP-09's MAC
uint8_t nextNodeMAC[] = {}; // MAC of next device in chain
// ===========================================

struct ControlPacket {
  char target[4];
  bool led;
};

struct AckPacket {
  char name[4];
};

unsigned long lastAck = 0;
const long ackInterval = 2000;

void sendAck() {
  AckPacket ack;
  strncpy(ack.name, selfName, 3);
  ack.name[3] = '\0';
  esp_now_send(controllerMAC, (uint8_t *)&ack, sizeof(ack));
}

void onReceive(uint8_t *mac, uint8_t *data, uint8_t len) {
  if (len == sizeof(ControlPacket)) {
    ControlPacket* pkt = (ControlPacket*)data;
    
    // Handle local command
    if (strcmp(pkt->target, selfName) == 0) {
      digitalWrite(LED_BUILTIN, pkt->led ? LOW : HIGH);
    }
    // Forward if we have a next node and packet isn't for us
    else if (sizeof(nextNodeMAC) == 6) {
      esp_now_send(nextNodeMAC, data, len);
    }
  }
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
  
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  if (esp_now_init() != 0) return;
  
  esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
  
  // Always connect to controller
  esp_now_add_peer(controllerMAC, ESP_NOW_ROLE_COMBO, 1, NULL, 0);
  
  // Only connect to next node if MAC is specified
  if (sizeof(nextNodeMAC) == 6) {
    esp_now_add_peer(nextNodeMAC, ESP_NOW_ROLE_COMBO, 1, NULL, 0);
  }

  esp_now_register_recv_cb(onReceive);
}

void loop() {
  if (millis() - lastAck >= ackInterval) {
    sendAck();
    lastAck = millis();
  }
}
