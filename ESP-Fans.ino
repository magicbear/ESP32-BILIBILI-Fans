#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Max72xxPanel.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>

#define UID "12"
int pinCS = D4; // Attach CS to this pin, DIN to MOSI and CLK to SCK (cf http://arduino.cc/en/Reference/SPI )
/**
 * ESP8266 Pin
 * D4 -> CS    -> CS
 * D5 -> HSCLK -> CLK
 * D7 -> HMOSI -> DIN
 */
int numberOfHorizontalDisplays = 4;
int numberOfVerticalDisplays = 1;
#define _DISPLAY_ROTATE 1

Max72xxPanel matrix = Max72xxPanel(pinCS, numberOfHorizontalDisplays, numberOfVerticalDisplays);

#ifndef STASSID
#define STASSID "█████████████████"
#define STAPSK  "█████████"
#endif

String tape = "123456";
int errorCode = 0;

const char* ssid = STASSID;
const char* password = STAPSK;

// 3x5 char maps
uint16_t numMap3x5[] = {
    32319,  //0
    10209,  //1
    24253, //2
    22207,  //3
    28831,  //4
    30391,  //5
    32439,  //6
    16927,  //7
    32447,  //8
    30399  //9
};
// 4x5 char maps
uint32_t numMap4x5[] = {
    476718,  //0
    10209,  //1
    315049, //2
    579246,  //3
    478178,  //4
    972470,  //5
    480418,  //6
    544408,  //7
    349866,  //8
    415406  //9
};

char msg_buf[256];

HTTPClient http;

unsigned long api_mtbs = 60000; //mean time between api requests
unsigned long api_lasttime = 60000;   //last time api request has been done

#define PIXEL_SHOW HIGH
#define PIXEL_HIDE LOW

void _drawPixel(Max72xxPanel display, uint8_t x, uint8_t y, uint8_t pixel)
{
  #ifdef _DISPLAY_FLIP
  display.drawPixel(x, 7-y, pixel);
  #else
  display.drawPixel(x, y, pixel);
  #endif
}

void _drawRoundRect(Max72xxPanel display, uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t r, uint8_t pixel)
{
  #ifdef _DISPLAY_FLIP
  display.drawRoundRect(x, 7-(h+y -1), w, h, r, pixel);
  #else
  display.drawRoundRect(x, y, w, h, r, pixel);
  #endif
}

void _drawLine(Max72xxPanel display, uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, uint8_t pixel)
{
  #ifdef _DISPLAY_FLIP
  display.drawLine(x1, 7-y1, x2, 7-y2, pixel);
  #else
  display.drawLine(x1, y1, x2, y2, pixel);
  #endif
}

void drawLogo(Max72xxPanel display, int eye_offset)
{
    display.fillRect(0, 0, 8, 8, PIXEL_HIDE);
    _drawPixel(display, 1, 0, PIXEL_SHOW);
    _drawPixel(display, 2, 1, PIXEL_SHOW);
    _drawPixel(display, 6, 0, PIXEL_SHOW);
    _drawPixel(display, 5, 1, PIXEL_SHOW);
    _drawPixel(display, 2, 4 + eye_offset, PIXEL_SHOW);
    _drawPixel(display, 5, 4 + eye_offset, PIXEL_SHOW);
    _drawRoundRect(display, 0, 2, 8, 6, 1, PIXEL_SHOW);
}

void drawSplashtop(Max72xxPanel display)
{
  // b
  _drawLine(display, 10, 1 - 1, 10, 6 - 1, PIXEL_SHOW);
  _drawLine(display, 11, 3 - 1, 12, 3 - 1, PIXEL_SHOW);
  _drawLine(display, 11, 6 - 1, 12, 6 - 1, PIXEL_SHOW);
  _drawLine(display, 13, 4 - 1, 13, 5 - 1, PIXEL_SHOW);

  // i
  _drawLine(display, 15, 1 - 1, 15, 6 - 1, PIXEL_SHOW);
  _drawPixel(display, 15, 2 - 1, PIXEL_HIDE);

  // l
  _drawLine(display, 17, 1 - 1, 17, 6 - 1, PIXEL_SHOW);

  // i
  _drawLine(display, 19, 1 - 1, 19, 6 - 1, PIXEL_SHOW);
  _drawPixel(display, 19, 2 - 1, PIXEL_HIDE);

  // b
  _drawLine(display, 21, 1 - 1, 21, 6 - 1, PIXEL_SHOW);
  _drawLine(display, 22, 3 - 1, 23, 3 - 1, PIXEL_SHOW);
  _drawLine(display, 22, 6 - 1, 23, 6 - 1, PIXEL_SHOW);
  _drawLine(display, 24, 4 - 1, 24, 5 - 1, PIXEL_SHOW);

  // i
  _drawLine(display, 26, 1 - 1, 26, 6 - 1, PIXEL_SHOW);
  _drawPixel(display, 26, 2 - 1, PIXEL_HIDE);

  // l
  _drawLine(display, 28, 1 - 1, 28, 6 - 1, PIXEL_SHOW);

  // i
  _drawLine(display, 30, 1 - 1, 30, 6 - 1, PIXEL_SHOW);
  _drawPixel(display, 30, 2 - 1, PIXEL_HIDE);

  display.write();
}

void drawMapValue3x5(Max72xxPanel display, uint8_t x, uint8_t y, uint32_t val)
{
    for (uint8_t i = 0; i < 20; i++)
    {
        if ((val >> i) & 1 == 1) {
            display.drawPixel(x + (3 - i / 5) - 1, y + (4 - i % 5), HIGH);
        }
    }
}

void drawMapValue4x5(Max72xxPanel display, uint8_t x, uint8_t y, uint32_t val)
{
    for (uint8_t i = 0; i < 20; i++)
    {
        if ((val >> i) & 1 == 1) {
            display.drawPixel(x + (4 - i / 5) - 1, y + (4 - i % 5), HIGH);
        }
    }
}

long parseRelationAPI(String json)
{
    // 4 FOR BLOCK + 5 FOR DATA +
    const size_t json_size = JSON_OBJECT_SIZE(4 + 5) + 128;
    DynamicJsonDocument doc(json_size);
    deserializeJson(doc, json);

    int code = doc["code"];
    const char *message = doc["message"];

    if (code != 0) {
        Serial.print("[API] Code:");
        Serial.print(code);
        Serial.print(" Message:");
        Serial.println(message);
        errorCode = code;
        return -1;
    }

    JsonObject data = doc["data"];
    unsigned long data_mid = data["mid"];
    int data_follower = data["follower"];
    if (data_mid == 0) {
        Serial.println("[API] Cannot found valid output");
        errorCode = -2;
        return -2;
    }
    errorCode = 0;
    Serial.print("[API] UID: ");
    Serial.print(data_mid);
    Serial.print(" Follower: ");
    Serial.println(data_follower);

    return data_follower;
}

void updateFans()
{
    std::unique_ptr<BearSSL::WiFiClientSecure>client(new BearSSL::WiFiClientSecure);

    client->setInsecure();

    HTTPClient https;

    Serial.print("[HTTPS] begin...\n");
    drawLogo(matrix, 1); matrix.write();
    if (https.begin(*client, "https://api.bilibili.com/x/relation/stat?vmid=" UID)) {  // HTTPS

        drawLogo(matrix, 0); matrix.write();
        Serial.print("[HTTPS] GET...\n");
        // start connection and send HTTP header
        int httpCode = https.GET();

        drawLogo(matrix, 1); matrix.write();
        // httpCode will be negative on error
        if (httpCode > 0) {
            // HTTP header has been send and Server response header has been handled
            Serial.printf("[HTTPS] GET... code: %d\n", httpCode);

            // file found at server
            if (httpCode == HTTP_CODE_OK) {
                drawLogo(matrix, 0); matrix.write();
                String payload = https.getString();
                long fans = parseRelationAPI(payload);
                if (fans >= 0)
                {
                    tape = fans;
                } else if (fans == -1)
                {
                    tape = "API -1";
                } else if (fans == -2)
                {
                    tape = "API -2";
                }
                drawLogo(matrix, 1); matrix.write();
            } else 
            {
                errorCode = httpCode;
            }
        } else {
            errorCode = httpCode;
            tape = "HTTPS Error";
            Serial.printf("[HTTPS] GET... failed, error: %s\n", https.errorToString(httpCode).c_str());
        }

        https.end();
    } else {
        tape = "Conn Error";
        Serial.printf("[HTTPS] Unable to connect\n");
    }
    drawLogo(matrix, 0); matrix.write();
}

void setup() {
    SPI.begin();
    SPI.setFrequency(10000000); // Here is 10Mhz
    matrix.setIntensity(2); // Set brightness between 0 and 15
    for (int i = 0; i< numberOfHorizontalDisplays;  i++)
        matrix.setRotation(i,_DISPLAY_ROTATE);
    matrix.fillScreen(PIXEL_HIDE);
    drawLogo(matrix, 0);
    drawSplashtop(matrix);
    
    WiFi.mode(WIFI_STA);
    wifi_fpm_set_sleep_type(LIGHT_SLEEP_T);
    WiFi.begin(ssid, password);

    Serial.begin(115200);
    Serial.println();
    Serial.printf("Flash: %d\n", ESP.getFlashChipRealSize());
    Serial.print("Connecting");
    int processbar = 0;
    while (WiFi.status() != WL_CONNECTED) {
        delay(100);
        Serial.print(".");
        drawLogo(matrix, processbar % 2);
        matrix.drawPixel(8 + (processbar % 24), 7, (processbar / 24) % 2 == 0 ? HIGH : LOW);
        matrix.write();
        processbar++;
    }

    Serial.println();

    Serial.print("Connected to wifi. My address:");
    IPAddress myAddress = WiFi.localIP();
    Serial.println(myAddress);

    WiFi.setSleepMode(WIFI_LIGHT_SLEEP, 1000);
    matrix.fillScreen(PIXEL_HIDE);
    drawLogo(matrix, 0);
    drawSplashtop(matrix);
}

void loop() {
    // because of unsigned long, when overflow, it will be huge number
    unsigned long duration = millis() - api_lasttime;
    if (duration >= api_mtbs)
    {
        api_lasttime = millis();
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("WiFi Disconnected");
            ESP.restart();
        }
        updateFans();
    }
    matrix.fillScreen(LOW);
    int x = 0;
    if (errorCode == 0)
    {
        drawLogo(matrix, 0);
        x = 9;

        Serial.println(duration);
        uint32_t waitBarLen = 20 - (duration / float(api_mtbs) * 20);
        if (waitBarLen > 20) waitBarLen = 20;
        for (uint8_t waitBar = 0; waitBar < waitBarLen; waitBar++)
        {
          _drawPixel(matrix, 9 + waitBar, 6, PIXEL_SHOW);
          _drawPixel(matrix, 9 + waitBar, 7, PIXEL_SHOW);
        }
    }
    for (int i=0; i<tape.length(); i++) {
        if (tape[i] >= '0' && tape[i] <= '9')
        {
            drawMapValue3x5(matrix, x, 0, numMap3x5[tape[i] - '0']);
            x += 4;
        } else if (tape[i] == '.') {
            matrix.drawPixel(x, 4, HIGH);
            x += 2;
        } else if (tape[i] == ' ') {
            x += 1;
        } else {
            matrix.drawChar(x, 0, tape[i], HIGH, LOW, 1);
            x+=5;
        }
    }
    matrix.write(); // Send bitmap to display
    delay(1000);
}
