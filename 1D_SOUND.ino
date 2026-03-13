// ==========================================================================
// PROJECT: ULTIMATE RGB INVADERS - V10.9.5 (Simon says fix)
// HARDWARE: ESP32-S3 (N16), MAX98357A, WS2812B
// CORE VERSION: 2.0.17 (Required!) I2S driver issues with 3.x.x
// ==========================================================================

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <driver/i2s.h>
#include <vector>
#include <Update.h>

// --- LED CONFIGURATION ---
#define FASTLED_ESP32_S3_PIN 7 
#define FASTLED_RMT_MAX_CHANNELS 1
#include <FastLED.h>

// --------------------------------------------------------------------------
// 1. DEFINITIONS & DATA TYPES
// --------------------------------------------------------------------------
#define I2S_BCLK        4
#define I2S_LRC         5
#define I2S_DOUT        6

#define PIN_LED_DATA    7
#define MAX_LEDS        1200 
#define LED_TYPE        WS2812B
#define COLOR_ORDER     GRB

#define PIN_BTN_BLUE    15
#define PIN_BTN_RED     16
#define PIN_BTN_GREEN   17
#define PIN_BTN_WHITE   18 

#define CONFIG_VERSION  42 // Version 10.9.5
#define FRAME_DELAY     16 // ~60 FPS
#define INPUT_BUFFER_MS 60 
#define SAMPLE_RATE     44100

const int FIRE_COOLDOWN = 100;

struct ToneCmd { int freq; int duration; };
typedef std::vector<ToneCmd> Melody;

enum SoundEvent { 
  EVT_NONE=0, EVT_START, EVT_WIN, EVT_LOSE, EVT_MISTAKE,      
  EVT_HIT_SUCCESS, 
  EVT_SHOT_BLUE, EVT_SHOT_RED, EVT_SHOT_GREEN, EVT_SHOT_WHITE,
  EVT_SHOT_YELLOW, EVT_SHOT_MAGENTA, EVT_SHOT_CYAN, 
  EVT_FINAL_WIN,
  EVT_BONUS_START, EVT_BONUS_WAVE, EVT_BONUS_SPEEDUP
};

enum GameState { 
  STATE_MENU, STATE_INTRO, STATE_PLAYING, STATE_BOSS_PLAYING, 
  STATE_LEVEL_COMPLETED, STATE_GAME_FINISHED, STATE_BASE_DESTROYED, STATE_GAMEOVER,
  STATE_BONUS_INTRO, STATE_BONUS_PLAYING, // Beatsaber
  STATE_BONUS_SIMON // Simon Says
};

enum Boss2State { B2_MOVE, B2_CHARGE, B2_SHOOT };
enum Boss3State { B3_MOVE, B3_PHASE_CHANGE, B3_BURST, B3_WAIT };

// Simon Says States
enum SimonState { S_MOVE, S_PREPARE, S_SHOW, S_INPUT, S_SUCCESS, S_FAIL };

struct LevelConfig { int speed; int length; int bossType; };
struct BossConfig { int moveSpeed; int shotSpeed; int hpPerLed; int shotFreq; int burstCount; int m1; int m2; int m3; };
struct Enemy { int color; float pos; bool flash; };
struct BossSegment { int color; int hp; int maxHp; bool active; int originalIndex; };
struct Shot { float position; int color; }; 
struct BossProjectile { float pos; int color; };

// --------------------------------------------------------------------------
// 2. GLOBAL VARIABLES
// --------------------------------------------------------------------------
CRGB leds[MAX_LEDS];
Preferences preferences;
WebServer server(80);
QueueHandle_t audioQueue;
TaskHandle_t audioTaskHandle;

// Statistics
unsigned long stat_totalShots = 0;
unsigned long stat_totalKills = 0;
unsigned long stat_boss3Kills = 0; 
int stat_lastGameShots = 0;

// Default Sound Strings
const String DEF_SND_START       = "523,80;659,80;784,80;1047,300";
const String DEF_SND_WIN         = "523,80;659,80;784,80;1047,300;0,150;1047,60;1319,60";
const String DEF_SND_FINAL_WIN   = "523,150;659,150;784,150;1047,400;784,150;1047,600;1319,150;1568,150;2093,800";
const String DEF_SND_LOSE        = "370,100;349,100;330,100;311,400";
const String DEF_SND_MISTAKE     = "60,150";
const String DEF_SND_SHOT_BLUE   = "698,50;659,50";
const String DEF_SND_SHOT_RED    = "784,30;1047,30;1319,30";
const String DEF_SND_SHOT_GREEN  = "523,30;554,30;523,30";
const String DEF_SND_SHOT_WHITE  = "1047,20;1319,20;1568,20;2093,4";
const String DEF_SND_HIT         = "2093,30";
const String DEF_SND_SPEEDUP     = "1500,80;0,50;1500,80"; 

const String DEF_SND_SHOT_Y      = "1500,40;1800,40";        
const String DEF_SND_SHOT_M      = "800,40;2000,40";        
const String DEF_SND_SHOT_C      = "1200,40;1000,40";       

String cfg_snd_start, cfg_snd_win, cfg_snd_lose, cfg_snd_mistake;
String cfg_snd_shot_b, cfg_snd_shot_r, cfg_snd_shot_g, cfg_snd_shot_w, cfg_snd_hit;

// Color Configuration
String hex_c1 = "#0000FF"; // Blue
String hex_c2 = "#FF0000"; // Red
String hex_c3 = "#00FF00"; // Green
String hex_c4 = "#FFFF00"; // Yellow
String hex_c5 = "#FF00FF"; // Magenta
String hex_c6 = "#00FFFF"; // Cyan
String hex_cw = "#FFFFFF"; // White
String hex_cb = "#222222"; // Dark

CRGB col_c1, col_c2, col_c3, col_c4, col_c5, col_c6, col_cw, col_cb;
Melody melStart, melWin, melLose, melMistake, melShotBlue, melShotRed, melShotGreen, melShotWhite, melHit, melFinalWin, melSpeedUp;
Melody melShotY, melShotM, melShotC;

// Config
int config_num_leds = 100;
int config_brightness_pct = 50;
int config_start_level = 1;
bool config_sacrifice_led = true; 
int config_homebase_size = 3;       
int config_shot_speed_pct = 100;
int ledStartOffset = 1;
// AUDIO CONFIG
bool config_sound_on = true;
int config_volume_pct = 50;
bool config_endless_mode = false;
String currentProfilePrefix = "def_"; 
String config_ssid = "";
String config_pass = "";
bool config_static_ip = false; String config_ip = "";
String config_gateway = ""; String config_subnet = "";
String config_dns = "";
bool wifiMode = false; 

// Game State
LevelConfig levels[11];
BossConfig boss1Cfg; BossConfig boss2Cfg; BossConfig boss3Cfg;
GameState currentState = STATE_MENU;

unsigned long lastLoopTime = 0; 
unsigned long stateTimer = 0;
unsigned long lastShotMove = 0;
unsigned long lastEnemyMove = 0;
unsigned long lastFireTime = 0;
unsigned long bossActionTimer = 0;
bool buttonsReleased = true;
unsigned long btnWhitePressTime = 0;
bool btnWhiteHeld = false;
unsigned long comboTimer = 0;
bool isWaitingForCombo = false; 

std::vector<Enemy> enemies;
std::vector<Shot> shots;
std::vector<BossSegment> bossSegments; 
std::vector<BossProjectile> bossProjectiles; 
float enemyFrontIndex = -1.0; 
int currentLevel = 1;
int currentBossType = 0;

// Boss Vars
Boss2State boss2State = B2_MOVE;
int boss2Section = 0; int boss2ShotsFired = 0;
int boss2LockedColor = 1; int markerPos[3]; int boss2TargetShots = 10; 
int boss1WrongHits = 0; bool boss1RageMode = false;
int boss1RageShots = 0;
Boss3State boss3State = B3_MOVE;
int boss3PhaseIndex = 0; int boss3BurstCounter = 0; int boss3Markers[2];
int currentScore = 0;
int highScore = 0; int lastGames[3] = {0, 0, 0};
unsigned long levelStartTime = 0;
int levelMaxPossibleScore = 0;
int levelAchievedScore = 0;     

// --- BONUS 1 (BEATSABER) VARIABLES ---
bool bonusPlayedThisLevel = false;
bool autoBonusTrigger = false; // Triggered by Perfect Score
int bonusEnemiesSpawned = 0;
int bonusLives = 10;
int bonusWaveCount = 0;
unsigned long bonusPauseTimer = 0;
bool bonusInPause = false;
float bonusSpeedMultiplier = 1.0; 
unsigned long bonusFlashTimer = 0;
std::vector<Enemy> bonusEnemies; 
std::vector<Shot> bonusShots;    
int bonusReturnLevel = 1;

// --- BONUS 2 (SIMON SAYS) VARIABLES ---
SimonState simonState = S_MOVE;
int simonLives = 3;
int simonStage = 0;          
int simonStopIndex = 0;      
float simonBossPos = 0.0;
std::vector<int> simonFullSequence; // Der KOMPLETTE Code
int simonPlaybackIdx = 0;    
int simonInputIdx = 0;       
unsigned long simonTimer = 0;
int simonTargetPos = 0;      

// --------------------------------------------------------------------------
// 3. HELPER FUNCTIONS
// --------------------------------------------------------------------------
CRGB hexToCRGB(String hex) {
  long number = strtol(&hex[1], NULL, 16);
  return CRGB((number >> 16) & 0xFF, (number >> 8) & 0xFF, number & 0xFF);
}

Melody parseSoundString(String data) {
  Melody m;
  if (data.length() == 0) return m;
  int start = 0;
  int end = data.indexOf(';');
  while (end != -1) {
    String pair = data.substring(start, end);
    int comma = pair.indexOf(',');
    if (comma != -1) {
      ToneCmd t;
      t.freq = pair.substring(0, comma).toInt();
      t.duration = pair.substring(comma + 1).toInt();
      m.push_back(t);
    }
    start = end + 1;
    end = data.indexOf(';', start);
  }
  String pair = data.substring(start);
  int comma = pair.indexOf(',');
  if (comma != -1) {
    ToneCmd t;
    t.freq = pair.substring(0, comma).toInt();
    t.duration = pair.substring(comma + 1).toInt();
    m.push_back(t);
  }
  return m;
}

void melodyFromStr(Melody& m, String s) { m = parseSoundString(s); }

// --------------------------------------------------------------------------
// 4. AUDIO ENGINE
// --------------------------------------------------------------------------
void playSound(SoundEvent evt) {
    if (!config_sound_on) return;
    xQueueSend(audioQueue, &evt, 0);
}

void playShotSound(int color) {
    switch(color) {
        case 1: playSound(EVT_SHOT_BLUE); break;
        case 2: playSound(EVT_SHOT_RED); break;
        case 3: playSound(EVT_SHOT_GREEN); break;
        case 4: playSound(EVT_SHOT_YELLOW); break; 
        case 5: playSound(EVT_SHOT_MAGENTA); break; 
        case 6: playSound(EVT_SHOT_CYAN); break; 
        case 7: playSound(EVT_SHOT_WHITE); break;
        default: playSound(EVT_SHOT_BLUE); break;
    }
}

void playToneI2S(int freq, int durationMs) {
    if (freq <= 0) {
        size_t bytes_written;
        int samples = (SAMPLE_RATE * durationMs) / 1000;
        int16_t *buffer = (int16_t *)malloc(samples * 2);
        memset(buffer, 0, samples * 2);
        i2s_write(I2S_NUM_0, buffer, samples * 2, &bytes_written, portMAX_DELAY);
        free(buffer);
        return;
    }
    size_t bytes_written;
    int samples = (SAMPLE_RATE * durationMs) / 1000;
    int16_t *buffer = (int16_t *)malloc(samples * 2);
    int halfPeriod = SAMPLE_RATE / freq / 2;
    int16_t volume = map(config_volume_pct, 0, 100, 0, 10000);
    for (int i = 0; i < samples; i++) {
        buffer[i] = ((i / halfPeriod) % 2 == 0) ? volume : -volume;
    }
    i2s_write(I2S_NUM_0, buffer, samples * 2, &bytes_written, portMAX_DELAY);
    free(buffer);
}

void audioTask(void *parameter) {
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT, 
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 4,
        .dma_buf_len = 512,
        .use_apll = false,
        .tx_desc_auto_clear = true
    };
    i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_BCLK,
        .ws_io_num = I2S_LRC,
        .data_out_num = I2S_DOUT,
        .data_in_num = I2S_PIN_NO_CHANGE 
    };
    i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_NUM_0, &pin_config);
    i2s_zero_dma_buffer(I2S_NUM_0);

    const Melody* currentMelody = nullptr;
    int noteIndex = 0;
    while(true) {
        SoundEvent newEvent;
        if (xQueueReceive(audioQueue, &newEvent, 0) == pdTRUE) {
            bool play = true;
            if ((currentMelody == &melWin || currentMelody == &melLose || currentMelody == &melFinalWin) && newEvent != EVT_START) {
                 play = false;
            }
            if (play) {
                switch(newEvent) {
                    case EVT_START:      currentMelody = &melStart; break;
                    case EVT_SHOT_BLUE:  currentMelody = &melShotBlue; break;
                    case EVT_SHOT_RED:   currentMelody = &melShotRed; break;
                    case EVT_SHOT_GREEN: currentMelody = &melShotGreen; break;
                    case EVT_SHOT_WHITE: currentMelody = &melShotWhite; break;
                    case EVT_SHOT_YELLOW: currentMelody = &melShotY; break;
                    case EVT_SHOT_MAGENTA:currentMelody = &melShotM; break;
                    case EVT_SHOT_CYAN:   currentMelody = &melShotC; break;
                    case EVT_MISTAKE:    currentMelody = &melMistake; break;
                    case EVT_HIT_SUCCESS:currentMelody = &melHit; break;
                    case EVT_WIN:        currentMelody = &melWin; break;
                    case EVT_FINAL_WIN:  currentMelody = &melFinalWin; break;
                    case EVT_LOSE:       currentMelody = &melLose; break;
                    case EVT_BONUS_START: currentMelody = &melWin; break; 
                    case EVT_BONUS_WAVE: currentMelody = &melShotWhite; break;
                    case EVT_BONUS_SPEEDUP: currentMelody = &melSpeedUp; break;
                    default: break;
                }
                noteIndex = 0;
                i2s_zero_dma_buffer(I2S_NUM_0);
            }
        }

        if (currentMelody != nullptr) {
            if (noteIndex >= currentMelody->size()) {
                currentMelody = nullptr;
                playToneI2S(0, 20); 
                i2s_zero_dma_buffer(I2S_NUM_0);
            } else {
                ToneCmd t = (*currentMelody)[noteIndex];
                playToneI2S(t.freq, t.duration);
                noteIndex++;
            }
        } else {
            vTaskDelay(5 / portTICK_PERIOD_MS);
        }
    }
}

// --------------------------------------------------------------------------
// 5. GRAPHICS ENGINE
// --------------------------------------------------------------------------
CRGB getColor(int colorCode) {
  switch (colorCode) {
    case 1: return col_c1; // Blue
    case 2: return col_c2; // Red
    case 3: return col_c3; // Green
    case 4: return col_c4; // Yellow
    case 5: return col_c5; // Magenta
    case 6: return col_c6; // Cyan
    case 7: return col_cw; // White  
    default: return CRGB::Black;
  }
}

void drawCrispPixel(float pos, CRGB color) {
    int idx = round(pos);
    if (idx < 0 || idx >= config_num_leds) return;
    leds[idx + ledStartOffset] = color;
}

void flashPixel(int pos) {
  if(pos >= 0 && pos < config_num_leds) leds[pos + ledStartOffset] = CRGB::White;
}

// --------------------------------------------------------------------------
// 6. LOGIC & CONFIGURATION
// --------------------------------------------------------------------------
void saveHighscores() {
  preferences.begin("game", false);
  preferences.putInt((currentProfilePrefix + "hs").c_str(), highScore);
  preferences.putInt((currentProfilePrefix + "l1").c_str(), lastGames[0]);
  preferences.putInt((currentProfilePrefix + "l2").c_str(), lastGames[1]);
  preferences.putInt((currentProfilePrefix + "l3").c_str(), lastGames[2]);
  preferences.putULong("st_shots", stat_totalShots);
  preferences.putULong("st_kills", stat_totalKills);
  preferences.putULong("st_b3kills", stat_boss3Kills); 
  preferences.end();
}

void loadHighscores() {
  preferences.begin("game", true);
  highScore = preferences.getInt((currentProfilePrefix + "hs").c_str(), 0);
  lastGames[0] = preferences.getInt((currentProfilePrefix + "l1").c_str(), 0);
  lastGames[1] = preferences.getInt((currentProfilePrefix + "l2").c_str(), 0);
  lastGames[2] = preferences.getInt((currentProfilePrefix + "l3").c_str(), 0);
  stat_totalShots = preferences.getULong("st_shots", 0);
  stat_totalKills = preferences.getULong("st_kills", 0);
  stat_boss3Kills = preferences.getULong("st_b3kills", 0); 
  preferences.end();
}

void registerGameEnd(int finalScore) {
  lastGames[2] = lastGames[1];
  lastGames[1] = lastGames[0];
  lastGames[0] = finalScore;
  if (finalScore > highScore) highScore = finalScore;
  saveHighscores();
  bonusPlayedThisLevel = false; 
}

void triggerBaseDestruction() {
  playSound(EVT_LOSE);
  registerGameEnd(currentScore);
  currentState = STATE_BASE_DESTROYED;
  stateTimer = millis();
}

void calculateLevelScore() {
  unsigned long duration = millis() - levelStartTime;
  int entityCount = 0;
  int calcLevel = currentLevel;
  if(currentLevel > 10) calcLevel = ((currentLevel - 1) % 10) + 1;
  if (currentLevel <= 10 && levels[currentLevel].bossType > 0) {
    if (levels[currentLevel].bossType == 1) entityCount = 9 * boss1Cfg.hpPerLed;
    else if (levels[currentLevel].bossType == 2) entityCount = 9 * boss2Cfg.hpPerLed;
    else if (levels[currentLevel].bossType == 3) entityCount = 15 * boss3Cfg.hpPerLed;
  } else {
    entityCount = levels[calcLevel].length;
  }
  
  int levelMultiplier = currentLevel;
  int basePoints = entityCount * 100 * levelMultiplier;
  
  unsigned long targetTime = 0;
  
  // BALANCING
  if (currentLevel <= 10 && levels[currentLevel].bossType == 2) {
      // MASTERBLASTER (Boss 2): Harder Time Limit
      targetTime = 36000;
  }
  else {
      // Tank & Normal Levels: Standard calc
      unsigned long travelTime = config_num_leds * 15;
      unsigned long processingTime = entityCount * 300; 
      targetTime = 3000 + travelTime + processingTime;
  }
  
  int timeBonus = 0;
  int maxTimeBonus = basePoints * 3;
  
  if (currentLevel <= 10 && levels[currentLevel].bossType == 3) {
      timeBonus = maxTimeBonus;
  } else {
      if (duration <= targetTime) timeBonus = maxTimeBonus;
      else {
        float ratio = (float)targetTime / (float)duration;
        timeBonus = (int)(maxTimeBonus * ratio);
      }
  }
  
  levelAchievedScore = basePoints + timeBonus;
  levelMaxPossibleScore = basePoints * 4; 
  currentScore += levelAchievedScore;
}

void checkWinCondition() {
  bool won = false;
  if (currentState == STATE_PLAYING && enemies.empty()) won = true;
  if (currentState == STATE_BOSS_PLAYING && bossSegments.empty()) won = true;
  if (won) {
    calculateLevelScore();
    
    // CHECK PERFECT SCORE (Automatic Bonus Trigger)
    if (levelAchievedScore >= levelMaxPossibleScore) {
        autoBonusTrigger = true;
    } else {
        autoBonusTrigger = false;
    }

    if (currentLevel <= 10 && levels[currentLevel].bossType == 3) {
        stat_boss3Kills++;
        saveHighscores();
    }

    if (!config_endless_mode && currentLevel >= 10) {
      playSound(EVT_FINAL_WIN); 
      registerGameEnd(currentScore);
      currentState = STATE_GAME_FINISHED;
    } else {
      playSound(EVT_WIN);
      currentState = STATE_LEVEL_COMPLETED;
      stateTimer = millis();
    }
  }
}

void startLevelIntro(int level) {
  playSound(EVT_START);
  if (level == config_start_level) {
     currentScore = 0;
     stat_lastGameShots = 0; 
  }
  
  if (level != currentLevel) {
      bonusPlayedThisLevel = false;
  }
  
  currentLevel = level; currentState = STATE_INTRO; stateTimer = millis();
  FastLED.clear();
  for(int i=0; i<config_num_leds; i++) leds[i+ledStartOffset] = CRGB(10,10,10);
  CRGB barColor = (level <= 10 && levels[level].bossType > 0) ? col_c2 : col_c3;
  int center = config_num_leds / 2;
  
  int displayLevel = (level > 10) ? 10 : level;
  int totalWidth = (displayLevel * 6) + ((displayLevel-1)*4); 
  int startPos = center - (totalWidth/2);
  if(startPos < 0) startPos = 0;
  int cursor = startPos;
  for(int i=0; i<displayLevel; i++) {
    for(int k=0; k<6; k++) { if(cursor < config_num_leds) leds[cursor + ledStartOffset] = barColor; cursor++; }
    cursor += 4; 
  }
  if(config_sacrifice_led) leds[0] = CRGB(20, 0, 0); 
  FastLED.show();
}

void drawLevelIntro(int level) {
  FastLED.clear();
  for(int i=0; i<config_num_leds; i++) leds[i+ledStartOffset] = CRGB(5,5,5);
  CRGB barColor = (level <= 10 && levels[level].bossType > 0) ? col_c2 : col_c3;
  int center = config_num_leds / 2;
  int displayLevel = (level > 10) ? 10 : level;
  int totalWidth = (displayLevel * 6) + ((displayLevel-1)*4);
  int startPos = center - (totalWidth/2); if(startPos < 0) startPos = 0;
  int cursor = startPos;
  for(int i=0; i<displayLevel; i++) {
    for(int k=0; k<6; k++) { if(cursor < config_num_leds) leds[cursor + ledStartOffset] = barColor; cursor++; }
    cursor += 4; 
  }
  if(config_sacrifice_led) leds[0] = CRGB(20, 0, 0);
  FastLED.show();
}

// --------------------------------------------------------------------------
// SIMON SAYS ENGINE (CUMULATIVE VERSION)
// --------------------------------------------------------------------------
int getSimonStageLength(int stage) {
    int lens[] = {4, 5, 6, 8, 9, 11, 13, 15, 17};
    if (stage >= 0 && stage <= 8) return lens[stage];
    return 17;
}

void generateSimonSequence() {
    simonFullSequence.clear();
    int totalMaxLen = 17; 
    
    for(int i=0; i<totalMaxLen; i++) {
        int c = 1;
        if (i < 8) {
            c = random(1, 4); 
        } else {
            c = random(1, 7);
        }
        simonFullSequence.push_back(c);
    }
}

void startSimonBonus() {
    simonLives = 3;
    simonStage = 0;
    simonStopIndex = 0;
    simonBossPos = (float)config_num_leds - 1.0;
    generateSimonSequence(); 
    currentState = STATE_BONUS_SIMON;
    simonState = S_MOVE;
    simonTargetPos = config_num_leds - ((simonStopIndex + 1) * (config_num_leds / 12));
    playSound(EVT_BONUS_WAVE);
}

void updateSimonBonus() {
    unsigned long now = millis();
    int bossLen = 9 - simonStage; 
    int currentSeqLen = getSimonStageLength(simonStage);

    if (simonLives <= 0 || (simonState == S_MOVE && simonBossPos <= config_homebase_size)) {
        playSound(EVT_LOSE);
        startLevelIntro(bonusReturnLevel);
        return;
    }

    switch(simonState) {
        case S_MOVE: {
            float spd = 20.0 + (simonStage * 3.0);
            simonBossPos -= (spd / 60.0);
            if (simonBossPos <= simonTargetPos) {
                simonBossPos = (float)simonTargetPos; 
                simonState = S_PREPARE;
                simonTimer = now;
                playSound(EVT_SHOT_WHITE); 
            }
            break;
        }
        case S_PREPARE: {
            if (now - simonTimer > 1000) {
                simonState = S_SHOW;
                simonPlaybackIdx = 0;
                simonTimer = now;
                
                // ====================================================================
                // FIX: Spiele den ersten Ton direkt beim Start der Wiedergabe ab, 
                // da das Licht ebenfalls sofort eingeschaltet wird!
                // ====================================================================
                playShotSound(simonFullSequence[simonPlaybackIdx]);
            }
            break;
        }
        case S_SHOW: {
            int delayMs = 600 - (simonStage * 40); 
            if (simonStage >= 5) delayMs = 400; 
            if (simonStage >= 7) delayMs = 300;

            if (now - simonTimer > delayMs) {
                // ====================================================================
                // FIX: Zuerst den Index erhöhen, da das Delay abgelaufen ist.
                // Danach sofort den Ton für den NEUEN Index aufrufen, um ihn mit 
                // der LED zu synchronisieren.
                // ====================================================================
                simonPlaybackIdx++;
                simonTimer = now;
                
                if (simonPlaybackIdx < currentSeqLen) {
                    playShotSound(simonFullSequence[simonPlaybackIdx]);
                } else {
                    simonState = S_INPUT;
                    simonInputIdx = 0;
                    buttonsReleased = true; 
                    isWaitingForCombo = false;
                }
            }
            break;
        }
        case S_INPUT: {
            bool b = (digitalRead(PIN_BTN_BLUE) == LOW);
            bool r = (digitalRead(PIN_BTN_RED) == LOW); 
            bool g = (digitalRead(PIN_BTN_GREEN) == LOW);
            bool pressed = (b || r || g);

            if (!pressed) { buttonsReleased = true; isWaitingForCombo = false; }
            
            if (pressed && buttonsReleased && !isWaitingForCombo && (now - lastFireTime > FIRE_COOLDOWN)) {
                 isWaitingForCombo = true;
                 comboTimer = now;
            }
            
            if (isWaitingForCombo && (now - comboTimer >= INPUT_BUFFER_MS)) {
                int c = 0;
                b = (digitalRead(PIN_BTN_BLUE) == LOW); 
                r = (digitalRead(PIN_BTN_RED) == LOW); 
                g = (digitalRead(PIN_BTN_GREEN) == LOW);
                
                if (r && g && b) c = 7; 
                else if (r && g) c = 4; // Yellow
                else if (r && b) c = 5; // Magenta
                else if (g && b) c = 6; // Cyan
                else if (b) c = 1; 
                else if (r) c = 2; 
                else if (g) c = 3;

                if (c > 0) {
                    playShotSound(c);
                    if (c == simonFullSequence[simonInputIdx]) {
                        simonInputIdx++;
                        if (simonInputIdx >= currentSeqLen) {
                            simonState = S_SUCCESS;
                            simonTimer = now;
                            playSound(EVT_HIT_SUCCESS);
                            currentScore += (250 * (simonStage + 1));
                        }
                    } else {
                        simonState = S_FAIL;
                        simonTimer = now;
                        playSound(EVT_MISTAKE);
                        simonLives--;
                    }
                    lastFireTime = now;
                }
                buttonsReleased = false; 
                isWaitingForCombo = false;
            }
            break;
        }
        case S_SUCCESS: {
            if (now - simonTimer > 1000) {
                 simonStage++;
                 if (simonStage >= 9) {
                     playSound(EVT_FINAL_WIN);
                     startLevelIntro(bonusReturnLevel);
                     return;
                 }
                 simonStopIndex++;
                 simonTargetPos = config_num_leds - ((simonStopIndex + 1) * (config_num_leds / 12));
                 simonState = S_MOVE;
            }
            break;
        }
        case S_FAIL: {
            if (now - simonTimer > 1000) {
                simonStopIndex++;
                simonTargetPos = config_num_leds - ((simonStopIndex + 1) * (config_num_leds / 12));
                simonState = S_MOVE;
            }
            break;
        }
    }

    FastLED.clear();
    
    // Draw Marker
    if (simonState == S_MOVE) {
        if(simonTargetPos >= 0 && simonTargetPos < config_num_leds)
            leds[simonTargetPos + ledStartOffset] = CRGB::Red;
    }

    // Draw Boss
    for(int i=0; i < bossLen; i++) {
        int pixelPos = (int)simonBossPos + i;
        if (pixelPos >= config_num_leds) continue;
        
        CRGB c = CRGB::Black;
        if (simonState == S_MOVE || simonState == S_PREPARE) {
             c = CHSV((i*20) + (millis()/10), 255, 255);
        } else if (simonState == S_SHOW) {
             if (i == 0) { 
                 int delayMs = 600 - (simonStage * 40); if(simonStage>=5) delayMs=400; if(simonStage>=7) delayMs=300;
                 long elapsedShow = now - simonTimer;
                 if (simonPlaybackIdx < currentSeqLen && elapsedShow < (delayMs - 100)) {
                     c = getColor(simonFullSequence[simonPlaybackIdx]);
                 } else {
                     c = CRGB::White; 
                 }
             } else {
                 c = CRGB::White;
             }
        } else if (simonState == S_INPUT) {
             c = CRGB::White;
        } else if (simonState == S_SUCCESS) {
             if (i==0) c = ((millis()/50)%2==0) ? CRGB::Red : CRGB::Yellow;
             else c = CRGB::Green;
        } else if (simonState == S_FAIL) {
             c = CRGB::Red;
        }

        if(pixelPos >= 0) leds[pixelPos + ledStartOffset] = c;
    }

    for(int i=0; i<simonLives; i++) {
        if(i+ledStartOffset < config_num_leds) leds[i+ledStartOffset] = CRGB::Blue;
    }

    if(config_sacrifice_led) leds[0] = CRGB(20,0,0); 
    FastLED.show();
}

// --------------------------------------------------------------------------
// ORIGINAL BONUS GAME & LEVEL LOGIC
// --------------------------------------------------------------------------
void startBonusGame() {
    bonusEnemiesSpawned = 0;
    bonusLives = 10;
    bonusWaveCount = 0;
    bonusInPause = false;
    bonusSpeedMultiplier = 1.0;
    bonusFlashTimer = 0;
    bonusEnemies.clear();
    bonusShots.clear();
    lastFireTime = millis();
    currentState = STATE_BONUS_PLAYING;
    playSound(EVT_BONUS_WAVE);
}

void updateLevelIntro() {
  if (!bonusPlayedThisLevel && (currentLevel <= 10 && levels[currentLevel].bossType > 0)) {
      if (digitalRead(PIN_BTN_RED) == LOW && digitalRead(PIN_BTN_BLUE) == LOW && digitalRead(PIN_BTN_GREEN) == LOW) {
           bonusPlayedThisLevel = true;
           bonusReturnLevel = currentLevel;
           currentState = STATE_BONUS_INTRO;
           stateTimer = millis();
           playSound(EVT_BONUS_START);
           return;
      }
  }

  unsigned long elapsed = millis() - stateTimer;
  if (elapsed > 2000 && elapsed < 4000) {
    if ((elapsed / 250) % 2 == 0) drawLevelIntro(currentLevel);
    else { FastLED.clear(); if(config_sacrifice_led) leds[0] = CRGB(20, 0, 0); FastLED.show(); }
  } else if (elapsed <= 2000) { drawLevelIntro(currentLevel);
  }

  if (elapsed >= 4000) {
    uint8_t bright = map(config_brightness_pct, 10, 100, 25, 255);
    FastLED.setBrightness(bright);
    levelStartTime = millis(); 
    
    bool isBossLevel = (currentLevel <= 10 && levels[currentLevel].bossType > 0);
    if (isBossLevel) {
      currentBossType = levels[currentLevel].bossType;
      bossSegments.clear(); enemies.clear(); shots.clear(); bossProjectiles.clear();
      enemyFrontIndex = (float)config_num_leds - 1.0;
      if (currentBossType == 1) {
        for(int i=0; i<3; i++) bossSegments.push_back({3, boss1Cfg.hpPerLed, boss1Cfg.hpPerLed, true, 0});
        for(int i=0; i<3; i++) bossSegments.push_back({1, boss1Cfg.hpPerLed, boss1Cfg.hpPerLed, true, 0});
        for(int i=0; i<3; i++) bossSegments.push_back({3, boss1Cfg.hpPerLed, boss1Cfg.hpPerLed, true, 0});
        bossActionTimer = millis();
        boss1WrongHits = 0;
        boss1RageMode = false;
      } 
      else if (currentBossType == 2) {
        for(int i=0; i<9; i++) bossSegments.push_back({0, boss2Cfg.hpPerLed, boss2Cfg.hpPerLed, false, i});
        boss2Section = 0; boss2State = B2_MOVE;
        markerPos[0] = (int)(config_num_leds * (boss2Cfg.m1 / 100.0));
        markerPos[1] = (int)(config_num_leds * (boss2Cfg.m2 / 100.0));
        markerPos[2] = (int)(config_num_leds * (boss2Cfg.m3 / 100.0));
      } 
      else if (currentBossType == 3) {
        for(int i=0; i<15; i++) { int mixColor = random(4, 7);
        bossSegments.push_back({mixColor, boss3Cfg.hpPerLed, boss3Cfg.hpPerLed, true, i}); }
        boss3State = B3_MOVE; 
        boss3PhaseIndex = 0;
        boss3Markers[0] = (int)(config_num_leds * 0.66);
        boss3Markers[1] = (int)(config_num_leds * 0.50);
        bossActionTimer = millis();
      }
      currentState = STATE_BOSS_PLAYING;
    } else {
      currentBossType = 0;
      enemies.clear(); shots.clear(); bossProjectiles.clear();
      
      int effectiveLevel = currentLevel;
      if (currentLevel > 10) effectiveLevel = ((currentLevel - 1) % 10) + 1;
      int count = levels[effectiveLevel].length;
      if (count <= 0) count = 10;
      for (int i = 0; i < count; i++) {
          int color = random(1, 4);
          if (currentLevel >= 11) color = random(1, 7); 
          enemies.push_back({color, 0.0});
      }
      enemyFrontIndex = (float)config_num_leds - 1.0;
      currentState = STATE_PLAYING;
    }
  }
}

void updateBonusIntro() {
    unsigned long elapsed = millis() - stateTimer;
    if (elapsed < 2500) {
        if ((elapsed / 250) % 2 == 0) {
            FastLED.clear();
            for(int i=0; i<config_num_leds; i+=2) leds[i+ledStartOffset] = CRGB::Yellow;
        } else {
            FastLED.clear();
        }
        if(config_sacrifice_led) leds[0] = CRGB(20,0,0);
        FastLED.show();
    } else {
        if (random(0, 100) < 50) {
            startSimonBonus();
        } else {
            startBonusGame();
        }
    }
}

void updateBonusGame() {
    unsigned long now = millis();
    bool r = (digitalRead(PIN_BTN_RED) == LOW);
    bool g = (digitalRead(PIN_BTN_GREEN) == LOW);
    bool pressed = (r || g);
    
    if (!pressed) buttonsReleased = true;
    if (pressed && buttonsReleased && (now - lastFireTime > FIRE_COOLDOWN)) {
        int c = 0;
        if (r) c = 2; // Red
        else if (g) c = 3; // Green
        
        if (c > 0) {
            bonusShots.push_back({0.0, c});
            playShotSound(c);
            lastFireTime = now;
            buttonsReleased = false;
        }
    }
    
    if (bonusEnemiesSpawned < 200) {
        if (bonusInPause) {
            if (now - bonusPauseTimer > 2000) {
                bonusInPause = false;
                bonusWaveCount = 0; 
                bonusSpeedMultiplier += 0.2;
                bonusFlashTimer = now;
                playSound(EVT_BONUS_SPEEDUP);
            }
        } else {
            static unsigned long lastBonusSpawn = 0;
            int spawnRate = (int)(600.0 / bonusSpeedMultiplier);
            if (now - lastBonusSpawn > spawnRate) { 
                lastBonusSpawn = now;
                int color = (random(0,2) == 0) ? 2 : 3; 
                bonusEnemies.push_back({color, (float)config_num_leds - 1.0, false});
                bonusEnemiesSpawned++;
                bonusWaveCount++;
                if (bonusWaveCount >= 25) {
                    bonusInPause = true;
                    bonusPauseTimer = now;
                }
            }
        }
    }
    
    float shotSpeed = (float)config_shot_speed_pct / 60.0 * 0.8;
    for (int i = bonusShots.size() - 1; i >= 0; i--) {
        bonusShots[i].position += shotSpeed;
        bool remove = false;
        
        if (!bonusEnemies.empty()) {
             for(int e=0; e<bonusEnemies.size(); e++) {
                 if (abs(bonusShots[i].position - bonusEnemies[e].pos) < 1.0) {
                     if (bonusShots[i].color == bonusEnemies[e].color) {
                         bonusEnemies.erase(bonusEnemies.begin() + e);
                         currentScore += 500; 
                         flashPixel((int)bonusShots[i].position);
                         remove = true;
                         break;
                     } else {
                         remove = true;
                         bonusLives--;
                         playSound(EVT_MISTAKE);
                         break;
                     }
                 }
             }
        }
        
        if (!remove && bonusShots[i].position >= config_num_leds) {
            remove = true;
            bonusLives--;
            playSound(EVT_MISTAKE);
        }
        
        if (remove) bonusShots.erase(bonusShots.begin() + i);
    }
    
    float enemySpeed = (25.0 / 60.0) * bonusSpeedMultiplier;
    for (int i = bonusEnemies.size() - 1; i >= 0; i--) {
        bonusEnemies[i].pos -= enemySpeed;
        if (bonusEnemies[i].pos <= config_homebase_size) {
            bonusEnemies.erase(bonusEnemies.begin() + i);
            bonusLives--;
            playSound(EVT_MISTAKE);
        }
    }
    
    if (bonusLives <= 0) {
        playSound(EVT_LOSE);
        startLevelIntro(bonusReturnLevel); 
        return;
    }
    
    if (bonusEnemiesSpawned >= 200 && bonusEnemies.empty()) {
        playSound(EVT_FINAL_WIN);
        startLevelIntro(bonusReturnLevel); 
        return;
    }

    FastLED.clear();
    bool doFlash = (now - bonusFlashTimer < 200) && (bonusFlashTimer > 0);
    for(auto &e : bonusEnemies) {
        CRGB c = getColor(e.color);
        if (doFlash) c = CRGB::White; 
        drawCrispPixel(e.pos, c);
    }
    for(auto &s : bonusShots) {
        drawCrispPixel(s.position, getColor(s.color));
    }
    for(int i=0; i<bonusLives; i++) {
        if(i+ledStartOffset < config_num_leds) leds[i+ledStartOffset] = CRGB::Yellow;
    }
    if(config_sacrifice_led) leds[0] = CRGB(20,0,0); 
    FastLED.show();
}

void updateLevelCompletedAnim() {
  unsigned long elapsed = millis() - stateTimer;
  if (elapsed < 1000) {
    fill_solid(leds, config_num_leds + ledStartOffset, col_c3);
    if(config_sacrifice_led) leds[0] = CRGB(20,0,0);
  } else if (elapsed < 5000) { 
    FastLED.clear();
    if(config_sacrifice_led) leds[0] = CRGB(20,0,0);
    float pct = (float)levelAchievedScore / (float)levelMaxPossibleScore;
    if (pct > 1.0) pct = 1.0;
    int fillLeds = (int)(config_num_leds * pct);
    for(int i=0; i<fillLeds; i++) leds[i+ledStartOffset] = CRGB(80, 60, 0); 
    for(int i=fillLeds; i<config_num_leds; i++) leds[i+ledStartOffset] = CRGB(20, 0, 0);
  } else {
    if (autoBonusTrigger) {
        autoBonusTrigger = false;
        bonusPlayedThisLevel = true;
        bonusReturnLevel = currentLevel + 1; 
        currentState = STATE_BONUS_INTRO;
        stateTimer = millis();
        playSound(EVT_BONUS_START);
    } else {
        startLevelIntro(currentLevel + 1);
    }
  }
  FastLED.show();
}

void updateBaseDestroyedAnim() {
  unsigned long elapsed = millis() - stateTimer;
  if (elapsed < 2000) {
    CRGB c = (elapsed / 100) % 2 == 0 ? col_c2 : CRGB::White;
    for(int i=0; i<config_homebase_size; i++) {
       if (i+ledStartOffset < config_num_leds) leds[i+ledStartOffset] = c;
    }
    for(int i=config_homebase_size; i<config_num_leds; i++) {
       leds[i+ledStartOffset].nscale8(240);
    }
    if(config_sacrifice_led) leds[0] = CRGB(20,0,0);
    FastLED.show();
  } else {
    currentState = STATE_GAMEOVER;
  }
}

void moveBossProjectiles(float speed) {
  static unsigned long lastMove = 0;
  float step = (float)speed / 60.0;
  if (step < 0.1) step = 0.1;
  for(int i=bossProjectiles.size()-1; i>=0; i--) {
      bossProjectiles[i].pos -= step;
      if (bossProjectiles[i].pos < config_homebase_size) {
        triggerBaseDestruction();
      }
  }
}

// --------------------------------------------------------------------------
// 7. CONFIG & WEB HANDLERS
// --------------------------------------------------------------------------
void loadColors() {
  preferences.begin("colors", true);
  hex_c1 = preferences.getString("c1", "#0000FF");
  hex_c2 = preferences.getString("c2", "#FF0000");
  hex_c3 = preferences.getString("c3", "#00FF00");
  hex_c4 = preferences.getString("c4", "#FFFF00");
  hex_c5 = preferences.getString("c5", "#FF00FF");
  hex_c6 = preferences.getString("c6", "#00FFFF");
  hex_cw = preferences.getString("cw", "#FFFFFF");
  hex_cb = preferences.getString("cb", "#222222");
  preferences.end();
  
  col_c1 = hexToCRGB(hex_c1);
  col_c2 = hexToCRGB(hex_c2);
  col_c3 = hexToCRGB(hex_c3);
  col_c4 = hexToCRGB(hex_c4);
  col_c5 = hexToCRGB(hex_c5);
  col_c6 = hexToCRGB(hex_c6);
  col_cw = hexToCRGB(hex_cw);
  col_cb = hexToCRGB(hex_cb);
}

void handleSaveColors() {
    if(server.hasArg("c1")) hex_c1 = server.arg("c1");
    if(server.hasArg("c2")) hex_c2 = server.arg("c2");
    if(server.hasArg("c3")) hex_c3 = server.arg("c3");
    if(server.hasArg("c4")) hex_c4 = server.arg("c4");
    if(server.hasArg("c5")) hex_c5 = server.arg("c5");
    if(server.hasArg("c6")) hex_c6 = server.arg("c6");
    if(server.hasArg("cw")) hex_cw = server.arg("cw");
    if(server.hasArg("cb")) hex_cb = server.arg("cb");
    preferences.begin("colors", false);
    preferences.putString("c1", hex_c1);
    preferences.putString("c2", hex_c2);
    preferences.putString("c3", hex_c3);
    preferences.putString("c4", hex_c4);
    preferences.putString("c5", hex_c5);
    preferences.putString("c6", hex_c6);
    preferences.putString("cw", hex_cw);
    preferences.putString("cb", hex_cb);
    preferences.end();
    loadColors();
    server.sendHeader("Location", "/colors"); 
    server.send(303);
}

void loadSounds() {
  preferences.begin("snds", true);
  cfg_snd_start     = preferences.getString("s_start", DEF_SND_START);
  cfg_snd_win       = preferences.getString("s_win", DEF_SND_WIN);
  cfg_snd_lose      = preferences.getString("s_lose", DEF_SND_LOSE);
  cfg_snd_mistake   = preferences.getString("s_mistake", DEF_SND_MISTAKE);
  cfg_snd_hit       = preferences.getString("s_hit", DEF_SND_HIT);
  cfg_snd_shot_b    = preferences.getString("s_shot_b", DEF_SND_SHOT_BLUE);
  cfg_snd_shot_r    = preferences.getString("s_shot_r", DEF_SND_SHOT_RED);
  cfg_snd_shot_g    = preferences.getString("s_shot_g", DEF_SND_SHOT_GREEN);
  cfg_snd_shot_w    = preferences.getString("s_shot_w", DEF_SND_SHOT_WHITE);
  preferences.end();

  melodyFromStr(melStart, cfg_snd_start);
  melodyFromStr(melWin, cfg_snd_win);
  melodyFromStr(melLose, cfg_snd_lose);
  melodyFromStr(melMistake, cfg_snd_mistake);
  melodyFromStr(melHit, cfg_snd_hit);
  melodyFromStr(melShotBlue, cfg_snd_shot_b);
  melodyFromStr(melShotRed, cfg_snd_shot_r);
  melodyFromStr(melShotGreen, cfg_snd_shot_g);
  melodyFromStr(melShotWhite, cfg_snd_shot_w);
  melodyFromStr(melFinalWin, DEF_SND_FINAL_WIN);
  melodyFromStr(melSpeedUp, DEF_SND_SPEEDUP);
  
  melodyFromStr(melShotY, DEF_SND_SHOT_Y);
  melodyFromStr(melShotM, DEF_SND_SHOT_M);
  melodyFromStr(melShotC, DEF_SND_SHOT_C);
}

void handleSaveSounds() {
  cfg_snd_start = server.arg("s_start");
  cfg_snd_win = server.arg("s_win");
  cfg_snd_lose = server.arg("s_lose");
  cfg_snd_mistake = server.arg("s_mistake");
  cfg_snd_hit = server.arg("s_hit");
  cfg_snd_shot_b = server.arg("s_shot_b");
  cfg_snd_shot_r = server.arg("s_shot_r");
  cfg_snd_shot_g = server.arg("s_shot_g");
  cfg_snd_shot_w = server.arg("s_shot_w");

  preferences.begin("snds", false);
  preferences.putString("s_start", cfg_snd_start);
  preferences.putString("s_win", cfg_snd_win);
  preferences.putString("s_lose", cfg_snd_lose);
  preferences.putString("s_mistake", cfg_snd_mistake);
  preferences.putString("s_hit", cfg_snd_hit);
  preferences.putString("s_shot_b", cfg_snd_shot_b);
  preferences.putString("s_shot_r", cfg_snd_shot_r);
  preferences.putString("s_shot_g", cfg_snd_shot_g);
  preferences.putString("s_shot_w", cfg_snd_shot_w);
  preferences.end();

  loadSounds(); 
  server.sendHeader("Location", "/sounds"); 
  server.send(303);
}

void applyProfileDefaults(String prefix) {
  if (prefix == "def_") {
    // STANDARD PROFILE
    levels[1] = {5, 15, 0};
    levels[2] = {6, 20, 0}; 
    levels[3] = {7, 25, 2}; 
    levels[4] = {8, 30, 0};
    levels[5] = {9, 35, 0};
    levels[6] = {10, 40, 1}; 
    levels[7] = {20, 20, 0}; levels[8] = {20, 25, 0};
    levels[9] = {10, 60, 0};
    levels[10] = {14, 60, 3}; 
    boss1Cfg = {4, 60, 4, 30, 0, 0,0,0};
    boss2Cfg = {10, 60, 5, 40, 0, 85, 55, 30}; 
    boss3Cfg = {7, 50, 3, 60, 3, 0,0,0};
  } else if (prefix == "kid_") {
    levels[1] = {5, 15, 0};
    levels[2] = {5, 20, 0};
    levels[3] = {6, 25, 2}; 
    levels[4] = {6, 20, 0}; levels[5] = {7, 25, 0};
    levels[6] = {10, 40, 1};
    levels[7] = {8, 30, 0}; levels[8] = {8, 35, 0}; levels[9] = {10, 20, 0};
    levels[10] = {14, 60, 3};
    boss1Cfg = {4, 60, 2, 40, 0, 0,0,0};
    boss2Cfg = {7, 40, 3, 40, 0, 85, 55, 30};
    boss3Cfg = {4, 40, 1, 80, 1, 0,0,0};
  } else { 
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
  preferences.putString("ssid",s);
  preferences.putString("pass",p); 
  preferences.putBool("sip_on",sip); preferences.putString("sip_ip",ip); 
  preferences.putString("sip_gw",gw); preferences.putString("sip_sn",sn); preferences.putString("sip_dns",dns); 
  
  preferences.putBool("sac_led", true); 
  preferences.putInt("hb_size", 3);
  preferences.putInt("shot_spd", 100); 
  
  preferences.putBool("snd_on", true);
  preferences.putInt("snd_vol", 50);
  preferences.putBool("endless", false);

  preferences.putULong("st_shots", 0);
  preferences.putULong("st_kills", 0);
  preferences.putULong("st_b3kills", 0);

  preferences.putInt("version", CONFIG_VERSION); 
  preferences.putString("act_prof", "def_");
  preferences.end(); 

  preferences.begin("snds", false); preferences.clear(); preferences.end();
  preferences.begin("colors", false); preferences.clear(); preferences.end();

  applyProfileDefaults("def_"); saveCurrentToPreferences("def_");
  applyProfileDefaults("kid_"); saveCurrentToPreferences("kid_");
  applyProfileDefaults("pro_"); saveCurrentToPreferences("pro_");
  applyProfileDefaults("def_");
}

void setupDefaultConfig() { applyProfileDefaults("def_");
}

void loadConfig(String prefix) {
  preferences.begin("game", true);
  config_num_leds = preferences.getInt((prefix+"leds").c_str(), config_num_leds);
  config_brightness_pct = preferences.getInt((prefix+"bright").c_str(), config_brightness_pct);
  config_start_level = preferences.getInt((prefix+"startlvl").c_str(), config_start_level);
  config_ssid = preferences.getString("ssid", ""); config_pass = preferences.getString("pass", "");
  config_static_ip = preferences.getBool("sip_on", false);
  config_ip = preferences.getString("sip_ip", "");
  config_gateway = preferences.getString("sip_gw", "");
  config_subnet = preferences.getString("sip_sn", ""); config_dns = preferences.getString("sip_dns", "");
  
  config_sacrifice_led = preferences.getBool("sac_led", true);
  config_homebase_size = preferences.getInt("hb_size", 3);
  config_shot_speed_pct = preferences.getInt("shot_spd", 100);
  ledStartOffset = config_sacrifice_led ? 1 : 0;
  
  config_sound_on = preferences.getBool("snd_on", true);
  config_volume_pct = preferences.getInt("snd_vol", 50);
  config_endless_mode = preferences.getBool("endless", false);

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
    preferences.begin("game", false);
    preferences.putString("act_prof", currentProfilePrefix); preferences.end(); 
    applyProfileDefaults(currentProfilePrefix); loadConfig(currentProfilePrefix); loadHighscores(); 
    server.sendHeader("Location", "/"); server.send(303); 
  } else server.send(400, "text/plain", "Bad Request"); 
}

void handleReset() { performFactoryReset();
  server.send(200, "text/html", "<h2>Reset successful!</h2><p>Values & Scores wiped. ESP restarting.</p>"); delay(1000); ESP.restart();
}

void handleSave() { 
  if (server.hasArg("leds")) config_num_leds = server.arg("leds").toInt(); if (server.hasArg("bright")) config_brightness_pct = server.arg("bright").toInt(); if (server.hasArg("startlvl")) config_start_level = server.arg("startlvl").toInt();
  if (server.hasArg("ssid")) config_ssid = server.arg("ssid"); if (server.hasArg("pass")) config_pass = server.arg("pass"); 
  config_static_ip = server.hasArg("static_ip"); config_ip = server.arg("ip"); config_gateway = server.arg("gw");
  config_subnet = server.arg("sn"); config_dns = server.arg("dns"); 
  if (server.hasArg("hb_size")) config_homebase_size = server.arg("hb_size").toInt();
  if (server.hasArg("shot_spd")) config_shot_speed_pct = server.arg("shot_spd").toInt();
  config_sacrifice_led = server.hasArg("sac_led");
  config_sound_on = server.hasArg("snd_on");
  config_endless_mode = server.hasArg("endless");
  if(server.hasArg("vol")) config_volume_pct = server.arg("vol").toInt();

  preferences.begin("game", false); 
  preferences.putString("ssid", config_ssid); preferences.putString("pass", config_pass); preferences.putBool("sip_on", config_static_ip); preferences.putString("sip_ip", config_ip);
  preferences.putString("sip_gw", config_gateway);
  preferences.putString("sip_sn", config_subnet); preferences.putString("sip_dns", config_dns); 
  preferences.putBool("sac_led", config_sacrifice_led); preferences.putInt("hb_size", config_homebase_size);
  preferences.putInt("shot_spd", config_shot_speed_pct);
  
  preferences.putBool("snd_on", config_sound_on);
  preferences.putInt("snd_vol", config_volume_pct);
  preferences.putBool("endless", config_endless_mode);
  String p = currentProfilePrefix; preferences.putInt((p+"leds").c_str(), config_num_leds);
  preferences.putInt((p+"bright").c_str(), config_brightness_pct); preferences.putInt((p+"startlvl").c_str(), config_start_level); 
  for(int i=1; i<=10; i++) { levels[i].speed = server.arg("lspd"+String(i)).toInt();
  levels[i].length = server.arg("llen"+String(i)).toInt(); levels[i].bossType = server.arg("lboss"+String(i)).toInt(); preferences.putInt((p+"l"+String(i)+"s").c_str(), levels[i].speed);
  preferences.putInt((p+"l"+String(i)+"l").c_str(), levels[i].length); preferences.putInt((p+"l"+String(i)+"b").c_str(), levels[i].bossType); } 
  boss1Cfg.moveSpeed = server.arg("b1mv").toInt();
  boss1Cfg.shotSpeed = server.arg("b1ss").toInt(); boss1Cfg.hpPerLed = server.arg("b1hp").toInt(); boss1Cfg.shotFreq = server.arg("b1fr").toInt();
  preferences.putBytes((p+"b1").c_str(), &boss1Cfg, sizeof(BossConfig)); 
  boss2Cfg.moveSpeed = server.arg("b2mv").toInt(); boss2Cfg.shotSpeed = server.arg("b2ss").toInt();
  boss2Cfg.hpPerLed = server.arg("b2hp").toInt(); boss2Cfg.shotFreq = server.arg("b2fr").toInt(); 
  boss2Cfg.m1 = server.arg("b2m1").toInt();
  boss2Cfg.m2 = server.arg("b2m2").toInt(); boss2Cfg.m3 = server.arg("b2m3").toInt(); 
  preferences.putBytes((p+"b2").c_str(), &boss2Cfg, sizeof(BossConfig));
  boss3Cfg.moveSpeed = server.arg("b3mv").toInt(); boss3Cfg.shotSpeed = 0; boss3Cfg.hpPerLed = server.arg("b3hp").toInt();
  boss3Cfg.shotFreq = server.arg("b3fr").toInt(); boss3Cfg.burstCount = server.arg("b3bc").toInt();
  preferences.putBytes((p+"b3").c_str(), &boss3Cfg, sizeof(BossConfig)); 
  preferences.end();
  server.send(200, "text/html", "<h2>Saved!</h2><p>ESP restarting...</p><a href='/'>Go Back</a>"); delay(1000); ESP.restart();
}

String getUpdateHTML() {
  String h = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  h += "<title>Firmware Update</title>";
  h += "<style>body{font-family:sans-serif;background:#111;color:#eee;padding:20px;max-width:600px;margin:auto;} h2{color:#0f0;} input{width:100%;margin-bottom:10px;padding:10px;}</style>";
  h += "</head><body><h2>SYSTEM UPDATE</h2>";
  h += "<p>Upload .bin file from Arduino IDE (Sketch -> Export Compiled Binary).</p>";
  h += "<form method='POST' action='/update' enctype='multipart/form-data'><input type='file' name='update'><br><button type='submit' style='padding:15px;background:#00f;color:white;font-weight:bold;width:100%;cursor:pointer;'>UPDATE FIRMWARE</button></form>";
  h += "<br><a href='/' style='color:#0f0;'>Back</a></body></html>";
  return h;
}

String getColorHTML() {
  String h = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  h += "<title>Color Config</title>";
  h += "<style>body{font-family:sans-serif;background:#111;color:#eee;padding:10px;max-width:800px;margin:auto;}input,button{width:100%;background:#333;color:#fff;border:1px solid #555;padding:8px;border-radius:4px;box-sizing:border-box;margin-bottom:5px;} .sec{margin-top:15px;border-top:1px solid #555;padding-top:15px;background:#222;padding:15px;border-radius:5px;} h3{margin-top:0;color:#0f0;} input[type=color] { height: 50px; cursor: pointer; } a { color: #00ff00; text-decoration: none; }</style>";
  h += "</head><body>";
  h += "<h2>🎨 CUSTOM COLORS</h2>";
  h += "<form action='/savecolors' method='POST'>";
  
  h += "<div class='sec'><h3>Main Enemies (Types 1-3)</h3>";
  h += "Type 1 (Blue btn): <input type='color' name='c1' value='" + hex_c1 + "'>";
  h += "Type 2 (Red btn): <input type='color' name='c2' value='" + hex_c2 + "'>";
  h += "Type 3 (Green btn): <input type='color' name='c3' value='" + hex_c3 + "'></div>";
  h += "<div class='sec'><h3>Boss / Mix Colors</h3>";
  h += "Mix Color 1: <input type='color' name='c4' value='" + hex_c4 + "'>";
  h += "Mix Color 2: <input type='color' name='c5' value='" + hex_c5 + "'>";
  h += "Mix Color 3: <input type='color' name='c6' value='" + hex_c6 + "'>";
  h += "Boss Generic/Dark: <input type='color' name='cb' value='" + hex_cb + "'></div>";

  h += "<div class='sec'><h3>Player Shots</h3>";
  h += "Combo Shot (White): <input type='color' name='cw' value='" + hex_cw + "'></div>";
  
  h += "<br><button type='submit' style='background:#009900;font-size:1.2em;'>SAVE COLORS</button>";
  h += "</form><br><a href='/'>&laquo; Back to Main Menu</a>";
  h += "</body></html>";
  return h;
}

String getSoundHTML() {
  String h = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  h += "<title>Sound Config</title>";
  h += "<style>body{font-family:sans-serif;background:#111;color:#eee;padding:10px;max-width:800px;margin:auto;}input,button{width:100%;background:#333;color:#fff;border:1px solid #555;padding:8px;border-radius:4px;box-sizing:border-box;margin-bottom:5px;} .sec{margin-top:15px;border-top:1px solid #555;padding-top:15px;background:#222;padding:15px;border-radius:5px;} h3{margin-top:0;color:#0f0;} .info{font-size:0.9em;color:#aaa;background:#333;padding:10px;border-radius:4px;margin-bottom:15px;} a { color: #00ff00; text-decoration: none; }</style>";
  h += "</head><body>";
  h += "<h2>🎵 CUSTOM SOUND EDITOR</h2>";
  h += "<div class='info'><b>How to use:</b><br>Format: <code>Freq,Duration;Freq,Duration</code><br>Example: <code>523,100;659,100</code> (Plays C5 then E5)<br>Leave empty for silence.</div>";
  h += "<form action='/savesounds' method='POST'>";
  h += "<div class='sec'><h3>Game Events</h3>";
  h += "Start: <input name='s_start' value='" + cfg_snd_start + "'>";
  h += "Win Level: <input name='s_win' value='" + cfg_snd_win + "'>";
  h += "Game Over: <input name='s_lose' value='" + cfg_snd_lose + "'>";
  h += "Mistake/Grow: <input name='s_mistake' value='" + cfg_snd_mistake + "'>";
  h += "Hit Success: <input name='s_hit' value='" + cfg_snd_hit + "'></div>";
  h += "<div class='sec'><h3>Player Shots</h3>";
  h += "Blue Shot: <input name='s_shot_b' value='" + cfg_snd_shot_b + "'>";
  h += "Red Shot: <input name='s_shot_r' value='" + cfg_snd_shot_r + "'>";
  h += "Green Shot: <input name='s_shot_g' value='" + cfg_snd_shot_g + "'>";
  h += "White Shot: <input name='s_shot_w' value='" + cfg_snd_shot_w + "'></div>";
  h += "<br><button type='submit' style='background:#009900;font-size:1.2em;'>SAVE SOUNDS</button>";
  h += "</form><br><a href='/'>&laquo; Back to Main Menu</a>";
  h += "</body></html>";
  return h;
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
  h += " .stat-grid { display:flex; gap:10px; text-align:center; } .stat-box { flex:1; background:#222; padding:10px; border-radius:5px; border:1px solid #444; } .stat-val { font-size:1.5em; font-weight:bold; color:#0ff; }";
  h += "</style>";
  h += "<script>var boss3Kills = " + String(stat_boss3Kills) + ";";
  h += "function updateCalc() { var count = parseInt(document.getElementById('ledCount').value)||0; var brightPct = parseInt(document.getElementById('brightness').value)||50; document.getElementById('brightVal').innerText = brightPct + '%'; var warn = document.getElementById('warning-box'); if (count > 0) { var brightFactor = brightPct / 100.0; var amps = ((count * 15 * brightFactor) + 120) / 1000; document.getElementById('ampValue').innerText = amps.toFixed(2) + ' A'; if(amps > 5.0) { warn.style.display='block'; warn.innerText='WARNING: > 5A! High Power PSU required!'; } else warn.style.display='none'; } } function toggleIP() { var x = document.getElementById('ipsettings'); if(document.getElementById('chkStatic').checked) x.style.display='block'; else x.style.display='none'; } function confirmReset() { return confirm('Really delete all settings and factory reset?'); } ";
  h += "function checkEndless(cb) { if(boss3Kills < 1) { alert('Defeat the RGB Overlord first to unlock this mode!'); cb.checked = false; return false; } return true; } ";
  h += "window.onload = function(){ updateCalc(); toggleIP(); };</script>";
  h += "</head><body>";
  h += "<div class='neon-text'>RGB INVADERS</div>";
h += "<div class='sub-head'>created by WorksAsDesigned - <span style='cursor:pointer; text-decoration:underline;' onclick='alert(\"Thanks janosch12 for finding the simon says sound bug :-)\")'>Version 10.9.5 </span></div>";
  h += "<div class='score-box'>ALL TIME BEST<div class='big-score'>" + String(highScore) + "</div>";
  h += "<div class='small-score'>Last Games: " + String(lastGames[0]) + " | " + String(lastGames[1]) + " | " + String(lastGames[2]) + "</div></div>";
  h += "<div class='sec'><h3>Battle Statistics</h3><div class='stat-grid'>";
  h += "<div class='stat-box'><div class='stat-val'>" + String(stat_totalShots) + "</div><div>Total Shots</div></div>";
  h += "<div class='stat-box'><div class='stat-val'>" + String(stat_totalKills) + "</div><div>Alien Kills</div></div>";
  h += "<div class='stat-box'><div class='stat-val'>" + String(stat_lastGameShots) + "</div><div>Last Game Shots</div></div>";
  h += "</div></div>";
  h += "<div style='display:flex;gap:10px;justify-content:center;margin-bottom:20px;margin-top:20px;'>";
  h += "<a href='/sounds' style='flex:1;'><button style='background:#ff00ff;font-weight:bold;font-size:1.1em;padding:12px;'>🎵 SOUNDS</button></a>";
  h += "<a href='/colors' style='flex:1;'><button style='background:#00ffff;color:#000;font-weight:bold;font-size:1.1em;padding:12px;'>🎨 COLOR CONFIG</button></a>";
  h += "</div>";

  String pName = (currentProfilePrefix == "def_") ? "Standard" : ((currentProfilePrefix == "kid_") ? "Kids" : "Pro");
  h += "<div class='sec'><h3>Profile Management</h3>Current Profile: <b>" + pName + "</b><br><form action='/loadprofile' method='POST' style='display:flex;gap:5px;margin-top:5px;'><select name='profile'><option value='def' " + String(currentProfilePrefix=="def_"?"selected":"") + ">Standard</option><option value='kid' " + String(currentProfilePrefix=="kid_"?"selected":"") + ">Kids</option><option value='pro' " + String(currentProfilePrefix=="pro_"?"selected":"") + ">Pro/Party</option></select><button type='submit'>Load Profile</button></form></div>";
  String maxLvl = (stat_boss3Kills > 0) ? "11" : "10";
  h += "<form action='/save' method='POST'><div class='sec'><h3>Hardware & General</h3>Start Level: <input type='number' name='startlvl' min='1' max='" + maxLvl + "' value='" + String(config_start_level) + "'><br>";
  h += "Total LEDs: <input id='ledCount' type='number' name='leds' value='" + String(config_num_leds) + "' oninput='updateCalc()'><br>";
  h += "<label>Sacrificial LED: <input type='checkbox' name='sac_led' value='1' " + String(config_sacrifice_led?"checked":"") + " style='width:auto;'></label><br>";
  h += "<label>Homebase Size: <input type='number' name='hb_size' min='1' max='5' value='" + String(config_homebase_size) + "'></label><br>";
  h += "<label>Player Shot Speed: <span id='shotVal'>" + String(config_shot_speed_pct) + "%</span></label><input type='range' name='shot_spd' min='50' max='150' value='" + String(config_shot_speed_pct) + "' oninput=\"document.getElementById('shotVal').innerText = this.value + '%';\"><br>";
  h += "<label>Default Brightness: <span id='brightVal'>" + String(config_brightness_pct) + "%</span></label><input id='brightness' type='range' name='bright' min='10' max='100' value='" + String(config_brightness_pct) + "' oninput='updateCalc()'><div style='margin-top:5px;'>Est. Current: <span id='ampValue' class='val-highlight'>0.00 A</span></div>";
  h += "<div style='margin-top:10px;border-top:1px dashed #555;padding-top:10px;'>";
  h += "<label>Sound Enabled: <input type='checkbox' name='snd_on' value='1' " + String(config_sound_on?"checked":"") + " style='width:auto;'></label><br>";
  h += "<label>Master Volume: <span id='volVal'>" + String(config_volume_pct) + "%</span></label><input type='range' name='vol' min='0' max='100' value='" + String(config_volume_pct) + "' oninput=\"document.getElementById('volVal').innerText = this.value + '%';\">";
  h += "</div>";
  
  h += "<div style='margin-top:10px;border-top:1px dashed #555;padding-top:10px;background:#2a2a2a;padding:10px;border-radius:5px;'>";
  String disab = (stat_boss3Kills > 0) ? "" : "disabled";
  h += "<label style='color:#00ff00;font-weight:bold;'>Endless Mode: <input type='checkbox' name='endless' value='1' " + String(config_endless_mode?"checked":"") + " onclick='return checkEndless(this)' style='width:auto;'></label><br><small>Unlocks infinite gameplay.</small>";
  h += "</div>";

  h += "<div id='warning-box'></div></div>";
  h += "<div class='sec'><h3>Network</h3><label>WiFi SSID:</label><input type='text' name='ssid' value='" + config_ssid + "' placeholder='WiFi Name'>Password: <input type='password' name='pass' value='" + config_pass + "'><br><input type='checkbox' id='chkStatic' name='static_ip' value='1' onchange='toggleIP()' " + String(config_static_ip ? "checked" : "") + " style='width:auto;'> Static IP<br><div id='ipsettings' style='display:none;margin-top:10px;'>IP: <input name='ip' value='" + config_ip + "'>Gateway: <input name='gw' value='" + config_gateway + "'>Subnet: <input name='sn' value='" + config_subnet + "'>DNS: <input name='dns' value='" + config_dns + "'></div></div>";
  h += "<div class='sec'><h3>Level Configuration</h3><table><thead><tr><th>Lvl</th><th>Speed</th><th>Len</th><th>Boss?</th></tr></thead><tbody>";
  for(int i=1; i<=10; i++) { h += "<tr><td data-label='Level'>" + String(i) + "</td><td data-label='Speed'><input name='lspd" + String(i) + "' value='" + String(levels[i].speed) + "'></td><td data-label='Len/Type'><input name='llen" + String(i) + "' value='" + String(levels[i].length) + "'></td><td data-label='Boss'><select name='lboss" + String(i) + "'><option value='0' " + String(levels[i].bossType==0?"selected":"") + ">-</option><option value='2' " + String(levels[i].bossType==2?"selected":"") + ">Masterblaster</option><option value='1' " + String(levels[i].bossType==1?"selected":"") + ">The Tank</option><option value='3' " + String(levels[i].bossType==3?"selected":"") + ">RGB Overlord</option></select></td></tr>";
  }
  h += "</tbody></table></div>";
  
  h += "<div class='sec'><h3>Boss Configuration</h3>";
  h += "<div class='b-card'><div class='b-head'>Masterblaster (Boss 1)</div><div class='form-grid'>";
  h += "<div class='f-item'><span class='lbl'>Move Speed</span><input name='b2mv' value='" + String(boss2Cfg.moveSpeed) + "'></div>";
  h += "<div class='f-item'><span class='lbl'>Shot Speed</span><input name='b2ss' value='" + String(boss2Cfg.shotSpeed) + "'></div>";
  h += "<div class='f-item'><span class='lbl'>HP/LED</span><input name='b2hp' value='" + String(boss2Cfg.hpPerLed) + "'></div>";
  h += "<div class='f-item'><span class='lbl'>Reload (0.1s)</span><input name='b2fr' value='" + String(boss2Cfg.shotFreq) + "'></div>";
  h += "<div class='f-item'><span class='lbl'>Mark 1 (%)</span><input name='b2m1' value='" + String(boss2Cfg.m1) + "'></div>";
  h += "<div class='f-item'><span class='lbl'>Mark 2 (%)</span><input name='b2m2' value='" + String(boss2Cfg.m2) + "'></div>";
  h += "<div class='f-item'><span class='lbl'>Mark 3 (%)</span><input name='b2m3' value='" + String(boss2Cfg.m3) + "'></div>";
  h += "</div></div>";
  h += "<div class='b-card'><div class='b-head'>The Tank (Boss 2)</div><div class='form-grid'>";
  h += "<div class='f-item'><span class='lbl'>Move Speed</span><input name='b1mv' value='" + String(boss1Cfg.moveSpeed) + "'></div>";
  h += "<div class='f-item'><span class='lbl'>Shot Speed</span><input name='b1ss' value='" + String(boss1Cfg.shotSpeed) + "'></div>";
  h += "<div class='f-item'><span class='lbl'>HP/LED</span><input name='b1hp' value='" + String(boss1Cfg.hpPerLed) + "'></div>";
  h += "<div class='f-item'><span class='lbl'>Shot Freq</span><input name='b1fr' value='" + String(boss1Cfg.shotFreq) + "'></div>";
  h += "</div></div>";
  h += "<div class='b-card'><div class='b-head'>RGB Overlord (Boss 3)</div><div class='form-grid'>";
  h += "<div class='f-item'><span class='lbl'>Move Speed</span><input name='b3mv' value='" + String(boss3Cfg.moveSpeed) + "'></div>";
  h += "<div class='f-item'><span class='lbl'>HP/LED</span><input name='b3hp' value='" + String(boss3Cfg.hpPerLed) + "'></div>";
  h += "<div class='f-item'><span class='lbl'>Shot Freq</span><input name='b3fr' value='" + String(boss3Cfg.shotFreq) + "'></div>";
  h += "<div class='f-item'><span class='lbl'>Burst Count</span><input name='b3bc' value='" + String(boss3Cfg.burstCount) + "'></div>";
  h += "</div></div>";
  
  h += "</div>";
  h += "<br><input type='submit' value='SAVE SETTINGS' style='width:100%;background:#009900;padding:15px;font-size:1.2em;cursor:pointer;font-weight:bold;'></form>";
  h += "<div style='margin-top:20px;text-align:center;'><a href='/update'><button style='background:#0044cc;padding:10px;font-size:1em;'>⬇ FIRMWARE UPDATE ⬇</button></a></div>";
  h += "<br><br><form action='/reset' method='POST' onsubmit='return confirmReset()'><button type='submit' style='background:#990000;padding:10px;'>FACTORY RESET (Clear Scores)</button></form>";
  h += "<div class='credits'><a href='https://paypal.me/WeisWernau' target='_blank'>paypal.me/WeisWernau</a><br><br><i>\"I don't need your money. But if I can buy my wife a bouquet of flowers, the chance increases that I can publish more funny projects - every married man knows what I'm talking about.\"</i></div>";
  h += "</body></html>";
  return h;
}

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

// --------------------------------------------------------------------------
// 8. SETUP & LOOP
// --------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  pinMode(PIN_BTN_BLUE, INPUT_PULLUP); pinMode(PIN_BTN_RED, INPUT_PULLUP); pinMode(PIN_BTN_GREEN, INPUT_PULLUP); pinMode(PIN_BTN_WHITE, INPUT_PULLUP);

  setupDefaultConfig();
  preferences.begin("game", true); 
  int storedVer = preferences.getInt("version", 0); 
  preferences.end();
  if (storedVer < CONFIG_VERSION) { performFactoryReset(); ESP.restart(); }
  preferences.begin("game", true);
  currentProfilePrefix = preferences.getString("act_prof", "def_"); preferences.end();
  loadConfig(currentProfilePrefix);        
  loadHighscores(); 
  loadSounds(); 
  loadColors();

  FastLED.addLeds<LED_TYPE, PIN_LED_DATA, COLOR_ORDER>(leds, config_num_leds + 1);
  FastLED.setBrightness(map(config_brightness_pct, 10, 100, 25, 255));
  FastLED.setDither(0);
  
  // 4. POWER PROTECTION
  FastLED.setMaxPowerInVoltsAndMilliamps(5, 2500); 
  
  if(config_sacrifice_led) leds[0] = CRGB(20, 0, 0);
  FastLED.show();

  // --- AUDIO SETUP CORE 0 ---
  audioQueue = xQueueCreate(10, sizeof(SoundEvent));
  xTaskCreatePinnedToCore(audioTask, "AudioTask", 4096, NULL, 1, &audioTaskHandle, 0);
  WiFi.mode(WIFI_OFF);
  
  // WEB ROUTES
  server.on("/", []() { server.send(200, "text/html", getHTML()); }); 
  server.on("/save", handleSave); 
  server.on("/loadprofile", handleProfileSwitch); 
  server.on("/reset", handleReset);
  server.on("/sounds", []() { server.send(200, "text/html", getSoundHTML()); });
  server.on("/savesounds", handleSaveSounds);
  server.on("/colors", []() { server.send(200, "text/html", getColorHTML()); });
  server.on("/savecolors", handleSaveColors);
  server.on("/admin/stats", [](){
      if(server.hasArg("hs")) highScore = server.arg("hs").toInt();
      if(server.hasArg("kills")) stat_totalKills = server.arg("kills").toInt(); 
      if(server.hasArg("shots")) stat_totalShots = server.arg("shots").toInt();
      if(server.hasArg("boss")) stat_boss3Kills = server.arg("boss").toInt(); // Manual unlock
      saveHighscores();
      server.send(200, "text/plain", "Stats restored manually.");
  });
  server.on("/update", HTTP_GET, []() { server.send(200, "text/html", getUpdateHTML()); });
  server.on("/update", HTTP_POST, []() {
    server.send(200, "text/plain", (Update.hasError()) ? "UPDATE FAILED" : "UPDATE SUCCESS! RESTARTING...");
    ESP.restart();
  }, []() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) Update.printError(Serial);
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) Serial.printf("Update Success: %u\n", upload.totalSize); else Update.printError(Serial);
    }
  });
  startLevelIntro(config_start_level);
}

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
    // EXIT WIFI MODE
    if (digitalRead(PIN_BTN_WHITE) == LOW) { delay(200); ESP.restart(); }
    return;
  }

  // 6. SETUP MODE FEEDBACK
  if (digitalRead(PIN_BTN_WHITE) == LOW) {
    if (!btnWhiteHeld) { btnWhiteHeld = true; btnWhitePressTime = now; } 
    else { 
       if (now - btnWhitePressTime > 3000) { 
           FastLED.clear();
           if ((now / 250) % 2 == 0) {
               for(int i=0; i<config_num_leds; i+=2) leds[i+ledStartOffset] = CRGB::Blue;
           }
           FastLED.show();
           return;
       }
    }
  } else {
    if (btnWhiteHeld) {
      unsigned long holdTime = now - btnWhitePressTime;
      btnWhiteHeld = false;
      if (holdTime > 3000) { 
          wifiMode = true; enableWiFi(); return; 
      } else if (holdTime < 1000) {
          startLevelIntro(config_start_level);
      }
    }
  }

  // GAME STATES DISPATCHER
  if (currentState == STATE_LEVEL_COMPLETED) { updateLevelCompletedAnim(); return; }
  if (currentState == STATE_BASE_DESTROYED) { updateBaseDestroyedAnim(); return; } 
  if (currentState == STATE_GAME_FINISHED) { for(int i=0; i<config_num_leds; i++) leds[i+ledStartOffset] = CHSV((now/10)+(i*5), 255, 255);
    if(config_sacrifice_led) leds[0]=CRGB(20,0,0); FastLED.show(); return; }
  if (currentState == STATE_GAMEOVER) { for(int i=0; i<config_num_leds; i++) leds[i+ledStartOffset] = CRGB::Red; if(config_sacrifice_led) leds[0]=CRGB(20,0,0);
    FastLED.show(); return; }
  
  if (currentState == STATE_INTRO) { updateLevelIntro(); return; }
  if (currentState == STATE_BONUS_INTRO) { updateBonusIntro(); return; }
  
  if (currentState == STATE_BONUS_PLAYING) { updateBonusGame(); return; }
  if (currentState == STATE_BONUS_SIMON)   { updateSimonBonus(); return; }

  // MAIN GAME LOGIC
  if (currentState == STATE_PLAYING || currentState == STATE_BOSS_PLAYING) {
    bool b = (digitalRead(PIN_BTN_BLUE) == LOW);
    bool r = (digitalRead(PIN_BTN_RED) == LOW); bool g = (digitalRead(PIN_BTN_GREEN) == LOW);
    bool isAnyBtnPressed = (b || r || g);
    if (!isAnyBtnPressed) { buttonsReleased = true; isWaitingForCombo = false; }
    
    // INPUT HANDLING
    if (currentBossType == 3 || currentLevel > 10) { 
       if (isAnyBtnPressed && buttonsReleased && !isWaitingForCombo && (now - lastFireTime > FIRE_COOLDOWN)) {
          isWaitingForCombo = true;
          comboTimer = now;
       }
       if (isWaitingForCombo && (now - comboTimer >= INPUT_BUFFER_MS)) {
          int c = 0;
          b = (digitalRead(PIN_BTN_BLUE) == LOW); r = (digitalRead(PIN_BTN_RED) == LOW); g = (digitalRead(PIN_BTN_GREEN) == LOW);
          if (r && g && b) c = 7; else if (r && g) c = 4;
          else if (r && b) c = 5; else if (g && b) c = 6;
          else if (b) c = 1; else if (r) c = 2; else if (g) c = 3;
          if (c > 0) { 
            shots.push_back({0.0, c}); 
            stat_totalShots++;
            stat_lastGameShots++; 
            lastFireTime = now; 
            playShotSound(c);
          } 
          buttonsReleased = false;
          isWaitingForCombo = false;
        }
    } else {
       if (isAnyBtnPressed && buttonsReleased && (now - lastFireTime > FIRE_COOLDOWN)) {
          int c = 0;
          if (b) c = 1; else if (r) c = 2; else if (g) c = 3;
          if (c > 0) { 
            shots.push_back({0.0, c}); 
            stat_totalShots++;
            stat_lastGameShots++; 
            lastFireTime = now; 
            playShotSound(c);
          } 
          buttonsReleased = false;
       }
    }

    // SHOT MOVEMENT
    float moveStep = (float)config_shot_speed_pct / 60.0;
    moveStep = moveStep * 0.6; 
    if(moveStep < 0.2) moveStep = 0.2;
    for (int i = shots.size() - 1; i >= 0; i--) {
        shots[i].position += moveStep;
        bool remove = false;
        
        // HIT DETECTION
        if (currentState == STATE_PLAYING) { 
          if (shots[i].position >= enemyFrontIndex && !enemies.empty()) { 
             if (shots[i].color == enemies[0].color) { 
                enemies.erase(enemies.begin());
                stat_totalKills++; 
                enemyFrontIndex += 1.0;
                flashPixel((int)shots[i].position); 
                remove = true; 
                checkWinCondition(); 
             } else { 
                enemies.insert(enemies.begin(), {shots[i].color, 0.0});
                enemyFrontIndex -= 1.0; 
                remove = true; 
                playSound(EVT_MISTAKE); 
             } 
          } 
        } 
        else if (currentState == STATE_BOSS_PLAYING) {
          // BOSS PROJECTILES COLLISION
          for(int p=0; p<bossProjectiles.size(); p++) { 
              if(shots[i].position >= bossProjectiles[p].pos) { 
                  if(shots[i].color == bossProjectiles[p].color) { 
                      bossProjectiles.erase(bossProjectiles.begin() + p);
                      flashPixel((int)shots[i].position); 
                  } 
                  remove = true;
                  break; 
              } 
          }

          // BOSS SEGMENT COLLISION
          if (!remove && shots[i].position >= enemyFrontIndex - 0.5 && !bossSegments.empty()) { 
             
             // GENERAL CLAMPING LOGIC FOR ALL BOSSES (v10.9.2 FIX)
             int hitIndex = (int)round(shots[i].position - enemyFrontIndex);
             
             // Prevent Tunneling: If shot is 'deep' inside/behind, clamp to last segment
             if (hitIndex >= bossSegments.size()) hitIndex = bossSegments.size() - 1;
             if (hitIndex < 0) hitIndex = 0;

             // Now hitIndex is always valid [0, size-1]
             if (hitIndex >= 0 && hitIndex < bossSegments.size()) { 
               bool vulnerable = false;
               if (currentBossType == 1) vulnerable = true; 
               else if (currentBossType == 2) { if (boss2State == B2_MOVE && bossSegments[hitIndex].active) vulnerable = true; } 
               else if (currentBossType == 3) { if (boss3State != B3_PHASE_CHANGE) vulnerable = true; } 
               
               if (vulnerable) { 
                 if (shots[i].color == bossSegments[hitIndex].color) { 
                    flashPixel((int)shots[i].position);
                    bossSegments[hitIndex].hp--; 
                    if (bossSegments[hitIndex].hp <= 0) { 
                        bossSegments.erase(bossSegments.begin() + hitIndex);
                        stat_totalKills++; 
                        if (hitIndex == 0) enemyFrontIndex += 1.0;
                        playSound(EVT_HIT_SUCCESS); 
                    } 
                    checkWinCondition();
                 } else {
                    if (currentBossType == 1) {
                         currentScore = (currentScore > 50) ? currentScore - 50 : 0;
                         if(!boss1RageMode) {
                            boss1WrongHits++;
                            if (boss1WrongHits >= 3) {
                                boss1RageMode = true;
                                boss1RageShots = 5; 
                                bossActionTimer = now;
                            }
                         }
                    }
                 }
               } remove = true;
             } 
          }
        }
        if (shots[i].position >= config_num_leds) remove = true;
        if (remove) shots.erase(shots.begin() + i);
    }
    
    // ENEMY MOVEMENT
    if (currentState == STATE_PLAYING) { 
        int spdVal = 0;
        int mapLevel = currentLevel;
        if(currentLevel > 10) mapLevel = ((currentLevel - 1) % 10) + 1;
        spdVal = levels[mapLevel].speed;
        float enemySpeed = (float)spdVal;
        float eStep = enemySpeed / 60.0; 
        enemyFrontIndex -= eStep; 
        if (enemyFrontIndex <= config_homebase_size) { triggerBaseDestruction(); } 
    }
    else if (currentState == STATE_BOSS_PLAYING) {
      int pSpeed = 60;
      if (currentBossType == 1) pSpeed = boss1Cfg.shotSpeed; if (currentBossType == 2) pSpeed = boss2Cfg.shotSpeed; 
      moveBossProjectiles((float)pSpeed);
      if (currentBossType == 1) { // THE TANK
        float bStep = (float)boss1Cfg.moveSpeed / 60.0;
        enemyFrontIndex -= bStep;
        if (enemyFrontIndex <= config_homebase_size) { triggerBaseDestruction(); } 
        
        if (boss1RageMode) {
             if (now - bossActionTimer > 200) { 
                 bossActionTimer = now;
                 int frontColor = 1; 
                 if(!bossSegments.empty()) frontColor = bossSegments[0].color;
                 int rageColor = 0;
                 do { rageColor = random(1, 4);
                 } while (rageColor == frontColor);
                 bossProjectiles.push_back({enemyFrontIndex, rageColor}); 
                 boss1RageShots--;
                 if (boss1RageShots <= 0) {
                     boss1RageMode = false;
                     boss1WrongHits = 0;
                 }
             }
        } else {
            if (now - bossActionTimer > (boss1Cfg.shotFreq * 100)) { 
               bossActionTimer = now;
               int shotColor = 0; int frontColor = 0; if(bossSegments.size() > 0) frontColor = bossSegments[0].color;
               if (random(100) < 20 && frontColor > 0) {
                  shotColor = frontColor;
               } else {
                  do { shotColor = random(1,4);
                  } while(shotColor == frontColor && frontColor > 0);
               }
               bossProjectiles.push_back({enemyFrontIndex, shotColor});
             } 
        }
      }
      else if (currentBossType == 2) { // MASTERBLASTER
          if (boss2State == B2_MOVE) {
             float bStep = (float)boss2Cfg.moveSpeed / 60.0;
             enemyFrontIndex -= bStep;
             if (boss2Section < 3) { if (enemyFrontIndex <= markerPos[boss2Section]) { 
                 boss2State = B2_CHARGE;
                 bossActionTimer = now;
                 int startRangeOfNewSection = 0;
                 if (boss2Section == 0) startRangeOfNewSection = 0;
                 else if (boss2Section == 1) startRangeOfNewSection = 3;
                 else startRangeOfNewSection = 6;
                 int oldSurvivors = 0;
                 for(auto &seg : bossSegments) {
                     if (seg.originalIndex < startRangeOfNewSection) oldSurvivors++;
                 }
                 boss2TargetShots = 10 + (oldSurvivors * 3);
             } } 
             if (enemyFrontIndex <= config_homebase_size) { triggerBaseDestruction(); } 
          }
          else if (boss2State == B2_CHARGE) { if (now - bossActionTimer < (boss2Cfg.shotFreq * 100)) { if (now % 100 < 20) boss2LockedColor = random(1,4);
            } else { boss2State = B2_SHOOT; boss2ShotsFired = 0; bossActionTimer = now; int startRange = 0; int endRange = 0;
            if (boss2Section == 0) { startRange=0; endRange=2; } else if (boss2Section == 1) { startRange=0; endRange=5; } else { startRange=0;
            endRange=8; } for(auto &seg : bossSegments) { if (seg.originalIndex >= startRange && seg.originalIndex <= endRange) seg.color = boss2LockedColor;
            } } } else if (boss2State == B2_SHOOT) { if (now - bossActionTimer > 150) { bossActionTimer = now;
            bossProjectiles.push_back({enemyFrontIndex, boss2LockedColor}); boss2ShotsFired++; 
          if (boss2ShotsFired >= boss2TargetShots) { // Variable shots
              int startRange = 0; int endRange = 0;
              if (boss2Section == 0) { startRange=0; endRange=2; } else if (boss2Section == 1) { startRange=3; endRange=5;
              } else { startRange=0; endRange=8; } for(auto &seg : bossSegments) { if (seg.originalIndex >= startRange && seg.originalIndex <= endRange) seg.active = true;
              } boss2State = B2_MOVE; boss2Section++; } } } 
      }
      else if (currentBossType == 3) { // OVERLORD
        float safeFireLimit = (config_num_leds > 180) ? 70.0 : (float)(config_homebase_size + 5);
        if (boss3State == B3_MOVE && boss3PhaseIndex < 2 && enemyFrontIndex <= boss3Markers[boss3PhaseIndex]) {
             boss3State = B3_PHASE_CHANGE; bossActionTimer = now; 
        }

        if (boss3State == B3_MOVE) {
           float bStep = (float)boss3Cfg.moveSpeed / 60.0;
           enemyFrontIndex -= bStep;
           if (enemyFrontIndex <= config_homebase_size) { triggerBaseDestruction(); }

           if (enemyFrontIndex > safeFireLimit && boss3Cfg.shotFreq > 0 && (now - bossActionTimer > (boss3Cfg.shotFreq * 100))) { 
               bossActionTimer = now;
               bossProjectiles.push_back({enemyFrontIndex, (int)random(1,4)}); 
           }
        } 
        else if (boss3State == B3_PHASE_CHANGE) {
           if (now - bossActionTimer > 4000) { 
               boss3State = B3_BURST; boss3BurstCounter = 0; 
               bossActionTimer = now; 
               for(auto &seg : bossSegments) seg.color = random(4, 8); 
               boss3PhaseIndex++;
           }
        }
        else if (boss3State == B3_BURST) {
           if (now - bossActionTimer > 200) { 
               bossActionTimer = now;
               if (enemyFrontIndex > safeFireLimit) {
                   bossProjectiles.push_back({enemyFrontIndex, (int)random(1,8)});
               }
               boss3BurstCounter++;
               if (boss3BurstCounter >= boss3Cfg.burstCount) { 
                   boss3State = B3_WAIT; bossActionTimer = now;
               } 
           }
        }
        else if (boss3State == B3_WAIT) {
            if (now - bossActionTimer > 2000) {
                boss3State = B3_MOVE; bossActionTimer = now; 
            }
        }
      }
    }
    
    FastLED.clear();
    if (currentState == STATE_BOSS_PLAYING) {
      if (currentBossType == 2) { for(int i=0; i<3; i++) { if (markerPos[i] < enemyFrontIndex) leds[markerPos[i]+ledStartOffset] = CRGB(50,0,0); } }
      else if (currentBossType == 3) { 
          if(boss3PhaseIndex <= 0) { leds[boss3Markers[0]+ledStartOffset] = CRGB(50,0,0); leds[boss3Markers[0]+ledStartOffset+1] = CRGB(50,0,0); }
          if(boss3PhaseIndex <= 1) { leds[boss3Markers[1]+ledStartOffset] = CRGB(50,0,0); leds[boss3Markers[1]+ledStartOffset+1] = CRGB(50,0,0); }
      }
    }
    if (currentState == STATE_PLAYING) { 
        for(int i=0; i<enemies.size(); i++) { drawCrispPixel(enemyFrontIndex + (float)i, getColor(enemies[i].color)); } 
    } 
    else if (currentState == STATE_BOSS_PLAYING) { 
      for(int i=0; i<bossSegments.size(); i++) { 
        float pos = enemyFrontIndex + (float)i;
        if (pos < config_num_leds && pos >=0) { 
           CRGB c = getColor(bossSegments[i].color);
           if (currentBossType == 1 && boss1RageMode) { if ((millis() / 50) % 2 == 0) c = CRGB::White; }
           
           if (currentBossType == 2) { c = col_cb;
           if (boss2State == B2_MOVE) { if (bossSegments[i].active) { c = getColor(bossSegments[i].color); if ((millis()/100)%2 == 0) c = CRGB::Black; } } 
           else if (boss2State == B2_CHARGE || boss2State == B2_SHOOT) { int oid = bossSegments[i].originalIndex; bool highlight = false;
           if (boss2Section == 0) { if (oid >= 0 && oid <= 2) highlight = true; } else if (boss2Section == 1) { if (oid >= 0 && oid <= 5) highlight = true;
           } else if (boss2Section >= 2) { highlight = true; } if (highlight) c = getColor(boss2LockedColor); } } 
           else if (currentBossType == 3 && boss3State == B3_PHASE_CHANGE) { c = CRGB::White; } 
           drawCrispPixel(pos, c);
        } 
      } 
      for(auto &p : bossProjectiles) { drawCrispPixel(p.pos, getColor(p.color)); } 
    }
    for(auto &s : shots) { drawCrispPixel(s.position, getColor(s.color)); }
    for(int i=0; i<config_homebase_size; i++) leds[i+ledStartOffset] = CRGB::White;
    if(config_sacrifice_led) leds[0] = CRGB(20,0,0); 
    FastLED.show();
  }
}
