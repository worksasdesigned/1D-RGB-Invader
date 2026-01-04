#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <FastLED.h>
#include <vector>

// --------------------------------------------------------------------------
// KONFIGURATION HARDWARE (Lolin S2 Mini)
// --------------------------------------------------------------------------
#define PIN_LED_DATA 16
#define PIN_BTN_BLUE 3
#define PIN_BTN_RED  5
#define PIN_BTN_GREEN 7
#define PIN_BTN_WHITE 9 

#define MAX_LEDS 1000 
#define LED_TYPE WS2812B
#define COLOR_ORDER GRB
#define CONFIG_VERSION 5 // Erhöht, um Reset zu erzwingen

// --------------------------------------------------------------------------
// DATENSTRUKTUREN
// --------------------------------------------------------------------------
struct LevelConfig {
  int speed;      
  int length;     
  int bossType;   // 0=Normal, 1=Tank, 2=Masterblaster, 3=RGB Overlord
};

struct BossConfig {
  int moveSpeed;
  int shotSpeed;
  int hpPerLed;
  int shotFreq; 
};

struct Enemy { int color; };
struct BossSegment { int color; int hp; int maxHp; bool active; int originalIndex; };
struct Shot { int position; int color; };
struct BossProjectile { int pos; int color; };

// --------------------------------------------------------------------------
// GLOBALE VARIABLEN
// --------------------------------------------------------------------------
CRGB leds[MAX_LEDS];
Preferences preferences;
WebServer server(80);

// -- Profile & System Config --
String currentProfilePrefix = "def_"; // def_, kid_, pro_
String config_ssid = "";
String config_pass = "";

// Statische IP Config 
bool config_static_ip = false;
String config_ip = "";
String config_gateway = "";
String config_subnet = "";
String config_dns = "";

// -- Game Config --
int config_num_leds = 100;
int config_start_level = 1;
int config_brightness_pct = 50; 

LevelConfig levels[11];
BossConfig boss1Cfg; // The Tank (Logic ID 1)
BossConfig boss2Cfg; // Masterblaster (Logic ID 2)
BossConfig boss3Cfg; // RGB Overlord (Logic ID 3)

// Spielstatus
enum GameState { 
  STATE_MENU, STATE_INTRO, STATE_PLAYING, STATE_BOSS_PLAYING, 
  STATE_LEVEL_COMPLETED, STATE_GAME_FINISHED, STATE_GAMEOVER 
};
GameState currentState = STATE_MENU;

// Timer & Flags
unsigned long lastLoopTime = 0;
unsigned long stateTimer = 0; 
unsigned long lastShotMove = 0;
unsigned long lastEnemyMove = 0;
unsigned long lastFireTime = 0;
unsigned long bossActionTimer = 0; 
bool buttonsReleased = true; 
const int FIRE_COOLDOWN = 100; 

// Entities
std::vector<Enemy> enemies; 
std::vector<Shot> shots;
std::vector<BossSegment> bossSegments; 
std::vector<BossProjectile> bossProjectiles; 

int enemyFrontIndex = -1; 
int currentLevel = 1;
int currentBossType = 0;

// Boss 2 (Masterblaster) Variablen
enum Boss2State { B2_MOVE, B2_CHARGE, B2_SHOOT };
Boss2State boss2State = B2_MOVE;
int boss2Section = 0; 
int boss2ShotsFired = 0;
int boss2LockedColor = 1; 
int markerPos[3]; 

// --------------------------------------------------------------------------
// HILFSFUNKTIONEN
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

void flashPixel(int pos) {
  if(pos >= 0 && pos < config_num_leds) {
    leds[pos] = CRGB::White;
    FastLED.show();
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
// LEVEL START & INTRO
// --------------------------------------------------------------------------
void startLevelIntro(int level) {
  currentLevel = level;
  currentState = STATE_INTRO;
  stateTimer = millis();
  
  FastLED.clear();
  fill_solid(leds, config_num_leds, CRGB(10,10,10)); // Gedimmt Weiß
  
  CRGB barColor = CRGB::Green;
  if (levels[level].bossType > 0) barColor = CRGB::Red;

  int center = config_num_leds / 2;
  int totalWidth = (level * 6) + ((level-1)*4); 
  int startPos = center - (totalWidth/2);
  
  if(startPos < 0) startPos = 0;
  
  int cursor = startPos;
  for(int i=0; i<level; i++) {
    for(int k=0; k<6; k++) {
      if(cursor < config_num_leds) leds[cursor] = barColor;
      cursor++;
    }
    cursor += 4; 
  }
  FastLED.show();
}

void updateLevelIntro() {
  unsigned long elapsed = millis() - stateTimer;
  
  if (elapsed > 2000 && elapsed < 4000) {
    uint8_t bright = map(config_brightness_pct, 10, 100, 25, 255);
    if ((elapsed / 250) % 2 == 0) FastLED.setBrightness(bright / 4);
    else FastLED.setBrightness(bright);
    FastLED.show();
  } 

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
      
      // THE TANK (ID 1)
      if (currentBossType == 1) {
        for(int i=0; i<3; i++) bossSegments.push_back({3, boss1Cfg.hpPerLed, boss1Cfg.hpPerLed, true, 0});
        for(int i=0; i<3; i++) bossSegments.push_back({1, boss1Cfg.hpPerLed, boss1Cfg.hpPerLed, true, 0});
        for(int i=0; i<3; i++) bossSegments.push_back({3, boss1Cfg.hpPerLed, boss1Cfg.hpPerLed, true, 0});
        bossActionTimer = millis();
      } 
      // MASTERBLASTER (ID 2)
      else if (currentBossType == 2) {
        for(int i=0; i<9; i++) {
           bossSegments.push_back({0, boss2Cfg.hpPerLed, boss2Cfg.hpPerLed, false, i});
        }
        boss2Section = 0;
        boss2State = B2_MOVE;
        markerPos[0] = config_num_leds - 12; 
        markerPos[1] = (int)(config_num_leds * 0.65);
        markerPos[2] = (int)(config_num_leds * 0.40);
      }
      // RGB OVERLORD (ID 3)
      else if (currentBossType == 3) {
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
      for (int i = 0; i < count; i++) {
        enemies.push_back({(int)random(1, 4)});
      }
      enemyFrontIndex = config_num_leds - 1;
      currentState = STATE_PLAYING;
    }
  }
}

// --------------------------------------------------------------------------
// PROJEKTILE BEWEGEN
// --------------------------------------------------------------------------
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
// WEBSERVER
// --------------------------------------------------------------------------
String getHTML() {
  String h = "<!DOCTYPE html><html><head>";
  h += "<meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  h += "<style>";
  h += "body{font-family:sans-serif;background:#1a1a1a;color:#eee;padding:10px;max-width:800px;margin:auto;}";
  h += "input,select,button{width:100%;background:#333;color:#fff;border:1px solid #555;padding:8px;border-radius:4px;box-sizing:border-box;margin-bottom:5px;}";
  h += "table{width:100%;border-collapse:collapse;margin-bottom:10px;} td,th{border:1px solid #444;padding:6px;text-align:center;}";
  h += ".sec{margin-top:25px;border-top:1px solid #555;padding-top:15px;background:#222;padding:15px;border-radius:5px;}";
  h += "#warning-box{margin-top:10px;padding:10px;background:#b30000;color:#fff;border:1px solid #ff0000;display:none;font-weight:bold;border-radius:4px;}";
  h += ".val-highlight{color:#0f0;font-weight:bold;}";
  h += "h2,h3{margin-top:0;} pre{font-family:monospace;color:#0f0;font-size:12px;overflow-x:auto;}";
  h += "@media (max-width: 600px) { table, thead, tbody, th, td, tr { display: block; } tr { margin-bottom: 10px; border: 1px solid #444; } td { text-align: right; padding-left: 50%; position: relative; } td::before { content: attr(data-label); position: absolute; left: 10px; font-weight: bold; text-align: left; } }";
  h += "</style>";
  
  h += "<script>";
  h += "function updateCalc() {";
  h += "  var count = parseInt(document.getElementById('ledCount').value) || 0;";
  h += "  var brightPct = parseInt(document.getElementById('brightness').value) || 50;";
  h += "  document.getElementById('brightVal').innerText = brightPct + '%';";
  h += "  var warn = document.getElementById('warning-box');";
  h += "  if (count > 0) {";
  h += "    var brightFactor = brightPct / 100.0;";
  h += "    var amps = ((count * 60 * brightFactor) + 120) / 1000;"; 
  h += "    document.getElementById('ampValue').innerText = amps.toFixed(2) + ' A';";
  h += "    if(amps > 5.0) { warn.style.display='block'; warn.innerText='ACHTUNG: > 5A! Hochleistungsnetzteil erforderlich!'; }";
  h += "    else warn.style.display='none';";
  h += "  } }";
  h += "function toggleIP() { var x = document.getElementById('ipsettings'); if(document.getElementById('chkStatic').checked) x.style.display='block'; else x.style.display='none'; }";
  h += "function confirmReset() { return confirm('Wirklich alle Einstellungen löschen und Werkseinstellungen laden?'); }";
  h += "window.onload = function(){ updateCalc(); toggleIP(); };";
  h += "</script>";
  h += "</head><body>";

  h += "<pre>";
  h += " _   _  _  _____  _                 _   \n";
  h += "| | | || ||_   _|(_)               | |  \n";
  h += "| | | || |  | |   _  _ __ __   __ _| |  \n";
  h += "| | | || |  | |  | || '_ \\\\ \\ / / _` | |\n";
  h += "| |_| || |__| |  | || | | |\\ V / (_| |_|\n";
  h += " \\___/ |_____/   |_||_| |_| \\_/ \\__,_(_)\n";
  h += "      ULTIMATE LED INVADERS\n";
  h += "</pre>";

  String pName = (currentProfilePrefix == "def_") ? "Standard" : ((currentProfilePrefix == "kid_") ? "Kids" : "Pro");
  h += "<div class='sec'><h3>Profil Verwaltung</h3>";
  h += "Aktuelles Profil: <b>" + pName + "</b><br>";
  h += "<form action='/loadprofile' method='POST' style='display:flex;gap:5px;margin-top:5px;'>";
  h += "<select name='profile'>";
  h += "<option value='def' " + String(currentProfilePrefix=="def_"?"selected":"") + ">Standard</option>";
  h += "<option value='kid' " + String(currentProfilePrefix=="kid_"?"selected":"") + ">Kids</option>";
  h += "<option value='pro' " + String(currentProfilePrefix=="pro_"?"selected":"") + ">Pro/Party</option>";
  h += "</select><button type='submit'>Profil Laden</button></form></div>";

  h += "<form action='/save' method='POST'>";
  
  h += "<div class='sec'><h3>Hardware & Allgemein</h3>";
  h += "Start Level: <input type='number' name='startlvl' min='1' max='10' value='" + String(config_start_level) + "'><br>";
  h += "LEDs Gesamt (Empfohlen: 240): <input id='ledCount' type='number' name='leds' value='" + String(config_num_leds) + "' oninput='updateCalc()'><br>";
  h += "<label>Standard Helligkeit: <span id='brightVal'>" + String(config_brightness_pct) + "%</span> (Empfohlen >75%)</label>";
  h += "<input id='brightness' type='range' name='bright' min='10' max='100' value='" + String(config_brightness_pct) + "' oninput='updateCalc()'>";
  h += "<div style='margin-top:5px;'>Max. Strombedarf (Weiß): <span id='ampValue' class='val-highlight'>0.00 A</span></div>";
  h += "<div id='warning-box'></div>";
  h += "</div>";
  
  h += "<div class='sec'><h3>Netzwerk</h3>";
  h += "<label>WLAN SSID:</label><select name='ssid'>";
  int n = WiFi.scanNetworks();
  if (n == 0) h += "<option value=''>Keine Netzwerke</option>";
  else {
    for (int i = 0; i < n; ++i) {
      String s = WiFi.SSID(i);
      String sel = (s == config_ssid) ? "selected" : "";
      h += "<option value='" + s + "' " + sel + ">" + s + " (" + WiFi.RSSI(i) + "dBm)</option>";
    }
  }
  h += "</select>";
  h += "Passwort: <input type='password' name='pass' value='" + config_pass + "'><br>";
  h += "<input type='checkbox' id='chkStatic' name='static_ip' value='1' onchange='toggleIP()' " + String(config_static_ip ? "checked" : "") + " style='width:auto;'> Statische IP nutzen<br>";
  h += "<div id='ipsettings' style='display:none;margin-top:10px;'>";
  h += "IP: <input name='ip' value='" + config_ip + "' placeholder='192.168.178.200'>";
  h += "Gateway: <input name='gw' value='" + config_gateway + "' placeholder='192.168.178.1'>";
  h += "Subnet: <input name='sn' value='" + config_subnet + "' placeholder='255.255.255.0'>";
  h += "DNS: <input name='dns' value='" + config_dns + "' placeholder='8.8.8.8'>";
  h += "</div></div>";
  
  h += "<div class='sec'><h3>Level Config</h3><table><thead><tr><th>Lvl</th><th>Speed</th><th>Len</th><th>Boss?</th></tr></thead><tbody>";
  for(int i=1; i<=10; i++) {
    h += "<tr><td data-label='Level'>" + String(i) + "</td>";
    h += "<td data-label='Speed'><input name='lspd" + String(i) + "' value='" + String(levels[i].speed) + "'></td>";
    h += "<td data-label='Len/Type'><input name='llen" + String(i) + "' value='" + String(levels[i].length) + "'></td>";
    h += "<td data-label='Boss'><select name='lboss" + String(i) + "'>";
    h += "<option value='0' " + String(levels[i].bossType==0?"selected":"") + ">-</option>";
    h += "<option value='2' " + String(levels[i].bossType==2?"selected":"") + ">Masterblaster</option>"; 
    h += "<option value='1' " + String(levels[i].bossType==1?"selected":"") + ">The Tank</option>";      
    h += "<option value='3' " + String(levels[i].bossType==3?"selected":"") + ">RGB Overlord</option>";      
    h += "</select></td></tr>";
  }
  h += "</tbody></table></div>";

  h += "<div class='sec'><h3>Masterblaster (ID 2)</h3>";
  h += "Speed: <input name='b2mv' value='" + String(boss2Cfg.moveSpeed) + "'> ";
  h += "ShotSpd: <input name='b2ss' value='" + String(boss2Cfg.shotSpeed) + "'> ";
  h += "HP/LED: <input name='b2hp' value='" + String(boss2Cfg.hpPerLed) + "'> ";
  h += "Ladezeit(0.1s): <input name='b2fr' value='" + String(boss2Cfg.shotFreq) + "'>";
  h += "</div>";

  h += "<div class='sec'><h3>The Tank (ID 1)</h3>";
  h += "Speed: <input name='b1mv' value='" + String(boss1Cfg.moveSpeed) + "'> ";
  h += "ShotSpd: <input name='b1ss' value='" + String(boss1Cfg.shotSpeed) + "'> ";
  h += "HP/LED: <input name='b1hp' value='" + String(boss1Cfg.hpPerLed) + "'> ";
  h += "Freq(0.1s): <input name='b1fr' value='" + String(boss1Cfg.shotFreq) + "'>";
  h += "</div>";

  h += "<div class='sec'><h3>RGB Overlord (ID 3)</h3>";
  h += "Speed: <input name='b3mv' value='" + String(boss3Cfg.moveSpeed) + "'> ";
  h += "HP/LED: <input name='b3hp' value='" + String(boss3Cfg.hpPerLed) + "'> ";
  h += "</div>";

  h += "<br><input type='submit' value='ALLES SPEICHERN' style='width:100%;background:#009900;padding:15px;font-size:1.2em;cursor:pointer;font-weight:bold;'>";
  h += "</form>";
  
  h += "<br><br><form action='/reset' method='POST' onsubmit='return confirmReset()'>";
  h += "<button type='submit' style='background:#990000;padding:10px;'>WERKSEINSTELLUNGEN LADEN (RESET)</button>";
  h += "</form>";
  
  h += "</body></html>";
  return h;
}

void setupDefaultConfig() {
  for(int i=1; i<=10; i++) {
    levels[i].speed = 3 + i; 
    levels[i].length = 10 + (i*5);
    levels[i].bossType = 0;
  }
  
  levels[7].speed = 15; levels[7].length = 20;
  levels[8].speed = 15; levels[8].length = 25;
  levels[9].speed = 12; levels[9].length = 50;

  levels[3].bossType = 2; // Masterblaster
  levels[6].bossType = 1; // The Tank
  levels[10].bossType = 3;// RGB Overlord

  boss1Cfg = {4, 60, 3, 35};  
  boss2Cfg = {10, 60, 5, 40}; 
  boss3Cfg = {10, 0, 4, 0};   
}

void loadConfig(String prefix) {
  preferences.begin("game", true);
  
  config_num_leds = preferences.getInt((prefix+"leds").c_str(), 100);
  config_brightness_pct = preferences.getInt((prefix+"bright").c_str(), 50);
  config_start_level = preferences.getInt((prefix+"startlvl").c_str(), 1);
  
  // Wifi Settings lesen (diese werden beim Version Reset NICHT gelöscht, da separat gespeichert)
  config_ssid = preferences.getString("ssid", "");
  config_pass = preferences.getString("pass", "");
  config_static_ip = preferences.getBool("sip_on", false);
  config_ip = preferences.getString("sip_ip", "");
  config_gateway = preferences.getString("sip_gw", "");
  config_subnet = preferences.getString("sip_sn", "");
  config_dns = preferences.getString("sip_dns", "");
  
  for(int i=1; i<=10; i++) {
    // FALLBACK: Wenn Key nicht existiert, nutze den korrekten Boss für das Level
    int defBoss = 0;
    if(i==3) defBoss = 2;
    if(i==6) defBoss = 1;
    if(i==10) defBoss = 3;
    
    levels[i].speed = preferences.getInt((prefix+"l"+String(i)+"s").c_str(), 4+i);
    levels[i].length = preferences.getInt((prefix+"l"+String(i)+"l").c_str(), 10+(i*5));
    levels[i].bossType = preferences.getInt((prefix+"l"+String(i)+"b").c_str(), defBoss);
  }
  
  if(preferences.isKey((prefix+"b1").c_str())) preferences.getBytes((prefix+"b1").c_str(), &boss1Cfg, sizeof(BossConfig));
  else boss1Cfg = {4, 60, 3, 35}; 

  if(preferences.isKey((prefix+"b2").c_str())) preferences.getBytes((prefix+"b2").c_str(), &boss2Cfg, sizeof(BossConfig));
  else boss2Cfg = {10, 60, 5, 40}; 

  if(preferences.isKey((prefix+"b3").c_str())) preferences.getBytes((prefix+"b3").c_str(), &boss3Cfg, sizeof(BossConfig));
  else boss3Cfg = {10, 0, 4, 0}; 

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
  } else {
    server.send(400, "text/plain", "Bad Request");
  }
}

void handleReset() {
  preferences.begin("game", false);
  // Wir löschen nur die Game-Settings, nicht Wifi!
  // Da Prefs aber alles mischt, müssen wir Wifi retten
  String s = preferences.getString("ssid", "");
  String p = preferences.getString("pass", "");
  bool sip = preferences.getBool("sip_on", false);
  String ip = preferences.getString("sip_ip", "");
  String gw = preferences.getString("sip_gw", "");
  String sn = preferences.getString("sip_sn", "");
  String dns = preferences.getString("sip_dns", "");

  preferences.clear(); 
  
  // Wifi zurückschreiben
  preferences.putString("ssid", s);
  preferences.putString("pass", p);
  preferences.putBool("sip_on", sip);
  preferences.putString("sip_ip", ip);
  preferences.putString("sip_gw", gw);
  preferences.putString("sip_sn", sn);
  preferences.putString("sip_dns", dns);
  
  // Version neu setzen
  preferences.putInt("version", CONFIG_VERSION);
  preferences.end();
  
  server.send(200, "text/html", "<h2>Reset erfolgreich!</h2><p>Lade Defaults... ESP Neustart.</p>");
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
  config_ip = server.arg("ip");
  config_gateway = server.arg("gw");
  config_subnet = server.arg("sn");
  config_dns = server.arg("dns");
  
  preferences.begin("game", false);
  preferences.putString("ssid", config_ssid);
  preferences.putString("pass", config_pass);
  preferences.putBool("sip_on", config_static_ip);
  preferences.putString("sip_ip", config_ip);
  preferences.putString("sip_gw", config_gateway);
  preferences.putString("sip_sn", config_subnet);
  preferences.putString("sip_dns", config_dns);

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

  boss1Cfg.moveSpeed = server.arg("b1mv").toInt();
  boss1Cfg.shotSpeed = server.arg("b1ss").toInt();
  boss1Cfg.hpPerLed = server.arg("b1hp").toInt();
  boss1Cfg.shotFreq = server.arg("b1fr").toInt();
  preferences.putBytes((p+"b1").c_str(), &boss1Cfg, sizeof(BossConfig));

  boss2Cfg.moveSpeed = server.arg("b2mv").toInt();
  boss2Cfg.shotSpeed = server.arg("b2ss").toInt();
  boss2Cfg.hpPerLed = server.arg("b2hp").toInt();
  boss2Cfg.shotFreq = server.arg("b2fr").toInt();
  preferences.putBytes((p+"b2").c_str(), &boss2Cfg, sizeof(BossConfig));

  boss3Cfg.moveSpeed = server.arg("b3mv").toInt();
  boss3Cfg.shotSpeed = 0; 
  boss3Cfg.hpPerLed = server.arg("b3hp").toInt();
  preferences.putBytes((p+"b3").c_str(), &boss3Cfg, sizeof(BossConfig));

  preferences.end();
  server.send(200, "text/html", "<h2>Gespeichert!</h2><p>ESP startet neu...</p><a href='/'>Zurueck</a>");
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

  // Version Check für Auto-Reset
  preferences.begin("game", false);
  int storedVer = preferences.getInt("version", 0);
  preferences.end();

  if (storedVer < CONFIG_VERSION) {
    Serial.println("Neue Version erkannt! Führe Reset durch (behalte WiFi)...");
    handleReset(); // Setzt alles auf Defaults
  }

  preferences.begin("game", true);
  currentProfilePrefix = preferences.getString("act_prof", "def_");
  preferences.end();

  loadConfig(currentProfilePrefix);         

  FastLED.addLeds<LED_TYPE, PIN_LED_DATA, COLOR_ORDER>(leds, MAX_LEDS);
  FastLED.setBrightness(map(config_brightness_pct, 10, 100, 25, 255));
  FastLED.clear();
  FastLED.show();

  WiFi.mode(WIFI_AP_STA);
  
  if(config_static_ip && config_ip.length() > 0) {
     IPAddress ip, gw, sn, dns;
     if(ip.fromString(config_ip) && gw.fromString(config_gateway) && sn.fromString(config_subnet)) {
        if(config_dns.length() > 0) dns.fromString(config_dns);
        else dns.fromString("8.8.8.8");
        if(!WiFi.config(ip, gw, sn, dns)) Serial.println("Static IP Failed");
     }
  }

  if(config_ssid != "") {
    WiFi.begin(config_ssid.c_str(), config_pass.c_str());
    unsigned long startWifi = millis();
    while(WiFi.status() != WL_CONNECTED && millis() - startWifi < 10000) delay(100);
  }

  WiFi.softAP("ESP32-Invader-Ult", "12345678"); 
  
  server.on("/", []() { server.send(200, "text/html", getHTML()); });
  server.on("/save", handleSave);
  server.on("/loadprofile", handleProfileSwitch);
  server.on("/reset", handleReset); 
  server.begin();

  startLevelIntro(config_start_level);
}

// --------------------------------------------------------------------------
// MAIN LOOP
// --------------------------------------------------------------------------
void loop() {
  server.handleClient();
  unsigned long now = millis();

  if (digitalRead(PIN_BTN_WHITE) == LOW) {
    delay(200); 
    startLevelIntro(config_start_level);
    return;
  }

  if (currentState == STATE_LEVEL_COMPLETED) {
    fill_solid(leds, config_num_leds, CRGB::Green);
    FastLED.show();
    if (now - stateTimer > 2000) startLevelIntro(currentLevel + 1);
    return;
  }

  if (currentState == STATE_GAME_FINISHED) {
    fill_rainbow(leds, config_num_leds, (now / 20), 7); 
    FastLED.show();
    return;
  }

  if (currentState == STATE_GAMEOVER) {
    fill_solid(leds, config_num_leds, CRGB::Red);
    FastLED.show();
    return;
  }

  if (currentState == STATE_INTRO) {
    updateLevelIntro();
    return;
  }

  if (currentState == STATE_PLAYING || currentState == STATE_BOSS_PLAYING) {
    bool isAnyBtnPressed = (digitalRead(PIN_BTN_BLUE) == LOW || 
                            digitalRead(PIN_BTN_RED) == LOW || 
                            digitalRead(PIN_BTN_GREEN) == LOW);
    if (!isAnyBtnPressed) buttonsReleased = true;

    if (isAnyBtnPressed && buttonsReleased && (now - lastFireTime > FIRE_COOLDOWN)) {
       int c = 0;
       if (currentBossType == 3) {
          delay(50);
          bool b = (digitalRead(PIN_BTN_BLUE) == LOW);
          bool r = (digitalRead(PIN_BTN_RED) == LOW);
          bool g = (digitalRead(PIN_BTN_GREEN) == LOW);
          if (r && g && b) c = 7;      
          else if (r && g) c = 4;      
          else if (r && b) c = 5;      
          else if (g && b) c = 6;      
          else if (b) c = 1; 
          else if (r) c = 2;
          else if (g) c = 3;
       } else {
          if (digitalRead(PIN_BTN_BLUE) == LOW) c = 1;
          else if (digitalRead(PIN_BTN_RED) == LOW) c = 2;
          else if (digitalRead(PIN_BTN_GREEN) == LOW) c = 3;
       }
       if (c > 0) {
         shots.push_back({0, c});
         lastFireTime = now;
         buttonsReleased = false; 
       }
    }

    if (now - lastShotMove > (1000/90)) {
      lastShotMove = now;
      for (int i = shots.size() - 1; i >= 0; i--) {
        shots[i].position++;
        bool remove = false;
        if (currentState == STATE_PLAYING) {
          if (shots[i].position >= enemyFrontIndex && !enemies.empty()) {
            if (shots[i].color == enemies[0].color) {
              enemies.erase(enemies.begin());
              enemyFrontIndex++;
              flashPixel(shots[i].position);
              remove = true;
              checkWinCondition(); 
            } else {
              enemies.insert(enemies.begin(), {shots[i].color});
              enemyFrontIndex--;
              remove = true;
            }
          }
        } 
        else if (currentState == STATE_BOSS_PLAYING) {
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
    
    if (currentState == STATE_PLAYING) {
      unsigned long delay = 1000 / levels[currentLevel].speed;
      if (now - lastEnemyMove > delay) {
        lastEnemyMove = now;
        enemyFrontIndex--;
        if (enemyFrontIndex <= 0) currentState = STATE_GAMEOVER;
      }
    }
    else if (currentState == STATE_BOSS_PLAYING) {
      int pSpeed = 60; 
      if (currentBossType == 1) pSpeed = boss1Cfg.shotSpeed;
      if (currentBossType == 2) pSpeed = boss2Cfg.shotSpeed;
      
      moveBossProjectiles(pSpeed);

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
        }
        else if (boss2State == B2_CHARGE) {
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
        }
        else if (boss2State == B2_SHOOT) {
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
      else if (currentBossType == 3) {
         if (now - lastEnemyMove > (1000/boss3Cfg.moveSpeed)) {
          lastEnemyMove = now;
          enemyFrontIndex--;
          if (enemyFrontIndex <= 0) currentState = STATE_GAMEOVER;
        }
      }
    }

    FastLED.clear();
    if (currentState == STATE_BOSS_PLAYING && currentBossType == 2) {
      for(int i=0; i<3; i++) {
         if (markerPos[i] < enemyFrontIndex) leds[markerPos[i]] = CRGB(50,0,0);
      }
    }
    if (currentState == STATE_PLAYING) {
      for(int i=0; i<enemies.size(); i++) {
        if (enemyFrontIndex+i < config_num_leds && enemyFrontIndex+i >=0)
          leds[enemyFrontIndex+i] = getColor(enemies[i].color);
      }
    } 
    else if (currentState == STATE_BOSS_PLAYING) {
      for(int i=0; i<bossSegments.size(); i++) {
        int pos = enemyFrontIndex+i;
        if (pos < config_num_leds && pos >=0) {
           CRGB c = getColor(bossSegments[i].color);
           if (currentBossType == 2) {
              c = CRGB(20,20,20); 
              if (boss2State == B2_MOVE) {
                 if (bossSegments[i].active) {
                    c = getColor(bossSegments[i].color);
                    if ((millis()/100)%2 == 0) c = CRGB::Black;
                 }
              }
              else if (boss2State == B2_CHARGE || boss2State == B2_SHOOT) {
                 int oid = bossSegments[i].originalIndex;
                 bool highlight = false;
                 if (boss2Section == 0) { if (oid >= 0 && oid <= 2) highlight = true; }
                 else if (boss2Section == 1) { if (oid >= 0 && oid <= 5) highlight = true; } 
                 else if (boss2Section >= 2) { highlight = true; }
                 if (highlight) c = getColor(boss2LockedColor);
              }
           }
           leds[pos] = c;
        }
      }
      for(auto &p : bossProjectiles) {
        if(p.pos >= 0 && p.pos < config_num_leds) leds[p.pos] = getColor(p.color);
      }
    }
    for(auto &s : shots) {
      if(s.position >= 0 && s.position < config_num_leds) leds[s.position] = getColor(s.color);
    }
    leds[0] = CRGB::White;
    FastLED.show();
  }
}
