#include <M5Cardputer.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <Preferences.h>

static const char* AP_NAME = "CivSwitch-Cardputer";
static const uint16_t LOADER_PORT = 9026;
static const uint32_t CHECK_INTERVAL_MS = 5000;

enum class ScreenState { HOME, WIFI_PORTAL, DIAGNOSTICS };
ScreenState screenState = ScreenState::HOME;

WiFiManager wifiManager;
Preferences preferences;
WiFiManagerParameter consoleIpField("switch_ip", "Switch 2 IP address", "192.168.1.100", 16);
String consoleIp = "192.168.1.100";

bool portalStarted = false;
bool loaderReachable = false;
bool checking = false;
uint32_t lastCheck = 0;
uint32_t lastDraw = 0;
uint32_t successfulChecks = 0;
uint32_t failedChecks = 0;
String lastDiagnostic = "Not checked";
String statusLine = "Starting...";
int progress = 0;

void drawHeader() {
  auto& d = M5Cardputer.Display;
  d.fillRect(0, 0, 240, 20, 0x2124);
  d.setTextColor(TFT_WHITE, 0x2124);
  d.setTextSize(1);
  d.setCursor(5, 6);
  d.print("CIVSWITCH  /  CARDPUTER-ADV");
}

void drawProgress(int pct) {
  auto& d = M5Cardputer.Display;
  pct = constrain(pct, 0, 100);
  d.drawRect(8, 105, 224, 12, TFT_WHITE);
  d.fillRect(10, 107, 220, 8, TFT_BLACK);
  if (pct > 0) d.fillRect(10, 107, (220 * pct) / 100, 8, TFT_GREEN);
  d.setTextColor(TFT_WHITE, TFT_BLACK);
  d.setCursor(8, 121);
  d.printf("%d%%", pct);
}

void drawHome() {
  auto& d = M5Cardputer.Display;
  d.fillScreen(TFT_BLACK);
  drawHeader();

  d.setTextColor(TFT_CYAN, TFT_BLACK);
  d.setTextSize(2);
  d.setCursor(8, 28);
  d.print("STANDALONE UI");

  d.setTextSize(1);
  d.setTextColor(TFT_WHITE, TFT_BLACK);
  d.setCursor(8, 55);
  d.printf("Wi-Fi: %s", WiFi.isConnected() ? "CONNECTED" : "OFFLINE");
  d.setCursor(8, 68);
  if (WiFi.isConnected()) d.printf("IP: %s", WiFi.localIP().toString().c_str());
  else d.print("IP: --");

  d.setCursor(8, 82);
  d.printf("CivSwitch loader: %s", loaderReachable ? "PORT 9026 OPEN" : "NOT VERIFIED");
  d.setCursor(8, 95);
  d.print(statusLine);
  drawProgress(progress);

  d.setTextColor(TFT_YELLOW, TFT_BLACK);
  d.setCursor(8, 133);
  d.print("A=Wi-Fi setup  B=diagnostics");
}

void drawDiagnostics() {
  auto& d = M5Cardputer.Display;
  d.fillScreen(TFT_BLACK);
  drawHeader();
  d.setTextColor(TFT_CYAN, TFT_BLACK);
  d.setTextSize(2);
  d.setCursor(8, 28);
  d.print("DIAGNOSTICS");
  d.setTextSize(1);
  d.setTextColor(TFT_WHITE, TFT_BLACK);
  d.setCursor(8, 56);
  d.printf("Wi-Fi: %s", WiFi.isConnected() ? "OK" : "NOT CONNECTED");
  d.setCursor(8, 70);
  d.printf("RSSI: %d dBm", WiFi.isConnected() ? WiFi.RSSI() : 0);
  d.setCursor(8, 84);
  d.printf("Switch: %s", consoleIp.c_str());
  d.printf("TCP %u: %s", LOADER_PORT, loaderReachable ? "OPEN" : "CLOSED");
  d.setCursor(8, 98);
  d.printf("Checks: %lu ok / %lu fail",
           (unsigned long)successfulChecks, (unsigned long)failedChecks);
  d.setCursor(8, 112);
  d.print(lastDiagnostic.substring(0, 37));
  d.setTextColor(TFT_YELLOW, TFT_BLACK);
  d.setCursor(8, 133);
  d.print("A=home  C=retest");
}

void drawPortal() {
  auto& d = M5Cardputer.Display;
  d.fillScreen(TFT_BLACK);
  drawHeader();
  d.setTextColor(TFT_CYAN, TFT_BLACK);
  d.setTextSize(2);
  d.setCursor(8, 30);
  d.print("WI-FI SETUP");
  d.setTextSize(1);
  d.setTextColor(TFT_WHITE, TFT_BLACK);
  d.setCursor(8, 60);
  d.print("Connect phone to:");
  d.setCursor(8, 74);
  d.print(AP_NAME);
  d.setCursor(8, 92);
  d.print("Then open 192.168.4.1");
  d.setCursor(8, 110);
  d.print("Select your Wi-Fi network");
  d.setTextColor(TFT_YELLOW, TFT_BLACK);
  d.setCursor(8, 133);
  d.print("Portal stays open until saved");
}

bool testLoaderPort() {
  if (WiFi.status() != WL_CONNECTED) {
    lastDiagnostic = "Wi-Fi disconnected";
    return false;
  }
  WiFiClient client;
  client.setTimeout(1500);
  bool ok = client.connect(consoleIp.c_str(), LOADER_PORT);
  // A successful TCP handshake only proves that a service accepts connections.
  // It does not prove that the CivSwitch loader is ready or compatible.
  if (ok) {
    client.stop();
    lastDiagnostic = "TCP handshake succeeded";
    successfulChecks++;
    return true;
  }
  lastDiagnostic = "No service at Switch IP:9026";
  failedChecks++;
  return false;
}

void runDiagnostics() {
  statusLine = "Checking Wi-Fi...";
  progress = 10;
  if (WiFi.status() != WL_CONNECTED) {
    loaderReachable = false;
    progress = 0;
    statusLine = "Connect to Wi-Fi first";
    return;
  }
  progress = 45;
  statusLine = "Testing TCP connection...";
  loaderReachable = testLoaderPort();
  progress = loaderReachable ? 100 : 0;
  statusLine = loaderReachable ? "TCP endpoint reachable" : "Loader not detected";
}

void startPortal() {
  screenState = ScreenState::WIFI_PORTAL;
  drawPortal();
  wifiManager.setConfigPortalTimeout(0);
  wifiManager.setAPCallback([](WiFiManager*) {
    portalStarted = true;
  });
  // Blocking captive portal; returns after credentials are saved.
  bool ok = wifiManager.startConfigPortal(AP_NAME);
  portalStarted = false;
  if (ok && WiFi.isConnected()) {
    screenState = ScreenState::HOME;
    statusLine = "Wi-Fi connected";
    runDiagnostics();
  } else {
    screenState = ScreenState::HOME;
    statusLine = "Wi-Fi setup ended";
  }
}

void setup() {
  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);
  M5Cardputer.Display.setRotation(0);
  M5Cardputer.Display.setTextWrap(false);
  M5Cardputer.Display.fillScreen(TFT_BLACK);
  drawHome();

  preferences.begin("civswitch", true);
  consoleIp = preferences.getString("switch_ip", "192.168.1.100");
  preferences.end();
  consoleIpField.setValue(consoleIp.c_str(), 16);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  wifiManager.addParameter(&consoleIpField);
  wifiManager.setSaveConfigCallback([]() {
    preferences.begin("civswitch", false);
    preferences.putString("switch_ip", consoleIpField.getValue());
    preferences.end();
  });
  wifiManager.setConnectTimeout(20);
  wifiManager.setConfigPortalTimeout(0);

  statusLine = "Connecting to saved Wi-Fi...";
  progress = 10;
  drawHome();
  bool connected = wifiManager.autoConnect(AP_NAME);
  if (connected) {
    progress = 35;
    statusLine = "Wi-Fi connected; checking loader";
    runDiagnostics();
  } else {
    statusLine = "Wi-Fi setup required";
    startPortal();
  }
  drawHome();
}

void loop() {
  M5Cardputer.update();

  if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
    auto keys = M5Cardputer.Keyboard.keysState();
    for (auto c : keys.word) {
      if (c == 'a' || c == 'A') {
        if (screenState == ScreenState::DIAGNOSTICS) {
          screenState = ScreenState::HOME;
          drawHome();
        } else if (screenState == ScreenState::HOME) {
          startPortal();
          drawHome();
        }
      } else if (c == 'b' || c == 'B') {
        screenState = ScreenState::DIAGNOSTICS;
        drawDiagnostics();
      } else if (c == 'c' || c == 'C') {
        runDiagnostics();
        screenState = ScreenState::DIAGNOSTICS;
        drawDiagnostics();
      } else if (c == 'r' || c == 'R') {
        WiFi.disconnect();
        statusLine = "Reconnecting...";
        progress = 5;
        bool ok = WiFi.reconnect();
        if (ok) runDiagnostics();
        drawHome();
      }
    }
  }

  if (screenState == ScreenState::HOME && millis() - lastCheck > CHECK_INTERVAL_MS) {
    lastCheck = millis();
    if (WiFi.isConnected()) {
      loaderReachable = testLoaderPort();
      progress = loaderReachable ? 100 : 35;
      statusLine = loaderReachable ? "TCP endpoint reachable" : "Wi-Fi OK; loader unverified";
    } else {
      loaderReachable = false;
      progress = 0;
      statusLine = "Wi-Fi disconnected";
    }
    drawHome();
  }

  if (screenState == ScreenState::DIAGNOSTICS && millis() - lastDraw > 1500) {
    lastDraw = millis();
    drawDiagnostics();
  }
  delay(10);
}
