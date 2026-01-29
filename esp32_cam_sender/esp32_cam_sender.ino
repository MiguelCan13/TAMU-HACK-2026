#include <Arduino.h>
#include <SPI.h>
#include <RF24.h>
#include "esp_camera.h"

// --- Camera Pins (AI-Thinker) ---
#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27
#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22

#define TRIG_PIN 4  
#define ECHO_PIN 16 
#define THRESHOLD_CM 300

long getDistance() {
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long duration = pulseIn(ECHO_PIN, HIGH, 30000); 
  long cm = (duration * 0.034 / 2);
  
  // Set pins to neutral to avoid Camera Bus interference
  pinMode(TRIG_PIN, INPUT);
  pinMode(ECHO_PIN, INPUT);
  return cm;
}

RF24 radio(2, 15); 
const byte senderAddress[] = "00001";  // Address for sending data
const byte receiverAddress[] = "00002"; // Address for receiving polls

// Poll packet with keys to prevent false triggers
struct PollPacket {
  uint8_t key1;    // 0xAA
  uint8_t key2;    // 0x55
  uint8_t node_id;   // Target node
  uint8_t key3;    // 0xCC
};

//image details
struct ImageHeader {
  uint32_t total_size;  
  uint32_t chunk_count; 
};

struct IndexedChunk {
  uint32_t chunk_index; // Which chunk this is (0, 1, 2...)
  uint8_t data[28];     // JPEG byte data (increased from 14 uint16_t)
}; 

// #define TUAH_BYTE 0xAA  // Acknowledgment signal

//there are 2 different camera modules. Each module has a register(ID num) 
const uint8_t node_id = 2;

void setup() {
  Serial.begin(115200);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);
  
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM; config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM; config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM; config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM; config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM; config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM; config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM; config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM; config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;  // Changed to JPEG
  config.frame_size = FRAMESIZE_VGA;     // VGA (640x480)
  config.jpeg_quality = 20;              // Balanced quality/size for VGA
  config.fb_count = 1;
  config.fb_location = CAMERA_FB_IN_DRAM;
  config.grab_mode = CAMERA_GRAB_LATEST;

  //camera settings
  esp_camera_init(&config);
  sensor_t * s = esp_camera_sensor_get();
  s->set_vflip(s, 1); 
  s->set_brightness(s, 0);     
  s->set_contrast(s, 0);       
  s->set_saturation(s, -1);    
  s->set_whitebal(s, 1);       
  s->set_awb_gain(s, 1);       
  s->set_exposure_ctrl(s, 1);  
  s->set_aec2(s, 1);           
  s->set_gain_ctrl(s, 1);      

  //nrf24l01 settings
  SPI.begin(14, 12, 13, 15);
  radio.begin();
  radio.setAutoAck(true);
  radio.enableDynamicPayloads();
  radio.setRetries(15, 15);
  radio.setChannel(115);
  radio.setDataRate(RF24_1MBPS);
  radio.setPALevel(RF24_PA_HIGH);
  radio.openWritingPipe(senderAddress);      // Write to receiver on senderAddress
  radio.openReadingPipe(1, receiverAddress); // Listen for polls on receiverAddress
  radio.stopListening();
  Serial.println("Sender Ready.");
}

void loop() {
  long distance = getDistance();
  
  if (distance > 0 && distance < THRESHOLD_CM) {

    pinMode(16, INPUT); // Release the pin immediately
    delay(10);         // Wait for electrical noise to settle

    //take 2 captures for some fucking reason then discard the first one idk
    camera_fb_t * fb = NULL;
    fb = esp_camera_fb_get();
    esp_camera_fb_return(fb);
    fb = NULL;
    fb = esp_camera_fb_get();

    if (!fb) return;

    Serial.printf("Captured JPEG: %d bytes\n", fb->len);
    
    if (fb->len > 20000) {
      Serial.println("WARNING: JPEG too large, may be unreliable!");
    }

    // Ensure clean radio state before polling
    radio.flush_tx();
    radio.flush_rx();

    //wait to recieve the register request from the reciever
    PollPacket poll;
    Serial.println("Waiting for receiver...");
    radio.startListening();  
    
    unsigned long pollStartTime = millis();
    bool gotPolled = false;
    
    while(millis() - pollStartTime < 10000) {  // 10 second timeout
      if(radio.available()){
        uint8_t payloadSize = radio.getDynamicPayloadSize();
        
        if(payloadSize == sizeof(PollPacket)) {
          radio.read(&poll, sizeof(PollPacket));
          
          if(poll.key1 == 0xAA && poll.key2 == 0x55 && 
             poll.key3 == 0xCC && poll.node_id == node_id) {

            radio.stopListening(); 
            delayMicroseconds(100); 
            
            // Confirm with same structure
            PollPacket response = {0xAA, 0x55, node_id, 0xCC};
            radio.write(&response, sizeof(PollPacket));
            Serial.println("Receiver ready, sending data...");
            gotPolled = true;
            break;
          }
        }
        // If wrong size or invalid packet, flush and keep listening
        radio.flush_rx();
      }
      delayMicroseconds(100); //prevent busy-waiting
    }
    
    // abort if timed out
    if(!gotPolled) {
      Serial.println("Timeout waiting for poll, aborting...");
      esp_camera_fb_return(fb);
      radio.stopListening();
      radio.flush_tx();
      radio.flush_rx();
      return;
    }

    // Send image header with size info
    ImageHeader header;
    header.total_size = fb->len;
    header.chunk_count = (fb->len + 27) / 28; 
    
    bool headerSent = false;
    int headerRetries = 0;
    while (!headerSent && headerRetries < 10) {
      ImageHeader header;
      header.total_size = fb->len;
      header.chunk_count = (fb->len + 27) / 28;

      // Use Hardware Auto-ACK only (much more reliable)
      radio.stopListening();
      if (!radio.write(&header, sizeof(ImageHeader))) {
          Serial.println("Failed to send header (Hardware ACK missing)");
          esp_camera_fb_return(fb);
          return; // Skip this frame and try again next loop
      }
      Serial.println("Header sent successfully.");
      headerSent = true;
    }
  
    // Send JPEG data in chunks of 28 bytes
    uint8_t* jpeg_data = fb->buf;
    IndexedChunk chunk;

    for (uint32_t i = 0; i < header.chunk_count; i++) {
      chunk.chunk_index = i;
      uint32_t byte_offset = i * 28;
      uint32_t bytes_remaining = fb->len - byte_offset;
      uint32_t bytes_to_send = (bytes_remaining < 28) ? bytes_remaining : 28;

      memcpy(chunk.data, &jpeg_data[byte_offset], bytes_to_send);
        
      // Zero padding if needed
      if (bytes_to_send < 28) {
          memset(&chunk.data[bytes_to_send], 0, 28 - bytes_to_send);
      }

      if (!radio.write(&chunk, sizeof(IndexedChunk))) {
        Serial.printf("Failed to send chunk %d (Hardware Auto-Retry failed)\n", i);
        delay(5); // Optional: Add a small software delay or break if connection is truly lost
      }
    }
      esp_camera_fb_return(fb);
      Serial.println("Frame sent. Waiting for receiver to process...");
      
      // Flush any stale data and reset to clean state
      radio.flush_tx();
      radio.flush_rx();
      radio.stopListening(); 
      
      delay(200); // Give receiver time to process and upload
  }
  delay(500);
}
