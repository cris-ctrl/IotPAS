#include <ESP8266WiFi.h>
#include <espnow.h>

// Peer MAC addresses (update with your actual MACs)
uint8_t macA4[] = {0xA4, 0xE5, 0x7C, 0xBB, 0xE9, 0xFC};
uint8_t mac2C[] = {0x2C, 0xF4, 0x32, 0x8C, 0x0D, 0x13};

WiFiServer server(80);

// Packet structures
struct ControlPacket {
  char target[4];
  bool led;
};

struct AckPacket {
  char name[4];
};

// Last received ACK timestamps
unsigned long lastA4Ack = 0;
unsigned long last2CAck = 0;

// Debug print function for MAC addresses
void printMac(const char* label, const uint8_t* mac) {
  Serial.printf("%s: %02X:%02X:%02X:%02X:%02X:%02X\n", 
                label, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void sendToggle(String target, bool state) {
  ControlPacket pkt;
  strncpy(pkt.target, target.c_str(), 3);
  pkt.target[3] = '\0';
  pkt.led = state;
  
  Serial.printf("[SEND] Target: %s, State: %d\n", pkt.target, pkt.led);
  esp_now_send(target == "A4" ? macA4 : mac2C, (uint8_t *)&pkt, sizeof(pkt));
}

void onReceive(uint8_t *mac, uint8_t *data, uint8_t len) {
  Serial.printf("\n[RECV] MAC: %02X:%02X:%02X:%02X:%02X:%02X | Len: %d\n", 
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], len);
  
  if (len == sizeof(AckPacket)) {
    AckPacket* ack = (AckPacket*)data;
    Serial.printf("[ACK] From: %s\n", ack->name);
    
    if (strcmp(ack->name, "A4") == 0) lastA4Ack = millis();
    else if (strcmp(ack->name, "2C") == 0) last2CAck = millis();
  }
}

void handleClient(WiFiClient client) {
  String request = client.readStringUntil('\r');
  client.flush();

  if (request.indexOf("/toggle") >= 0) {
    String tgt = request.indexOf("2C") >= 0 ? "2C" : "A4";
    bool state = request.indexOf("state=1") >= 0;
    sendToggle(tgt, state);
  }
  else if (request.indexOf("/status") >= 0) {
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: application/json");
    client.println("Connection: close\r\n");
    client.printf("{\"A4\":%lu,\"2C\":%lu}", 
                 millis() - lastA4Ack, millis() - last2CAck);
    return;
  }

  // Send the complete HTML with working JavaScript
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html");
  client.println("Connection: close\r\n");
  client.println(R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <title>ESP Dashboard</title>
  <style>
    .circle {
      width: 50px; 
      height: 50px;
      border-radius: 50%;
      background: #f0f0f0;
      margin: 10px;
      display: inline-block;
      transition: background 0.3s;
      border: 2px solid #cccccc;
    }
    .green {
      background: #00ff88 !important;
    }
    .status {
      color: #666;
      font-size: 0.8em;
      margin-left: 10px;
    }
    label {
      vertical-align: super;
    }
  </style>
</head>
<body>
  <h2>ESP Controller</h2>
  <div>
    <div id="circle2C" class="circle"></div>
    <label>
      <input type="checkbox" onchange="toggle('2C', this.checked)"> 2C LED
    </label>
    <span id="status2C" class="status"></span>
  </div>
  <div>
    <div id="circleA4" class="circle"></div>
    <label>
      <input type="checkbox" onchange="toggle('A4', this.checked)"> A4 LED
    </label>
    <span id="statusA4" class="status"></span>
  </div>

  <script>
    let lastA4 = 0, last2C = 0;
    
    function checkStatus() {
      fetch('/status')
        .then(response => response.json())
        .then(data => {
          // Handle A4 updates
          if (data.A4 < lastA4) {
            flash('A4');
          }
          document.getElementById('statusA4').textContent = 
            `${Math.floor(data.A4/1000)} seconds ago`;
          lastA4 = data.A4;

          // Handle 2C updates (using bracket notation for numeric key)
          if (data['2C'] < last2C) {
            flash('2C');
          }
          document.getElementById('status2C').textContent = 
            `${Math.floor(data['2C']/1000)} seconds ago`;
          last2C = data['2C'];

          // Repeat every 100ms
          setTimeout(checkStatus, 100);
        })
        .catch(error => {
          console.error('Status check failed:', error);
          setTimeout(checkStatus, 1000);
        });
    }

    function flash(target) {
      const circle = document.getElementById(`circle${target}`);
      circle.classList.add('green');
      setTimeout(() => {
        circle.classList.remove('green');
      }, 200);
    }

    function toggle(target, state) {
      fetch(`/toggle?target=${target}&state=${state ? 1 : 0}`)
        .catch(error => console.error('Toggle failed:', error));
    }

    // Start status checks when page loads
    checkStatus();
  </script>
</body>
</html>
)rawliteral");
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n\n[BOOT] Starting controller...");

  // Get controller's MAC address
  uint8_t controllerMac[6];
  WiFi.macAddress(controllerMac);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin("Fiodor", "dotoievski");
  
  Serial.print("[WiFi] Connecting");
  unsigned long wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (millis() - wifiStart > 10000) {
      Serial.println("\n[WiFi] Failed to connect!");
      return;
    }
  }
  Serial.printf("\n[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());

  // Print MAC addresses
  printMac("[MAC] Controller", controllerMac);
  printMac("[PEER] A4", macA4);
  printMac("[PEER] 2C", mac2C);

  // Initialize ESP-NOW
  if (esp_now_init() != 0) {
    Serial.println("[ESP-NOW] Init failed!");
    return;
  }

  esp_now_set_self_role(ESP_NOW_ROLE_CONTROLLER);
  
  // Add peers with success checks
  if (esp_now_add_peer(macA4, ESP_NOW_ROLE_COMBO, 1, NULL, 0) == 0) {
    Serial.println("[PEER] A4 added successfully");
  } else {
    Serial.println("[PEER] Failed to add A4");
  }
  
  if (esp_now_add_peer(mac2C, ESP_NOW_ROLE_COMBO, 1, NULL, 0) == 0) {
    Serial.println("[PEER] 2C added successfully");
  } else {
    Serial.println("[PEER] Failed to add 2C");
  }

  esp_now_register_recv_cb(onReceive);
  server.begin();
  Serial.println("[HTTP] Server started");
}

void loop() {
  static unsigned long lastStatus = 0;
  
  // Periodic status updates
  if (millis() - lastStatus > 5000) {
    Serial.printf("[STATUS] A4: %lums ago | 2C: %lums ago\n", 
                 millis() - lastA4Ack, millis() - last2CAck);
    lastStatus = millis();
  }
  
  WiFiClient client = server.available();
  if (client) handleClient(client);
}
