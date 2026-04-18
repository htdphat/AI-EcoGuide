#include <WiFi.h>
#include <WebSocketMCP.h>
#include <DHT.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ArduinoJson.h>

// =================================================================================
// CẤU HÌNH HỆ THỐNG (HARDWARE CONFIG)
// =================================================================================
#define DHTPIN      4
#define DHTTYPE     DHT22
#define MQ135_PIN   34
#define MQ2_PIN     35
#define FAN_PIN     33

// Thông số hiệu chỉnh cảm biến
const float SENSITIVITY_MQ135 = 6.25;
const float SENSITIVITY_MQ2   = 420.0;

// Cấu hình mạng & Server
const char* WIFI_SSID     = "robot";
const char* WIFI_PASSWORD = "robotesp";
const char* MCP_ENDPOINT  = "wss://api.xiaozhi.me/mcp/?token=eyJhbGciOiJFUzI1NiIsInR5cCI6IkpXVCJ9.eyJ1c2VySWQiOjQ5MDE5MywiYWdlbnRJZCI6MTI2MjM0MywiZW5kcG9pbnRJZCI6ImFnZW50XzEyNjIzNDMiLCJwdXJwb3NlIjoibWNwLWVuZHBvaW50IiwiaWF0IjoxNzY2ODI5NzU3LCJleHAiOjE3OTgzODczNTd9.18AWx-WSnzTIXeB6JTm-Iem4OcR669cyt72a6nf8bwpYbtCPsHjiGDtjhS0yS7smj07IG3CpcyFz_60SqLo9Dg";

// Khởi tạo đối tượng
WebSocketMCP mcpClient;
DHT dht(DHTPIN, DHTTYPE);
LiquidCrystal_I2C lcd(0x27, 16, 2);

// =================================================================================
// XỬ LÝ CẢM BIẾN
// =================================================================================

/**
 * Đọc dữ liệu từ tất cả cảm biến, hiển thị LCD và trả về JSON string
 */
String getSensorData() {
    float temp = dht.readTemperature();
    float hum  = dht.readHumidity();
    int raw135 = analogRead(MQ135_PIN);
    int raw2   = analogRead(MQ2_PIN);

    // Kiểm tra lỗi cảm biến DHT
    if (isnan(temp) || isnan(hum)) {
        Serial.println(F("[Error] Không đọc được DHT22!"));
        temp = 0; hum = 0;
    }

    // Tính toán PPM (ADC 12-bit: 4095)
    float ch4_ppm = (raw135 * SENSITIVITY_MQ135) / 4095.0;
    float co2_ppm = (raw2 * SENSITIVITY_MQ2) / 4095.0;

    // Output ra Serial Console
    Serial.printf("\n--- SENSOR DATA ---\nTemp: %.1f°C | Hum: %.1f%%\nCH4: %.2f ppm | CO2: %.2f ppm\n-------------------\n", 
                  temp, hum, ch4_ppm, co2_ppm);

    // Cập nhật LCD
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.printf("T:%.1fC H:%.0f%%", temp, hum);
    lcd.setCursor(0, 1);
    lcd.printf("CH4:%.1f CO2:%.1f", ch4_ppm, co2_ppm);

    // Đóng gói JSON chuyên nghiệp bằng thư viện ArduinoJson
    StaticJsonDocument<200> doc;
    doc["temperature"] = serialized(String(temp, 1));
    doc["humidity"]    = serialized(String(hum, 1));
    doc["ch4_ppm"]     = serialized(String(ch4_ppm, 2));
    doc["co2_ppm"]     = serialized(String(co2_ppm, 2));

    String output;
    serializeJson(doc, output);
    return output;
}

// =================================================================================
// MCP TOOL REGISTRATION (CÔNG CỤ ĐIỀU KHIỂN)
// =================================================================================

void registerMcpTools() {
    // 1. Công cụ đọc cảm biến
    mcpClient.registerTool(
        "read_sensors",
        "Đọc thông số môi trường (Nhiệt độ, độ ẩm, CH4, CO2)",
        "{}",
        [](const String& args) {
            return WebSocketMCP::ToolResponse(getSensorData());
        }
    );

    // 2. Công cụ điều khiển quạt
    mcpClient.registerTool(
        "fan_control",
        "Điều khiển quạt thông khí (on/off/blink)",
        "{\"type\":\"object\",\"properties\":{\"state\":{\"type\":\"string\",\"enum\":[\"on\",\"off\",\"blink\"]}},\"required\":[\"state\"]}",
        [](const String& args) {
            StaticJsonDocument<128> doc;
            deserializeJson(doc, args);
            String state = doc["state"] | "off";

            if (state == "on") {
                digitalWrite(FAN_PIN, HIGH);
            } else if (state == "off") {
                digitalWrite(FAN_PIN, LOW);
            } else if (state == "blink") {
                for (int i = 0; i < 5; i++) {
                    digitalWrite(FAN_PIN, HIGH); delay(200);
                    digitalWrite(FAN_PIN, LOW);  delay(200);
                }
            }
            
            Serial.printf("[Action] Fan status changed to: %s\n", state.c_str());
            return WebSocketMCP::ToolResponse("{\"success\":true,\"state\":\"" + state + "\"}");
        }
    );

    Serial.println(F("[MCP] All tools registered successfully."));
}

void onConnectionStatus(bool connected) {
    if (connected) {
        Serial.println(F("[MCP] Connected to Server"));
        registerMcpTools();
    } else {
        Serial.println(F("[MCP] Connection Lost"));
    }
}

// =================================================================================
// SETUP & LOOP
// =================================================================================

void setup() {
    Serial.begin(115200);
    pinMode(FAN_PIN, OUTPUT);
    digitalWrite(FAN_PIN, LOW);
    
    dht.begin();
    lcd.init();
    lcd.backlight();
    
    // Kết nối WiFi
    lcd.print("Connecting WiFi");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    
    Serial.printf("\nWiFi Connected. IP: %s\n", WiFi.localIP().toString().c_str());
    lcd.clear();
    lcd.print("WiFi Connected");
    delay(1000);

    // Khởi tạo MCP
    lcd.clear();
    lcd.print("Init MCP...");
    mcpClient.begin(MCP_ENDPOINT, onConnectionStatus);

    lcd.clear();
    lcd.print("System Ready!");
}

unsigned long lastSend = 0;
const unsigned long SEND_INTERVAL = 5000; // 5 giây

void loop() {
    mcpClient.loop();

    // Gửi dữ liệu định kỳ không chặn (Non-blocking)
    if (millis() - lastSend >= SEND_INTERVAL) {
        lastSend = millis();
        mcpClient.sendMessage(getSensorData());
    }
    
    delay(10);
}