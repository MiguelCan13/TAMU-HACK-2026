#include <Arduino.h>
#include <SPI.h>
#include <RF24.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <lvgl.h>
#include <TFT_eSPI.h>
#include <ui.h>
#include <ESP32Encoder.h>

const char* ssid = "SM-G950U7AD";
const char* password = "361 658 6872";
const char* flaskServerUrl = "http://10.124.30.48:5000/upload"; 

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

int current_capacity = 0;
int node_id = 1;
int count = 0;
int num_lots = 6;
int available_spots = 0;
float capacity = 0;
bool state = 0;

void update_capacity();


ESP32Encoder encoder;

typedef struct {
    const char * lot_name;
    int num_spots;
} SettingsItem_t;

// Replace these names with actual SLS asset names eventually ig
const SettingsItem_t lots[] = {
    {"Lot: 100c", 629},
    {"Lot: 100e", 645},
    {"Lot: 100f", 279},
    {"Lot: 100g", 332},
    {"Lot: 102", 118},
    {"Lot: 97", 492}
};


#define ITEM_COUNT (sizeof(lots) / sizeof(lots[0]))
int current_idx = 0;
bool is_editing = false;

/*screen res*/
static const uint16_t screenWidth  = 480;
static const uint16_t screenHeight = 320;

static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf[ screenWidth * screenHeight / 10 ];

TFT_eSPI tft = TFT_eSPI(screenWidth, screenHeight); //TFT instance

#if LV_USE_LOG != 0
/* Serial debugging */
void my_print(const char * buf)
{
    Serial.printf(buf);
    Serial.flush();
}
#endif

void uploadToFlask() {
    if (WiFi.status() != WL_CONNECTED) return;

    Serial.println("\n[HTTP] Transmission gap detected. Uploading frame...");
    HTTPClient http;
    http.begin(flaskServerUrl); 

    int val = node_id;
    char buffer[5];
    itoa(val, buffer, 10);
    http.addHeader("X-Camera-ID", buffer);           // Change this for each camera

    http.addHeader("X-Lot-Name", lots[current_idx].lot_name); // send lot name

    val = lots[current_idx].num_spots;
    buffer[5];
    itoa(val, buffer, 10);
    http.addHeader("X-Max-Spots", buffer);               // Maximum capacity for this lot

    val = lots[current_idx].num_spots - available_spots;
    buffer[5];
    itoa(val, buffer, 10);

    http.addHeader("X-Available-Spots", buffer);

    // Send the raw frame_buffer to the Python server
    int httpResponseCode = http.POST((uint8_t*)frame_buffer, sizeof(frame_buffer));

    if (httpResponseCode > 0) {
        Serial.printf("[HTTP] Success! Server Response: %d\n", httpResponseCode);
        
        // READ RESPONSE FROM SERVER
        String response = http.getString();
        Serial.println("[SERVER RESPONSE] " + response);
        
        // Example: Parse simple JSON response (you can add ArduinoJson for complex parsing)
        int index = response.indexOf("\"validation\":");
        int num_cars = 0;
        if (index != -1) { // -1 means the word wasn't found
            // 2. Move the index past the length of "validation":
            // The string "\"validation\":" is 13 characters long.
            index += 13; 

            // 3. Extract the substring from that point forward
            String valStr = response.substring(index);

            // 4. Convert that string to an actual integer
            num_cars = valStr.toInt();

        }

        switch (num_cars){
            case 0:
            break;

            case 1:
            available_spots -= 1;
            break;

            case 2:
            available_spots += 1;
            break;
        };
        Serial.println(num_cars);
        update_capacity();
        
    } else {
        Serial.printf("[HTTP] Failed. Error: %s\n", http.errorToString(httpResponseCode).c_str());
    }
    http.end();
}

// Display flushing
void my_disp_flush( lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p )
{
    uint32_t w = ( area->x2 - area->x1 + 1 );
    uint32_t h = ( area->y2 - area->y1 + 1 );

    tft.startWrite();
    tft.setAddrWindow( area->x1, area->y1, w, h );
    tft.pushColors( ( uint16_t * )&color_p->full, w * h, true );
    tft.endWrite();

    lv_disp_flush_ready( disp );
}

//not using touch
void my_touchpad_read( lv_indev_drv_t * indev_driver, lv_indev_data_t * data )
{
    uint16_t touchX = 0, touchY = 0;

    bool touched = false;

    if( !touched )
    {
        data->state = LV_INDEV_STATE_REL;
    }
    else
    {
        data->state = LV_INDEV_STATE_PR;

        /*Set the coordinates*/
        data->point.x = touchX;
        data->point.y = touchY;

        Serial.print( "Data x " );
        Serial.println( touchX );

        Serial.print( "Data y " );
        Serial.println( touchY );
    }
}

void setup()
{
    Serial.begin( 115200 ); /* prepare for possible serial debug */

    encoder.attachHalfQuad(45, 48); // CLK, DT
    pinMode(47, INPUT_PULLUP);     // SW

    String LVGL_Arduino = "Hello Arduino! ";
    LVGL_Arduino += String('V') + lv_version_major() + "." + lv_version_minor() + "." + lv_version_patch();

    Serial.println( LVGL_Arduino );
    Serial.println( "I am LVGL_Arduino" );

    lv_init();

#if LV_USE_LOG != 0
    lv_log_register_print_cb( my_print ); /* register print function for debugging */
#endif

    tft.begin();          /* TFT init */
    tft.setRotation( 3 ); /* Landscape orientation, flipped */

    lv_disp_draw_buf_init( &draw_buf, buf, NULL, screenWidth * screenHeight / 10 );

    /*Initialize the display*/
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init( &disp_drv );
    /*Change the following line to your display resolution*/
    disp_drv.hor_res = screenWidth;
    disp_drv.ver_res = screenHeight;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register( &disp_drv );

    /*Initialize the (dummy) input device driver*/
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init( &indev_drv );
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_touchpad_read;
    lv_indev_drv_register( &indev_drv );

    ui_init();

    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) { delay(100); Serial.print("."); }
    Serial.println("\n[WIFI] Connected.");

    radio.begin();
    radio.setAutoAck(false);
    radio.setChannel(115);
    radio.setDataRate(RF24_2MBPS);
    radio.setPALevel(RF24_PA_LOW);   
    radio.openWritingPipe(address);      // For sending poll requests
    radio.openReadingPipe(1, address);   // For receiving data
    radio.startListening();


    Serial.println( "Setup done" );
    Serial.println("[SYSTEM] Receiver listening...");
}

void change_lot() { // kinda fucked but it works 
    if(digitalRead(47) == LOW) {
        state = 1;
        while(digitalRead(47) == LOW) {
                delay(1); //debounce type shi
        }

        blink_Animation(ui_Label8, 0);
        blink_Animation(ui_Label3, 0);

        while(digitalRead(47) != LOW) {
            lv_anim_del(ui_Label4, NULL);
            lv_obj_set_style_opa(ui_Label4, LV_OPA_COVER, 0);

            count = encoder.getCount();
            current_idx = (count % num_lots + num_lots) % num_lots; //safe modulus 

            lv_label_set_text(ui_Label8, lots[current_idx].lot_name); // set name
            lv_label_set_text_fmt(ui_Label3, "Total: %d", lots[current_idx].num_spots); // set num spots

            lv_timer_handler();
        }


        while(digitalRead(47) == LOW) {
            delay(1); //debounce type shi

        }

    }

    if(state) {
        blink_Animation(ui_Label4, 0);
        encoder.clearCount();
        count = 0;
        while(digitalRead(47) != LOW) {
            lv_anim_del(ui_Label8, NULL);
            lv_anim_del(ui_Label3, NULL);
            lv_obj_set_style_opa(ui_Label8, LV_OPA_COVER, 0);
            lv_obj_set_style_opa(ui_Label3, LV_OPA_COVER, 0);

            count = encoder.getCount();
            available_spots = abs(count);
            lv_label_set_text_fmt(ui_Label4, "Available: %d", abs(count)); // change num current available spots
            lv_timer_handler();
        }

        while(digitalRead(47) == LOW) {
            delay(1); //debounce type shi

        }

        state = 0;
        update_capacity();
    }

    lv_anim_del(ui_Label4, NULL);
    lv_obj_set_style_opa(ui_Label4, LV_OPA_COVER, 0);
    lv_anim_del(ui_Label8, NULL);
    lv_anim_del(ui_Label3, NULL);
    lv_obj_set_style_opa(ui_Label8, LV_OPA_COVER, 0);
    lv_obj_set_style_opa(ui_Label3, LV_OPA_COVER, 0);

}

void update_capacity() {
    capacity = 100 - ((float(available_spots) / float(lots[current_idx].num_spots)) * 100);
    lv_arc_set_value(ui_Arc1, int(capacity));
    lv_label_set_text_fmt(ui_Label2 , "%d%%", int(capacity));
    lv_label_set_text_fmt(ui_Label4, "Available: %d", available_spots);

    Serial.println(available_spots);
    Serial.println(lots[current_idx].num_spots);
    Serial.println(capacity);

}

void handle_image_process() {
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
            node_id = targetNode;
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


void loop() {
    lv_timer_handler();
    static uint32_t last_update = 0;

    handle_image_process();

    change_lot();
}
