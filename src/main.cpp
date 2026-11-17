#include <Arduino.h>
#include <M5Cardputer.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <Preferences.h>
#include <SPI.h>
#include <SD.h>
#include <sys/socket.h>

// CivSwitch Cardputer-Adv: local configuration + TCP test + SD inspection.
// No exploit, jailbreak, or arbitrary payload sender is implemented.
static constexpr uint16_t PORT = 9026;
static constexpr uint32_t PROBE_INTERVAL = 10000;
static constexpr uint32_t CONNECT_TIMEOUT = 20000;
static constexpr int SD_CS = 12;
static constexpr int SD_SCK = 40;
static constexpr int SD_MISO = 39;
static constexpr int SD_MOSI = 14;
static constexpr int MAX_NETWORKS = 16;

enum class Page { HOME, MENU, WIFI_SCAN, WIFI_LIST, EDIT_SSID, EDIT_PASSWORD,
                  EDIT_IP, CONNECTING, DIAGNOSTICS, SD_INFO, PAYLOAD_LIST, PAYLOAD_CONFIRM, PAYLOAD_SEND, PAYLOAD_RESULT };
Page page = Page::HOME;
Preferences prefs;
String switchIp = "192.168.1.100";
String pendingSsid;
String editor;
String status = "Ready";
String lastDiag = "Not tested";
String payloadPath;
String payloadHeader;
String lastKey = "-";
String networks[MAX_NETWORKS];
bool networkOpen[MAX_NETWORKS] = {};
bool sdPresent = false, payloadPresent = false, fakeConsole = false, portOpen = false;
uint32_t payloadSize = 0, checksOK = 0, checksFail = 0;
uint32_t lastProbe = 0, lastDraw = 0, connectStart = 0;
int networkCount = 0, selected = 0, menuSelected = 0, progress = 0;
bool drawingDirty = true;

// Upstream source runs inside the console's existing CivSwitch Remote JS Loader.
// We only stream prebuilt JavaScript files from Cardputer microSD over TCP 9026.
static constexpr int SCRIPT_COUNT = 4;
static const char* SCRIPT_LABELS[SCRIPT_COUNT] = {
  "FS privilege promotion", "FS + kaiHEN full chain",
  "FS + SD chain", "kaiHEN after promotion"
};
static const char* SCRIPT_PATHS[SCRIPT_COUNT] = {
  "/civswitch/fs.js", "/civswitch/chain.js",
  "/civswitch/chain-sd.js", "/civswitch/kaihen.js"
};
static constexpr size_t MAX_LOADER_SIZE = 500000; // upstream loader limit
int scriptSelected = 0;
int readyScripts = 0;
uint32_t scriptSize = 0, scriptSent = 0, senderStarted = 0;
bool senderRunning = false, scriptDelivered = false, remoteSuccess = false, remoteError = false, remoteEmulator = false;
String remoteLine, remoteLast = "No output", senderResult = "Not started";
WiFiClient senderClient;
File senderFile;

String cut(String value, unsigned int width);
void inspectSD();
void go(Page next);
void closeSender() {
  if (senderFile) senderFile.close();
  senderClient.stop();
  senderRunning = false;
}
void onRemoteLine(const String& line) {
  if (line.length() == 0) return;
  remoteLast = cut(line, 38);
  Serial.println("REMOTE: " + line);
  if (line.indexOf("CIVSWITCH-EMU/1") >= 0) remoteEmulator = true;
  if (line.indexOf("You just Civ Sevened your machine") >= 0 ||
      line.indexOf("HEN IS RUNNING") >= 0) remoteSuccess = true;
  if (line.indexOf("Installation failed") >= 0 ||
      line.indexOf("KH: failed") >= 0 ||
      line.indexOf("CHAIN: promotion did not retain") >= 0 ||
      line.indexOf("No promoted CivSwitchFS session") >= 0) remoteError = true;
  drawingDirty = true;
}
void endSend(const String& detail) {
  if (remoteLine.length()) { onRemoteLine(remoteLine); remoteLine = ""; }
  closeSender();
  if (remoteEmulator) senderResult = "PC emulator only (no execution)";
  else if (remoteError) senderResult = "Remote reported an error";
  else if (remoteSuccess) senderResult = "Success marker reported";
  else senderResult = detail;
  status = senderResult;
  go(Page::PAYLOAD_RESULT);
}
bool prepareSelectedPayload() {
  if (!sdPresent) inspectSD();
  if (!sdPresent) { senderResult = "No microSD card mounted"; return false; }
  File f = SD.open(SCRIPT_PATHS[scriptSelected], FILE_READ);
  if (!f || f.isDirectory()) { if (f) f.close(); senderResult = "Missing selected .js file"; return false; }
  scriptSize = f.size();
  f.close();
  if (scriptSize == 0 || scriptSize >= MAX_LOADER_SIZE) {
    senderResult = "JS must be 1 to 499999 bytes"; return false;
  }
  return true;
}
void beginPayloadSend() {
  if (WiFi.status() != WL_CONNECTED) {
    senderResult = "Connect Cardputer to Wi-Fi first";
    go(Page::PAYLOAD_RESULT);
    return;
  }
  if (!prepareSelectedPayload()) { go(Page::PAYLOAD_RESULT); return; }
  IPAddress target;
  if (!target.fromString(switchIp)) {
    senderResult = "Set a valid target IPv4 address";
    go(Page::PAYLOAD_RESULT);
    return;
  }
  remoteSuccess = remoteError = remoteEmulator = scriptDelivered = false;
  remoteLast = "Connecting to loader...";
  remoteLine = "";
  senderResult = "Connecting...";
  scriptSent = 0;
  senderClient.stop();
  if (!senderClient.connect(target, PORT, 1800)) {
    senderResult = "TCP 9026 not accepting connections";
    go(Page::PAYLOAD_RESULT);
    return;
  }
  senderFile = SD.open(SCRIPT_PATHS[scriptSelected], FILE_READ);
  if (!senderFile) {
    senderResult = "Could not reopen SD file";
    closeSender();
    go(Page::PAYLOAD_RESULT);
    return;
  }
  senderStarted = millis();
  senderRunning = true;
  progress = 0;
  go(Page::PAYLOAD_SEND);
}

// Stream in small slices so keyboard/display updates continue.
// Socket half-close matches upstream Node.js socket.end(), allowing loader logs
// to arrive over the same TCP connection after the payload body has been sent.
void pollSender() {
  if (!senderRunning || page != Page::PAYLOAD_SEND) return;
  if (!scriptDelivered) {
    if (senderFile.available()) {
      uint8_t buf[1024];
      size_t bytes = senderFile.read(buf, sizeof(buf));
      size_t offset = 0;
      while (offset < bytes) {
        int written = senderClient.write(buf + offset, bytes - offset);
        if (written <= 0) {
          senderResult = "TCP write failed";
          endSend(senderResult);
          return;
        }
        offset += (size_t)written;
        scriptSent += written;
      }
      progress = scriptSize ? (int)(80ULL * scriptSent / scriptSize) : 0;
      drawingDirty = true;
    }
    if (!senderFile.available()) {
      senderFile.close();
      if (senderClient.fd() < 0 || ::shutdown(senderClient.fd(), SHUT_WR) != 0) {
        endSend("Could not finish TCP request");
        return;
      }
      scriptDelivered = true;
      progress = 85;
      remoteLast = "Script sent; awaiting logs...";
      drawingDirty = true;
    }
  } else {
    unsigned int budget = 512;
    while (budget-- && senderClient.available()) {
      char c = (char)senderClient.read();
      if (c == '\n') { onRemoteLine(remoteLine); remoteLine = ""; }
      else if (c >= 32 || c == '\t') {
        if (remoteLine.length() < 150) remoteLine += c;
      }
    }
    if (!senderClient.connected() && senderClient.available() == 0) {
      endSend("Remote finished without success marker");
      return;
    }
  }
  if (millis() - senderStarted > 900000UL) {
    endSend("15 minute timeout; result unverified");
  }
}


void go(Page next) { page = next; drawingDirty = true; }
String cut(String value, unsigned int width = 37) {
  if (value.length() > width) return value.substring(0, width - 2) + "..";
  return value;
}
void lineAt(int y, const String &value, uint16_t color = TFT_WHITE) {
  auto &d = M5Cardputer.Display;
  d.setTextColor(color, TFT_BLACK);
  d.setCursor(7, y);
  d.print(cut(value));
}
void footer(const String &value) {
  M5Cardputer.Display.fillRect(0, 121, 240, 14, TFT_BLACK);
  lineAt(125, value, TFT_YELLOW);
}
void header(const String &title) {
  auto &d = M5Cardputer.Display;
  d.fillScreen(TFT_BLACK);
  d.fillRect(0, 0, 240, 20, 0x2124);
  d.setTextSize(1);
  d.setTextColor(TFT_WHITE, 0x2124);
  d.setCursor(5, 6);
  d.print("CIVSWITCH / " + title);
}

void draw() {
  auto &d = M5Cardputer.Display;
  switch (page) {
    case Page::HOME: {
      header("HOME");
      lineAt(29, "WiFi: " + String(WiFi.isConnected() ? WiFi.SSID() : "OFFLINE"), TFT_CYAN);
      lineAt(42, "Device IP: " + (WiFi.isConnected() ? WiFi.localIP().toString() : "--"));
      lineAt(55, "Switch IP: " + switchIp);
      lineAt(68, "Port 9026: " + String(fakeConsole ? "PC EMULATOR" : (portOpen ? "TCP OPEN" : "NOT CONNECTED")));
      lineAt(81, "SD JS scripts: " + String(readyScripts) + "/" + String(SCRIPT_COUNT));
      lineAt(94, status);
      d.drawRect(8, 109, 224, 9, TFT_WHITE);
      d.fillRect(10, 111, 220, 5, TFT_BLACK);
      if (progress) d.fillRect(10, 111, (220 * progress) / 100, 5, TFT_GREEN);
      footer("A:Setup B:Diag P:Payload C:Test");
      break;
    }
    case Page::MENU:
      header("ON-DEVICE SETUP");
      lineAt(26, "W/S move, ENTER choose", TFT_CYAN);
      {
        const char* items[] = {"Scan Wi-Fi networks", "Enter Wi-Fi name", "Set Switch / PC IP", "Check microSD payload", "Back to Home"};
        for (int i = 0; i < 5; ++i)
          lineAt(43 + 15 * i, String(i == menuSelected ? "> " : "  ") + items[i],
                 i == menuSelected ? TFT_YELLOW : TFT_WHITE);
      }
      footer("X:Home");
      break;
    case Page::WIFI_SCAN:
      header("WI-FI SCAN");
      lineAt(45, "Looking for nearby networks...", TFT_CYAN);
      lineAt(65, "Please wait.");
      footer("X:Back");
      break;
    case Page::WIFI_LIST: {
      header("WI-FI NETWORKS");
      lineAt(24, String(networkCount) + " found; W/S select", TFT_CYAN);
      int start = (selected / 5) * 5;
      for (int i = start; i < networkCount && i < start + 5; ++i) {
        lineAt(41 + (i - start) * 15,
               String(selected == i ? "> " : "  ") + cut(networks[i], 28) + (networkOpen[i] ? " [O]" : " [*]"),
               selected == i ? TFT_YELLOW : TFT_WHITE);
      }
      if (!networkCount) lineAt(53, "None found; M to enter manually");
      footer("ENTER:Join M:Manual X:Back");
      break;
    }
    case Page::EDIT_SSID:
    case Page::EDIT_PASSWORD:
    case Page::EDIT_IP: {
      const bool pass = (page == Page::EDIT_PASSWORD);
      header(page == Page::EDIT_SSID ? "WI-FI NAME" : (pass ? "WI-FI PASSWORD" : "SWITCH / PC IP"));
      lineAt(32, pass ? "Network: " + pendingSsid : (page == Page::EDIT_IP ? "IPv4 address of target:" : "Type the network name:"), TFT_CYAN);
      String shown = pass ? String("") : editor;
      if (pass) for (unsigned int i = 0; i < editor.length(); ++i) shown += '*';
      lineAt(59, cut(shown, 36), TFT_WHITE);
      d.drawFastHLine(7, 72, 226, TFT_CYAN);
      lineAt(83, pass ? "Password is hidden" : "Use the built-in keyboard");
      lineAt(99, "Backspace removes a character");
      footer("ENTER:Save Fn+ESC:Cancel");
      break;
    }
    case Page::CONNECTING:
      header("CONNECTING");
      lineAt(35, "Wi-Fi: " + pendingSsid, TFT_CYAN);
      lineAt(56, "Connecting... up to 20 seconds");
      lineAt(80, cut(status));
      footer("X:Cancel");
      break;
    case Page::DIAGNOSTICS:
      header("DIAGNOSTICS");
      lineAt(26, "Wi-Fi: " + String(WiFi.isConnected() ? "CONNECTED" : "OFFLINE"), TFT_CYAN);
      lineAt(39, "RSSI: " + String(WiFi.isConnected() ? WiFi.RSSI() : 0) + " dBm");
      lineAt(52, "Target: " + switchIp);
      lineAt(65, "TCP 9026: " + String(fakeConsole ? "PC EMULATOR" : (portOpen ? "OPEN" : "CLOSED")));
      lineAt(78, "Checks: " + String(checksOK) + " OK / " + String(checksFail) + " failed");
      lineAt(91, "SD: " + String(payloadPresent ? "payload found" : (sdPresent ? "payload missing" : "no card")));
      lineAt(104, cut(lastDiag));
      footer("A:Home C:Retest D:SD");
      break;
    case Page::PAYLOAD_LIST:
      header("FS-KAIHEN SEND");
      lineAt(23, "Scripts on Cardputer SD:", TFT_CYAN);
      for (int i = 0; i < SCRIPT_COUNT; i++) {
        lineAt(41 + i*17, String(scriptSelected==i ? "> " : "  ") +
              SCRIPT_LABELS[i] + (sdPresent && SD.exists(SCRIPT_PATHS[i]) ? " [+]" : " [-]"),
              scriptSelected==i ? TFT_YELLOW : TFT_WHITE);
      }
      footer("W/S:Choose ENTER:Check X:Home");
      break;
    case Page::PAYLOAD_CONFIRM:
      header("CONFIRM SEND");
      lineAt(27, SCRIPT_LABELS[scriptSelected], TFT_CYAN);
      lineAt(41, cut(SCRIPT_PATHS[scriptSelected]));
      lineAt(55, String(scriptSize) + " bytes -> " + switchIp + ":9026");
      lineAt(73, "Risk: save corruption or crash", TFT_YELLOW);
      lineAt(90, "Requires Switch Remote JS Loader");
      lineAt(103, "And correct firmware/game setup");
      footer("Y:SEND to Switch  X:Cancel");
      break;
    case Page::PAYLOAD_SEND:
      header("PAYLOAD SENDER");
      lineAt(24, SCRIPT_LABELS[scriptSelected], TFT_CYAN);
      lineAt(42, "Sent: " + String(scriptSent) + "/" + String(scriptSize));
      lineAt(59, scriptDelivered ? "Running remotely; reading logs" : "Transferring over Wi-Fi");
      lineAt(76, cut(remoteLast, 36));
      d.drawRect(8, 102, 224, 12, TFT_WHITE);
      d.fillRect(10, 104, 220, 8, TFT_BLACK);
      if (progress) d.fillRect(10, 104, (progress*220)/100, 8, TFT_GREEN);
      footer("X:Abort   USB:serial logs");
      break;
    case Page::PAYLOAD_RESULT:
      header("SEND RESULT");
      lineAt(28, senderResult, remoteSuccess && !remoteEmulator ? TFT_GREEN : TFT_YELLOW);
      lineAt(49, "Sent " + String(scriptSent) + "/" + String(scriptSize) + " bytes");
      lineAt(66, "Loader output:");
      lineAt(82, cut(remoteLast, 36), TFT_CYAN);
      lineAt(99, "No marker = no proof of success");
      footer("X:Payload list  A:Home");
      break;
    case Page::SD_INFO:
      header("MICROSD PAYLOAD");
      lineAt(29, "Card: " + String(sdPresent ? "MOUNTED" : "NOT DETECTED"), TFT_CYAN);
      lineAt(44, "Payload: " + String(payloadPresent ? "FOUND" : "NOT FOUND"));
      lineAt(59, "File: " + (payloadPresent ? payloadPath : "/civswitch/payload.bin"));
      lineAt(74, "Size: " + String(payloadPresent ? String(payloadSize) + " bytes" : "--"));
      lineAt(89, "First bytes: " + (payloadPresent ? payloadHeader : "--"));
      lineAt(104, "Only inspected; NOT transmitted", TFT_YELLOW);
      footer("C:Rescan A:Home");
      break;
  }
  drawingDirty = false;
  lastDraw = millis();
}

void inspectSD() {
  sdPresent = false;
  payloadPresent = false;
  payloadPath = "";
  payloadHeader = "";
  payloadSize = 0;
  readyScripts = 0;
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS, SPI, 10000000)) {
    status = "microSD not mounted";
    return;
  }
  sdPresent = SD.cardType() != CARD_NONE;
  if (!sdPresent) return;
  for (int i = 0; i < SCRIPT_COUNT; ++i) {
    File f = SD.open(SCRIPT_PATHS[i], FILE_READ);
    if (f && !f.isDirectory() && f.size() > 0 && f.size() < MAX_LOADER_SIZE) ++readyScripts;
    if (f) f.close();
  }
  const char* paths[] = {"/civswitch/payload.bin", "/payload.bin"};
  for (auto path : paths) {
    if (!SD.exists(path)) continue;
    File f = SD.open(path, FILE_READ);
    if (!f || f.isDirectory()) { if (f) f.close(); continue; }
    payloadPath = path;
    payloadSize = f.size();
    payloadPresent = payloadSize > 0;
    for (int i = 0; i < 8 && f.available(); ++i) {
      uint8_t b = (uint8_t) f.read();
      char hex[4];
      snprintf(hex, sizeof(hex), "%02X", b);
      if (i) payloadHeader += " ";
      payloadHeader += hex;
    }
    f.close();
    break;
  }
}

void testPort() {
  portOpen = false;
  fakeConsole = false;
  if (WiFi.status() != WL_CONNECTED) {
    status = "Wi-Fi offline";
    lastDiag = status;
    progress = 0;
    drawingDirty = true;
    return;
  }
  IPAddress target;
  if (!target.fromString(switchIp)) {
    status = "Invalid target IPv4 address";
    lastDiag = status;
    progress = 0;
    drawingDirty = true;
    return;
  }
  WiFiClient client;
  if (!client.connect(target, PORT, 1200)) {
    checksFail++;
    status = "No TCP service at port 9026";
    lastDiag = status;
    progress = 35;
  } else {
    checksOK++;
    portOpen = true;
    uint32_t start = millis();
    String banner;
    while (millis() - start < 220 && banner.length() < 48) {
      while (client.available() && banner.length() < 48) banner += (char)client.read();
      if (banner.indexOf('\n') >= 0) break;
      delay(5);
    }
    fakeConsole = banner.startsWith("CIVSWITCH-EMU/1");
    client.stop();
    lastDiag = fakeConsole ? "Test PC emulator handshake OK" : "TCP accepted (loader unverified)";
    status = fakeConsole ? "PC emulator reachable" : "TCP 9026 open";
    progress = 100; // Connectivity only; NOT exploit or payload progress.
  }
  lastProbe = millis();
  drawingDirty = true;
}

void startScan() {
  networkCount = 0;
  selected = 0;
  WiFi.scanDelete();
  WiFi.scanNetworks(true);
  go(Page::WIFI_SCAN);
}

void beginConnect() {
  if (pendingSsid.length() == 0) { status = "Wi-Fi name is empty"; go(Page::MENU); return; }
  WiFi.mode(WIFI_STA);
  WiFi.persistent(true);
  WiFi.disconnect(false, false);
  if (editor.length() > 0) WiFi.begin(pendingSsid.c_str(), editor.c_str());
  else WiFi.begin(pendingSsid.c_str());
  editor = ""; // Do not retain the password in the UI.
  connectStart = millis();
  progress = 20;
  status = "Trying " + pendingSsid;
  go(Page::CONNECTING);
}

void saveIP() {
  IPAddress ip;
  if (!ip.fromString(editor)) {
    status = "Invalid IPv4 (e.g. 192.168.1.50)";
    go(Page::HOME);
    return;
  }
  switchIp = ip.toString();
  prefs.begin("civswitch", false);
  prefs.putString("switch_ip", switchIp);
  prefs.end();
  status = "Target IP saved";
  portOpen = false;
  fakeConsole = false;
  go(Page::HOME);
}

void selectMenu() {
  switch (menuSelected) {
    case 0: startScan(); break;
    case 1: editor = ""; go(Page::EDIT_SSID); break;
    case 2: editor = switchIp; go(Page::EDIT_IP); break;
    case 3: inspectSD(); go(Page::SD_INFO); break;
    default: go(Page::HOME); break;
  }
}

void handleKeyboard(const Keyboard_Class::KeysState &k) {
  if (page == Page::EDIT_SSID || page == Page::EDIT_PASSWORD || page == Page::EDIT_IP) {
    if (k.tab) { editor = ""; go(Page::MENU); return; }
    if (k.del) {
      if (editor.length()) editor.remove(editor.length() - 1);
      drawingDirty = true;
    }
    for (char c : k.word) {
      const size_t limit = page == Page::EDIT_SSID ? 32 : (page == Page::EDIT_PASSWORD ? 63 : 15);
      if (c >= 32 && c <= 126 && editor.length() < limit) { editor += c; drawingDirty = true; }
    }
    if (k.enter) {
      if (page == Page::EDIT_SSID) {
        pendingSsid = editor;
        editor = "";
        go(Page::EDIT_PASSWORD);
      } else if (page == Page::EDIT_PASSWORD) beginConnect();
      else saveIP();
    }
    return;
  }

  for (char c : k.word) {
    if (page == Page::HOME) {
      if (c == 'p') { scriptSelected = 0; inspectSD(); go(Page::PAYLOAD_LIST); }
      else if (c == 'a') { menuSelected = 0; go(Page::MENU); }
      else if (c == 'b') go(Page::DIAGNOSTICS);
      else if (c == 'c') testPort();
      else if (c == 'r') {
        if (WiFi.isConnected()) { WiFi.disconnect(false, false); }
        WiFi.reconnect();
        status = "Wi-Fi reconnect requested";
        drawingDirty = true;
      }
    } else if (page == Page::PAYLOAD_LIST) {
      if (c == 'w' && scriptSelected > 0) { scriptSelected--; drawingDirty=true; }
      else if (c == 's' && scriptSelected < SCRIPT_COUNT-1) { scriptSelected++; drawingDirty=true; }
      else if (c == 'x') go(Page::HOME);
    } else if (page == Page::PAYLOAD_CONFIRM) {
      if (c == 'y') beginPayloadSend();
      else if (c == 'x') go(Page::PAYLOAD_LIST);
    } else if (page == Page::PAYLOAD_SEND) {
      if (c == 'x') endSend("Aborted by user; remote state unknown");
    } else if (page == Page::PAYLOAD_RESULT) {
      if (c == 'a') go(Page::HOME);
      else if (c == 'x') go(Page::PAYLOAD_LIST);
    } else if (page == Page::MENU) {
      if (c == 'w' && menuSelected > 0) { menuSelected--; drawingDirty = true; }
      else if (c == 's' && menuSelected < 4) { menuSelected++; drawingDirty = true; }
      else if (c == 'x') go(Page::HOME);
    } else if (page == Page::WIFI_LIST) {
      if (c == 'w' && selected > 0) { selected--; drawingDirty = true; }
      else if (c == 's' && selected < networkCount - 1) { selected++; drawingDirty = true; }
      else if (c == 'm') { editor = ""; go(Page::EDIT_SSID); }
      else if (c == 'x') go(Page::MENU);
    } else if (page == Page::WIFI_SCAN && c == 'x') go(Page::MENU);
    else if (page == Page::CONNECTING && c == 'x') {
      WiFi.disconnect(false, false);
      status = "Connection cancelled";
      go(Page::HOME);
    } else if (page == Page::DIAGNOSTICS) {
      if (c == 'a' || c == 'x') go(Page::HOME);
      else if (c == 'c') testPort();
      else if (c == 'd') { inspectSD(); go(Page::SD_INFO); }
    } else if (page == Page::SD_INFO) {
      if (c == 'a' || c == 'x') go(Page::HOME);
      else if (c == 'c') { inspectSD(); drawingDirty = true; }
    }
  }

  if (k.enter) {
    if (page == Page::MENU) selectMenu();
    else if (page == Page::PAYLOAD_LIST) {
      if (prepareSelectedPayload()) go(Page::PAYLOAD_CONFIRM);
      else go(Page::PAYLOAD_RESULT);
    }
    else if (page == Page::WIFI_LIST && networkCount > 0) {
      pendingSsid = networks[selected];
      editor = "";
      if (networkOpen[selected]) beginConnect();
      else go(Page::EDIT_PASSWORD);
    }
  }
}

void setup() {
  Serial.begin(115200);
  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);
  M5Cardputer.Display.setRotation(0); // Preserve the original orientation.
  M5Cardputer.Display.setTextWrap(false);
  prefs.begin("civswitch", true);
  switchIp = prefs.getString("switch_ip", "192.168.1.100");
  prefs.end();

  inspectSD();
  WiFi.mode(WIFI_STA);
  WiFi.persistent(true);
  WiFi.setSleep(false);
  WiFi.begin(); // Reuse saved ESP32 credentials; no phone setup.
  progress = 10;
  status = "A: Configure on Cardputer";
  draw();
}

void loop() {
  M5Cardputer.update();
  if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
    handleKeyboard(M5Cardputer.Keyboard.keysState());
  }

  pollSender();
  if (page == Page::WIFI_SCAN) {
    int n = WiFi.scanComplete();
    if (n >= 0) {
      networkCount = n < MAX_NETWORKS ? n : MAX_NETWORKS;
      for (int i = 0; i < networkCount; ++i) {
        networks[i] = WiFi.SSID(i);
        networkOpen[i] = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
      }
      WiFi.scanDelete();
      selected = 0;
      go(Page::WIFI_LIST);
    } else if (n == WIFI_SCAN_FAILED) {
      status = "Wi-Fi scan failed";
      go(Page::MENU);
    }
  }
  if (page == Page::CONNECTING) {
    if (WiFi.status() == WL_CONNECTED) {
      status = "Connected to " + WiFi.SSID();
      progress = 40;
      go(Page::HOME);
    } else if (millis() - connectStart > CONNECT_TIMEOUT) {
      status = "Wi-Fi failed; check password";
      progress = 0;
      go(Page::HOME);
    }
  }
  if (page == Page::HOME && WiFi.isConnected() && millis() - lastProbe > PROBE_INTERVAL) {
    testPort();
  }
  if (page == Page::HOME && millis() - lastDraw > 2500) drawingDirty = true;
  if (drawingDirty) draw();
  delay(10);
}
