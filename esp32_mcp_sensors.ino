#include <WiFi.h>
#include <WebSocketMCP.h>
#include <DHT.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ArduinoJson.h>

// ---------------------- Cấu hình chân ----------------------
#define DHTPIN 4              // Chân DHT22
#define DHTTYPE DHT22         // Cảm biến DHT22
#define MQ135_PIN 34          // MQ135 đo CH4
#define MQ2_CO2_PIN 35        // MQ2 đo CO2
#define MQ2_BUTANE_PIN 32     // MQ2 đo Butane (C4H10)
#define MQ3_H2S_PIN 33        // MQ3 đo H2S
#define FAN_PIN 25            // Quạt thông khí

// ---------------------- WiFi ----------------------
const char* ssid = "robot";
const char* password = "robotesp";

// ---------------------- MCP Server ----------------------
const char* mcpEndpoint = "wss://api.xiaozhi.me/mcp/?token=eyJhbGciOiJFUzI1NiIsInR5cCI6IkpXVCJ9.eyJ1c2VySWQiOjQ5MDE5MywiYWdlbnRJZCI6NzEwNDY1LCJlbmRwb2ludElkIjoiYWdlbnRfNzEwNDY1IiwicHVycG9zZSI6Im1jcC1lbmRwb2ludCIsImlhdCI6MTc4MTEwNDIzOCwiZXhwIjoxODEyNjYxODM4fQ.ZJNo9cfHB3B1JUoPCHvQxSr3JZi4T4mh9GeIBIbHrAPw0ZCuORanL9zTRnlqvwys5oI9ZQ2kEXQkU4z0SrqtVA";

WebSocketMCP mcpClient;
DHT dht(DHTPIN, DHTTYPE);
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ---------------------- Biến global lưu giá trị cảm biến mới nhất ----------------------
// Lambda của WebSocketMCP KHÔNG capture local scope đáng tin cậy trên ESP32
// -> tất cả tool đọc từ biến global để đảm bảo butane & h2s luôn trả đúng giá trị
float g_temperature  = 0.0;
float g_humidity     = 0.0;
float g_ch4_ppm      = 0.0;
float g_co2_ppm      = 0.0;
float g_butane_ppm   = 0.0;
float g_h2s_ppm      = 0.0;

// ---------------------- Hệ số hiệu chỉnh ----------------------
float sensitivity_MQ135  = 6.25;   // MQ135  -> CH4
float sensitivity_CO2    = 420.0;  // MQ2 #1 -> CO2
float sensitivity_Butane = 38.0;   // MQ2 #2 -> Butane
float sensitivity_H2S    = 2.8;    // MQ3    -> H2S

// ---------------------- Hàm tính ppm ----------------------
float calcCH4(int raw)    { return (raw * sensitivity_MQ135) / 4095.0; }
float calcCO2(int raw)    { return (raw * sensitivity_CO2)   / 4095.0; }
float calcButane(int raw) { float v = (float)raw / 4095.0; return sensitivity_Butane * v * v * 100.0; }
float calcH2S(int raw)    { return sensitivity_H2S * sqrt((float)raw); }

// ---------------------- Cập nhật toàn bộ biến global từ cảm biến ----------------------
void updateSensorGlobals() {
  g_temperature = dht.readTemperature();
  g_humidity    = dht.readHumidity();
  g_ch4_ppm     = calcCH4(analogRead(MQ135_PIN));
  g_co2_ppm     = calcCO2(analogRead(MQ2_CO2_PIN));
  g_butane_ppm  = calcButane(analogRead(MQ2_BUTANE_PIN));
  g_h2s_ppm     = calcH2S(analogRead(MQ3_H2S_PIN));
}

// ---------------------- Tạo JSON từ biến global ----------------------
String buildSensorJson() {
  return "{\"temperature\":"  + String(g_temperature, 1) +
         ",\"humidity\":"     + String(g_humidity, 1)    +
         ",\"ch4_ppm\":"      + String(g_ch4_ppm, 2)     +
         ",\"co2_ppm\":"      + String(g_co2_ppm, 2)     +
         ",\"butane_ppm\":"   + String(g_butane_ppm, 2)  +
         ",\"h2s_ppm\":"      + String(g_h2s_ppm, 2)     + "}";
}

// ---------------------- Đọc dữ liệu + Serial + LCD ----------------------
void getSensorData() {
  updateSensorGlobals();

  // In ra Serial
  Serial.println("===== Du lieu cam bien =====");
  Serial.print("Nhiet do : "); Serial.print(g_temperature); Serial.println(" C");
  Serial.print("Do am    : "); Serial.print(g_humidity);    Serial.println(" %");
  Serial.print("CH4      : "); Serial.print(g_ch4_ppm);    Serial.println(" ppm");
  Serial.print("CO2      : "); Serial.print(g_co2_ppm);    Serial.println(" ppm");
  Serial.print("Butane   : "); Serial.print(g_butane_ppm); Serial.println(" ppm");
  Serial.print("H2S      : "); Serial.print(g_h2s_ppm);    Serial.println(" ppm");
  Serial.println("============================");

  // ---------------------- LCD luân phiên 2 trang (3 giây/trang) ----------------------
  // Trang 0: T:36.1  C4:2.20   /  H:86%  Bt:3.10
  // Trang 1: CO2: 414.0 ppm    /  H2S: 5.40 ppm
  static uint8_t lcdPage = 0;
  static unsigned long lastLcdSwitch = 0;
  if (millis() - lastLcdSwitch > 3000) {
    lcdPage = (lcdPage + 1) % 2;
    lastLcdSwitch = millis();
  }

  lcd.clear();
  char r1[17], r2[17];

  if (lcdPage == 0) {
    snprintf(r1, sizeof(r1), "T:%.1f  C4:%.2f",  g_temperature, g_ch4_ppm);
    snprintf(r2, sizeof(r2), "H:%.0f%%  Bt:%.2f", g_humidity,   g_butane_ppm);
  } else {
    snprintf(r1, sizeof(r1), "CO2:%.1f ppm",  g_co2_ppm);
    snprintf(r2, sizeof(r2), "H2S:%.2f ppm",  g_h2s_ppm);
  }

  lcd.setCursor(0, 0); lcd.print(r1);
  lcd.setCursor(0, 1); lcd.print(r2);
}

// ---------------------- Sự kiện kết nối MCP ----------------------
void onConnectionStatus(bool connected) {
  if (connected) {
    Serial.println("[MCP] Da ket noi toi server");
    registerMcpTools();
  } else {
    Serial.println("[MCP] Mat ket noi toi server");
  }
}

// ---------------------- Đăng ký công cụ MCP ----------------------
void registerMcpTools() {

  // read_sensors: trả toàn bộ 6 thông số từ biến global
  mcpClient.registerTool(
    "read_sensors",
    "Doc tat ca cam bien: nhiet do, do am, CH4, CO2, Butane, H2S",
    "{}",
    [](const String& args) {
      updateSensorGlobals();          // làm mới trước khi trả về
      return WebSocketMCP::ToolResponse(buildSensorJson());
    }
  );

  // read_ch4
  mcpClient.registerTool(
    "read_ch4",
    "Doc nong do khi CH4 (ppm) tu MQ135",
    "{}",
    [](const String& args) {
      g_ch4_ppm = calcCH4(analogRead(MQ135_PIN));
      return WebSocketMCP::ToolResponse("{\"ch4_ppm\":" + String(g_ch4_ppm, 2) + "}");
    }
  );

  // read_co2
  mcpClient.registerTool(
    "read_co2",
    "Doc nong do khi CO2 (ppm) tu MQ2",
    "{}",
    [](const String& args) {
      g_co2_ppm = calcCO2(analogRead(MQ2_CO2_PIN));
      return WebSocketMCP::ToolResponse("{\"co2_ppm\":" + String(g_co2_ppm, 2) + "}");
    }
  );

  // read_butane — đọc trực tiếp + ghi global
  mcpClient.registerTool(
    "read_butane",
    "Doc nong do khi Butane C4H10 (ppm) tu MQ2",
    "{}",
    [](const String& args) {
      g_butane_ppm = calcButane(analogRead(MQ2_BUTANE_PIN));
      return WebSocketMCP::ToolResponse("{\"butane_ppm\":" + String(g_butane_ppm, 2) + "}");
    }
  );

  // read_h2s — đọc trực tiếp + ghi global
  mcpClient.registerTool(
    "read_h2s",
    "Doc nong do khi H2S (ppm) tu MQ3",
    "{}",
    [](const String& args) {
      g_h2s_ppm = calcH2S(analogRead(MQ3_H2S_PIN));
      return WebSocketMCP::ToolResponse("{\"h2s_ppm\":" + String(g_h2s_ppm, 2) + "}");
    }
  );

  // fan_control
  mcpClient.registerTool(
    "fan_control",
    "Dieu khien quat thong khi (on/off/blink)",
    "{\"type\":\"object\",\"properties\":{\"state\":{\"type\":\"string\",\"enum\":[\"on\",\"off\",\"blink\"]}},\"required\":[\"state\"]}",
    [](const String& args) {
      DynamicJsonDocument doc(256);
      deserializeJson(doc, args);
      String state = doc["state"].as<String>();

      if (state == "on") {
        digitalWrite(FAN_PIN, HIGH);
        Serial.println("Quat thong khi: BAT");
      } else if (state == "off") {
        digitalWrite(FAN_PIN, LOW);
        Serial.println("Quat thong khi: TAT");
      } else if (state == "blink") {
        Serial.println("Quat thong khi: NHAP NHAY 5 lan");
        for (int i = 0; i < 5; i++) {
          digitalWrite(FAN_PIN, HIGH); delay(200);
          digitalWrite(FAN_PIN, LOW);  delay(200);
        }
      }
      return WebSocketMCP::ToolResponse("{\"success\":true,\"state\":\"" + state + "\"}");
    }
  );

  Serial.println("[MCP] Da dang ky: read_sensors, read_ch4, read_co2, read_butane, read_h2s, fan_control");
}

// ---------------------- Thiết lập ban đầu ----------------------
void setup() {
  Serial.begin(115200);
  pinMode(FAN_PIN, OUTPUT);
  digitalWrite(FAN_PIN, LOW);
  dht.begin();

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Dang ket noi WiFi");

  Serial.print("Dang ket noi WiFi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi da ket noi");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  lcd.clear();
  lcd.print("WiFi da ket noi");
  delay(1000);

  lcd.clear();
  lcd.print("Khoi tao MCP...");
  mcpClient.begin(mcpEndpoint, onConnectionStatus);

  lcd.clear();
  lcd.print("He thong san sang");
  delay(1000);
}

// ---------------------- Vòng lặp chính ----------------------
unsigned long lastSend = 0;

void loop() {
  mcpClient.loop();

  // Đọc & gửi toàn bộ dữ liệu mỗi 5 giây
  if (millis() - lastSend > 5000) {
    getSensorData();                          // cập nhật global + LCD + Serial
    mcpClient.sendMessage(buildSensorJson()); // gửi JSON đầy đủ 6 trường lên MCP
    lastSend = millis();
  }

  delay(10);
}
