/* Meteo Station for ESP32 CYD*/

#include <WebServer.h>
#include <lvgl.h>
#include <TFT_eSPI.h>
#include "weather_images.h"
#include <WiFi.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

// -------------------- WIFI PROVISIONING --------------------
Preferences prefs;
WebServer server(80);
String ssidSaved;
String passSaved;

// -------------------- NTP --------------------
const char* ntpServer = "pool.ntp.org";
long gmtOffset_sec = 3600;
int daylightOffset_sec = 3600;


// -------------------- PAGINA WEB PROVISIONING --------------------
void handleRoot() {
  int n = WiFi.scanNetworks();

  String html = R"rawliteral(
  <html>
  <head>
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <style>
      body { font-family: Arial; background:#f2f2f2; text-align:center; padding-top:40px; }
      .card { background:white; padding:20px; margin:auto; width:320px; border-radius:12px; 
              box-shadow:0 0 12px rgba(0,0,0,0.15); }
      select, input { width:100%; padding:12px; margin-top:12px; border-radius:6px; border:1px solid #ccc; font-size:16px; }
      button { margin-top:20px; padding:12px; width:100%; background:#0078ff; color:white; 
               border:none; border-radius:6px; font-size:18px; font-weight:bold; }
      .signal { font-size:14px; color:#555; margin-top:4px; }
      .title { font-size:22px; font-weight:bold; margin-bottom:10px; }
      .footer { margin-top:20px; font-size:12px; color:#777; }
    </style>
    <script>
      function togglePass() {
        var p = document.getElementById("pass");
        p.type = (p.type === "password") ? "text" : "password";
      }
    </script>
  </head>
  <body>
    <div class="card">
      <div class="title">Configurazione WiFi</div>
      <form action="/save">
        <label>Rete WiFi:</label>
        <select name="ssid">
  )rawliteral";

  for (int i = 0; i < n; i++) {
    int rssi = WiFi.RSSI(i);
    String strength = (rssi > -60) ? "🔵🔵🔵🔵 Forte" :
                      (rssi > -70) ? "🔵🔵🔵 Medio" :
                      (rssi > -80) ? "🔵🔵 Debole" : "🔵 Molto debole";

    html += "<option value='" + WiFi.SSID(i) + "'>" + WiFi.SSID(i) + " (" + strength + ")</option>";
  }

  html += R"rawliteral(
        </select>

        <label>Password:</label>
        <input id="pass" name="pass" type="password">
        <input type="checkbox" onclick="togglePass()"> Mostra password

        <button type="submit">Salva e Connetti</button>
      </form>

      <div class="footer">
        MeteoStation • Setup WiFi
      </div>
    </div>
  </body>
  </html>
  )rawliteral";

  server.send(200, "text/html", html);
}


void handleSave() {
  prefs.putString("ssid", server.arg("ssid"));
  prefs.putString("pass", server.arg("pass"));
  server.send(200, "text/html", "<h2>Salvato! Riavvio...</h2>");
  delay(1000);
  ESP.restart();
}

void startAPMode() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.disconnect(true);
  delay(500);
  WiFi.softAP("MeteoStation_Setup", "Meteo1234");
  server.on("/", handleRoot);
  server.on("/save", handleSave);
  delay(300);
  server.begin();
}

bool tryConnectWiFi() {
  ssidSaved = prefs.getString("ssid", "");
  passSaved = prefs.getString("pass", "");
  if (ssidSaved == "") return false;

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssidSaved.c_str(), passSaved.c_str());

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    lv_task_handler();   // la GUI continua a funzionare
    lv_tick_inc(5);
    delay(5);

    if (millis() - start > 8000) {
      return false;      // timeout
    }
  }

  return true;
}

// -------------------- NTP --------------------
void initNTP() {
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
}

String getDateString() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "--:--";

  char buffer[20];
  strftime(buffer, sizeof(buffer), "%H:%M", &timeinfo);
  return String(buffer);
}

// Replace with the latitude and longitude to where you want to get the weather
String latitude = "XXXXXXXXX";
String longitude = "XXXXXXXXXXX";
// Enter your location
String location = "XXXXXXXXXXXX";
// Type the timezone you want to get the time for
String timezone = "Europe/Rome";

// Store date and time
String current_date;
String last_weather_update;
String temperature;
String humidity;
int is_day;
int weather_code = 0;
String weather_description;

// SET VARIABLE TO 0 FOR TEMPERATURE IN FAHRENHEIT DEGREES
#define TEMP_CELSIUS 1

#if TEMP_CELSIUS
  String temperature_unit = "";
  const char degree_symbol[] = "\u00B0C";
#else
  String temperature_unit = "&temperature_unit=fahrenheit";
  const char degree_symbol[] = "\u00B0F";
#endif

#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 320

#define DRAW_BUF_SIZE (SCREEN_WIDTH * SCREEN_HEIGHT / 10 * (LV_COLOR_DEPTH / 8))
uint32_t draw_buf[DRAW_BUF_SIZE / 4];

// If logging is enabled, it will inform the user about what is happening in the library
void log_print(lv_log_level_t level, const char * buf) {
  LV_UNUSED(level);
  Serial.println(buf);
  Serial.flush();
}


static lv_obj_t * weather_image;
static lv_obj_t * text_label_date;
static lv_obj_t * text_label_temperature;
static lv_obj_t * text_label_humidity;
static lv_obj_t * text_label_weather_description;
static lv_obj_t * text_label_time_location;

static void timer_cb_hour(lv_timer_t * timer_hour){
  LV_UNUSED(timer_hour);
  lv_label_set_text(text_label_date, getDateString().c_str());
}

static void timer_cb_gui(lv_timer_t * timer_gui){
  LV_UNUSED(timer_gui);
  get_weather_data();
  get_weather_description(weather_code);
  lv_label_set_text(text_label_temperature, String("      " + temperature + degree_symbol).c_str());
  lv_label_set_text(text_label_humidity, String("   " + humidity + "%").c_str());
  lv_label_set_text(text_label_weather_description, weather_description.c_str());
  lv_label_set_text(text_label_time_location, String("Last Update: " + last_weather_update + "  |  " + location).c_str());
}

void lv_create_main_gui(void) {
  LV_IMAGE_DECLARE(image_weather_sun);
  LV_IMAGE_DECLARE(image_weather_cloud);
  LV_IMAGE_DECLARE(image_weather_rain);
  LV_IMAGE_DECLARE(image_weather_thunder);
  LV_IMAGE_DECLARE(image_weather_snow);
  LV_IMAGE_DECLARE(image_weather_night);
  LV_IMAGE_DECLARE(image_weather_temperature);
  LV_IMAGE_DECLARE(image_weather_humidity);

  // Get the weather data from open-meteo.com API
  get_weather_data();

  weather_image = lv_image_create(lv_screen_active());
  lv_obj_align(weather_image, LV_ALIGN_CENTER, -80, -20);
  
  get_weather_description(weather_code);

  text_label_date = lv_label_create(lv_screen_active());
  lv_label_set_text(text_label_date, getDateString().c_str());
  lv_obj_align(text_label_date, LV_ALIGN_CENTER, 70, -70);
  lv_obj_set_style_text_font((lv_obj_t*) text_label_date, &lv_font_montserrat_32, 0);
  lv_obj_set_style_text_color((lv_obj_t*) text_label_date, lv_palette_main(LV_PALETTE_TEAL), 0);

  lv_obj_t * weather_image_temperature = lv_image_create(lv_screen_active());
  lv_image_set_src(weather_image_temperature, &image_weather_temperature);
  lv_obj_align(weather_image_temperature, LV_ALIGN_CENTER, 30, -25);
  text_label_temperature = lv_label_create(lv_screen_active());
  lv_label_set_text(text_label_temperature, String("      " + temperature + degree_symbol).c_str());
  lv_obj_align(text_label_temperature, LV_ALIGN_CENTER, 70, -25);
  lv_obj_set_style_text_font((lv_obj_t*) text_label_temperature, &lv_font_montserrat_22, 0);

  lv_obj_t * weather_image_humidity = lv_image_create(lv_screen_active());
  lv_image_set_src(weather_image_humidity, &image_weather_humidity);
  lv_obj_align(weather_image_humidity, LV_ALIGN_CENTER, 30, 20);
  text_label_humidity = lv_label_create(lv_screen_active());
  lv_label_set_text(text_label_humidity, String("   " + humidity + "%").c_str());
  lv_obj_align(text_label_humidity, LV_ALIGN_CENTER, 70, 20);
  lv_obj_set_style_text_font((lv_obj_t*) text_label_humidity, &lv_font_montserrat_22, 0);

  text_label_weather_description = lv_label_create(lv_screen_active());
  lv_label_set_text(text_label_weather_description, weather_description.c_str());
  lv_obj_align(text_label_weather_description, LV_ALIGN_BOTTOM_MID, 0, -40);
  lv_obj_set_style_text_font((lv_obj_t*) text_label_weather_description, &lv_font_montserrat_18, 0);

  // Create a text label for the time and timezone aligned center in the bottom of the screen
  text_label_time_location = lv_label_create(lv_screen_active());
  lv_label_set_text(text_label_time_location, String("Last Update: " + last_weather_update + "  |  " + location).c_str());
  lv_obj_align(text_label_time_location, LV_ALIGN_BOTTOM_MID, 0, -10);
  lv_obj_set_style_text_font((lv_obj_t*) text_label_time_location, &lv_font_montserrat_12, 0);

  lv_timer_t * timer_hour = lv_timer_create(timer_cb_hour, 60000, NULL);
  lv_timer_ready(timer_hour);

  lv_timer_t * timer_gui = lv_timer_create(timer_cb_gui, 600000, NULL);
  lv_timer_ready(timer_gui);
}

void get_weather_description(int code) {
  switch (code) {
    case 0:
      if(is_day==1) { lv_image_set_src(weather_image, &image_weather_sun); }
      else { lv_image_set_src(weather_image, &image_weather_night); }
      weather_description = "CIELO LIMPIDO";
      break;
    case 1: 
      if(is_day==1) { lv_image_set_src(weather_image, &image_weather_sun); }
      else { lv_image_set_src(weather_image, &image_weather_night); }
      weather_description = "CIELO VELATO";
      break;
    case 2: 
      lv_image_set_src(weather_image, &image_weather_cloud);
      weather_description = "PARZIALMENTE NUVOLOSO";
      break;
    case 3:
      lv_image_set_src(weather_image, &image_weather_cloud);
      weather_description = "NUVOLOSO";
      break;
    case 45:
      lv_image_set_src(weather_image, &image_weather_cloud);
      weather_description = "NEBBIA";
      break;
    case 48:
      lv_image_set_src(weather_image, &image_weather_cloud);
      weather_description = "NEBBIA GHIACCIATA";
      break;
    case 51:
      lv_image_set_src(weather_image, &image_weather_rain);
      weather_description = "PIOGGIA DI LEGGERA INTENSITA'";
      break;
    case 53:
      lv_image_set_src(weather_image, &image_weather_rain);
      weather_description = "PIOGGIA DI MODERATA INTENSITA'";
      break;
    case 55:
      lv_image_set_src(weather_image, &image_weather_rain); 
      weather_description = "PIOGGIA INTENSA";
      break;
    case 56:
      lv_image_set_src(weather_image, &image_weather_rain);
      weather_description = "PIOGGIA CONGELATA LEGGERA";
      break;
    case 57:
      lv_image_set_src(weather_image, &image_weather_rain);
      weather_description = "PIOGGIA CONGELATA INTENSA";
      break;
    case 61:
      lv_image_set_src(weather_image, &image_weather_rain);
      weather_description = "PIOGGIA LEGGERA";
      break;
    case 63:
      lv_image_set_src(weather_image, &image_weather_rain);
      weather_description = "PIOGGIA MODERATA";
      break;
    case 65:
      lv_image_set_src(weather_image, &image_weather_rain);
      weather_description = "PIOGGIA PESANTE";
      break;
    case 66:
      lv_image_set_src(weather_image, &image_weather_rain);
      weather_description = "PIOGGIA GHIACCIATA LEGGERA";
      break;
    case 67:
      lv_image_set_src(weather_image, &image_weather_rain);
      weather_description = "PIOGGIA GHIACCIATA PESANTE";
      break;
    case 71:
      lv_image_set_src(weather_image, &image_weather_snow);
      weather_description = "NEVISCHIO";
      break;
    case 73:
      lv_image_set_src(weather_image, &image_weather_snow);
      weather_description = "NEVE MODERATA";
      break;
    case 75:
      lv_image_set_src(weather_image, &image_weather_snow);
      weather_description = "NEVE INTENSA";
      break;
    case 77:
      lv_image_set_src(weather_image, &image_weather_snow);
      weather_description = "GRANELLI DI NEVE";
      break;
    case 80:
      lv_image_set_src(weather_image, &image_weather_rain);
      weather_description = "PIOGGIA LEGGERA";
      break;
    case 81:
      lv_image_set_src(weather_image, &image_weather_rain);
      weather_description = "PIOGGIA MODERATA";
      break;
    case 82:
      lv_image_set_src(weather_image, &image_weather_rain);
      weather_description = "PIOGGIA FORTE";
      break;
    case 85:
      lv_image_set_src(weather_image, &image_weather_snow);
      weather_description = "NEVE LEGGERA";
      break;
    case 86:
      lv_image_set_src(weather_image, &image_weather_snow);
      weather_description = "NEVE PESANTE";
      break;
    case 95:
      lv_image_set_src(weather_image, &image_weather_thunder);
      weather_description = "TEMPORALE";
      break;
    case 96:
      lv_image_set_src(weather_image, &image_weather_thunder);
      weather_description = "GRANDINE LEGGERA";
      break;
    case 99:
      lv_image_set_src(weather_image, &image_weather_thunder);
      weather_description = "GRANDINE PESANTE";
      break;
    default: 
      weather_description = "CODICE METEO SCONOSCIUTO";
      break;
  }
}

void get_weather_data() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    // Construct the API endpoint
    String url = String("http://api.open-meteo.com/v1/forecast?latitude=" + latitude + "&longitude=" + longitude + "&current=temperature_2m,relative_humidity_2m,is_day,precipitation,rain,weather_code" + temperature_unit + "&timezone=" + timezone + "&forecast_days=1");
    http.begin(url);
    int httpCode = http.GET(); // Make the GET request

    if (httpCode > 0) {
      // Check for the response
      if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        //Serial.println("Request information:");
        //Serial.println(payload);
        // Parse the JSON to extract the time
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload);
        if (!error) {
          const char* datetime = doc["current"]["time"];
          temperature = String(doc["current"]["temperature_2m"]);
          humidity = String(doc["current"]["relative_humidity_2m"]);
          is_day = String(doc["current"]["is_day"]).toInt();
          weather_code = String(doc["current"]["weather_code"]).toInt();
          /*Serial.println(temperature);
          Serial.println(humidity);
          Serial.println(is_day);
          Serial.println(weather_code);
          Serial.println(String(timezone));*/
          // Split the datetime into date and time
          String datetime_str = String(datetime);
          int splitIndex = datetime_str.indexOf('T');
          current_date = datetime_str.substring(0, splitIndex);
          last_weather_update = datetime_str.substring(splitIndex + 1, splitIndex + 9); // Extract time portion
        } else {
          Serial.print("deserializeJson() failed: ");
          Serial.println(error.c_str());
        }
      }
      else {
        Serial.println("Failed");
      }
    } else {
      Serial.printf("GET request failed, error: %s\n", http.errorToString(httpCode).c_str());
    }
    http.end(); // Close connection
  } else {
    Serial.println("Not connected to Wi-Fi");
  }
}

void setup() {
  Serial.begin(115200);
  prefs.begin("wifi", false);

  String LVGL_Arduino = String("LVGL Library Version: ") + lv_version_major() + "." + lv_version_minor() + "." + lv_version_patch();
  Serial.println(LVGL_Arduino);
  
  // Start LVGL
  lv_init();
  // Register print function for debugging
  lv_log_register_print_cb(log_print);

  // Create a display object
  lv_display_t * disp;
  // Initialize the TFT display using the TFT_eSPI library
  disp = lv_tft_espi_create(SCREEN_WIDTH, SCREEN_HEIGHT, draw_buf, sizeof(draw_buf));
  lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_90);

  // Function to draw the GUI
  lv_create_main_gui();

  // Connect to Wi-Fi
  if (!tryConnectWiFi()) {
    startAPMode();   // AP per configurazione
  } else {
    initNTP();       // Connessione OK → NTP
  }
  
}

void loop() {
  // Gestisci il server SOLO se l'AP è attivo
  if (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA) {
    server.handleClient();
  }

  lv_task_handler();  // let the GUI do its work
  lv_tick_inc(5);     // tell LVGL how much time has passed
  delay(5);           // let this time pass
}
