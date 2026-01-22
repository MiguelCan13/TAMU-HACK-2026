#include <Arduino.h>
#include <SPI.h>
#include <RF24.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <FastLED.h>

const char* ssid = "heyguyswhatsup";
const char* password = "myroommatesarecool";
const char* flaskServerUrl = "http://192.168.0.198:5000/upload"; 

#define LED_PIN 38
CRGB leds[1];

RF24 radio(9, 10); 
const byte address[] = "00001";

uint16_t frame_buffer[19200]; 
struct IndexedChunk {
  uint32_t pixel_index;
  uint16_t pixels[14];
};

struct Node{
  uint8_t id;
};

uint8_t nodes[] = {1, 2};

unsigned long lastPacketTime = 0;
bool hasNewData = false;

void uploadToFlask() {
  if (WiFi.status() != WL_CONNECTED) return;

  Serial.println("\n[HTTP] Transmission gap detected. Uploading frame...");
  HTTPClient http;
  http.begin(flaskServerUrl); 
  http.addHeader("Content-Type", "application/octet-stream");
  http.addHeader("X-Device-ID", "ESP32-S3-Receiver");

  // Send the raw frame_buffer to the Python server
  int httpResponseCode = http.POST((uint8_t*)frame_buffer, sizeof(frame_buffer));

  if (httpResponseCode > 0) {
    Serial.printf("[HTTP] Success! Server Response: %d\n", httpResponseCode);
    
    // READ RESPONSE FROM SERVER
    String response = http.getString();
    Serial.println("[SERVER RESPONSE] " + response);
    
    // Example: Parse simple JSON response (you can add ArduinoJson for complex parsing)
    int num_cars = response.indexOf("\"num_cars\":");
    Serial.printf("[INFO] Number of cars detected: %d\n", num_cars);
    
  } else {
    Serial.printf("[HTTP] Failed. Error: %s\n", http.errorToString(httpResponseCode).c_str());
  }
  http.end();
}

void setup() {
  Serial.begin(115200);
  FastLED.addLeds<WS2811, LED_PIN, GRB>(leds, 1);
  leds[0] = CRGB::Red; FastLED.show();

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(100); Serial.print("."); }
  Serial.println("\n[WIFI] Connected.");

  radio.begin();
  radio.setAutoAck(false);
  radio.setChannel(115);
  radio.setDataRate(RF24_2MBPS);
  radio.openWritingPipe(address);      // For sending poll requests
  radio.openReadingPipe(1, address);   // For receiving data
  radio.startListening();

  leds[0] = CRGB::Green; FastLED.show();
  Serial.println("[SYSTEM] Receiver listening...");
}

void loop() {
  uint8_t identity = 0;
  bool nodeFound = false;

  // 1. Poll the nodes to see who is ready
  //there are 2 nodes, in order to not recieve 2 different images at the same time,
  //each node will have an id and the reciever will alternate between them, checking if 
  //data is ready to be recieved. This will be done by sending the register of the node
  //and if the node recieves its register back it will send data, otherwise it will wait.

  //check to see which node is ready to send data
  for (int i = 0; i < sizeof(nodes); i++) {
    radio.stopListening();
    uint8_t targetNode = nodes[i];
    radio.write(&targetNode, sizeof(uint8_t)); // Send poll [cite: 67]
    
    radio.startListening();
    unsigned long startWait = millis();
    while (millis() - startWait < 15) { // 15ms window to hear back [cite: 69]
      if (radio.available()) {
        radio.read(&identity, sizeof(uint8_t));
        if (identity == targetNode) {
          nodeFound = true;
          Serial.printf("[SYSTEM] Node %d is ready.\n", targetNode); 
          break;
        }
      }
    }
    if (nodeFound) break;
  }

  // 2. If a node responded, receive the image chunks
  if (nodeFound) {
    lastPacketTime = millis();
    hasNewData = true;
    
    // Updated limit for QQVGA (160x120 = 19200 pixels)
    uint32_t pixelsReceived = 0;
    while (pixelsReceived < 19200) { 
      if (radio.available()) {
        IndexedChunk incoming;
        radio.read(&incoming, sizeof(IndexedChunk)); 
        
        if (incoming.pixel_index <= (19200 - 14)) {
          for (int i = 0; i < 14; i++) {
            uint16_t p = incoming.pixels[i];
            uint8_t highByte = p >> 8; 
            uint8_t lowByte  = p & 0xFF; 
            frame_buffer[incoming.pixel_index + i] = (lowByte << 8) | highByte; 
          }
          pixelsReceived += 14;
          lastPacketTime = millis(); // Refresh timeout
        }
      }
      
      // Safety exit if sender stops mid-frame
      if (millis() - lastPacketTime > 200) break; 
    }
  }

  // 3. Gap detection for upload [cite: 77, 78]
  if (hasNewData && (millis() - lastPacketTime > 100)) {
    uploadToFlask();
    hasNewData = false;
  }
}
