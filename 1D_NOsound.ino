// ==========================================================================
// PROJECT: ULTIMATE LED INVADERS
// VERSION: 2.0 (PUBLIC RELEASE)
// HARDWARE: Generic ESP32, WS2812B Strip, 4 Buttons
// ==========================================================================

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <FastLED.h>
#include <vector>

// --------------------------------------------------------------------------
// HARDWARE DEFINITIONS
// --------------------------------------------------------------------------
#define PIN_LED_DATA    16
#define PIN_BTN_BLUE    3
#define PIN_BTN_RED     5
#define PIN_BTN_GREEN   7
#define PIN_BTN_WHITE   9 

#define MAX_LEDS        1200 
#define LED_TYPE        WS2812B
#define COLOR_ORDER     GRB
#define CONFIG_VERSION  7 // Internal config structure version

// FPS LIMIT: 33ms = ~30 FPS
#define FRAME_DELAY     33 

// --------------------------------------------------------------------------
// --- USER CONFIGURATION (DEFAULT SETTINGS) ---
// Change these values if you want to hardcode different defaults!
// --------------------------------------------------------------------------

// Total number of LEDs on your strip (excluding the sacrificial LED logic)
int config_num_leds = 100; 

// Default brightness in percent (10-100)
int config_brightness_pct = 50; 

// Level to start with after boot
int config_start_level = 1;

// --------------------------------------------------------------------------
// DATA STRUCTURES
// --------------------------------------------------------------------------
struct LevelConfig { 
  int speed; 
  int length; 
  int bossType; 
};

struct BossConfig { 
  int moveSpeed; 
  int shotSpeed; 
  int hpPerLed; 
  int shotFreq; 
};

struct Enemy { 
  int color; 
};

struct BossSegment { 
  int color; 
  int hp; 
  int maxHp; 
  bool active; 
  int originalIndex; 
};

struct Shot { 
  int position; 
  int color; 
};

struct BossProjectile { 
  int pos; 
  int color; 
};

// --------------------------------------------------------------------------
// GLOBAL VARIABLES
// --------------------------------------------------------------------------
CRGB leds[MAX_LEDS];
Preferences preferences;
WebServer server(80);

String currentProfilePrefix = "def_"; 
String config_ssid = ""; 
String config_pass = "";
bool config_static_ip = false; 
String config_ip = ""; 
String config_gateway = ""; 
String config_subnet = ""; 
String config_dns = "";

// Mode Switch
bool wifiMode = false; 

LevelConfig levels[11];
BossConfig boss1Cfg; 
BossConfig boss2Cfg; 
BossConfig boss3Cfg; 

enum GameState { 
  STATE_MENU, 
  STATE_INTRO, 
  STATE_PLAYING, 
  STATE_BOSS_PLAYING, 
  STATE_LEVEL_COMPLETED, 
  STATE_GAME_FINISHED, 
  STATE_GAMEOVER 
};
GameState currentState = STATE_MENU;

unsigned long lastLoopTime = 0; 
unsigned long stateTimer = 0; 
unsigned long lastShotMove = 0;
unsigned long lastEnemyMove = 0;
unsigned long lastFireTime = 0;
unsigned long bossActionTimer = 0; 
bool buttonsReleased = true; 
const int FIRE_COOLDOWN = 100; 

// Button Timer (for Menu Access)
unsigned long btnWhitePressTime = 0;
bool btnWhiteHeld = false;

std::vector<Enemy> enemies; 
std::vector<Shot> shots;
std::vector<BossSegment> bossSegments; 
std::vector<BossProjectile> bossProjectiles; 

int enemyFrontIndex = -1; 
int currentLevel = 1;
int currentBossType = 0;

// Boss 2 Specifics (Masterblaster)
enum Boss2State { B2_MOVE, B2_CHARGE, B2_SHOOT };
Boss2State boss2State = B2_MOVE;
int boss2Section = 0; 
int boss2ShotsFired = 0;
int boss2LockedColor = 1; 
int markerPos[3]; 

// --------------------------------------------------------------------------
// HELPER FUNCTIONS
// --------------------------------------------------------------------------
CRGB getColor(int colorCode) {
  switch (colorCode) {
    case 1: return CRGB::Blue;
    case 2: return CRGB::Red;
    case 3: return CRGB::Green;
    case 4: return CRGB::Yellow;  
    case 5: return CRGB::Magenta; 
    case 6: return CRGB::Cyan;    
    case 7: return CRGB::White;   
    default: return CRGB::Black;
  }
}

// Flash effect on hit
void flashPixel(int pos) {
  if(pos >= 0 && pos < config_num_leds) {
    leds[pos + 1] = CRGB::White; 
  }
}

void checkWinCondition() {
  bool won = false;
  if (currentState == STATE_PLAYING && enemies.empty()) won = true;
  if (currentState == STATE_BOSS_PLAYING && bossSegments.empty()) won = true;

  if (won) {
    if (currentLevel >= 10) currentState = STATE_GAME_FINISHED;
    else {
      currentState = STATE_LEVEL_COMPLETED;
      stateTimer = millis();
    }
  }
}

// --------------------------------------------------------------------------
// LEVEL START & INTRO ANIMATIONS
// --------------------------------------------------------------------------
void startLevelIntro(int level) {
  currentLevel = level;
  currentState = STATE_INTRO;
  stateTimer = millis();
  
  FastLED.clear();
  // Background slightly gray
  for(int i=0; i<config_num_leds; i++) leds[i+1] = CRGB(10,10,10);
  
  CRGB barColor = levels[level].bossType > 0 ? CRGB::Red : CRGB::Green;
  int center = config_num_leds / 2;
  int totalWidth = (level * 6) + ((level-1)*4); 
  int startPos = center - (totalWidth/2);
  if(startPos < 0) startPos = 0;
  
  int cursor = startPos;
  for(int i=0; i<level; i++) {
    for(int k=0; k<6; k++) {
      if(cursor < config_num_leds) leds[cursor + 1] = barColor; 
      cursor++;
    }
    cursor += 4; 
  }
  
  leds[0] = CRGB(20, 0, 0); // Sacrificial LED Status
  FastLED.show();
}

void drawLevelIntro(int level) {
  FastLED.clear();
  for(int i=0; i<config_num_leds; i++) leds[i+1] = CRGB(5,5,5); 
  
  CRGB barColor = levels[level].bossType > 0 ? CRGB::Red : CRGB::Green;
  int center = config_num_leds / 2;
  int totalWidth = (level * 6) + ((level-1)*4); 
  int startPos = center - (totalWidth/2);
  if(startPos < 0) startPos = 0;
  
  int cursor = startPos;
  for(int i=0; i<level; i++) {
    for(int k=0; k<6; k++) {
      if(cursor < config_num_leds) leds[cursor + 1] = barColor; 
      cursor++;
    }
    cursor += 4; 
  }
  leds[0] = CRGB(20, 0, 0);
  FastLED.show();
}

void updateLevelIntro() {
  unsigned long elapsed = millis() - stateTimer;
  
  // Blink effect
  if (elapsed > 2000 && elapsed < 4000) {
    if ((elapsed / 250) % 2 == 0) drawLevelIntro(currentLevel); 
    else {
       FastLED.clear(); 
       leds[0] = CRGB(20, 0, 0);
       FastLED.show();
    }
  } else if (elapsed <= 2000) {
     drawLevelIntro(currentLevel);
  }

  // Start Level
  if (elapsed >= 4000) {
    uint8_t bright = map(config_brightness_pct, 10, 100, 25, 255);
    FastLED.setBrightness(bright);
    
    // Level Init
    if (levels[currentLevel].bossType > 0) {
      currentBossType = levels[currentLevel].bossType;
      bossSegments.clear(); 
      enemies.clear(); 
      shots.clear(); 
      bossProjectiles.clear();
      enemyFrontIndex = config_num_leds - 1; 
      
      // Boss Setup
      if (currentBossType == 1) { // The Tank
        for(int i=0; i<3; i++) bossSegments.push_back({3, boss1Cfg.hpPerLed, boss1Cfg.hpPerLed, true, 0});
        for(int i=0; i<3; i++) bossSegments.push_back({1, boss1Cfg.hpPerLed, boss1Cfg.hpPerLed, true, 0});
        for(int i=0; i<3; i++) bossSegments.push_back({3, boss1Cfg.hpPerLed, boss1Cfg.hpPerLed, true, 0});
        bossActionTimer = millis();
      } 
      else if (currentBossType == 2) { // Masterblaster
        for(int i=0; i<9; i++) bossSegments.push_back({0, boss2Cfg.hpPerLed, boss2Cfg.hpPerLed, false, i});
        boss2Section = 0; 
        boss2State = B2_MOVE;
        markerPos[0] = config_num_leds - 12; 
        markerPos[1] = (int)(config_num_leds * 0.65);
        markerPos[2] = (int)(config_num_leds * 0.40);
      }
      else if (currentBossType == 3) { // RGB Overlord
        for(int i=0; i<15; i++) {
           int mixColor = random(4, 8); 
           bossSegments.push_back({mixColor, boss3Cfg.hpPerLed, boss3Cfg.hpPerLed, true, i});
        }
      }
      currentState = STATE_BOSS_PLAYING;
    } else {
      // Normal Level
      enemies.clear(); 
      shots.clear(); 
      bossProjectiles.clear();
      int count = levels[currentLevel].length;
      for (int i = 0; i < count; i++) enemies.push_back({(int)random(1, 4)});
      enemyFrontIndex = config_num_leds - 1;
      currentState = STATE_PLAYING;
    }
  }
}

void moveBossProjectiles(int speed) {
  static unsigned long lastMove = 0;
  if (millis() - lastMove > (1000/speed)) {
    lastMove = millis();
    for(int i=bossProjectiles.size()-1; i>=0; i--) {
      bossProjectiles[i].pos--; 
      if (bossProjectiles[i].pos <= 0) currentState = STATE_GAMEOVER;
    }
  }
}

// --------------------------------------------------------------------------
// WIFI SETUP
// --------------------------------------------------------------------------
void enableWiFi() {
  // Visual feedback BEFORE enabling WiFi
  fill_solid(leds, config_num_leds + 1, CRGB::Blue);
  FastLED.show();

  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false); 
  
  if(config_static_ip && config_ip.length() > 0) {
      IPAddress ip, gw, sn, dns;
      if(ip.fromString(config_ip) && gw.fromString(config_gateway) && sn.fromString(config_subnet)) {
         if(config_dns.length() > 0) dns.fromString(config_dns); else dns.fromString("8.8.8.8");
         WiFi.config(ip, gw, sn, dns);
      }
  }
  if(config_ssid != "") {
    WiFi.begin(config_ssid.c_str(), config_pass.c_str());
  }
  WiFi.softAP("ESP32-Invader-Ult", "12345678"); 
  server.begin();
}

// --------------------------------------------------------------------------
// WEBSERVER (HTML STRINGS)
// --------------------------------------------------------------------------
String getHTML() {
  // HTML Header & Style
  String h = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  h += "<title>Ultimate LED Invaders</title>";
  h += "<style>body{font-family:sans-serif;background:#1a1a1a;color:#eee;padding:10px;max-width:800px;margin:auto;}input,select,button{width:100%;background:#333;color:#fff;border:1px solid #555;padding:8px;border-radius:4px;box-sizing:border-box;margin-bottom:5px;}table{width:100%;border-collapse:collapse;margin-bottom:10px;} td,th{border:1px solid #444;padding:6px;text-align:center;}.sec{margin-top:25px;border-top:1px solid #555;padding-top:15px;background:#222;padding:15px;border-radius:5px;}#warning-box{margin-top:10px;padding:10px;background:#b30000;color:#fff;border:1px solid #ff0000;display:none;font-weight:bold;border-radius:4px;}.val-highlight{color:#0f0;font-weight:bold;}h2,h3{margin-top:0;} pre{font-family:monospace;color:#0f0;font-size:12px;overflow-x:auto;}@media (max-width: 600px) { table, thead, tbody, th, td, tr { display: block; } tr { margin-bottom: 10px; border: 1px solid #444; } td { text-align: right; padding-left: 50%; position: relative; } td::before { content: attr(data-label); position: absolute; left: 10px; font-weight: bold; text-align: left; } }</style>";
  h += "<script>function updateCalc() { var count = parseInt(document.getElementById('ledCount').value)||0; var brightPct = parseInt(document.getElementById('brightness').value)||50; document.getElementById('brightVal').innerText = brightPct + '%'; var warn = document.getElementById('warning-box'); if (count > 0) { var brightFactor = brightPct / 100.0; var amps = ((count * 60 * brightFactor) + 120) / 1000; document.getElementById('ampValue').innerText = amps.toFixed(2) + ' A'; if(amps > 5.0) { warn.style.display='block'; warn.innerText='WARNING: > 5A! High Power PSU required!'; } else warn.style.display='none'; } } function toggleIP() { var x = document.getElementById('ipsettings'); if(document.getElementById('chkStatic').checked) x.style.display='block'; else x.style.display='none'; } function confirmReset() { return confirm('Really delete all settings and factory reset?'); } window.onload = function(){ updateCalc(); toggleIP(); };</script>";
  
  // Banner & Version
  h += "</head><body><pre> _   _  _  _____  _                 _   \n| | | || ||_   _|(_)               | |  \n| | | || |  | |   _  _ __ __   __ _| |  \n| | | || |  | |  | || '_ \\\\ \\ / / _` | |\n| |_| || |__| |  | || | | |\\ V / (_| |_|\n \\___/ |_____/   |_||_| |_| \\_/ \\__,_(_)\n      ULTIMATE LED INVADERS\n</pre>";
  h += "<div style='text-align:center; color:#888; margin-bottom:10px;'>Version 2.0</div>";
  
  // Profile Management
  String pName = (currentProfilePrefix == "def_") ? "Standard" : ((currentProfilePrefix == "kid_") ? "Kids" : "Pro");
  h += "<div class='sec'><h3>Profile Management</h3>Current Profile: <b>" + pName + "</b><br><form action='/loadprofile' method='POST' style='display:flex;gap:5px;margin-top:5px;'><select name='profile'><option value='def' " + String(currentProfilePrefix=="def_"?"selected":"") + ">Standard</option><option value='kid' " + String(currentProfilePrefix=="kid_"?"selected":"") + ">Kids</option><option value='pro' " + String(currentProfilePrefix=="pro_"?"selected":"") + ">Pro/Party</option></select><button type='submit'>Load Profile</button></form></div>";
  
  // Hardware Settings
  h += "<form action='/save' method='POST'><div class='sec'><h3>Hardware & General</h3>Start Level: <input type='number' name='startlvl' min='1' max='10' value='" + String(config_start_level) + "'><br>Total LEDs (Recommended: 240): <input id='ledCount' type='number' name='leds' value='" + String(config_num_leds) + "' oninput='updateCalc()'><br><label>Default Brightness: <span id='brightVal'>" + String(config_brightness_pct) + "%</span> (Recommended >75%)</label><input id='brightness' type='range' name='bright' min='10' max='100' value='" + String(config_brightness_pct) + "' oninput='updateCalc()'><div style='margin-top:5px;'>Max. Current (White): <span id='ampValue' class='val-highlight'>0.00 A</span></div><div id='warning-box'></div></div>";
  
  // WiFi Settings
  h += "<div class='sec'><h3>Network</h3><label>WiFi SSID:</label><select name='ssid'>";
  int n = WiFi.scanNetworks(); if (n == 0) h += "<option value=''>No Networks found</option>"; else { for (int i = 0; i < n; ++i) { String s = WiFi.SSID(i); String sel = (s == config_ssid) ? "selected" : ""; h += "<option value='" + s + "' " + sel + ">" + s + " (" + WiFi.RSSI(i) + "dBm)</option>"; } }
  h += "</select>Password: <input type='password' name='pass' value='" + config_pass + "'><br><input type='checkbox' id='chkStatic' name='static_ip' value='1' onchange='toggleIP()' " + String(config_static_ip ? "checked" : "") + " style='width:auto;'> Use Static IP<br><div id='ipsettings' style='display:none;margin-top:10px;'>IP: <input name='ip' value='" + config_ip + "' placeholder='192.168.178.200'>Gateway: <input name='gw' value='" + config_gateway + "' placeholder='192.168.178.1'>Subnet: <input name='sn' value='" + config_subnet + "' placeholder='255.255.255.0'>DNS: <input name='dns' value='" + config_dns + "' placeholder='8.8.8.8'></div></div>";
  
  // Level Config Table
  h += "<div class='sec'><h3>Level Configuration</h3><table><thead><tr><th>Lvl</th><th>Speed</th><th>Len</th><th>Boss?</th></tr></thead><tbody>";
  for(int i=1; i<=10; i++) { h += "<tr><td data-label='Level'>" + String(i) + "</td><td data-label='Speed'><input name='lspd" + String(i) + "' value='" + String(levels[i].speed) + "'></td><td data-label='Len/Type'><input name='llen" + String(i) + "' value='" + String(levels[i].length) + "'></td><td data-label='Boss'><select name='lboss" + String(i) + "'><option value='0' " + String(levels[i].bossType==0?"selected":"") + ">-</option><option value='2' " + String(levels[i].bossType==2?"selected":"") + ">Masterblaster</option><option value='1' " + String(levels[i].bossType==1?"selected":"") + ">The Tank</option><option value='3' " + String(levels[i].bossType==3?"selected":"") + ">RGB Overlord</option></select></td></tr>"; }
  h += "</tbody></table></div><div class='sec'><h3>Masterblaster</h3>Speed: <input name='b2mv' value='" + String(boss2Cfg.moveSpeed) + "'> ShotSpd: <input name='b2ss' value='" + String(boss2Cfg.shotSpeed) + "'> HP/LED: <input name='b2hp' value='" + String(boss2Cfg.hpPerLed) + "'> Reload(0.1s): <input name='b2fr' value='" + String(boss2Cfg.shotFreq) + "'></div><div class='sec'><h3>The Tank</h3>Speed: <input name='b1mv' value='" + String(boss1Cfg.moveSpeed) + "'> ShotSpd: <input name='b1ss' value='" + String(boss1Cfg.shotSpeed) + "'> HP/LED: <input name='b1hp' value='" + String(boss1Cfg.hpPerLed) + "'> Freq(0.1s): <input name='b1fr' value='" + String(boss1Cfg.shotFreq) + "'></div><div class='sec'><h3>RGB Overlord</h3>Speed: <input name='b3mv' value='" + String(boss3Cfg.moveSpeed) + "'> HP/LED: <input name='b3hp' value='" + String(boss3Cfg.hpPerLed) + "'></div><br><input type='submit' value='SAVE ALL SETTINGS' style='width:100%;background:#009900;padding:15px;font-size:1.2em;cursor:pointer;font-weight:bold;'></form><br><br><form action='/reset' method='POST' onsubmit='return confirmReset()'><button type='submit' style='background:#990000;padding:10px;'>FACTORY RESET</button></form></body></html>";
  return h;
}

// --------------------------------------------------------------------------
// CONFIG LOAD/SAVE
// --------------------------------------------------------------------------
void setupDefaultConfig() {
  for(int i=1; i<=10; i++) { levels[i].speed = 3 + i; levels[i].length = 10 + (i*5); levels[i].bossType = 0; }
  levels[7].speed = 15; levels[7].length = 20; levels[8].speed = 15; levels[8].length = 25; levels[9].speed = 12; levels[9].length = 50;
  levels[3].bossType = 2; levels[6].bossType = 1; levels[10].bossType = 3;
  boss1Cfg = {4, 60, 3, 35}; boss2Cfg = {10, 60, 5, 40}; boss3Cfg = {10, 0, 4, 0};   
}

void loadConfig(String prefix) {
  preferences.begin("game", true);
  config_num_leds = preferences.getInt((prefix+"leds").c_str(), 100);
  config_brightness_pct = preferences.getInt((prefix+"bright").c_str(), 50);
  config_start_level = preferences.getInt((prefix+"startlvl").c_str(), 1);
  
  // Global WiFi settings (not profile dependent)
  config_ssid = preferences.getString("ssid", ""); config_pass = preferences.getString("pass", "");
  config_static_ip = preferences.getBool("sip_on", false); config_ip = preferences.getString("sip_ip", "");
  config_gateway = preferences.getString("sip_gw", ""); config_subnet = preferences.getString("sip_sn", ""); config_dns = preferences.getString("sip_dns", "");
  
  for(int i=1; i<=10; i++) { 
    int defBoss = 0; 
    if(i==3) defBoss=2; if(i==6) defBoss=1; if(i==10) defBoss=3; 
    levels[i].speed = preferences.getInt((prefix+"l"+String(i)+"s").c_str(), 4+i); 
    levels[i].length = preferences.getInt((prefix+"l"+String(i)+"l").c_str(), 10+(i*5)); 
    levels[i].bossType = preferences.getInt((prefix+"l"+String(i)+"b").c_str(), defBoss); 
  }
  
  if(preferences.isKey((prefix+"b1").c_str())) preferences.getBytes((prefix+"b1").c_str(), &boss1Cfg, sizeof(BossConfig)); else boss1Cfg = {4, 60, 3, 35}; 
  if(preferences.isKey((prefix+"b2").c_str())) preferences.getBytes((prefix+"b2").c_str(), &boss2Cfg, sizeof(BossConfig)); else boss2Cfg = {10, 60, 5, 40}; 
  if(preferences.isKey((prefix+"b3").c_str())) preferences.getBytes((prefix+"b3").c_str(), &boss3Cfg, sizeof(BossConfig)); else boss3Cfg = {10, 0, 4, 0}; 
  preferences.end();
}

void handleProfileSwitch() { 
  if (server.hasArg("profile")) { 
    String p = server.arg("profile"); 
    if(p == "kid") currentProfilePrefix = "kid_"; 
    else if(p == "pro") currentProfilePrefix = "pro_"; 
    else currentProfilePrefix = "def_"; 
    preferences.begin("game", false); 
    preferences.putString("act_prof", currentProfilePrefix); 
    preferences.end(); 
    loadConfig(currentProfilePrefix); 
    server.sendHeader("Location", "/"); 
    server.send(303); 
  } else server.send(400, "text/plain", "Bad Request"); 
}

void handleReset() { 
  preferences.begin("game", false); 
  // Backup network settings before wipe
  String s=preferences.getString("ssid",""), p=preferences.getString("pass",""), ip=preferences.getString("sip_ip",""), gw=preferences.getString("sip_gw",""), sn=preferences.getString("sip_sn",""), dns=preferences.getString("sip_dns",""); 
  bool sip=preferences.getBool("sip_on", false); 
  preferences.clear(); 
  preferences.putString("ssid",s); preferences.putString("pass",p); 
  preferences.putBool("sip_on",sip); preferences.putString("sip_ip",ip); 
  preferences.putString("sip_gw",gw); preferences.putString("sip_sn",sn); preferences.putString("sip_dns",dns); 
  preferences.putInt("version", CONFIG_VERSION); 
  preferences.end(); 
  server.send(200, "text/html", "<h2>Reset successful!</h2><p>Loading defaults... ESP restarting.</p>"); 
  delay(1000); 
  ESP.restart(); 
}

void handleSave() { 
  if (server.hasArg("leds")) config_num_leds = server.arg("leds").toInt(); 
  if (server.hasArg("bright")) config_brightness_pct = server.arg("bright").toInt(); 
  if (server.hasArg("startlvl")) config_start_level = server.arg("startlvl").toInt(); 
  if (server.hasArg("ssid")) config_ssid = server.arg("ssid"); 
  if (server.hasArg("pass")) config_pass = server.arg("pass"); 
  config_static_ip = server.hasArg("static_ip"); 
  config_ip = server.arg("ip"); config_gateway = server.arg("gw"); config_subnet = server.arg("sn"); config_dns = server.arg("dns"); 
  
  preferences.begin("game", false); 
  preferences.putString("ssid", config_ssid); preferences.putString("pass", config_pass); 
  preferences.putBool("sip_on", config_static_ip); preferences.putString("sip_ip", config_ip); 
  preferences.putString("sip_gw", config_gateway); preferences.putString("sip_sn", config_subnet); preferences.putString("sip_dns", config_dns); 
  
  String p = currentProfilePrefix; 
  preferences.putInt((p+"leds").c_str(), config_num_leds); 
  preferences.putInt((p+"bright").c_str(), config_brightness_pct); 
  preferences.putInt((p+"startlvl").c_str(), config_start_level); 
  
  for(int i=1; i<=10; i++) { 
    levels[i].speed = server.arg("lspd"+String(i)).toInt(); 
    levels[i].length = server.arg("llen"+String(i)).toInt(); 
    levels[i].bossType = server.arg("lboss"+String(i)).toInt(); 
    preferences.putInt((p+"l"+String(i)+"s").c_str(), levels[i].speed); 
    preferences.putInt((p+"l"+String(i)+"l").c_str(), levels[i].length); 
    preferences.putInt((p+"l"+String(i)+"b").c_str(), levels[i].bossType); 
  } 
  
  boss1Cfg.moveSpeed = server.arg("b1mv").toInt(); boss1Cfg.shotSpeed = server.arg("b1ss").toInt(); boss1Cfg.hpPerLed = server.arg("b1hp").toInt(); boss1Cfg.shotFreq = server.arg("b1fr").toInt(); 
  preferences.putBytes((p+"b1").c_str(), &boss1Cfg, sizeof(BossConfig)); 
  
  boss2Cfg.moveSpeed = server.arg("b2mv").toInt(); boss2Cfg.shotSpeed = server.arg("b2ss").toInt(); boss2Cfg.hpPerLed = server.arg("b2hp").toInt(); boss2Cfg.shotFreq = server.arg("b2fr").toInt(); 
  preferences.putBytes((p+"b2").c_str(), &boss2Cfg, sizeof(BossConfig)); 
  
  boss3Cfg.moveSpeed = server.arg("b3mv").toInt(); boss3Cfg.shotSpeed = 0; boss3Cfg.hpPerLed = server.arg("b3hp").toInt(); 
  preferences.putBytes((p+"b3").c_str(), &boss3Cfg, sizeof(BossConfig)); 
  preferences.end(); 
  
  server.send(200, "text/html", "<h2>Saved!</h2><p>ESP restarting...</p><a href='/'>Go Back</a>"); 
  delay(1000); 
  ESP.restart(); 
}

// --------------------------------------------------------------------------
// MAIN SETUP
// --------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  pinMode(PIN_BTN_BLUE, INPUT_PULLUP); 
  pinMode(PIN_BTN_RED, INPUT_PULLUP); 
  pinMode(PIN_BTN_GREEN, INPUT_PULLUP); 
  pinMode(PIN_BTN_WHITE, INPUT_PULLUP);

  setupDefaultConfig(); 
  preferences.begin("game", false); 
  int storedVer = preferences.getInt("version", 0); 
  preferences.end();
  
  if (storedVer < CONFIG_VERSION) handleReset(); 
  
  preferences.begin("game", true); 
  currentProfilePrefix = preferences.getString("act_prof", "def_"); 
  preferences.end();
  loadConfig(currentProfilePrefix);        

  // +1 for "Sacrificial LED" (Buffer Pixel)
  // This shifts the logical game strip by 1 to avoid signal corruption on the first pixel
  FastLED.addLeds<LED_TYPE, PIN_LED_DATA, COLOR_ORDER>(leds, config_num_leds + 1);
  FastLED.setBrightness(map(config_brightness_pct, 10, 100, 25, 255));
  leds[0] = CRGB(20, 0, 0); // Sacrificial LED Red = Boot
  FastLED.show();

  // IMPORTANT: Disable WiFi by default to prevent WS2812B flickering on ESP32!
  WiFi.mode(WIFI_OFF);
  
  server.on("/", []() { server.send(200, "text/html", getHTML()); }); 
  server.on("/save", handleSave); 
  server.on("/loadprofile", handleProfileSwitch); 
  server.on("/reset", handleReset); 
  
  startLevelIntro(config_start_level);
}

// --------------------------------------------------------------------------
// MAIN LOOP
// --------------------------------------------------------------------------
void loop() {
  unsigned long now = millis();

  // ----------------------------------------------------------------------
  // FPS LIMITER & WIFI HANDLING
  // ----------------------------------------------------------------------
  if (now - lastLoopTime < FRAME_DELAY) return;
  lastLoopTime = now;

  // WIFI / MENU MODE
  if (wifiMode) {
    server.handleClient();
    
    // VISUAL: Blue Strip in Menu Mode
    fill_solid(leds, config_num_leds + 1, CRGB::Blue);
    FastLED.show();
    
    // Exit WiFi (Short Click White)
    if (digitalRead(PIN_BTN_WHITE) == LOW) {
       delay(200); // Debounce
       ESP.restart(); // Clean Restart into Game Mode
    }
    return; // STOP GAME LOGIC here
  }

  // ----------------------------------------------------------------------
  // BUTTON CHECK (White = Config Mode Trigger)
  // ----------------------------------------------------------------------
  if (digitalRead(PIN_BTN_WHITE) == LOW) {
    if (!btnWhiteHeld) {
      btnWhiteHeld = true;
      btnWhitePressTime = now;
    } else {
      if (now - btnWhitePressTime > 3000) { // Hold for 3 seconds
         // ENTER WIFI MODE
         wifiMode = true;
         while(digitalRead(PIN_BTN_WHITE) == LOW) { delay(10); } // Wait for release
         enableWiFi();
         return;
      }
    }
  } else {
    // Released
    if (btnWhiteHeld && (now - btnWhitePressTime < 1000)) {
       // Short Click -> Restart Level
       startLevelIntro(config_start_level);
    }
    btnWhiteHeld = false;
  }

  // ----------------------------------------------------------------------
  // GAME LOGIC (Only when WiFi is OFF)
  // ----------------------------------------------------------------------
  
  if (currentState == STATE_LEVEL_COMPLETED) { 
    for(int i=0; i<config_num_leds; i++) leds[i+1] = CRGB::Green; 
    leds[0]=CRGB(20,0,0); 
    FastLED.show(); 
    if (now - stateTimer > 2000) startLevelIntro(currentLevel + 1); 
    return; 
  }
  
  if (currentState == STATE_GAME_FINISHED) { 
    for(int i=0; i<config_num_leds; i++) leds[i+1] = CHSV((now/10)+(i*5), 255, 255); 
    leds[0]=CRGB(20,0,0); 
    FastLED.show(); 
    return; 
  }
  
  if (currentState == STATE_GAMEOVER) { 
    for(int i=0; i<config_num_leds; i++) leds[i+1] = CRGB::Red; 
    leds[0]=CRGB(20,0,0); 
    FastLED.show(); 
    return; 
  }
  
  if (currentState == STATE_INTRO) { 
    updateLevelIntro(); 
    return; 
  }

  // GAME LOOP
  if (currentState == STATE_PLAYING || currentState == STATE_BOSS_PLAYING) {
    
    // INPUT
    bool isAnyBtnPressed = (digitalRead(PIN_BTN_BLUE) == LOW || digitalRead(PIN_BTN_RED) == LOW || digitalRead(PIN_BTN_GREEN) == LOW);
    if (!isAnyBtnPressed) buttonsReleased = true;
    
    if (isAnyBtnPressed && buttonsReleased && (now - lastFireTime > FIRE_COOLDOWN)) {
       int c = 0; 
       // Color Mixing for Boss 3 (Overlord)
       if (currentBossType == 3) { 
         bool b = (digitalRead(PIN_BTN_BLUE)==LOW);
         bool r = (digitalRead(PIN_BTN_RED)==LOW);
         bool g = (digitalRead(PIN_BTN_GREEN)==LOW); 
         if (r && g && b) c=7; 
         else if (r && g) c=4; 
         else if (r && b) c=5; 
         else if (g && b) c=6; 
         else if (b) c=1; 
         else if (r) c=2; 
         else if (g) c=3; 
       } else { 
         // Standard Shot
         if (digitalRead(PIN_BTN_BLUE)==LOW) c=1; 
         else if (digitalRead(PIN_BTN_RED)==LOW) c=2; 
         else if (digitalRead(PIN_BTN_GREEN)==LOW) c=3; 
       }
       if (c > 0) { 
         shots.push_back({0, c}); 
         lastFireTime = now; 
         buttonsReleased = false; 
       }
    }

    // PROJECTILES & COLLISION
    if (now - lastShotMove > (1000/90)) {
      lastShotMove = now;
      for (int i = shots.size() - 1; i >= 0; i--) {
        shots[i].position++; 
        bool remove = false;
        
        // Collision Normal Level
        if (currentState == STATE_PLAYING) { 
          if (shots[i].position >= enemyFrontIndex && !enemies.empty()) { 
            if (shots[i].color == enemies[0].color) { 
              enemies.erase(enemies.begin()); 
              enemyFrontIndex++; 
              flashPixel(shots[i].position); 
              remove = true; 
              checkWinCondition(); 
            } else { 
              // Wrong Color = Penalty (Enemy moves closer)
              enemies.insert(enemies.begin(), {shots[i].color}); 
              enemyFrontIndex--; 
              remove = true; 
            } 
          } 
        } 
        // Collision Boss Level
        else if (currentState == STATE_BOSS_PLAYING) {
          // Hit Boss Projectiles
          for(int p=0; p<bossProjectiles.size(); p++) { 
            if(shots[i].position >= bossProjectiles[p].pos) { 
              if(shots[i].color == bossProjectiles[p].color) { 
                bossProjectiles.erase(bossProjectiles.begin() + p); 
                flashPixel(shots[i].position); 
              } 
              remove = true; 
              break; 
            } 
          }
          // Hit Boss Segments
          if (!remove && shots[i].position >= enemyFrontIndex && !bossSegments.empty()) { 
            int hitIndex = shots[i].position - enemyFrontIndex; 
            if (hitIndex >= 0 && hitIndex < bossSegments.size()) { 
              bool vulnerable = false; 
              if (currentBossType == 1) vulnerable = true; 
              else if (currentBossType == 2) { 
                if (boss2State == B2_MOVE && bossSegments[hitIndex].active) vulnerable = true; 
              } 
              else if (currentBossType == 3) vulnerable = true; 
              
              if (vulnerable) { 
                if (shots[i].color == bossSegments[hitIndex].color) { 
                  flashPixel(shots[i].position); 
                  bossSegments[hitIndex].hp--; 
                  if (bossSegments[hitIndex].hp <= 0) { 
                    bossSegments.erase(bossSegments.begin() + hitIndex); 
                    if (hitIndex == 0) enemyFrontIndex++; 
                  } 
                  checkWinCondition(); 
                } 
              } 
              remove = true; 
            } 
          }
        }
        
        if (shots[i].position >= config_num_leds) remove = true; 
        if (remove) shots.erase(shots.begin() + i);
      }
    }
    
    if (currentState == STATE_LEVEL_COMPLETED || currentState == STATE_GAME_FINISHED) return;

    // ENEMY / BOSS MOVEMENT & AI
    if (currentState == STATE_PLAYING) { 
      unsigned long delay = 1000 / levels[currentLevel].speed; 
      if (now - lastEnemyMove > delay) { 
        lastEnemyMove = now; 
        enemyFrontIndex--; 
        if (enemyFrontIndex <= 0) currentState = STATE_GAMEOVER; 
      } 
    }
    else if (currentState == STATE_BOSS_PLAYING) {
      // Move Boss Projectiles
      int pSpeed = 60; 
      if (currentBossType == 1) pSpeed = boss1Cfg.shotSpeed; 
      if (currentBossType == 2) pSpeed = boss2Cfg.shotSpeed; 
      moveBossProjectiles(pSpeed);

      // Boss 1: The Tank (Moves slow, shoots often)
      if (currentBossType == 1) { 
        if (now - lastEnemyMove > (1000/boss1Cfg.moveSpeed)) { 
          lastEnemyMove = now; 
          enemyFrontIndex--; 
          if (enemyFrontIndex <= 0) currentState = STATE_GAMEOVER; 
        } 
        if (now - bossActionTimer > (boss1Cfg.shotFreq * 100)) { 
          bossActionTimer = now; 
          bossProjectiles.push_back({enemyFrontIndex, (int)random(1,4)}); 
        } 
      }
      // Boss 2: Masterblaster (3 Phases)
      else if (currentBossType == 2) { 
        if (boss2State == B2_MOVE) { 
          if (now - lastEnemyMove > (1000/boss2Cfg.moveSpeed)) { 
            lastEnemyMove = now; 
            enemyFrontIndex--; 
            if (boss2Section < 3) { 
              if (enemyFrontIndex <= markerPos[boss2Section]) { 
                boss2State = B2_CHARGE; 
                bossActionTimer = now; 
              } 
            } 
            if (enemyFrontIndex <= 0) currentState = STATE_GAMEOVER; 
          } 
        } else if (boss2State == B2_CHARGE) { 
          if (now - bossActionTimer < (boss2Cfg.shotFreq * 100)) { 
            if (now % 100 < 20) boss2LockedColor = random(1,4); 
          } else { 
            boss2State = B2_SHOOT; 
            boss2ShotsFired = 0; 
            bossActionTimer = now; 
            int startRange = 0; int endRange = 0; 
            if (boss2Section == 0) { startRange=0; endRange=2; } 
            else if (boss2Section == 1) { startRange=0; endRange=5; } 
            else { startRange=0; endRange=8; } 
            for(auto &seg : bossSegments) { 
              if (seg.originalIndex >= startRange && seg.originalIndex <= endRange) seg.color = boss2LockedColor; 
            } 
          } 
        } else if (boss2State == B2_SHOOT) { 
          if (now - bossActionTimer > 150) { 
            bossActionTimer = now; 
            bossProjectiles.push_back({enemyFrontIndex, boss2LockedColor}); 
            boss2ShotsFired++; 
            if (boss2ShotsFired >= 10) { 
              int startRange = 0; int endRange = 0; 
              if (boss2Section == 0) { startRange=0; endRange=2; } 
              else if (boss2Section == 1) { startRange=3; endRange=5; } 
              else { startRange=0; endRange=8; } 
              for(auto &seg : bossSegments) { 
                if (seg.originalIndex >= startRange && seg.originalIndex <= endRange) seg.active = true; 
              } 
              boss2State = B2_MOVE; 
              boss2Section++; 
            } 
          } 
        } 
      }
      // Boss 3: RGB Overlord
      else if (currentBossType == 3) { 
        if (now - lastEnemyMove > (1000/boss3Cfg.moveSpeed)) { 
          lastEnemyMove = now; 
          enemyFrontIndex--; 
          if (enemyFrontIndex <= 0) currentState = STATE_GAMEOVER; 
        } 
      }
    }
    
    // RENDERING
    FastLED.clear();
    
    // Boss 2 Markers
    if (currentState == STATE_BOSS_PLAYING && currentBossType == 2) { 
      for(int i=0; i<3; i++) { 
        if (markerPos[i] < enemyFrontIndex) leds[markerPos[i]+1] = CRGB(50,0,0); 
      } 
    }
    
    // Draw Enemy / Boss
    if (currentState == STATE_PLAYING) { 
      for(int i=0; i<enemies.size(); i++) { 
        if (enemyFrontIndex+i < config_num_leds && enemyFrontIndex+i >=0) 
          leds[enemyFrontIndex+i+1] = getColor(enemies[i].color); 
      } 
    } 
    else if (currentState == STATE_BOSS_PLAYING) { 
      for(int i=0; i<bossSegments.size(); i++) { 
        int pos = enemyFrontIndex+i; 
        if (pos < config_num_leds && pos >=0) { 
          CRGB c = getColor(bossSegments[i].color); 
          
          // Boss 2 Visual Logic
          if (currentBossType == 2) { 
            c = CRGB(20,20,20); // Armor Grey
            if (boss2State == B2_MOVE) { 
              if (bossSegments[i].active) { 
                c = getColor(bossSegments[i].color); 
                if ((millis()/100)%2 == 0) c = CRGB::Black; // Blink active
              } 
            } else if (boss2State == B2_CHARGE || boss2State == B2_SHOOT) { 
              int oid = bossSegments[i].originalIndex; 
              bool highlight = false; 
              if (boss2Section == 0) { if (oid >= 0 && oid <= 2) highlight = true; } 
              else if (boss2Section == 1) { if (oid >= 0 && oid <= 5) highlight = true; } 
              else if (boss2Section >= 2) { highlight = true; } 
              if (highlight) c = getColor(boss2LockedColor); 
            } 
          } 
          leds[pos+1] = c; 
        } 
      } 
      // Boss Projectiles
      for(auto &p : bossProjectiles) { 
        if(p.pos >= 0 && p.pos < config_num_leds) leds[p.pos+1] = getColor(p.color); 
      } 
    }
    
    // Player Shots
    for(auto &s : shots) { 
      if(s.position >= 0 && s.position < config_num_leds) leds[s.position+1] = getColor(s.color); 
    }
    
    leds[1] = CRGB::White; // Player Position
    leds[0] = CRGB(20,0,0); // Sacrificial LED
    FastLED.show();
  }
}
