// ==========================================================================
// PROJECT: ULTIMATE RGB INVADERS
// VERSION: 3.2 (Final Polish: Power Save & Death Anim)
// AUTHOR: Qwer.Tzui / worksasdesigned
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
#define CONFIG_VERSION  15 // Version 15: Boss 2 update & New Game Over Logic

#define FRAME_DELAY     33 
#define INPUT_BUFFER_MS 60 

const int FIRE_COOLDOWN = 100;

// --------------------------------------------------------------------------
// USER CONFIGURATION (DEFAULTS)
// --------------------------------------------------------------------------
int config_num_leds = 100; 
int config_brightness_pct = 50; 
int config_start_level = 1;
bool config_sacrifice_led = true; 
int config_homebase_size = 3;     
int config_shot_speed_pct = 100; 
int ledStartOffset = 1; 

// --------------------------------------------------------------------------
// DATA STRUCTURES
// --------------------------------------------------------------------------
struct LevelConfig { int speed; int length; int bossType; };
struct BossConfig { int moveSpeed; int shotSpeed; int hpPerLed; int shotFreq; int burstCount; int m1; int m2; int m3; };
struct Enemy { int color; };
struct BossSegment { int color; int hp; int maxHp; bool active; int originalIndex; };
struct Shot { int position; int color; };
struct BossProjectile { int pos; int color; };

// --------------------------------------------------------------------------
// GLOBAL VARIABLES
// --------------------------------------------------------------------------
CRGB leds[MAX_LEDS];
Preferences preferences;
WebServer server(80);

String currentProfilePrefix = "def_"; 
String config_ssid = ""; String config_pass = "";
bool config_static_ip = false; String config_ip = ""; String config_gateway = ""; String config_subnet = ""; String config_dns = "";
bool wifiMode = false; 

LevelConfig levels[11];
BossConfig boss1Cfg; BossConfig boss2Cfg; BossConfig boss3Cfg; 

enum GameState { 
  STATE_MENU, 
  STATE_INTRO, 
  STATE_PLAYING, 
  STATE_BOSS_PLAYING, 
  STATE_LEVEL_COMPLETED, 
  STATE_GAME_FINISHED, 
  STATE_BASE_DESTROYED, // NEW: Transition before Game Over
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

// Input Buffer
unsigned long btnWhitePressTime = 0;
bool btnWhiteHeld = false;
unsigned long comboTimer = 0;    
bool isWaitingForCombo = false; 

// Game Logic
std::vector<Enemy> enemies; 
std::vector<Shot> shots;
std::vector<BossSegment> bossSegments; 
std::vector<BossProjectile> bossProjectiles; 
int enemyFrontIndex = -1; 
int currentLevel = 1;
int currentBossType = 0;

// Boss Vars
enum Boss2State { B2_MOVE, B2_CHARGE, B2_SHOOT };
Boss2State boss2State = B2_MOVE;
int boss2Section = 0; int boss2ShotsFired = 0; int boss2LockedColor = 1; int markerPos[3]; 

enum Boss3State { B3_MOVE, B3_PHASE_CHANGE, B3_BURST };
Boss3State boss3State = B3_MOVE;
bool boss3PhaseTriggered = false; int boss3BurstCounter = 0; int boss3MarkerPos = 0;

// HIGHSCORE SYSTEM
int currentScore = 0;
int highScore = 0;
int lastGames[3] = {0, 0, 0};
unsigned long levelStartTime = 0;
int levelMaxPossibleScore = 0; 
int levelAchievedScore = 0;    

// --------------------------------------------------------------------------
// HELPER FUNCTIONS
// --------------------------------------------------------------------------
CRGB getColor(int colorCode) {
  switch (colorCode) {
    case 1: return CRGB::Blue; case 2: return CRGB::Red; case 3: return CRGB::Green;
    case 4: return CRGB::Yellow; case 5: return CRGB::Magenta; case 6: return CRGB::Cyan; case 7: return CRGB::White;   
    default: return CRGB::Black;
  }
}

void flashPixel(int pos) {
  if(pos >= 0 && pos < config_num_leds) leds[pos + ledStartOffset] = CRGB::White; 
}

// --------------------------------------------------------------------------
// SCORE & SAVE LOGIC
// --------------------------------------------------------------------------
void saveHighscores() {
  preferences.begin("game", false);
  preferences.putInt((currentProfilePrefix + "hs").c_str(), highScore);
  preferences.putInt((currentProfilePrefix + "l1").c_str(), lastGames[0]);
  preferences.putInt((currentProfilePrefix + "l2").c_str(), lastGames[1]);
  preferences.putInt((currentProfilePrefix + "l3").c_str(), lastGames[2]);
  preferences.end();
}

void loadHighscores() {
  preferences.begin("game", true);
  highScore = preferences.getInt((currentProfilePrefix + "hs").c_str(), 0);
  lastGames[0] = preferences.getInt((currentProfilePrefix + "l1").c_str(), 0);
  lastGames[1] = preferences.getInt((currentProfilePrefix + "l2").c_str(), 0);
  lastGames[2] = preferences.getInt((currentProfilePrefix + "l3").c_str(), 0);
  preferences.end();
}

void registerGameEnd(int finalScore) {
  lastGames[2] = lastGames[1];
  lastGames[1] = lastGames[0];
  lastGames[0] = finalScore;
  if (finalScore > highScore) highScore = finalScore;
  saveHighscores();
}

// Trigger transition to Game Over Sequence
void triggerBaseDestruction() {
  registerGameEnd(currentScore);
  currentState = STATE_BASE_DESTROYED;
  stateTimer = millis();
}

void calculateLevelScore() {
  unsigned long duration = millis() - levelStartTime;
  int entityCount = 0;
  
  if (levels[currentLevel].bossType == 0) entityCount = levels[currentLevel].length;
  else {
    if (levels[currentLevel].bossType == 1) entityCount = 9 * boss1Cfg.hpPerLed;
    else if (levels[currentLevel].bossType == 2) entityCount = 9 * boss2Cfg.hpPerLed;
    else if (levels[currentLevel].bossType == 3) entityCount = 15 * boss3Cfg.hpPerLed;
  }

  int levelMultiplier = currentLevel; 
  int basePoints = entityCount * 100 * levelMultiplier;

  unsigned long targetTime = 0;
  if (levels[currentLevel].bossType == 2) targetTime = 38000; 
  else {
     unsigned long travelTime = config_num_leds * 15; 
     unsigned long processingTime = entityCount * 300; 
     targetTime = 3000 + travelTime + processingTime;
  }

  int timeBonus = 0;
  if (levels[currentLevel].bossType == 3) timeBonus = basePoints; 
  else {
     if (duration <= targetTime) timeBonus = basePoints; 
     else {
       float ratio = (float)targetTime / (float)duration;
       timeBonus = (int)(basePoints * ratio);
     }
  }

  levelAchievedScore = basePoints + timeBonus;
  levelMaxPossibleScore = basePoints * 2; 
  currentScore += levelAchievedScore;
}

void checkWinCondition() {
  bool won = false;
  if (currentState == STATE_PLAYING && enemies.empty()) won = true;
  if (currentState == STATE_BOSS_PLAYING && bossSegments.empty()) won = true;
  
  if (won) {
    calculateLevelScore(); 
    if (currentLevel >= 10) {
      registerGameEnd(currentScore);
      currentState = STATE_GAME_FINISHED;
    } else {
      currentState = STATE_LEVEL_COMPLETED;
      stateTimer = millis();
    }
  }
}

// --------------------------------------------------------------------------
// ANIMATIONS
// --------------------------------------------------------------------------
void startLevelIntro(int level) {
  if (level == config_start_level) currentScore = 0; 
  currentLevel = level; currentState = STATE_INTRO; stateTimer = millis();
  FastLED.clear();
  for(int i=0; i<config_num_leds; i++) leds[i+ledStartOffset] = CRGB(10,10,10);
  CRGB barColor = levels[level].bossType > 0 ? CRGB::Red : CRGB::Green;
  int center = config_num_leds / 2;
  int totalWidth = (level * 6) + ((level-1)*4); 
  int startPos = center - (totalWidth/2); if(startPos < 0) startPos = 0;
  int cursor = startPos;
  for(int i=0; i<level; i++) {
    for(int k=0; k<6; k++) { if(cursor < config_num_leds) leds[cursor + ledStartOffset] = barColor; cursor++; }
    cursor += 4; 
  }
  if(config_sacrifice_led) leds[0] = CRGB(20, 0, 0); 
  FastLED.show();
}

void drawLevelIntro(int level) {
  FastLED.clear();
  for(int i=0; i<config_num_leds; i++) leds[i+ledStartOffset] = CRGB(5,5,5); 
  CRGB barColor = levels[level].bossType > 0 ? CRGB::Red : CRGB::Green;
  int center = config_num_leds / 2;
  int totalWidth = (level * 6) + ((level-1)*4); 
  int startPos = center - (totalWidth/2); if(startPos < 0) startPos = 0;
  int cursor = startPos;
  for(int i=0; i<level; i++) {
    for(int k=0; k<6; k++) { if(cursor < config_num_leds) leds[cursor + ledStartOffset] = barColor; cursor++; }
    cursor += 4; 
  }
  if(config_sacrifice_led) leds[0] = CRGB(20, 0, 0);
  FastLED.show();
}

void updateLevelIntro() {
  unsigned long elapsed = millis() - stateTimer;
  if (elapsed > 2000 && elapsed < 4000) {
    if ((elapsed / 250) % 2 == 0) drawLevelIntro(currentLevel); 
    else { FastLED.clear(); if(config_sacrifice_led) leds[0] = CRGB(20, 0, 0); FastLED.show(); }
  } else if (elapsed <= 2000) { drawLevelIntro(currentLevel); }

  if (elapsed >= 4000) {
    uint8_t bright = map(config_brightness_pct, 10, 100, 25, 255);
    FastLED.setBrightness(bright);
    levelStartTime = millis(); 
    
    if (levels[currentLevel].bossType > 0) {
      currentBossType = levels[currentLevel].bossType;
      bossSegments.clear(); enemies.clear(); shots.clear(); bossProjectiles.clear();
      enemyFrontIndex = config_num_leds - 1; 
      
      if (currentBossType == 1) {
        for(int i=0; i<3; i++) bossSegments.push_back({3, boss1Cfg.hpPerLed, boss1Cfg.hpPerLed, true, 0});
        for(int i=0; i<3; i++) bossSegments.push_back({1, boss1Cfg.hpPerLed, boss1Cfg.hpPerLed, true, 0});
        for(int i=0; i<3; i++) bossSegments.push_back({3, boss1Cfg.hpPerLed, boss1Cfg.hpPerLed, true, 0});
        bossActionTimer = millis();
      } 
      else if (currentBossType == 2) {
        for(int i=0; i<9; i++) bossSegments.push_back({0, boss2Cfg.hpPerLed, boss2Cfg.hpPerLed, false, i});
        boss2Section = 0; boss2State = B2_MOVE;
        markerPos[0] = (int)(config_num_leds * (boss2Cfg.m1 / 100.0));
        markerPos[1] = (int)(config_num_leds * (boss2Cfg.m2 / 100.0));
        markerPos[2] = (int)(config_num_leds * (boss2Cfg.m3 / 100.0));
      } 
      else if (currentBossType == 3) {
        for(int i=0; i<15; i++) { int mixColor = random(4, 8); bossSegments.push_back({mixColor, boss3Cfg.hpPerLed, boss3Cfg.hpPerLed, true, i}); }
        boss3State = B3_MOVE; boss3PhaseTriggered = false; boss3MarkerPos = config_num_leds / 2; bossActionTimer = millis();
      }
      currentState = STATE_BOSS_PLAYING;
    } else {
      enemies.clear(); shots.clear(); bossProjectiles.clear();
      int count = levels[currentLevel].length;
      for (int i = 0; i < count; i++) enemies.push_back({(int)random(1, 4)});
      enemyFrontIndex = config_num_leds - 1;
      currentState = STATE_PLAYING;
    }
  }
}

void updateLevelCompletedAnim() {
  unsigned long elapsed = millis() - stateTimer;
  if (elapsed < 1000) {
    fill_solid(leds, config_num_leds + ledStartOffset, CRGB::Green);
    if(config_sacrifice_led) leds[0] = CRGB(20,0,0);
  } else if (elapsed < 5000) { 
    FastLED.clear();
    if(config_sacrifice_led) leds[0] = CRGB(20,0,0);
    float pct = (float)levelAchievedScore / (float)levelMaxPossibleScore;
    if (pct > 1.0) pct = 1.0;
    int fillLeds = (int)(config_num_leds * pct);
    // POWER SAVE: Use dimmed gold CRGB(80, 60, 0)
    for(int i=0; i<fillLeds; i++) leds[i+ledStartOffset] = CRGB(80, 60, 0); 
    for(int i=fillLeds; i<config_num_leds; i++) leds[i+ledStartOffset] = CRGB(20, 0, 0); 
  } else {
    startLevelIntro(currentLevel + 1);
  }
  FastLED.show();
}

void updateBaseDestroyedAnim() {
  unsigned long elapsed = millis() - stateTimer;
  
  // Blink Homebase for 2 seconds
  if (elapsed < 2000) {
    // Fast blink Red/White
    CRGB c = (elapsed / 100) % 2 == 0 ? CRGB::Red : CRGB::White;
    for(int i=0; i<config_homebase_size; i++) {
       if (i+ledStartOffset < config_num_leds) leds[i+ledStartOffset] = c;
    }
    // Fade out rest of strip
    for(int i=config_homebase_size; i<config_num_leds; i++) {
       leds[i+ledStartOffset].nscale8(240); // slow fade
    }
    if(config_sacrifice_led) leds[0] = CRGB(20,0,0);
    FastLED.show();
  } else {
    currentState = STATE_GAMEOVER; // Finally trigger red screen
  }
}

void moveBossProjectiles(int speed) {
  static unsigned long lastMove = 0;
  if (millis() - lastMove > (1000/speed)) {
    lastMove = millis();
    for(int i=bossProjectiles.size()-1; i>=0; i--) {
      bossProjectiles[i].pos--; 
      if (bossProjectiles[i].pos < config_homebase_size) {
        triggerBaseDestruction();
      }
    }
  }
}

// --------------------------------------------------------------------------
// WIFI & WEB
// --------------------------------------------------------------------------
void enableWiFi() {
  FastLED.clear(); FastLED.show();
  WiFi.mode(WIFI_AP_STA);
  if(config_ssid != "") { WiFi.begin(config_ssid.c_str(), config_pass.c_str()); }
  if(config_static_ip && config_ip.length() > 0) {
      IPAddress ip, gw, sn, dns;
      if(ip.fromString(config_ip) && gw.fromString(config_gateway) && sn.fromString(config_subnet)) {
         if(config_dns.length() > 0) dns.fromString(config_dns); else dns.fromString("8.8.8.8");
         WiFi.config(ip, gw, sn, dns);
      }
  }
  WiFi.softAP("ESP-RGB-INVADERS", "12345678"); 
  server.begin();
}

String getHTML() {
  String h = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  h += "<title>Ultimate RGB Invaders</title>";
  h += "<style>body{font-family:sans-serif;background:#111;color:#eee;padding:10px;max-width:800px;margin:auto;}input,select,button{width:100%;background:#333;color:#fff;border:1px solid #555;padding:8px;border-radius:4px;box-sizing:border-box;margin-bottom:5px;}table{width:100%;border-collapse:collapse;margin-bottom:10px;} td,th{border:1px solid #444;padding:6px;text-align:center;}.sec{margin-top:25px;border-top:1px solid #555;padding-top:15px;background:#222;padding:15px;border-radius:5px;}#warning-box{margin-top:10px;padding:10px;background:#b30000;color:#fff;border:1px solid #ff0000;display:none;font-weight:bold;border-radius:4px;}.val-highlight{color:#0f0;font-weight:bold;}h2,h3{margin-top:0;} pre{font-family:monospace;color:#0f0;font-size:12px;overflow-x:auto;} .score-box{background:#004d00; border:2px solid #00ff00; padding:15px; text-align:center; margin-bottom:15px; border-radius:8px;} .big-score{font-size:32px; font-weight:bold; color:#fff;} .small-score{font-size:14px; color:#aaa;}";
  h += " .b-card { background: #2a2a2a; padding: 15px; border-radius: 5px; margin-bottom: 15px; border: 1px solid #444; }";
  
  h += " .b-head { font-size: 1.1em; font-weight: bold; margin-bottom: 10px; color: #fff; border-bottom: 1px solid #555; padding-bottom: 5px; }";
  h += " .form-grid { display: grid; grid-template-columns: 1fr 1fr 1fr 1fr; gap: 10px; }";
  h += " .f-item { display: flex; flex-direction: column; }";
  h += " .lbl { font-size: 11px; color: #aaa; margin-bottom: 4px; }";
  h += " .neon-text { font-family: sans-serif; font-weight: 900; font-size: 3.8em; text-align: center; text-transform: uppercase; letter-spacing: 4px; margin: 20px 0; background: linear-gradient(90deg, #ff0000, #ffff00, #00ff00, #00ffff, #0000ff, #ff00ff, #ff0000); background-size: 400%; -webkit-background-clip: text; background-clip: text; color: rgba(255,255,255,0.1); animation: rgbAnim 4s linear infinite; text-shadow: 0 0 20px rgba(255,255,255,0.2); }";
  h += " @keyframes rgbAnim { 0% { background-position: 0%; } 100% { background-position: 400%; } }";
  h += " .sub-head { text-align: center; font-size: 0.8em; color: #888; margin-bottom: 20px; }";
  h += " .credits { text-align: center; margin-top: 30px; font-size: 0.8em; color: #555; border-top: 1px solid #333; padding-top: 10px; }";
  h += " a { color: #00ff00; text-decoration: none; }";
  h += "</style>";
  
  h += "<script>function updateCalc() { var count = parseInt(document.getElementById('ledCount').value)||0; var brightPct = parseInt(document.getElementById('brightness').value)||50; document.getElementById('brightVal').innerText = brightPct + '%'; var warn = document.getElementById('warning-box'); if (count > 0) { var brightFactor = brightPct / 100.0; var amps = ((count * 15 * brightFactor) + 120) / 1000; document.getElementById('ampValue').innerText = amps.toFixed(2) + ' A'; if(amps > 5.0) { warn.style.display='block'; warn.innerText='WARNING: > 5A! High Power PSU required!'; } else warn.style.display='none'; } } function toggleIP() { var x = document.getElementById('ipsettings'); if(document.getElementById('chkStatic').checked) x.style.display='block'; else x.style.display='none'; } function confirmReset() { return confirm('Really delete all settings and factory reset?'); } window.onload = function(){ updateCalc(); toggleIP(); };</script>";
  h += "</head><body>";
  
  h += "<div class='neon-text'>RGB INVADERS</div>";
  h += "<div class='sub-head'>created by Qwer.Tzui / worksasdesigned - Version 3.2</div>";
  
  h += "<div class='score-box'>ALL TIME BEST<div class='big-score'>" + String(highScore) + "</div>";
  h += "<div class='small-score'>Last Games: " + String(lastGames[0]) + " | " + String(lastGames[1]) + " | " + String(lastGames[2]) + "</div></div>";
  String pName = (currentProfilePrefix == "def_") ? "Standard" : ((currentProfilePrefix == "kid_") ? "Kids" : "Pro");
  h += "<div class='sec'><h3>Profile Management</h3>Current Profile: <b>" + pName + "</b><br><form action='/loadprofile' method='POST' style='display:flex;gap:5px;margin-top:5px;'><select name='profile'><option value='def' " + String(currentProfilePrefix=="def_"?"selected":"") + ">Standard</option><option value='kid' " + String(currentProfilePrefix=="kid_"?"selected":"") + ">Kids</option><option value='pro' " + String(currentProfilePrefix=="pro_"?"selected":"") + ">Pro/Party</option></select><button type='submit'>Load Profile</button></form></div>";
  
  h += "<form action='/save' method='POST'><div class='sec'><h3>Hardware & General</h3>Start Level: <input type='number' name='startlvl' min='1' max='10' value='" + String(config_start_level) + "'><br>";
  h += "Total LEDs: <input id='ledCount' type='number' name='leds' value='" + String(config_num_leds) + "' oninput='updateCalc()'><br>";
  h += "<label>Sacrificial LED: <input type='checkbox' name='sac_led' value='1' " + String(config_sacrifice_led?"checked":"") + " style='width:auto;'></label><br>";
  h += "<label>Homebase Size: <input type='number' name='hb_size' min='1' max='5' value='" + String(config_homebase_size) + "'></label><br>";
  h += "<label>Player Shot Speed: <span id='shotVal'>" + String(config_shot_speed_pct) + "%</span></label><input type='range' name='shot_spd' min='50' max='150' value='" + String(config_shot_speed_pct) + "' oninput=\"document.getElementById('shotVal').innerText = this.value + '%';\"><br>";
  h += "<label>Default Brightness: <span id='brightVal'>" + String(config_brightness_pct) + "%</span></label><input id='brightness' type='range' name='bright' min='10' max='100' value='" + String(config_brightness_pct) + "' oninput='updateCalc()'><div style='margin-top:5px;'>Est. Current: <span id='ampValue' class='val-highlight'>0.00 A</span></div><div id='warning-box'></div></div>";
  
  h += "<div class='sec'><h3>Network</h3><label>WiFi SSID:</label><input type='text' name='ssid' value='" + config_ssid + "' placeholder='WiFi Name'>Password: <input type='password' name='pass' value='" + config_pass + "'><br><input type='checkbox' id='chkStatic' name='static_ip' value='1' onchange='toggleIP()' " + String(config_static_ip ? "checked" : "") + " style='width:auto;'> Static IP<br><div id='ipsettings' style='display:none;margin-top:10px;'>IP: <input name='ip' value='" + config_ip + "'>Gateway: <input name='gw' value='" + config_gateway + "'>Subnet: <input name='sn' value='" + config_subnet + "'>DNS: <input name='dns' value='" + config_dns + "'></div></div>";
  
  h += "<div class='sec'><h3>Level Configuration</h3><table><thead><tr><th>Lvl</th><th>Speed</th><th>Len</th><th>Boss?</th></tr></thead><tbody>";
  for(int i=1; i<=10; i++) { h += "<tr><td data-label='Level'>" + String(i) + "</td><td data-label='Speed'><input name='lspd" + String(i) + "' value='" + String(levels[i].speed) + "'></td><td data-label='Len/Type'><input name='llen" + String(i) + "' value='" + String(levels[i].length) + "'></td><td data-label='Boss'><select name='lboss" + String(i) + "'><option value='0' " + String(levels[i].bossType==0?"selected":"") + ">-</option><option value='2' " + String(levels[i].bossType==2?"selected":"") + ">Masterblaster</option><option value='1' " + String(levels[i].bossType==1?"selected":"") + ">The Tank</option><option value='3' " + String(levels[i].bossType==3?"selected":"") + ">RGB Overlord</option></select></td></tr>"; }
  h += "</tbody></table></div>";
  
  h += "<div class='sec'><h3>Boss Configuration</h3>";
  
  // Card 1: Masterblaster (Boss 2 Internal)
  h += "<div class='b-card'><div class='b-head'>Masterblaster (Boss 1)</div><div class='form-grid'>";
  h += "<div class='f-item'><span class='lbl'>Move Speed</span><input name='b2mv' value='" + String(boss2Cfg.moveSpeed) + "'></div>";
  h += "<div class='f-item'><span class='lbl'>Shot Speed</span><input name='b2ss' value='" + String(boss2Cfg.shotSpeed) + "'></div>";
  h += "<div class='f-item'><span class='lbl'>HP/LED</span><input name='b2hp' value='" + String(boss2Cfg.hpPerLed) + "'></div>";
  h += "<div class='f-item'><span class='lbl'>Reload (0.1s)</span><input name='b2fr' value='" + String(boss2Cfg.shotFreq) + "'></div>";
  h += "<div class='f-item'><span class='lbl'>Mark 1 (%)</span><input name='b2m1' value='" + String(boss2Cfg.m1) + "'></div>";
  h += "<div class='f-item'><span class='lbl'>Mark 2 (%)</span><input name='b2m2' value='" + String(boss2Cfg.m2) + "'></div>";
  h += "<div class='f-item'><span class='lbl'>Mark 3 (%)</span><input name='b2m3' value='" + String(boss2Cfg.m3) + "'></div>";
  h += "</div></div>";

  // Card 2: The Tank (Boss 1 Internal)
  h += "<div class='b-card'><div class='b-head'>The Tank (Boss 2)</div><div class='form-grid'>";
  h += "<div class='f-item'><span class='lbl'>Move Speed</span><input name='b1mv' value='" + String(boss1Cfg.moveSpeed) + "'></div>";
  h += "<div class='f-item'><span class='lbl'>Shot Speed</span><input name='b1ss' value='" + String(boss1Cfg.shotSpeed) + "'></div>";
  h += "<div class='f-item'><span class='lbl'>HP/LED</span><input name='b1hp' value='" + String(boss1Cfg.hpPerLed) + "'></div>";
  h += "<div class='f-item'><span class='lbl'>Shot Freq</span><input name='b1fr' value='" + String(boss1Cfg.shotFreq) + "'></div>";
  h += "</div></div>";

  // Card 3: Overlord (Boss 3 Internal)
  h += "<div class='b-card'><div class='b-head'>RGB Overlord (Boss 3)</div><div class='form-grid'>";
  h += "<div class='f-item'><span class='lbl'>Move Speed</span><input name='b3mv' value='" + String(boss3Cfg.moveSpeed) + "'></div>";
  h += "<div class='f-item'><span class='lbl'>HP/LED</span><input name='b3hp' value='" + String(boss3Cfg.hpPerLed) + "'></div>";
  h += "<div class='f-item'><span class='lbl'>Shot Freq</span><input name='b3fr' value='" + String(boss3Cfg.shotFreq) + "'></div>";
  h += "<div class='f-item'><span class='lbl'>Burst Count</span><input name='b3bc' value='" + String(boss3Cfg.burstCount) + "'></div>";
  h += "</div></div>";
  
  h += "</div>"; // Close sec
  
  h += "<br><input type='submit' value='SAVE SETTINGS' style='width:100%;background:#009900;padding:15px;font-size:1.2em;cursor:pointer;font-weight:bold;'></form><br><br><form action='/reset' method='POST' onsubmit='return confirmReset()'><button type='submit' style='background:#990000;padding:10px;'>FACTORY RESET (Clear Scores)</button></form>";
  
  h += "<div class='credits'><a href='https://paypal.me/WeisWernau' target='_blank'>paypal.me/WeisWernau</a><br><br><i>\"I don't need your money. But if I can buy my wife a bouquet of flowers, the chance increases that I can publish more funny projects - every married man knows what I'm talking about.\"</i></div>";
  h += "</body></html>";
  return h;
}

// --------------------------------------------------------------------------
// CONFIG LOAD/SAVE
// --------------------------------------------------------------------------
void applyProfileDefaults(String prefix) {
  if (prefix == "def_") {
    // STANDARD PROFILE
    levels[1] = {5, 15, 0}; levels[2] = {6, 20, 0}; 
    levels[3] = {7, 25, 2}; // Masterblaster
    levels[4] = {8, 30, 0}; levels[5] = {9, 35, 0}; 
    levels[6] = {10, 40, 1}; // The Tank
    levels[7] = {20, 20, 0}; levels[8] = {20, 25, 0}; levels[9] = {10, 60, 0}; 
    levels[10] = {14, 60, 3}; 
    
    boss1Cfg = {4, 60, 4, 30, 0, 0,0,0}; // The Tank (UPDATED: HP4, Freq30)
    boss2Cfg = {10, 60, 5, 40, 0, 85, 55, 30}; // Masterblaster
    boss3Cfg = {10, 50, 4, 60, 5, 0,0,0}; 
  } else if (prefix == "kid_") {
    // KIDS PROFILE
    levels[1] = {5, 15, 0}; levels[2] = {5, 20, 0}; levels[3] = {6, 25, 2}; 
    levels[4] = {6, 20, 0}; levels[5] = {7, 25, 0}; levels[6] = {10, 40, 1}; 
    levels[7] = {8, 30, 0}; levels[8] = {8, 35, 0}; levels[9] = {10, 20, 0}; levels[10] = {14, 60, 3}; 
    boss1Cfg = {4, 60, 2, 40, 0, 0,0,0}; 
    boss2Cfg = {7, 40, 3, 40, 0, 85, 55, 30}; 
    boss3Cfg = {6, 40, 3, 60, 3, 0,0,0}; 
  } else { 
    // PRO PROFILE
    for(int i=1; i<=10; i++) { levels[i] = {5+i, 15+(i*5), 0}; }
    levels[3].bossType=2; levels[6].bossType=1; levels[10].bossType=3;
    boss1Cfg = {6, 80, 5, 25, 0, 0,0,0}; 
    boss2Cfg = {15, 80, 6, 30, 0, 90, 60, 30}; 
    boss3Cfg = {15, 60, 6, 40, 8, 0,0,0};
  }
}

void saveCurrentToPreferences(String prefix) {
  preferences.begin("game", false);
  preferences.putInt((prefix+"leds").c_str(), config_num_leds);
  preferences.putInt((prefix+"bright").c_str(), config_brightness_pct);
  preferences.putInt((prefix+"startlvl").c_str(), config_start_level);
  for(int i=1; i<=10; i++) { 
    preferences.putInt((prefix+"l"+String(i)+"s").c_str(), levels[i].speed); 
    preferences.putInt((prefix+"l"+String(i)+"l").c_str(), levels[i].length); 
    preferences.putInt((prefix+"l"+String(i)+"b").c_str(), levels[i].bossType); 
  } 
  preferences.putBytes((prefix+"b1").c_str(), &boss1Cfg, sizeof(BossConfig)); 
  preferences.putBytes((prefix+"b2").c_str(), &boss2Cfg, sizeof(BossConfig)); 
  preferences.putBytes((prefix+"b3").c_str(), &boss3Cfg, sizeof(BossConfig)); 
  preferences.end(); 
}

void performFactoryReset() {
  preferences.begin("game", true);
  String s=preferences.getString("ssid",""); String p=preferences.getString("pass","");
  String ip=preferences.getString("sip_ip",""); String gw=preferences.getString("sip_gw",""); String sn=preferences.getString("sip_sn",""); String dns=preferences.getString("sip_dns","");
  bool sip=preferences.getBool("sip_on", false);
  preferences.end(); 

  preferences.begin("game", false);
  preferences.clear(); 
  preferences.putString("ssid",s); preferences.putString("pass",p); 
  preferences.putBool("sip_on",sip); preferences.putString("sip_ip",ip); 
  preferences.putString("sip_gw",gw); preferences.putString("sip_sn",sn); preferences.putString("sip_dns",dns); 
  
  preferences.putBool("sac_led", true); 
  preferences.putInt("hb_size", 3);
  preferences.putInt("shot_spd", 100); 
  preferences.putInt("version", CONFIG_VERSION); 
  preferences.putString("act_prof", "def_");
  preferences.end(); 

  applyProfileDefaults("def_"); saveCurrentToPreferences("def_");
  applyProfileDefaults("kid_"); saveCurrentToPreferences("kid_");
  applyProfileDefaults("pro_"); saveCurrentToPreferences("pro_");
  applyProfileDefaults("def_");
}

void setupDefaultConfig() { applyProfileDefaults("def_"); }

void loadConfig(String prefix) {
  preferences.begin("game", true);
  config_num_leds = preferences.getInt((prefix+"leds").c_str(), config_num_leds);
  config_brightness_pct = preferences.getInt((prefix+"bright").c_str(), config_brightness_pct);
  config_start_level = preferences.getInt((prefix+"startlvl").c_str(), config_start_level);
  config_ssid = preferences.getString("ssid", ""); config_pass = preferences.getString("pass", "");
  config_static_ip = preferences.getBool("sip_on", false); config_ip = preferences.getString("sip_ip", "");
  config_gateway = preferences.getString("sip_gw", ""); config_subnet = preferences.getString("sip_sn", ""); config_dns = preferences.getString("sip_dns", "");
  
  config_sacrifice_led = preferences.getBool("sac_led", true);
  config_homebase_size = preferences.getInt("hb_size", 3);
  config_shot_speed_pct = preferences.getInt("shot_spd", 100); 
  ledStartOffset = config_sacrifice_led ? 1 : 0;

  for(int i=1; i<=10; i++) { 
    levels[i].speed = preferences.getInt((prefix+"l"+String(i)+"s").c_str(), levels[i].speed); 
    levels[i].length = preferences.getInt((prefix+"l"+String(i)+"l").c_str(), levels[i].length); 
    levels[i].bossType = preferences.getInt((prefix+"l"+String(i)+"b").c_str(), levels[i].bossType); 
  }
  
  if(preferences.isKey((prefix+"b1").c_str())) preferences.getBytes((prefix+"b1").c_str(), &boss1Cfg, sizeof(BossConfig)); 
  if(preferences.isKey((prefix+"b2").c_str())) preferences.getBytes((prefix+"b2").c_str(), &boss2Cfg, sizeof(BossConfig)); 
  if(preferences.isKey((prefix+"b3").c_str())) preferences.getBytes((prefix+"b3").c_str(), &boss3Cfg, sizeof(BossConfig)); 
  preferences.end();
}

void handleProfileSwitch() { 
  if (server.hasArg("profile")) { 
    String p = server.arg("profile"); 
    if(p == "kid") currentProfilePrefix = "kid_"; else if(p == "pro") currentProfilePrefix = "pro_"; else currentProfilePrefix = "def_"; 
    preferences.begin("game", false); preferences.putString("act_prof", currentProfilePrefix); preferences.end(); 
    applyProfileDefaults(currentProfilePrefix); loadConfig(currentProfilePrefix); loadHighscores(); 
    server.sendHeader("Location", "/"); server.send(303); 
  } else server.send(400, "text/plain", "Bad Request"); 
}

void handleReset() { performFactoryReset(); server.send(200, "text/html", "<h2>Reset successful!</h2><p>Values & Scores wiped. ESP restarting.</p>"); delay(1000); ESP.restart(); }

void handleSave() { 
  if (server.hasArg("leds")) config_num_leds = server.arg("leds").toInt(); if (server.hasArg("bright")) config_brightness_pct = server.arg("bright").toInt(); if (server.hasArg("startlvl")) config_start_level = server.arg("startlvl").toInt(); 
  if (server.hasArg("ssid")) config_ssid = server.arg("ssid"); if (server.hasArg("pass")) config_pass = server.arg("pass"); 
  config_static_ip = server.hasArg("static_ip"); config_ip = server.arg("ip"); config_gateway = server.arg("gw"); config_subnet = server.arg("sn"); config_dns = server.arg("dns"); 
  if (server.hasArg("hb_size")) config_homebase_size = server.arg("hb_size").toInt();
  if (server.hasArg("shot_spd")) config_shot_speed_pct = server.arg("shot_spd").toInt();
  config_sacrifice_led = server.hasArg("sac_led");

  preferences.begin("game", false); 
  preferences.putString("ssid", config_ssid); preferences.putString("pass", config_pass); preferences.putBool("sip_on", config_static_ip); preferences.putString("sip_ip", config_ip); preferences.putString("sip_gw", config_gateway); preferences.putString("sip_sn", config_subnet); preferences.putString("sip_dns", config_dns); 
  preferences.putBool("sac_led", config_sacrifice_led); preferences.putInt("hb_size", config_homebase_size);
  preferences.putInt("shot_spd", config_shot_speed_pct);

  String p = currentProfilePrefix; preferences.putInt((p+"leds").c_str(), config_num_leds); preferences.putInt((p+"bright").c_str(), config_brightness_pct); preferences.putInt((p+"startlvl").c_str(), config_start_level); 
  for(int i=1; i<=10; i++) { levels[i].speed = server.arg("lspd"+String(i)).toInt(); levels[i].length = server.arg("llen"+String(i)).toInt(); levels[i].bossType = server.arg("lboss"+String(i)).toInt(); preferences.putInt((p+"l"+String(i)+"s").c_str(), levels[i].speed); preferences.putInt((p+"l"+String(i)+"l").c_str(), levels[i].length); preferences.putInt((p+"l"+String(i)+"b").c_str(), levels[i].bossType); } 
  boss1Cfg.moveSpeed = server.arg("b1mv").toInt(); boss1Cfg.shotSpeed = server.arg("b1ss").toInt(); boss1Cfg.hpPerLed = server.arg("b1hp").toInt(); boss1Cfg.shotFreq = server.arg("b1fr").toInt(); preferences.putBytes((p+"b1").c_str(), &boss1Cfg, sizeof(BossConfig)); 
  boss2Cfg.moveSpeed = server.arg("b2mv").toInt(); boss2Cfg.shotSpeed = server.arg("b2ss").toInt(); boss2Cfg.hpPerLed = server.arg("b2hp").toInt(); boss2Cfg.shotFreq = server.arg("b2fr").toInt(); 
  boss2Cfg.m1 = server.arg("b2m1").toInt(); boss2Cfg.m2 = server.arg("b2m2").toInt(); boss2Cfg.m3 = server.arg("b2m3").toInt(); 
  preferences.putBytes((p+"b2").c_str(), &boss2Cfg, sizeof(BossConfig)); 
  boss3Cfg.moveSpeed = server.arg("b3mv").toInt(); boss3Cfg.shotSpeed = 0; boss3Cfg.hpPerLed = server.arg("b3hp").toInt(); boss3Cfg.shotFreq = server.arg("b3fr").toInt(); boss3Cfg.burstCount = server.arg("b3bc").toInt();
  preferences.putBytes((p+"b3").c_str(), &boss3Cfg, sizeof(BossConfig)); 
  preferences.end(); server.send(200, "text/html", "<h2>Saved!</h2><p>ESP restarting...</p><a href='/'>Go Back</a>"); delay(1000); ESP.restart(); 
}

// --------------------------------------------------------------------------
// MAIN SETUP
// --------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  pinMode(PIN_BTN_BLUE, INPUT_PULLUP); pinMode(PIN_BTN_RED, INPUT_PULLUP); pinMode(PIN_BTN_GREEN, INPUT_PULLUP); pinMode(PIN_BTN_WHITE, INPUT_PULLUP);

  setupDefaultConfig(); 
  
  // CHECK VERSION
  preferences.begin("game", true); 
  int storedVer = preferences.getInt("version", 0); 
  preferences.end();
  if (storedVer < CONFIG_VERSION) { performFactoryReset(); ESP.restart(); }
  
  preferences.begin("game", true); currentProfilePrefix = preferences.getString("act_prof", "def_"); preferences.end();
  loadConfig(currentProfilePrefix);        
  loadHighscores(); 

  FastLED.addLeds<LED_TYPE, PIN_LED_DATA, COLOR_ORDER>(leds, config_num_leds + 1);
  FastLED.setBrightness(map(config_brightness_pct, 10, 100, 25, 255));
  if(config_sacrifice_led) leds[0] = CRGB(20, 0, 0); 
  FastLED.show();

  WiFi.mode(WIFI_OFF);
  
  server.on("/", []() { server.send(200, "text/html", getHTML()); }); server.on("/save", handleSave); server.on("/loadprofile", handleProfileSwitch); server.on("/reset", handleReset); 
  startLevelIntro(config_start_level);
}

// --------------------------------------------------------------------------
// MAIN LOOP
// --------------------------------------------------------------------------
void loop() {
  unsigned long now = millis();
  if (now - lastLoopTime < FRAME_DELAY) return;
  lastLoopTime = now;

  if (wifiMode) {
    server.handleClient();
    static unsigned long lastWifiLedUpdate = 0;
    if (now - lastWifiLedUpdate > 2000) {
      lastWifiLedUpdate = now; FastLED.clear();
      for(int i=0; i<=config_num_leds; i+=2) leds[i+ledStartOffset] = CRGB::Blue;
      FastLED.show();
    }
    if (digitalRead(PIN_BTN_WHITE) == LOW) { delay(200); ESP.restart(); }
    return;
  }

  if (digitalRead(PIN_BTN_WHITE) == LOW) {
    if (!btnWhiteHeld) { btnWhiteHeld = true; btnWhitePressTime = now; } 
    else { if (now - btnWhitePressTime > 3000) { wifiMode = true; while(digitalRead(PIN_BTN_WHITE) == LOW) { delay(10); } enableWiFi(); return; } }
  } else {
    if (btnWhiteHeld && (now - btnWhitePressTime < 1000)) startLevelIntro(config_start_level);
    btnWhiteHeld = false;
  }

  // --- STATES & ANIMATIONS ---
  if (currentState == STATE_LEVEL_COMPLETED) { updateLevelCompletedAnim(); return; }
  if (currentState == STATE_BASE_DESTROYED) { updateBaseDestroyedAnim(); return; } // NEW STATE
  if (currentState == STATE_GAME_FINISHED) { for(int i=0; i<config_num_leds; i++) leds[i+ledStartOffset] = CHSV((now/10)+(i*5), 255, 255); if(config_sacrifice_led) leds[0]=CRGB(20,0,0); FastLED.show(); return; }
  if (currentState == STATE_GAMEOVER) { for(int i=0; i<config_num_leds; i++) leds[i+ledStartOffset] = CRGB::Red; if(config_sacrifice_led) leds[0]=CRGB(20,0,0); FastLED.show(); return; }
  if (currentState == STATE_INTRO) { updateLevelIntro(); return; }

  if (currentState == STATE_PLAYING || currentState == STATE_BOSS_PLAYING) {
    bool b = (digitalRead(PIN_BTN_BLUE) == LOW); bool r = (digitalRead(PIN_BTN_RED) == LOW); bool g = (digitalRead(PIN_BTN_GREEN) == LOW);
    bool isAnyBtnPressed = (b || r || g);

    if (!isAnyBtnPressed) { buttonsReleased = true; isWaitingForCombo = false; }
    
    // INPUT HANDLING
    if (currentBossType == 3) {
       if (isAnyBtnPressed && buttonsReleased && !isWaitingForCombo && (now - lastFireTime > FIRE_COOLDOWN)) {
          isWaitingForCombo = true; comboTimer = now;
       }
       if (isWaitingForCombo && (now - comboTimer >= INPUT_BUFFER_MS)) {
          int c = 0; 
          b = (digitalRead(PIN_BTN_BLUE) == LOW); r = (digitalRead(PIN_BTN_RED) == LOW); g = (digitalRead(PIN_BTN_GREEN) == LOW);
          if (r && g && b) c = 7; else if (r && g) c = 4; else if (r && b) c = 5; else if (g && b) c = 6; else if (b) c = 1; else if (r) c = 2; else if (g) c = 3; 
          if (c > 0) { shots.push_back({0, c}); lastFireTime = now; }
          buttonsReleased = false; isWaitingForCombo = false; 
       }
    } else {
       if (isAnyBtnPressed && buttonsReleased && (now - lastFireTime > FIRE_COOLDOWN)) {
          int c = 0;
          if (b) c = 1; else if (r) c = 2; else if (g) c = 3;
          if (c > 0) { shots.push_back({0, c}); lastFireTime = now; }
          buttonsReleased = false;
       }
    }

    // SHOT MOVEMENT
    unsigned long shotInterval = 1100 / config_shot_speed_pct;
    if (now - lastShotMove > shotInterval) {
      lastShotMove = now;
      for (int i = shots.size() - 1; i >= 0; i--) {
        shots[i].position++; bool remove = false;
        if (currentState == STATE_PLAYING) { 
          if (shots[i].position >= enemyFrontIndex && !enemies.empty()) { 
            if (shots[i].color == enemies[0].color) { enemies.erase(enemies.begin()); enemyFrontIndex++; flashPixel(shots[i].position); remove = true; checkWinCondition(); } 
            else { enemies.insert(enemies.begin(), {shots[i].color}); enemyFrontIndex--; remove = true; } 
          } 
        } 
        else if (currentState == STATE_BOSS_PLAYING) {
          for(int p=0; p<bossProjectiles.size(); p++) { if(shots[i].position >= bossProjectiles[p].pos) { if(shots[i].color == bossProjectiles[p].color) { bossProjectiles.erase(bossProjectiles.begin() + p); flashPixel(shots[i].position); } remove = true; break; } }
          
          if (!remove && shots[i].position >= enemyFrontIndex && !bossSegments.empty()) { 
             int hitIndex = shots[i].position - enemyFrontIndex; 
             if (hitIndex >= 0 && hitIndex < bossSegments.size()) { 
               bool vulnerable = false; 
               if (currentBossType == 1) vulnerable = true; 
               else if (currentBossType == 2) { if (boss2State == B2_MOVE && bossSegments[hitIndex].active) vulnerable = true; } 
               else if (currentBossType == 3) { if (boss3State != B3_PHASE_CHANGE) vulnerable = true; } 
               
               if (vulnerable) { 
                 if (shots[i].color == bossSegments[hitIndex].color) { 
                    flashPixel(shots[i].position); 
                    bossSegments[hitIndex].hp--; 
                    if (bossSegments[hitIndex].hp <= 0) { bossSegments.erase(bossSegments.begin() + hitIndex); if (hitIndex == 0) enemyFrontIndex++; } 
                    checkWinCondition(); 
                 } 
               } remove = true; 
             } 
          }
        }
        if (shots[i].position >= config_num_leds) remove = true; if (remove) shots.erase(shots.begin() + i);
      }
    }
    
    // MOVEMENT & GAME LOGIC
    if (currentState == STATE_LEVEL_COMPLETED || currentState == STATE_GAME_FINISHED || currentState == STATE_BASE_DESTROYED) return;

    if (currentState == STATE_PLAYING) { unsigned long delay = 1000 / levels[currentLevel].speed; if (now - lastEnemyMove > delay) { lastEnemyMove = now; enemyFrontIndex--; if (enemyFrontIndex <= config_homebase_size) { triggerBaseDestruction(); } } }
    else if (currentState == STATE_BOSS_PLAYING) {
      int pSpeed = 60; if (currentBossType == 1) pSpeed = boss1Cfg.shotSpeed; if (currentBossType == 2) pSpeed = boss2Cfg.shotSpeed; moveBossProjectiles(pSpeed);
      if (currentBossType == 1) { 
        if (now - lastEnemyMove > (1000/boss1Cfg.moveSpeed)) { lastEnemyMove = now; enemyFrontIndex--; if (enemyFrontIndex <= config_homebase_size) { triggerBaseDestruction(); } } 
        if (now - bossActionTimer > (boss1Cfg.shotFreq * 100)) { 
           bossActionTimer = now; 
           // 20% Chance for Front Color, 80% Random
           int shotColor = 0; int frontColor = 0; if(bossSegments.size() > 0) frontColor = bossSegments[0].color;
           if (random(100) < 20 && frontColor > 0) shotColor = frontColor; else shotColor = random(1,4);
           bossProjectiles.push_back({enemyFrontIndex, shotColor}); 
        } 
      }
      else if (currentBossType == 2) { if (boss2State == B2_MOVE) { if (now - lastEnemyMove > (1000/boss2Cfg.moveSpeed)) { lastEnemyMove = now; enemyFrontIndex--; if (boss2Section < 3) { if (enemyFrontIndex <= markerPos[boss2Section]) { boss2State = B2_CHARGE; bossActionTimer = now; } } if (enemyFrontIndex <= config_homebase_size) { triggerBaseDestruction(); } } } else if (boss2State == B2_CHARGE) { if (now - bossActionTimer < (boss2Cfg.shotFreq * 100)) { if (now % 100 < 20) boss2LockedColor = random(1,4); } else { boss2State = B2_SHOOT; boss2ShotsFired = 0; bossActionTimer = now; int startRange = 0; int endRange = 0; if (boss2Section == 0) { startRange=0; endRange=2; } else if (boss2Section == 1) { startRange=0; endRange=5; } else { startRange=0; endRange=8; } for(auto &seg : bossSegments) { if (seg.originalIndex >= startRange && seg.originalIndex <= endRange) seg.color = boss2LockedColor; } } } else if (boss2State == B2_SHOOT) { if (now - bossActionTimer > 150) { bossActionTimer = now; bossProjectiles.push_back({enemyFrontIndex, boss2LockedColor}); boss2ShotsFired++; if (boss2ShotsFired >= 10) { int startRange = 0; int endRange = 0; if (boss2Section == 0) { startRange=0; endRange=2; } else if (boss2Section == 1) { startRange=3; endRange=5; } else { startRange=0; endRange=8; } for(auto &seg : bossSegments) { if (seg.originalIndex >= startRange && seg.originalIndex <= endRange) seg.active = true; } boss2State = B2_MOVE; boss2Section++; } } } }
      else if (currentBossType == 3) {
        if (!boss3PhaseTriggered && enemyFrontIndex <= boss3MarkerPos) { boss3State = B3_PHASE_CHANGE; boss3PhaseTriggered = true; bossActionTimer = now; }
        if (boss3State == B3_MOVE) {
           if (now - lastEnemyMove > (1000/boss3Cfg.moveSpeed)) { lastEnemyMove = now; enemyFrontIndex--; if (enemyFrontIndex <= config_homebase_size) { triggerBaseDestruction(); } }
           if (boss3Cfg.shotFreq > 0 && (now - bossActionTimer > (boss3Cfg.shotFreq * 100))) { bossActionTimer = now; bossProjectiles.push_back({enemyFrontIndex, (int)random(1,4)}); }
        } 
        else if (boss3State == B3_PHASE_CHANGE) {
           if (now - bossActionTimer > 4000) { boss3State = B3_BURST; boss3BurstCounter = 0; bossActionTimer = now; for(auto &seg : bossSegments) seg.color = random(4, 8); }
        }
        else if (boss3State == B3_BURST) {21:59 16.01.2026
           if (now - bossActionTimer > 200) { bossActionTimer = now; bossProjectiles.push_back({enemyFrontIndex, (int)random(1,8)}); boss3BurstCounter++; if (boss3BurstCounter >= boss3Cfg.burstCount) { boss3State = B3_MOVE; } }
        }
      }
    }
    
    // RENDERING
    FastLED.clear();
    if (currentState == STATE_BOSS_PLAYING) {
      if (currentBossType == 2) { for(int i=0; i<3; i++) { if (markerPos[i] < enemyFrontIndex) leds[markerPos[i]+ledStartOffset] = CRGB(50,0,0); } }
      else if (currentBossType == 3) { if(!boss3PhaseTriggered) leds[boss3MarkerPos+ledStartOffset] = CRGB(50,0,0); leds[boss3MarkerPos+ledStartOffset+1] = CRGB(50,0,0); }
    }
    if (currentState == STATE_PLAYING) { for(int i=0; i<enemies.size(); i++) { if (enemyFrontIndex+i < config_num_leds && enemyFrontIndex+i >=0) leds[enemyFrontIndex+i+ledStartOffset] = getColor(enemies[i].color); } } 
    else if (currentState == STATE_BOSS_PLAYING) { 
      for(int i=0; i<bossSegments.size(); i++) { 
        int pos = enemyFrontIndex+i; 
        if (pos < config_num_leds && pos >=0) { 
           CRGB c = getColor(bossSegments[i].color); 
           if (currentBossType == 2) { c = CRGB(20,20,20); if (boss2State == B2_MOVE) { if (bossSegments[i].active) { c = getColor(bossSegments[i].color); if ((millis()/100)%2 == 0) c = CRGB::Black; } } else if (boss2State == B2_CHARGE || boss2State == B2_SHOOT) { int oid = bossSegments[i].originalIndex; bool highlight = false; if (boss2Section == 0) { if (oid >= 0 && oid <= 2) highlight = true; } else if (boss2Section == 1) { if (oid >= 0 && oid <= 5) highlight = true; } else if (boss2Section >= 2) { highlight = true; } if (highlight) c = getColor(boss2LockedColor); } } 
           else if (currentBossType == 3 && boss3State == B3_PHASE_CHANGE) { c = CRGB::White; } 
           leds[pos+ledStartOffset] = c; 
        } 
      } 
      for(auto &p : bossProjectiles) { if(p.pos >= 0 && p.pos < config_num_leds) leds[p.pos+ledStartOffset] = getColor(p.color); } 
    }
    for(auto &s : shots) { if(s.position >= 0 && s.position < config_num_leds) leds[s.position+ledStartOffset] = getColor(s.color); }
    for(int i=0; i<config_homebase_size; i++) leds[i+ledStartOffset] = CRGB::White;
    if(config_sacrifice_led) leds[0] = CRGB(20,0,0); 
    FastLED.show();
  }
}
