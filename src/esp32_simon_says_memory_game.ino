// 1) I'm including some libraries because I need them for Wi-Fi, Blynk, LCD stuff, etc.
// Provision these from your Blynk Console -> Devices -> Auth Token / Template.
// Never commit real values to source control.
#define BLYNK_TEMPLATE_ID   "YOUR_BLYNK_TEMPLATE_ID"
#define BLYNK_TEMPLATE_NAME "YOUR_BLYNK_TEMPLATE_NAME"
#define BLYNK_AUTH_TOKEN    "YOUR_BLYNK_TOKEN_HERE"

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>

#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// Using FreeRTOS so we can run multiple tasks at once
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Wi-Fi credentials for the ESP32 to join the LAN.
// Replace the placeholders below with your own network details.
const char* ssid     = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// I2C pins we picked for LCD
#define I2C_SDA 16
#define I2C_SCL 4

// our LCD settings 
#define LCD_I2C_ADDR  0x27
#define LCD_COLS      16
#define LCD_ROWS      2

// These are the pins where our LEDs are connected
#define LED1 19
#define LED2 18
#define LED3 5
#define LED4 17

// These are the virtual pins we used in Blynk
#define VPIN_START  V0
#define VPIN_LED1   V2
#define VPIN_LED2   V3
#define VPIN_LED3   V4
#define VPIN_LED4   V5

LiquidCrystal_I2C lcd(LCD_I2C_ADDR, LCD_COLS, LCD_ROWS);

// 2) Here we are deciding how many levels and mistakes we allow in the game. 

const int MAX_LEVEL            = 20;    // number of levels
const int MAX_PATTERN_LENGTH   = 22;    // max number of leds lightened in levels
const int MAX_TOTAL_MISTAKES   = 10;   // If the player misses up 10 times overall, game over
const int MAX_MISTAKES_PER_LVL = 3;    // If the player misses up 3 times in one level, he/she lose

// 3) Shared stuff used by game and tasks

// we have 4 LED pins, so we put them in an array to index them easily
int ledPins[4] = {LED1, LED2, LED3, LED4};

// This is where we will store the pattern (sequence) the game shows.
int patternSequence[MAX_PATTERN_LENGTH];

// Variables that help us track the game's current state
bool isEnabled        = false;  // True if the game is started
bool isGameOver       = false;  // true if the game is over
bool isShowingPattern = false;  // True when we're showing the LED pattern to the player

int currentLevel      = 1;
int patternLength     = 0;
int currentStep       = 0;      // This is the next LED the player needs to match

// For tracking score and mistakes
int score             = 0;
int totalMistakes     = 0;
int mistakesThisLevel = 0;

// Flags to tell the GameTask what to do next
bool requestNewGame     = false;
bool requestShowPattern = false;  // If true, GameTask will begin showing the pattern

// Here are some variables for the non-blocking pattern display
int patternIndexToShow   = 0;       // Which step we currently on for showing the pattern
unsigned long lastBlinkTime = 0;    // Keeps track of when we last blinked 
bool ledOnPhase          = false;   // True if we're currently in the "LED ON" phase

// 4) Forward declarations of functions we used

void BlynkTask(void *pvParameters);
void GameTask(void *pvParameters);

void startNewGame();
void generatePattern(int level);
void requestPatternShow();
void handleUserInput(int pressedIndex);
void nextLevel();
void loseGame();
void winGame();
void resetGameState();

void updateLCD(const String &line1, const String &line2);
void showStatusLine(const String &msg);

// 5) In setup, I do a bunch of initial stuff
void setup() {
  Serial.begin(115200);
  Serial.println("Initializing...");

  // Setting up the pins for the LEDs as outputs and turning them off
  pinMode(LED1, OUTPUT);
  pinMode(LED2, OUTPUT);
  pinMode(LED3, OUTPUT);
  pinMode(LED4, OUTPUT);
  digitalWrite(LED1, LOW);
  digitalWrite(LED2, LOW);
  digitalWrite(LED3, LOW);
  digitalWrite(LED4, LOW);

  // Setting up our LCD 
  //(start I2C)
  //init the screen
  //backlight on 
  //clear
  Wire.begin(I2C_SDA, I2C_SCL);
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.print("Initializing...");

  // Connecting to the Wi-Fi network
  Serial.print("Connecting to Wi-Fi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected to Wi-Fi");

  // Connecting to Blynk so we can control stuff from our phone
  Serial.print("Connecting to Blynk");
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, password);
  Serial.println("\nConnected to Blynk");

  // Seeding our random numbers
  randomSeed(analogRead(0));

  // Let the user know we're ready
  updateLCD("System Ready", "Press Start!");

  // Creating two tasks: one for Blynk, one for the game
  xTaskCreatePinnedToCore(
    BlynkTask,         // This function does the Blynk stuff
    "BlynkTask",       // Name for debugging
    2048,              // Stack size
    NULL,              // Parameter
    1,                 // Priority
    NULL,              // Handle
    0                  // Run on core 0
  );

  xTaskCreatePinnedToCore(
    GameTask,          // This function does the game logic
    "GameTask",
    4096,
    NULL,
    1,
    NULL,
    1                  // Run on core 1
  );
}

void loop() {
  // We're not using this loop because we rely on FreeRTOS tasks
}

// 6) This is the Blynk task, it keeps Blynk running
void BlynkTask(void *pvParameters) {
  for (;;) {
    Blynk.run(); // Keep Blynk connection alive and handle events
    vTaskDelay(5 / portTICK_PERIOD_MS); // Tiny delay so other tasks get CPU time
  }
}

// 7) This task is for the game logic
void GameTask(void *pvParameters) {
  // We're using these durations for how long an LED stays on and off when showing pattern
  const int LED_ON_DURATION  = 400; // ms
  const int LED_OFF_DURATION = 200; // ms

  while (true) {
    // If we're supposed to show the pattern and we're not already showing it, and the game isn't over
    if (requestShowPattern && !isShowingPattern && !isGameOver) {
      // Start showing the pattern in a non-blocking way
      isShowingPattern = true;
      requestShowPattern = false;
      patternIndexToShow = 0;
      ledOnPhase = false;
      lastBlinkTime = millis();
      Serial.println("Starting non-blocking pattern display...");
      updateLCD("Showing Pattern", "");
    }

    // If we're currently in the middle of showing the pattern
    if (isShowingPattern) {
      unsigned long now = millis();

      // If we've shown all the steps, stop
      if (patternIndexToShow >= patternLength) {
        isShowingPattern = false;
        updateLCD("Your Turn!", "Repeat Pattern");
        Serial.println("Finished showing pattern. Waiting for user input...");
      }
      else {
        // We do LED ON then LED OFF
        if (!ledOnPhase) {
          // LED is currently off, so let's turn it on
          digitalWrite(ledPins[patternSequence[patternIndexToShow]], HIGH);
          ledOnPhase = true;
          lastBlinkTime = now;
        }
        else {
          // LED is on, check if it's time to turn it off
          if (now - lastBlinkTime >= LED_ON_DURATION) {
            // Turn off
            digitalWrite(ledPins[patternSequence[patternIndexToShow]], LOW);
            ledOnPhase = false;
            patternIndexToShow++;
            lastBlinkTime = now;

            // After a short off period, we'll show the next LED
            if (patternIndexToShow < patternLength) {
              vTaskDelay(LED_OFF_DURATION / portTICK_PERIOD_MS);
            }
          }
        }
      }
    }

    // Short delay before repeating, so we don't hog the CPU
    vTaskDelay(5 / portTICK_PERIOD_MS);
  }
}

// 8) These are the BLYNK_WRITE callbacks, so when we press a button on the Blynk app, they get triggered

// Start button
BLYNK_WRITE(VPIN_START) {
  int startState = param.asInt(); // it's 0 or 1
  isEnabled = (startState == 1);

  if (isEnabled) {
    Serial.println("[BLYNK] Start button: ENABLED -> Starting new game...");
    startNewGame();
  } else {
    Serial.println("[BLYNK] Start button: DISABLED -> Resetting game...");
    resetGameState();
  }
}

// LED button for index 0
BLYNK_WRITE(VPIN_LED1) {
  if (param.asInt() == 1) handleUserInput(0);
}

// LED button for index 1
BLYNK_WRITE(VPIN_LED2) {
  if (param.asInt() == 1) handleUserInput(1);
}

// LED button for index 2
BLYNK_WRITE(VPIN_LED3) {
  if (param.asInt() == 1) handleUserInput(2);
}

// LED button for index 3
BLYNK_WRITE(VPIN_LED4) {
  if (param.asInt() == 1) handleUserInput(3);
}

// 9) Here are the main game functions

// This happens when we press the Start button in the app
void startNewGame() {
  isGameOver       = false;
  currentLevel     = 1;
  score            = 0;
  totalMistakes    = 0;
  mistakesThisLevel= 0;

  Serial.println("Starting new game at Level 1");
  updateLCD("New Game", "Level 1 Start");
  
  generatePattern(currentLevel);
  requestPatternShow();
}

void generatePattern(int level) {
  // The pattern length is basically level + 2
  patternLength     = level + 2;
  currentStep       = 0; 
  mistakesThisLevel = 0;

  for (int i = 0; i < patternLength; i++) {
    patternSequence[i] = random(0, 4); // pick a random LED
  }
  // Debug info so we can see the pattern in Serial
  Serial.print("Generated Pattern [Level ");
  Serial.print(level);
  Serial.print("]: ");
  for (int i = 0; i < patternLength; i++) {
    Serial.print(patternSequence[i]);
    Serial.print(" ");
  }
  Serial.println();
}

// This just sets a flag so the GameTask knows to show the pattern
void requestPatternShow() {
  requestShowPattern = true;
}

// This is called when we press a LED button in Blynk
void handleUserInput(int pressedIndex) {
  // If the game isn't enabled, or it's over, or we're still showing pattern, ignore the input
  if (!isEnabled || isGameOver || isShowingPattern) {
    return;
  }

  Serial.printf("User pressed LED %d. Expected: %d\n",
                pressedIndex, patternSequence[currentStep]);

  if (pressedIndex == patternSequence[currentStep]) {
    // Right button
    currentStep++;
    score++;
    Serial.printf(
      "Correct press! Step=%d/%d, Score=%d, LevelMistakes=%d, TotalMistakes=%d\n",
      currentStep, patternLength, score, mistakesThisLevel, totalMistakes
    );
    showStatusLine("Correct!");

    // Did we complete the pattern?
    if (currentStep == patternLength) {
      Serial.printf("Level %d completed successfully!\n", currentLevel);
      nextLevel();
    }
  }
  else {
    // Wrong button
    mistakesThisLevel++;
    totalMistakes++;
    Serial.printf(
      "Wrong press! mistakesThisLevel=%d/%d, totalMistakes=%d/%d\n",
      mistakesThisLevel, MAX_MISTAKES_PER_LVL, 
      totalMistakes, MAX_TOTAL_MISTAKES
    );
    showStatusLine("Wrong!");

    // Did we lose for messing up too many times?
    if (mistakesThisLevel >= MAX_MISTAKES_PER_LVL) {
      Serial.printf("Level %d failed! Reached %d mistakes this level.\n",
                    currentLevel, mistakesThisLevel);
      loseGame();
    }
    else if (totalMistakes >= MAX_TOTAL_MISTAKES) {
      Serial.println("Reached max total mistakes (10). Game Over.");
      loseGame();
    }
    else {
      // Show the same pattern again so we can try again
      currentStep = 0;
      Serial.println("Re-showing pattern for the same level...");
      requestPatternShow();  
    }
  }
}

void nextLevel() {
  currentLevel++;
  if (currentLevel > MAX_LEVEL) {
    winGame();
    return;
  }

  Serial.printf("Advancing to level %d\n", currentLevel);
  updateLCD("Level Up!", String("Level ") + currentLevel);

  generatePattern(currentLevel);
  // Show the new pattern
  requestPatternShow();
}

void loseGame() {
  isGameOver = true;
  updateLCD("Game Over", "Press Start");
  
  Serial.println(">>>> GAME OVER <<<<");
  Serial.printf("Final Score=%d, LevelMistakes=%d, TotalMistakes=%d\n",
                score, mistakesThisLevel, totalMistakes);

  // Turn off everything
  for (int i=0; i<4; i++) {
    digitalWrite(ledPins[i], LOW);
  }
}

void winGame() {
  isGameOver = true;
  updateLCD("You Won!", "Press Start");

  Serial.println(">>>> YOU WON! <<<<");
  Serial.printf("Final Score=%d, TotalMistakes=%d\n", score, totalMistakes);

  // Blink all LEDs a few times so we can celebrate
  for (int k=0; k<5; k++) {
    for (int i=0; i<4; i++) {
      digitalWrite(ledPins[i], HIGH);
    }
    delay(200);
    for (int i=0; i<4; i++) {
      digitalWrite(ledPins[i], LOW);
    }
    delay(200);
  }
}

void resetGameState() {
  isGameOver        = false;
  currentLevel      = 1;
  patternLength     = 0;
  currentStep       = 0;
  score             = 0;
  totalMistakes     = 0;
  mistakesThisLevel = 0;
  isShowingPattern  = false;
  requestShowPattern = false;

  for (int i=0; i<4; i++) {
    digitalWrite(ledPins[i], LOW);
  }
  Serial.println("Game state reset. All LEDs off.");
  updateLCD("Game Reset", "Press Start");
}

// 10) Functions to handle the LCD

void updateLCD(const String &line1, const String &line2) {
  // Clear the screen and print the two lines
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(line1);
  lcd.setCursor(0, 1);
  lcd.print(line2);
}

void showStatusLine(const String &msg) {
  // If we say "Correct!", show it on top, 
  // and on bottom show something like "S=5 L=1 T=2" 
  // (score, mistakesThisLevel, totalMistakes)
  String secondLine = "S=" + String(score) 
                    + " L=" + String(mistakesThisLevel) 
                    + " T=" + String(totalMistakes);
  updateLCD(msg, secondLine);
}
