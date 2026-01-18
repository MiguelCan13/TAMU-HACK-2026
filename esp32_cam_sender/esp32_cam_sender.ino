/*
 * ESP32-CAM Image Sender via NRF24L01+
 * Captures image from camera and sends it in chunks to receiver
 */

#include <SPI.h>
#include <RF24.h>
#include "esp_camera.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// NRF24L01 Configuration
#define CE_PIN 4    // Adjust based on your wiring
#define CSN_PIN 5   // Adjust based on your wiring
RF24 radio(CE_PIN, CSN_PIN);

const byte address[6] = "00001";  // Communication address

// Packet structure
#define PACKET_SIZE 32
#define HEADER_SIZE 6
#define PAYLOAD_SIZE (PACKET_SIZE - HEADER_SIZE)  // 26 bytes for image data

struct Packet {
  uint8_t packetType;      // 0=start, 1=data, 2=end
  uint16_t packetNumber;   // Sequential packet number
  uint16_t totalPackets;   // Total packets for this image
  uint8_t dataLength;      // Actual data length in this packet
  uint8_t data[PAYLOAD_SIZE];
};

// ESP32-CAM AI-Thinker pin definitions
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

void setup() {
  Serial.begin(115200);
  Serial.println("ESP32-CAM NRF24 Sender Initializing...");

  // Disable brownout detector
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  // Initialize camera
  if (!initCamera()) {
    Serial.println("Camera initialization failed!");
    while (1);
  }
  Serial.println("Camera initialized");

  // Initialize NRF24
  if (!radio.begin()) {
    Serial.println("NRF24 initialization failed!");
    Serial.println("Check wiring and power supply!");
    while (1);
  }
  
  radio.setPALevel(RF24_PA_MAX);
  radio.setDataRate(RF24_250KBPS);  // Slower but more reliable
  radio.setChannel(108);
  radio.openWritingPipe(address);
  radio.stopListening();
  
  Serial.println("NRF24 initialized");
  
  // Print diagnostic info
  Serial.println("\n=== NRF24 Configuration ===");
  Serial.print("Channel: ");
  Serial.println(radio.getChannel());
  Serial.print("Data Rate: ");
  Serial.println(radio.getDataRate());
  Serial.print("PA Level: ");
  Serial.println(radio.getPALevel());
  Serial.print("Is Chip Connected: ");
  Serial.println(radio.isChipConnected() ? "YES" : "NO");
  Serial.println("==========================\n");
  
  Serial.println("System ready!");
}

void loop() {
  Serial.println("\n--- Capturing and sending image ---");
  
  // Capture image
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    delay(2000);
    return;
  }
  
  Serial.printf("Image captured: %d bytes, %dx%d\n", fb->len, fb->width, fb->height);
  
  // Send image
  bool success = sendImage(fb->buf, fb->len);
  
  // Return framebuffer
  esp_camera_fb_return(fb);
  
  if (success) {
    Serial.println("Image sent successfully!");
  } else {
    Serial.println("Image transmission failed");
  }
  
  delay(5000);  // Wait 5 seconds before next capture
}

bool initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  
  // Use lower resolution for faster transmission
  config.frame_size = FRAMESIZE_QVGA;  // 320x240
  config.jpeg_quality = 12;  // 0-63, lower = higher quality (but larger size)
  config.fb_count = 1;
  
  // Initialize camera
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", err);
    return false;
  }
  
  return true;
}

bool sendImage(uint8_t* imageData, size_t imageSize) {
  uint16_t totalPackets = (imageSize + PAYLOAD_SIZE - 1) / PAYLOAD_SIZE;
  Serial.printf("Sending %d bytes in %d packets\n", imageSize, totalPackets);
  
  Packet packet;
  uint16_t packetNum = 0;
  size_t offset = 0;
  
  // Send START packet
  packet.packetType = 0;
  packet.packetNumber = 0;
  packet.totalPackets = totalPackets;
  packet.dataLength = 0;
  while(!sendPacketWithRetry(packet)) {
    Serial.println("Failed to send START packet");
  }
  Serial.println("START packet sent");
  
  // Send DATA packets
  while (offset < imageSize) {
    packetNum++;
    size_t remaining = imageSize - offset;
    size_t chunkSize = (remaining < PAYLOAD_SIZE) ? remaining : PAYLOAD_SIZE;
    
    packet.packetType = 1;
    packet.packetNumber = packetNum;
    packet.totalPackets = totalPackets;
    packet.dataLength = chunkSize;
    memcpy(packet.data, imageData + offset, chunkSize);
    
    while(!sendPacketWithRetry(packet)) {
      Serial.printf("Failed to send packet %d\n", packetNum);
    }
    
    offset += chunkSize;
    
    // Print progress every 50 packets
    if (packetNum % 50 == 0) {
      Serial.printf("Progress: %d/%d packets\n", packetNum, totalPackets);
    }
  }
  
  // Send END packet
  packet.packetType = 2;
  packet.packetNumber = packetNum + 1;
  packet.totalPackets = totalPackets;
  packet.dataLength = 0;
  while(!sendPacketWithRetry(packet)) {
    Serial.println("Failed to send END packet");
  }
  Serial.println("END packet sent");
  
  return true;
}

bool sendPacketWithRetry(Packet& packet) {
  const int maxRetries = 3;
  
  for (int i = 0; i < maxRetries; i++) {
    if (radio.write(&packet, PACKET_SIZE)) {
      return true;
    }
    
    // Print diagnostic info on failure
    if (i == 0) {
      Serial.printf("Send failed (attempt %d/%d). Diagnostics:\n", i+1, maxRetries);
      Serial.printf("  - Chip connected: %s\n", radio.isChipConnected() ? "YES" : "NO");
      Serial.printf("  - Packet type: %d, number: %d\n", packet.packetType, packet.packetNumber);
    }
    
    delay(10);  // Short delay before retry
  }
  
  Serial.println("FAILED after all retries!");
  Serial.println("\nTroubleshooting tips:");
  Serial.println("1. Check NRF24 power (add 10µF capacitor!)");
  Serial.println("2. Verify wiring (CE, CSN, SCK, MOSI, MISO)");
  Serial.println("3. Ensure receiver is powered on and listening");
  Serial.println("4. Try reducing distance between modules");
  Serial.println("5. Check for loose connections");
  
  return false;
}
