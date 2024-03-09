// Plants waterer (dual flusher)
// Copyright (c) 2024 Aleksandr.ru
// @see http://aleksandr.ru
//
// +-----------------------+
// | Энкодер               |
// +-----------------------+
// | GND    | Бел   | GND  |
// | S1 CLK | Чер   | D2   |
// | S2 DT  | Корич | D3   |
// | KEY SW | Крас  | D4   |
// | 5V     | Оранж | 5V   |
// +-----------------------+
// | Дисплей (снизу вверх) |
// +-----------------------+
// | GND | Фиол | GND      |
// | VCC | Син  | 5V       |
// | SCL | Зел  | A5       |
// | SDA | Жел  | A4       |
// +-----------------------+
// | Датчики: A1, A2       |
// +-----------------------+
// | Реле: D5, D6          |
// +-----------------------+
//
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Fonts/FreeSansBold9pt7b.h>

#define EB_NO_FOR           // отключить поддержку pressFor/holdFor/stepFor и счётчик степов (экономит 2 байта оперативки)
#define EB_NO_CALLBACK      // отключить обработчик событий attach (экономит 2 байта оперативки)
#define EB_NO_COUNTER       // отключить счётчик энкодера (экономит 4 байта оперативки)
// #define EB_NO_BUFFER        // отключить буферизацию энкодера (экономит 1 байт оперативки)
#include <EncButton.h>

#include <EEPROM.h>

// #define DEBUG 115200

#define ENCODER_S1  2
#define ENCODER_S2  3
#define ENCODER_KEY 4
#define ENCODER_REVERSE 1 // 0

#define RELAY1 5
#define RELAY2 6

#define SENSOR1 A1
#define SENSOR2 A2
#define MEASURE_DEBOUNCE 1500
#define SENSOR_MIN 0
#define SENSOR_MAX 1023

#define MENU_LENGTH 9
#define VALUE_FAST_STEP 5

#include "images.h"

Adafruit_SSD1306 display(128, 32, &Wire, -1);
EncButton eb(ENCODER_S1, ENCODER_S2, ENCODER_KEY);

typedef enum {
  MODE_STATUS,
  MODE_MENU,  
  MODE_VALUE,
  MODE_ACTION
} Modes;

typedef enum {
  MENU_FLUSH1,
  MENU_FLUSH2,
  MENU_TIME1,
  MENU_CHECK1,
  MENU_MIN1,
  MENU_TIME2,
  MENU_CHECK2,
  MENU_MIN2,
  MENU_EXIT
} MenuIds;

String menuItems[MENU_LENGTH] = {
  "Flush #1",
  "Flush #2",  
  "Time #1",  // sec
  "Check #1", // hour
  "Level #1", // percent
  "Time #2",  // sec
  "Check #2", // hour
  "Level #2", // percent
  "Exit"
};
String menuSubtext[MENU_LENGTH] = {
  "",
  "",
  "Seconds",
  "Hours",
  "Percent",
  "Seconds",
  "Hours",
  "Percent",
  ""
};
byte menuValues[MENU_LENGTH] = {
  0, 
  0, 
  3,  // sec
  2,  // minute 
  30, // percent
  5,  // sec
  10, // hour
  30, // percent
  0
};
byte menuValuesMax[MENU_LENGTH] = {
  0, 
  0, 
  254, 
  254, 
  99, 
  254, 
  254, 
  99, 
  0
};

byte mode = MODE_STATUS;
byte menuIndex = 0;

unsigned long lastCheck1 = 0;
unsigned long lastCheck2 = 0;
unsigned long actionStart = 0;

void setup() {
  #ifdef DEBUG
  Serial.begin(DEBUG);
  #endif

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  pinMode(RELAY1, OUTPUT);
  pinMode(RELAY2, OUTPUT);
  pinMode(SENSOR1, INPUT);
  pinMode(SENSOR2, INPUT);

  digitalWrite(RELAY1, HIGH);
  digitalWrite(RELAY2, HIGH);
  
  #ifdef DEBUG
  Serial.print("EEPROM:");
  #endif
  for(byte i = 0; i < MENU_LENGTH; i++) {
    byte val = EEPROM.read(i);
    #ifdef DEBUG
    Serial.print(" ("); Serial.print(i); 
    Serial.print( ")="); Serial.print(val);
    #endif
    if (menuValues[i] && val && !isnan(val) && !isinf(val) && val <= menuValuesMax[i]) {
      menuValues[i] = val;
      #ifdef DEBUG
      Serial.print("*");
      #endif
    }
  }
  #ifdef DEBUG
  Serial.println("");
  #endif

  eb.setEncReverse(ENCODER_REVERSE);
}

void loop() {
  eb.tick();

  if (eb.turn()) {
    int dir = eb.dir();

    switch(mode) {
      case MODE_MENU:
        menuIndex += dir;
        if (menuIndex > MENU_LENGTH) menuIndex = MENU_LENGTH - 1;    
        else if (menuIndex == MENU_LENGTH) menuIndex = 0;
        drawMenu();
        break;
      case MODE_VALUE:
        if (eb.fast() && 
            menuValues[menuIndex] > VALUE_FAST_STEP && 
            menuValues[menuIndex] < menuValuesMax[menuIndex] - VALUE_FAST_STEP
        ) {
          menuValues[menuIndex] += dir * VALUE_FAST_STEP;
        }
        else {
          menuValues[menuIndex] += dir;
        }
        if (menuValues[menuIndex] > menuValuesMax[menuIndex]) menuValues[menuIndex] = 1;
        else if (menuValues[menuIndex] < 1) menuValues[menuIndex] = menuValuesMax[menuIndex];
        drawMenu();
        break;
    }

    return;
  }
  
  if (eb.click()) {
    switch (mode) {
      case MODE_STATUS:
        menuIndex = 0;
        mode = MODE_MENU;
        drawMenu();
        break;
      case MODE_MENU:
        switch(menuIndex) {
          case MENU_FLUSH1:
          case MENU_FLUSH2:
            mode = MODE_ACTION;
            actionStart = millis();
            if (menuIndex == MENU_FLUSH1) {
              digitalWrite(RELAY1, LOW);
              lastCheck1 = millis();
            }
            else {
              digitalWrite(RELAY2, LOW);
              lastCheck2 = millis();
            }
            drawAction();
            break;        
          case MENU_MIN1:
          case MENU_MIN2:
          case MENU_CHECK1:
          case MENU_TIME1:          
          case MENU_CHECK2:
          case MENU_TIME2:
            mode = MODE_VALUE;
            drawMenu();
            break;
          case MENU_EXIT:
            mode = MODE_STATUS;
            drawStatus();
            break;
        }
        break;
      case MODE_VALUE:        
        EEPROM.update(menuIndex, menuValues[menuIndex]);
        #ifdef DEBUG
        Serial.print("EEPROM("); Serial.print(menuIndex); 
        Serial.print( ") = "); Serial.println(menuValues[menuIndex]);
        #endif

        mode = MODE_MENU;
        drawMenu();
        break;
      case MODE_ACTION:
        digitalWrite(RELAY1, HIGH);
        digitalWrite(RELAY2, HIGH);
        mode = MODE_STATUS;
        drawStatus();
        break;
    }

    return;
  }
  
  byte sec = 0;
  byte val = 0;
  unsigned int value = 0;
  switch(mode) {
    case MODE_ACTION:
      switch(menuIndex) {
        case MENU_FLUSH1:
          sec = menuValues[MENU_TIME1];
          break;
        case MENU_FLUSH2:
          sec = menuValues[MENU_TIME2];
          break;
      }

      if (millis() - actionStart > 1000 * (unsigned long)sec) {
        digitalWrite(RELAY1, HIGH);
        digitalWrite(RELAY2, HIGH);
        mode = MODE_STATUS;
        drawStatus();
      }
      else {
        drawAction();      
      }
      break;
    case MODE_STATUS:
      if (millis() - lastCheck1 > (unsigned long)menuValues[MENU_CHECK1] * 3600000) {
        lastCheck1 = millis();      
        val = measure1();
        
        #ifdef DEBUG
        Serial.print("Check #1 ("); Serial.print(menuValues[MENU_CHECK1]); Serial.print( " h): "); 
        Serial.print(val); Serial.print(" of "); Serial.println(menuValues[MENU_MIN1]);
        #endif

        if (val <= menuValues[MENU_MIN1]) {
          menuIndex = MENU_FLUSH1;
          mode = MODE_ACTION;
          actionStart = millis();
          digitalWrite(RELAY1, LOW);
          drawAction();
          return ;        
        }
      }

      if (millis() - lastCheck2 > (unsigned long)menuValues[MENU_CHECK2] * 3600000) {
        lastCheck2 = millis();
        val = measure2();
        
        #ifdef DEBUG
        Serial.print("Check #2 ("); Serial.print(menuValues[MENU_CHECK2]); Serial.print( " h): "); 
        Serial.print(val); Serial.print(" of "); Serial.println(menuValues[MENU_MIN2]);
        #endif

        if (val <= menuValues[MENU_MIN2]) {
          menuIndex = MENU_FLUSH2;
          mode = MODE_ACTION;
          actionStart = millis();
          digitalWrite(RELAY2, LOW);
          drawAction();
          return ;        
        }      
      }
      
      drawStatus();      
      break;  
  }
}

void drawMenu() {
  display.clearDisplay();
  //display.drawLine(0, 31, 127, 31, WHITE); //DELME

  display.setTextColor(1);
  display.setFont();

  if (menuSubtext[menuIndex] != "") {  
    display.setCursor(0, 24);
    display.print(menuSubtext[menuIndex]);
    display.setCursor(0, 9);
  }
  else display.setCursor(0, 17);

  display.setFont(&FreeSansBold9pt7b);
  display.print(menuItems[menuIndex]);

  if (menuValues[menuIndex]) {
    display.drawRoundRect(88, 0, 40, 31, 1, WHITE);
    
    if (mode == MODE_VALUE) {
      display.fillRect(89, 1, 38, 29, WHITE);
      display.setTextColor(0);
    }
    if (menuValues[menuIndex] > 99) display.setCursor(93, 20);
    else if (menuValues[menuIndex] > 9) display.setCursor(98, 20);
    else display.setCursor(103, 20);
    display.print(menuValues[menuIndex]);
  }

  display.display();
}

void drawAction() {
  display.clearDisplay();
  display.setFont(&FreeSansBold9pt7b);

  byte sec = 0;
  switch(menuIndex) {
    case MENU_FLUSH1:
      sec = menuValues[MENU_TIME1];
      break;
    case MENU_FLUSH2:
      sec = menuValues[MENU_TIME2];
      break;
  }
  int w = map(millis(), actionStart, actionStart + 1000 * (unsigned long)sec, 1, 127);
  display.fillRect(0, 0, w, 31, WHITE);

  if (w > 64) {
    display.setTextColor(0);
    display.setCursor(4, 20);
  }
  else {
    display.setTextColor(1);
    display.setCursor(104, 20);    
  }
  switch(menuIndex) {
    case MENU_FLUSH1:
      display.print("#1");
      break;
    case MENU_FLUSH2:
      display.print("#2");
      break;
  }  

  display.display();
}

void drawStatus() {
  static unsigned long prevMillis = 0;
  static int dy = 0;
  static int dir = 1;
  
  display.clearDisplay();
  //display.drawLine(0, 31, 127, 31, WHITE); //DELME

  display.setFont();
  display.setTextColor(1);
  display.drawBitmap(98, 0 - dy, epd_bitmap_1, 30, 64, 1);

  drawStatusLine(1, dy, 
    measure1(), 
    menuValues[MENU_MIN1], 
    lastCheck1 / 1000 + (unsigned long)menuValues[MENU_CHECK1] * 3600 - millis() / 1000
  );
  
  drawStatusLine(2, dy, 
    measure2(), 
    menuValues[MENU_MIN2], 
    lastCheck2 / 1000 + (unsigned long)menuValues[MENU_CHECK2] * 3600 - millis() / 1000
  );

  display.display();

  unsigned int pause = 50;
  if (dy >= 32 || dy < 0) pause = 3000;
  
  if (millis() - prevMillis > pause) {    
    prevMillis = millis();
    dy += 1 * dir;
    if (dy >= 32) dir = -1;  
    else if (dy < 0) dir = 1;
  }
}

void drawStatusLine(byte line, int dy, byte p, byte minP, unsigned long sec) {
  dy += (line - 1) * -32;
  display.setTextSize(3);
  display.setCursor(0, 7 - dy);
  if (p < 10) display.print(" ");
  display.print(p);
  display.setTextSize(1);
  display.setCursor(36, 7 - dy);
  display.print("% ");
  byte k = min(max(((int)p - (int)minP) / 10, 1), 3);
  for(byte i=0; i<k; i++) display.write(0xAF);
  display.print(" ");
  display.print(minP);
  display.print("%");
  display.setCursor(38, 22 - dy);
  display.print(format_seconds(sec));
}

String format_seconds(unsigned long sec) {
      byte days = floor(sec / 86400);
      byte hours = floor((sec % 86400) / 3600);
      byte minutes = floor((sec % 3600) / 60);
      byte seconds = (sec % 3600) % 60; 
      String times = "";
      if (days > 0) {
        times.concat(days);
        times.concat("d ");
      }
      if (hours < 10) times.concat("0");
      times.concat(hours);
      times.concat(":");
      if (minutes < 10) times.concat("0");
      times.concat(minutes);
      if (days < 1) {
        times.concat(":");
        if (seconds < 10) times.concat("0");
        times.concat(seconds);   
      }
      return times;
}

byte measure1() {
  static unsigned long oldTime = 0;
  static byte oldVal = 0;
  if (!oldVal || millis() - oldTime > MEASURE_DEBOUNCE) {    
    int value = analogRead(SENSOR1);
    #ifdef DEBUG
    Serial.print("Measure #1: "); 
    Serial.println(value);
    #endif
    
    oldTime = millis();
    oldVal = scaleValue(value);
  }
  return oldVal;
}

byte measure2() {
  static unsigned long oldTime = 0;
  static byte oldVal = 0;
  if (!oldVal || millis() - oldTime > MEASURE_DEBOUNCE) {    
    int value = analogRead(SENSOR2);
    #ifdef DEBUG
    Serial.print("Measure #2: "); 
    Serial.println(value);
    #endif
    
    oldTime = millis();
    oldVal = scaleValue(value);
  }
  return oldVal;
}

byte scaleValue(int value) {
  byte ret = 100 - map(value, SENSOR_MIN, SENSOR_MAX, 1, 99);
  if (ret > 99) ret = 99;
  return ret;
}
