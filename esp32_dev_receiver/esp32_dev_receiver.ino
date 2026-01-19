/*
 * ESP32-Dev Image Receiver via NRF24L01+
 * Receives image chunks and reassembles them
 * Uploads received images to web server
 */

#include <SPI.h>
#include <RF24.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <HTTPClient.h>

// WiFi Configuration
const char* ssid = "heyguyswhatsup";      // Change this
const char* password = "myroommatesarecool";  // Change this

// Web Server Configuration
const char* serverUrl = "http://192.168.0.198:5000/upload";  // Change this to your server
// Examples:
// Local server: "http://192.168.1.100:5000/upload"
// Cloud server: "https://yourserver.com/api/upload"
// Python Flask default: "http://192.168.1.100:5000/upload"

// NRF24L01 Configuration
#define CE_PIN 9    // Adjust based on your wiring
#define CSN_PIN 10   // Adjust based on your wiring
RF24 radio(CE_PIN, CSN_PIN);

const byte address[6] = "00001";  // Must match sender - receive data on this
const byte ackAddress[6] = "00002";  // Send ACKs on this

// Packet structure (must match sender)
#define PACKET_SIZE 32
#define HEADER_SIZE 6
#define PAYLOAD_SIZE (PACKET_SIZE - HEADER_SIZE)

struct Packet {
  uint8_t packetType;      // 0=start, 1=data, 2=end
  uint16_t packetNumber;   // Sequential packet number
  uint16_t totalPackets;   // Total packets for this image
  uint8_t dataLength;      // Actual data length in this packet
  uint8_t data[PAYLOAD_SIZE];
};

// ACK packet structure (must match sender)
struct AckPacket {
  uint8_t ackType;         // 0=ACK (success), 1=NACK (retry), 2=READY
  uint16_t packetNumber;   // Which packet is being acknowledged
  uint8_t padding[29];     // Pad to 32 bytes
};

//function prototypes
void handlePacket(Packet& packet);
void handleStartPacket(Packet& packet);
bool handleDataPacket(Packet& packet);
void handleEndPacket(Packet& packet);
void resetReceiver();
bool uploadImageToServer(const char* filepath);
void sendAck(uint16_t packetNum, bool success);
//void sendImageOverSerial();

// Image reception state
bool receivingImage = false;
uint16_t expectedPacket = 0;
uint16_t totalPackets = 0;
size_t totalBytesReceived = 0;
File imageFile;
unsigned long lastPacketTime = 0;
const unsigned long TIMEOUT_MS = 5000;  // 5 second timeout

void setup() {
  Serial.begin(115200);
  Serial.println("ESP32 NRF24 Receiver Initializing...");

  // Initialize SPIFFS for image storage
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS initialization failed!");
    while (1);
  }
  Serial.println("SPIFFS initialized");

  // Connect to WiFi
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);
  int wifiAttempts = 0;
  while (WiFi.status() != WL_CONNECTED && wifiAttempts < 20) {
    delay(500);
    Serial.print(".");
    wifiAttempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nWiFi connection failed! Will save images locally only.");
  }

  // Initialize NRF24
  //SPI.begin(14, 12, 13, 15);
  if (!radio.begin()) {
    Serial.println("NRF24 initialization failed!");
    while (1);
  }
  
  radio.setPALevel(RF24_PA_MAX);
  radio.setDataRate(RF24_250KBPS);
  radio.setChannel(108);
  radio.openWritingPipe(ackAddress);     // Write ACKs on pipe "00002"
  radio.openReadingPipe(1, address);     // Read data on pipe "00001"
  radio.startListening();
  
  Serial.println("NRF24 initialized with ACK support");
  Serial.println("  RX pipe: 00001 (data)");
  Serial.println("  TX pipe: 00002 (ACKs)");
  Serial.println("Waiting for images...");
}

void loop() {
  // Check for timeout
  if (receivingImage && (millis() - lastPacketTime > TIMEOUT_MS)) {
    Serial.println("Timeout! Resetting receiver");
    resetReceiver();
  }

  // Check for incoming packets
  if (radio.available()) {
    Packet packet;
    radio.read(&packet, PACKET_SIZE);
    lastPacketTime = millis();
    
    handlePacket(packet);
  }
}

void handlePacket(Packet& packet) {
  bool success = false;
  
  switch (packet.packetType) {
    case 0:  // START packet
      handleStartPacket(packet);
      success = true;
      break;
      
    case 1:  // DATA packet
      success = handleDataPacket(packet);
      break;
      
    case 2:  // END packet
      handleEndPacket(packet);
      success = true;
      break;
      
    default:
      Serial.printf("Unknown packet type: %d\n", packet.packetType);
      success = false;
  }
  
  // Send ACK or NACK
  sendAck(packet.packetNumber, success);
}

void handleStartPacket(Packet& packet) {
  Serial.println("\n=== Receiving new image ===");
  Serial.printf("Total packets expected: %d\n", packet.totalPackets);
  
  // Clean up any previous reception
  if (receivingImage) {
    imageFile.close();
  }
  
  // Open new file for writing
  imageFile = SPIFFS.open("/received_image.jpg", FILE_WRITE);
  if (!imageFile) {
    Serial.println("Failed to open file for writing!");
    return;
  }
  
  receivingImage = true;
  expectedPacket = 1;
  totalPackets = packet.totalPackets;
  totalBytesReceived = 0;
}

bool handleDataPacket(Packet& packet) {
  if (!receivingImage) {
    Serial.println("Received data packet but not in receiving mode!");
    return false;
  }
  
  // Verify packet sequence - CRITICAL for image integrity
  if (packet.packetNumber != expectedPacket) {
    Serial.printf("❌ PACKET ORDER ERROR! Expected %d, got %d - REJECTING\n", 
                  expectedPacket, packet.packetNumber);
    // DO NOT increment expectedPacket - we need THIS packet
    return false;  // Send NACK to request correct packet
  }
  
  // Verify data length is valid
  if (packet.dataLength == 0 || packet.dataLength > PAYLOAD_SIZE) {
    Serial.printf("❌ Invalid data length: %d - sending NACK\n", packet.dataLength);
    return false;  // Send NACK
  }
  
  // Write data to file
  size_t written = imageFile.write(packet.data, packet.dataLength);
  if (written != packet.dataLength) {
    Serial.printf("❌ Write error! Expected %d, wrote %d - sending NACK\n", 
                  packet.dataLength, written);
    return false;  // Send NACK
  }
  
  totalBytesReceived += written;
  expectedPacket++;
  
  // Print progress every 50 packets
  if (expectedPacket % 50 == 0) {
    Serial.printf("✓ Progress: %d/%d packets, %d bytes\n", 
                  expectedPacket - 1, totalPackets, totalBytesReceived);
  }
  
  return true;  // Send ACK
}

void handleEndPacket(Packet& packet) {
  if (!receivingImage) {
    Serial.println("Received END packet but not in receiving mode!");
    return;
  }
  
  // END packet comes after all data packets, so it's packetNumber should be expectedPacket
  Serial.printf("END packet received (packet #%d, expected #%d)\n", 
                packet.packetNumber, expectedPacket);
  
  Serial.println("\n=== Image reception complete ===");
  Serial.printf("Total packets received: %d/%d\n", expectedPacket - 1, totalPackets);
  Serial.printf("Total bytes: %d\n", totalBytesReceived);
  
  imageFile.flush();  // Ensure all data is written
  imageFile.close();
  
  // Display file info
  File file = SPIFFS.open("/received_image.jpg", FILE_READ);
  if (file) {
    Serial.printf("Saved file size: %d bytes\n", file.size());
    
    // Verify it's a valid JPEG (starts with FF D8)
    if (file.size() >= 2) {
      uint8_t header[2];
      file.read(header, 2);
      if (header[0] == 0xFF && header[1] == 0xD8) {
        Serial.println("✓ Valid JPEG header (FF D8)");
      } else {
        Serial.printf("❌ Invalid JPEG header: 0x%02X 0x%02X (expected FF D8)\n", header[0], header[1]);
      }
      
      // Check JPEG footer (should end with FF D9)
      if (file.size() >= 2) {
        file.seek(file.size() - 2);
        uint8_t footer[2];
        file.read(footer, 2);
        if (footer[0] == 0xFF && footer[1] == 0xD9) {
          Serial.println("✓ Valid JPEG footer (FF D9)");
        } else {
          Serial.printf("❌ Invalid JPEG footer: 0x%02X 0x%02X (expected FF D9)\n", footer[0], footer[1]);
          Serial.println("⚠️  Image may be incomplete or corrupted!");
        }
      }
      
      // Print first 16 bytes for debugging
      file.seek(0);
      Serial.print("First 16 bytes: ");
      for (int i = 0; i < 16 && i < file.size(); i++) {
        Serial.printf("%02X ", file.read());
      }
      Serial.println();
    }
    
    file.close();
    
    Serial.println("Image saved as: /received_image.jpg");
    
    // Upload to web server
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nUploading to web server...");
      if (uploadImageToServer("/received_image.jpg")) {
        Serial.println("Upload successful!");
      } else {
        Serial.println("Upload failed!");
      }
    } else {
      Serial.println("WiFi not connected. Image saved locally only.");
    }
  }
  
  receivingImage = false;
}

void resetReceiver() {
  if (receivingImage && imageFile) {
    imageFile.close();
  }
  
  receivingImage = false;
  expectedPacket = 0;
  totalPackets = 0;
  totalBytesReceived = 0;
}

// Send ACK or NACK to sender
void sendAck(uint16_t packetNum, bool success) {
  AckPacket ack;
  ack.ackType = success ? 0 : 1;  // 0=ACK, 1=NACK
  ack.packetNumber = packetNum;
  memset(ack.padding, 0, sizeof(ack.padding));
  
  radio.stopListening();
  bool sent = radio.write(&ack, sizeof(AckPacket));
  radio.startListening();
  
  if (!sent) {
    Serial.printf("Failed to send %s for packet %d\n", success ? "ACK" : "NACK", packetNum);
  }
}

// Function to upload image to web server
bool uploadImageToServer(const char* filepath) {
  File file = SPIFFS.open(filepath, FILE_READ);
  if (!file) {
    Serial.println("Failed to open file for upload");
    return false;
  }
  
  size_t fileSize = file.size();
  Serial.printf("Uploading %d bytes...\n", fileSize);
  
  // Read file into buffer
  uint8_t* buffer = (uint8_t*)malloc(fileSize);
  if (!buffer) {
    Serial.println("Failed to allocate buffer");
    file.close();
    return false;
  }
  
  size_t bytesRead = file.read(buffer, fileSize);
  file.close();
  
  if (bytesRead != fileSize) {
    Serial.printf("⚠️  File read mismatch: read %d, expected %d\n", bytesRead, fileSize);
    free(buffer);
    return false;
  }
  
  // Verify JPEG integrity before upload
  if (buffer[0] != 0xFF || buffer[1] != 0xD8) {
    Serial.printf("❌ Buffer has invalid JPEG header: 0x%02X 0x%02X\n", buffer[0], buffer[1]);
  }
  if (buffer[fileSize-2] != 0xFF || buffer[fileSize-1] != 0xD9) {
    Serial.printf("❌ Buffer has invalid JPEG footer: 0x%02X 0x%02X\n", 
                  buffer[fileSize-2], buffer[fileSize-1]);
  }
  
  HTTPClient http;
  http.begin(serverUrl);
  http.setTimeout(10000);  // 10 second timeout
  
  // Set content type for JPEG image
  http.addHeader("Content-Type", "image/jpeg");
  http.addHeader("Content-Length", String(fileSize));
  http.addHeader("X-Device-ID", "ESP32-Receiver");
  http.addHeader("X-Timestamp", String(millis()));
  
  // Send POST request
  Serial.println("Sending HTTP POST...");
  int httpResponseCode = http.POST(buffer, fileSize);
  
  free(buffer);
  
  if (httpResponseCode > 0) {
    Serial.printf("HTTP Response code: %d\n", httpResponseCode);
    String response = http.getString();
    Serial.println("Server response: " + response);
    http.end();
    return (httpResponseCode == 200 || httpResponseCode == 201);
  } else {
    Serial.printf("HTTP Error: %s\n", http.errorToString(httpResponseCode).c_str());
    http.end();
    return false;
  }
}

// Function to send image over Serial (for testing/debugging)
// void sendImageOverSerial() {
//   File file = SPIFFS.open("/received_image.jpg", FILE_READ);
//   if (!file) {
//     Serial.println("No image file found!");
//     return;
//   }
  
//   Serial.println("Sending image over serial...");
//   while (file.available()) {
//     Serial.write(file.read());
//   }
//   file.close();
//   Serial.println("\nImage sent!");
// }
