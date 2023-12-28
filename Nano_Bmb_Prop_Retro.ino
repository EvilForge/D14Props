/* D14 Bomb Prop Replacement Code v2.1
    -2.1 switches to a 9 digit PIN (max)
   Replaces existing old MCU and unknown code with customized code for D14's needs.
   Includes replacement of MCU board with newer Arduino Pro Mini (5v)
   Hardware has:
   1. 2 Strobes, switched by relay.
   2. Air Horns, switched by relay.
   3. LCD (traditional parallel interface) 2x16 Blue.
   4. 4x4 Keypad (traditional matrix interface).
   5. Buzzer.
   6. Large Red Button.
   7. Pin switch in canister connection on top of unit for canister presence.

   MODES:
   Power Up starts 5 second countdown, pressing red button enters program mode. Battery voltaqe displayed durinng power up.
   1. PIN-Countdown - Set a PIN here (2-6 digits), countdown time and incorrect PIN action. Unit is inert until PIN is entered then countdown begins. PIN disables countdown. Wrong PIN either starts 2x countdown or expodes.
   2. Canister-Countdown - Set countdown timer. Unit waits until canister is connected, then starts charge sequence. Remove canister to start countdown. Reconnecting canister and red button disarms.
   3. Repeating Countdown - Set countdown timer. Unit counts down and explodes unless red reset button is pressed. Reset restarts countdown.
   4. Simple Countdown - Set timer. Unit counts down and explodes unless red cancel button is pressed. pressing red button restarts.
   5. Misc Settings mode - Set whether Strobes flash during countdown, whether unit beeps during countdown.

   EEPROM Structure:
   0 - Default game mode.
   1 - Strobe during countdown(Bit 1), Beep during countdown (Bit 2), EOD Mode (Bit 3)
   2 - Alert Duration (default 20s).
   3-4 - Countdown time (Seconds, 0-65535)
   5-8 - PIN (long)
*/
#include <avr/sleep.h>      // powerdown library
#include <EEPROM.h>
#include <Keypad.h>
#include <LiquidCrystal_I2C.h>
const byte brbPIN = 10; // Input for Big Red Button. LOW = pressed.
const byte buzzPIN = 11; //HIGH = Piezo Buzzer on.
const byte canPIN = 12; // Input for canister sense pin. LOW = detached.
const byte strobePIN = 13; // HIGH = strobes on.
const byte hornPIN = 14; // HIGH = air horns on.
// A3 is the voltage measurement PIN.
bool cdBeep = false;
bool cdStrobe = false;
bool eodMode = false;
bool firstRun = true;
bool gameOn = true;
bool optionShown = false;
bool progMode = false;
bool progSelected = false;
byte alertPeriod = 20; // 20s default alert period when triggered.
byte configByte = 0;
byte eepromHigh = 0;
byte eepromLow = 0;
byte gameMode = 1;
byte gameStep = 0; // What step in the PIN game mode we are in.
byte progMenu = 0;
byte x = 5;
byte pinFail = 0;
byte rowPins[4] = {2,9,8,6}; // 4,2,1,8 connect to the row pinouts of the keypad
byte colPins[4] = {7,5,4,3}; // 3,5,6,7 connect to the column pinouts of the keypad
char keys[4][4] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
char key;
int cdPeriod = 1200; // Number of seconds for countdown.
int voltRaw = 0; // input valu for Analog voltage measurement.
float pinVoltage = 0; // holds calc voltage for Analog voltage measurement.
long enteredPIN = 0;
long eodTimeout = 0; // end of eod bypass period.
long lngPIN = 0;
long timeOut = 0; // millis when we reach end of countdown.
long nextCheck = 0; // millis when clock needs updating.
long nextEODCheck = 0; // millis when EOD clock needs updating.
long sleepTime = millis() + 7200000; // When the system goes to sleep - 2 hours away.
String lcdLine1 = "";
String lcdLine2 = "";
String gameNames[4] = {"PIN Countdown","Can-Countdown","Rpt-Countdown","Countdown"};
Keypad keypad = Keypad( makeKeymap(keys), rowPins, colPins, 4, 4 );
LiquidCrystal_I2C lcd(0x3F, 2, 1, 0, 4, 5, 6, 7, 3, POSITIVE);  // Set the LCD I2C address

void setup() {
  // Set PinModes.
  pinMode(3,INPUT); // Analog A3 voltage input
  pinMode(strobePIN, OUTPUT);
  digitalWrite(strobePIN,LOW);
  pinMode(hornPIN, OUTPUT);
  digitalWrite(hornPIN,LOW);
  pinMode(buzzPIN, OUTPUT);
  digitalWrite(buzzPIN,LOW);
  pinMode(brbPIN, INPUT_PULLUP);
  pinMode(canPIN, INPUT_PULLUP);
  // 0 Get Default game mode from EEPROM
  gameMode = EEPROM.read(0);
  if(gameMode < 1 || gameMode > 4) {
    EEPROM.write(0,1);
    gameMode = 1;
  }
  // 1 Get Config Byte from EEPROM
  configByte = EEPROM.read(1);
  if(configByte > 7) {
    EEPROM.write(1,0);
    configByte = 0;
  }
  if(configByte & 1) cdStrobe = true;
  if(configByte & 2) cdBeep = true;
  if(configByte & 4) eodMode = true;
  // 2 Get explosion Duration from EEPROM
  alertPeriod = EEPROM.read(2);
  if(alertPeriod < 1 || alertPeriod > 240) {
    EEPROM.write(2,20);
    alertPeriod = 20;
  }
  // 3,4 Get Countdown time (Seconds, 0-64000) from EEPROM
  eepromHigh = EEPROM.read(3);
  eepromLow = EEPROM.read(4);
  cdPeriod = word(eepromHigh,eepromLow);
  if(cdPeriod > 64000 || cdPeriod < 1) {
    EEPROM.write(3,4);
    EEPROM.write(4,176);
    cdPeriod = 1200; // 20 minutes or 1200 seconds
  }
  // 5 Get PIN from EEPROM
  lngPIN = EEPROMReadLong(5);
  if(lngPIN < 1 || lngPIN > 999999999) {
    lngPIN = 1234;
    EEPROMWriteLong(5,lngPIN);
  }
  lcd.begin(16,2);   // initialize the lcd for 16 chars 2 lines, turn on backlight
  lcd.backlight();
  lcdLine1=F("D14 Airsoft Bomb");
  lcdLine2=F("FW Version 2.1  ");
  writeLCD(0);
  beep(200);
  timeOut = millis() + 5000;
  while(millis() < timeOut){ // For 5 seconds after power up, wait for BRB press.
    if(digitalRead(brbPIN) == LOW) { // If BRB is pressed, enter program mode.
      progMode = true;
      gameOn = false;
      break;
    }
  }
  randomSeed(analogRead(1));
  voltRaw = analogRead(3);
  pinVoltage = voltRaw * 0.0164;       //  Calculate the voltage on the A/D pin
  lcdLine2 = "Battery: " + String(pinVoltage);
  if (pinVoltage < 11.2) {
    lcdLine1 = "LOW BATTERY!";
    writeLCD(0);
    beep(2000);
  } else {
    writeLCD(2000); 
  }
}

void loop() {
  if(progMode) {
    programMode();
  }
  if(gameOn) {
    if (firstRun) {
      lcdLine1 = gameNames[gameMode-1];
      lcdLine2 = "Game On!";
      writeLCD(2000);
      firstRun = false;
      timeOut = millis() + (long(cdPeriod) * 1000);
      nextCheck = millis() + 1000;
    }
    switch (gameMode) {
      case 1: //PIN countdown
        pinCountdown();
        break;
      case 2: //Canister countdown
        canCount();
        break;
      case 3: //Repeating countdown
        simpleCD(true);
        break;
      case 4: //Simple countdown
        simpleCD(false);
        break;
    }
  }
  if (millis() > sleepTime) {
    // System has been idle, power it all down to save battery.
    digitalWrite(strobePIN,LOW); // Strobe Off
    digitalWrite(hornPIN,LOW); // Horn Off
    lcd.off();
    set_sleep_mode(SLEEP_MODE_PWR_DOWN);  // setting up for sleep ...
    sleep_enable();                       // setting up for sleep ...
    ADCSRA &= ~(1 << ADEN);    // Disable ADC
    PRR = 0xFF;   // Power down functions
    sleep_mode();                         // now go to Sleep and waits
  }
}

void canCount() {
  if (gameStep == 0) {
    // Initialize
    lcdLine1 = F("Detach can and");
    lcdLine2 = F("press red button");
    writeLCD(0);
    gameStep = 1;
  }
  if (gameStep == 1 ) {
    if (digitalRead(brbPIN) == LOW && digitalRead(canPIN) == LOW) {
      gameStep = 2;
      timeOut = millis() + (long(cdPeriod) * 1000);
      nextCheck = millis() + 10;
      lcdLine1 = F("Ready to Charge!");
      lcdLine2 = F("Attach Canister.");
      writeLCD(0);
    }
  }
  if (gameStep == 2) {
    if (digitalRead(canPIN) == HIGH) {
      // Canister was attached. Start charging.
      lcdLine1 = F("Priming U235");
      for (x = 5; x > 1; x--) {
        lcdLine2 = "pump. Wait (" + String(x) + ")";
        beep(20);
        writeLCD(2000);
      }
      lcdLine1 = F("Ready to charge.");
      lcdLine2 = F("Hold Red Button.");
      writeLCD(0);
      beep(500);
      gameStep = 3;
      x = 5;
    }
  }
  if (gameStep == 3) {
    if (digitalRead(brbPIN) == LOW && digitalRead(canPIN) == HIGH ) {
      if (x > 0) {
        lcdLine1 = F("Charging, hold");
        lcdLine2 = "button (" + String(x) + ")";
        beep(20);
        writeLCD(2000);
        x--;
      } else {
        lcdLine1 = F("Charged!");
        lcdLine2 = F("Detach Canister.");
        beep(1000);
        writeLCD(0);
        gameStep = 4;
      }
    }
  }
  if (gameStep == 4) {
    if (digitalRead(canPIN) == LOW) {
      // Can detached, countdown started.
      lcdLine2 = F("Use Can 2 Disarm");
      writeLCD(1000);
      timeOut = millis() + (long(cdPeriod) * 1000);
      nextCheck = millis() + 10;
      gameStep = 5;
      x = 5;
    }
  }
  if (gameStep == 5) {
    if (nextCheck < millis() ) {
      nextCheck = checkTimer(timeOut);
      if (eodTimeout > 0) {
        nextEODCheck = checkEODTimer(eodTimeout);
      }
      if (cdBeep) beep(20);
      if (cdStrobe) digitalWrite(strobePIN,HIGH);
    }
    if (nextCheck == 0) { // Time to detonate.
      detonate();
    }
    if (eodMode) {
      if (eodTimeout == 0) { // no EOD timeout set, assume its starting
        if (digitalRead(canPIN) == LOW && digitalRead(brbPIN) == LOW) {
          // No can present, Red Btn Pressed, time to start countdown (4 mins).
          eodTimeout = millis() + 240000;
          nextEODCheck = millis() + 1000;
          if (eodTimeout > timeOut) {
            // its too late, detonate with malice!
            speedDet();
          }
        }
      } else { // eod Timeout already started. verify button and no can
        if (digitalRead(canPIN) == LOW && digitalRead(brbPIN) == LOW) { // no can and buton is still pressed, increment eod display line
          if (nextEODCheck == 0) {
            // Bypassed!
            lcdLine1 = "EOD Bypass";
            lcdLine2 = "success!";
            beep(1000);
            writeLCD(0);
            gameOn = false;
            sleepTime = millis() + 1800000; // Put system to sleep in 30 mins.
          } // No else here cause BTn is held, no can, EOD time left, so do nothing and continue countdown..
        } else {
          // if button isnt pressed or can is attached, turn off eod countdown
          eodTimeout = 0;
          lcdLine2 = F("Use Can 2 Disarm");
        }
      }
    }
    if (digitalRead(canPIN) == HIGH) {
      // Can reattached, tell em to hold button
      lcdLine2 = F("Use Can 2 Disarm");
//      x = 5;
    }
    if (digitalRead(canPIN) == HIGH && digitalRead(brbPIN) == HIGH) {
      // Can reattached, tell em to hold button
      lcdLine2 = F("Hold Red Button!");
      x = 5;
    }
    if (digitalRead(canPIN) == HIGH && digitalRead(brbPIN) == LOW) {
      if (x > 0) {
        lcdLine1 = F("Discharge: hold");
        lcdLine2 = "button (" + String(x) + ")";
        beep(20);
        writeLCD(2000);
        x--;
      } else {
        lcdLine1 = F("Discharged!");
        lcdLine2 = F("");
        beep(1000);
        writeLCD(0);
        gameOn = false;
        sleepTime = millis() + 1800000; // Put system to sleep in 30 mins.
      }
    }
  }
}

void pinCountdown() {
  if (gameStep == 0) {
    // Initialize and ask for PIN
    lcdLine1 = "Enter PIN:";
    writeLCD(0);
    enteredPIN = getLong(1,999999999, true);
    if (enteredPIN == lngPIN) {
      gameStep = 1;
      timeOut = millis() + (long(cdPeriod) * 1000);
      nextCheck = millis() + 10;
      lcdLine2 = "PIN?";
      enteredPIN = 0;
    } else {
      lcdLine2 = F("INCORRECT.");
      writeLCD(0);
      beep(1000);
    }
  }
  if (gameStep == 1) { // Countdown, check for pin or if EOD is allowed.
    if (nextCheck < millis() ) {
      nextCheck = checkTimer(timeOut);
      if (cdBeep) beep(20);
      if (cdStrobe) digitalWrite(strobePIN,HIGH);
    }
    key = keypad.getKey();
    if (key != NO_KEY) { // Check for key and process.
      switch (key) {
         case NO_KEY:
            break;
         case '0': case '1': case '2': case '3': case '4':
         case '5': case '6': case '7': case '8': case '9':
            beep(30);
            enteredPIN = enteredPIN * 10 + (key - '0');
            lcdLine2 = enteredPIN;
            writeLCD(0);
            break;
         case '*':
            beep(30);
            if (eodMode && enteredPIN == 0) {
              lcdLine2 = String(lngPIN+random(200)) + "-" + String(lngPIN-random(200));
            } else {
              lcdLine2 = "";
            }
            writeLCD(0);
            enteredPIN = 0;
            break;
         case '#':
            // CHeck for valid PIN
            if (enteredPIN == lngPIN) {
              gameStep = 2;
              sleepTime = millis() + 1800000; // Put system to sleep in 30 mins.
              cdPeriod = (timeOut - millis()) / 1000; // Set new cdPeriod to remaining time.
            } else {
              if (eodMode) {
                if (enteredPIN > lngPIN ) {
                  lcdLine2 = F("PIN too high.");
                } else {
                  lcdLine2 = F("PIN too low.");
                }
              } else {
                lcdLine2 = F("INVALID PIN.");
                writeLCD(0);
                pinFail ++;
                if (pinFail > 3) {
                  detonate();
                }
              }
              enteredPIN = 0;
            }
            break;      
      }
    }
    if (nextCheck == 0) { // Time to detonate.
      detonate();
    }
  }
  if (gameStep == 2) {
    // Countdown paused.
    lcdLine1 = F("Paused - PIN?");
    writeLCD(0);
    digitalWrite(strobePIN,LOW);
    enteredPIN = getLong(1,999999, true);
    if (enteredPIN == lngPIN) {
      gameStep = 1;
      pinFail = 0;
      timeOut = millis() + (long(cdPeriod) * 1000);
      nextCheck = millis() + 10;
      lcdLine2 = "PIN?";
      enteredPIN = 0;
    } else {
      lcdLine2 = F("INCORRECT.");
      writeLCD(0);
      beep(1000);
    }
  }
}

void simpleCD(bool repeating) {
  if (gameStep == 0) {
    if (repeating) {
      lcdLine2 = "Red Btn Restarts";
    } else {
      lcdLine2 = "Red Btn Pauses";
    }
    writeLCD(1000);
    lcdLine1 = "Press red Btn";
    lcdLine2 = "to start.";
    writeLCD(0);
    gameStep = 1;
  }
  if (gameStep == 1) { // waiting for start.
    if(digitalRead(brbPIN) == LOW) {
      // Start this mode.
      timeOut = millis() + (long(cdPeriod) * 1000);
      nextCheck = millis() + 10;
      gameStep = 2;
      beep(250);
      delay(1000);
      lcdLine2 = "Press red btn";
    }
  }
  if (gameStep == 2) {
    if (cdStrobe) digitalWrite(strobePIN,HIGH);
    if (nextCheck < millis() ) {
      nextCheck = checkTimer(timeOut);
      if (cdBeep) beep(20);
    }
    if(digitalRead(brbPIN) == LOW) { // If BRB is pressed, hold or reset countdown.
      if (repeating) {
        beep(30);
        delay(250);
        beep(30);
        timeOut = millis() + (long(cdPeriod) * 1000);
        nextCheck = millis() + 10;
      } else {
        gameStep = 3; // paused
      }
    }
  }
  if (gameStep == 3) {
    lcdLine1 = F("Countdown Paused");
    lcdLine2 = F("Red Btn restarts");
    writeLCD(0);
    digitalWrite(strobePIN,LOW);
    delay(1000);
    cdPeriod = (timeOut - millis()) / 1000; // Set new cdPeriod to remaining time.
    sleepTime = millis() + 1800000; // Put system to sleep in 30 mins.
    gameStep = 4;
  }
  if (gameStep == 4) {
    if(digitalRead(brbPIN) == LOW) { // If BRB is pressed, continue countdown.
      gameStep = 2;
      lcdLine1 = F("Countdown is");
      lcdLine2 = F("restarting...");
      writeLCD(0);
      beep(500);
      delay(500);
      lcdLine2 = "Press red btn";
      timeOut = millis() + (long(cdPeriod) * 1000);
      nextCheck = millis() + 10;
    }
  }
  if (nextCheck == 0) { // Time to detonate.
    detonate();
  }
}

void speedDet() {
    bool hornOn = false;
    int remaining = (timeOut - millis())/1000;
    int x =0;
    lcdLine1 = F("Have a nice life");
    lcdLine2 = F("All 30sec of it");
    writeLCD(1000);
    // accellerate countdown
    for (x;x<(remaining/3);x++) {
      lcdLine1 = String(remaining - x);
      lcdLine2 = "RUN.";
      writeLCD(0);
      beep(100);
      delay(100);
    }
    for (x;x<(remaining*.66);x++) {
      lcdLine1 = String(remaining - x);
      lcdLine2 = "RUN. FASTER.";
      writeLCD(0);
      beep(50);
      delay(50);
    }
    for (x;x<remaining;x++) {
      lcdLine1 = String(remaining - x);
      lcdLine2 = "RUN. FASTER. NOW!";
      writeLCD(0);
      beep(10);
      delay(10);
    }
    digitalWrite(strobePIN,HIGH); // Strobe On
    digitalWrite(hornPIN,HIGH); // Strobe On
    hornOn = true;
    timeOut = millis() + (long(alertPeriod) * 1000); // set end of alert period
    nextCheck = millis() + 1000; // next check in 1 sec
    while (millis() < timeOut) {
      // set up 1sec on 1 sec off alternating..
      if (nextCheck < millis()) {
        nextCheck = millis() + 1000;
        if (hornOn) {
          digitalWrite(hornPIN,LOW);
          hornOn = false;
        } else {
          digitalWrite(hornPIN,HIGH);
          hornOn = true;
        }
      }
    }
    digitalWrite(strobePIN,LOW); // Strobe Off
    digitalWrite(hornPIN,LOW); // Horn Off
    gameOn = false; // End this session.
    sleepTime = millis() + 1800000; // Put system to sleep in 30 mins.
}

void detonate() {
    bool hornOn = false;
    lcdLine1 = F("DETONATED");
    lcdLine2 = "";
    writeLCD(0);
    digitalWrite(strobePIN,HIGH); // Strobe On
    digitalWrite(hornPIN,HIGH); // Strobe On
    hornOn = true;
    timeOut = millis() + (long(alertPeriod) * 1000); // set end of alert period
    nextCheck = millis() + 1000; // next check in 1 sec
    while (millis() < timeOut) {
      // set up 1sec on 1 sec off alternating..
      if (nextCheck < millis()) {
        nextCheck = millis() + 1000;
        if (hornOn) {
          digitalWrite(hornPIN,LOW);
          hornOn = false;
        } else {
          digitalWrite(hornPIN,HIGH);
          hornOn = true;
        }
      }
    }
    digitalWrite(strobePIN,LOW); // Strobe Off
    digitalWrite(hornPIN,LOW); // Horn Off
    gameOn = false; // End this session.
    sleepTime = millis() + 1800000; // Put system to sleep in 30 mins.
}

long checkEODTimer(long endmillis) {
  // increment display and check timer to verify its hasnt ended. return next millis to calling routine or 0 to indicate end.
  if (endmillis > millis() ) {
    // calc difference in time from millis to end millis
    long remaining = endmillis - millis();
    int hours = int(remaining/3600000);
    if (hours >0) {
      remaining = remaining - (hours * 3600000);
    } else {
      hours = 0;
    }
    int mins = int(remaining/60000);
    if (mins >0) {
      remaining = remaining - (mins * 60000);
    } else {
      mins = 0;
    }
    int secs = int(remaining/1000);
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("EOD Bypass:");
    lcd.setCursor(0,1);
    if (hours < 10) lcd.print("0");
    lcd.print(String(hours) + ":");
    if (mins < 10) lcd.print("0");
    lcd.print(String(mins) + ":");
    if (secs < 10) lcd.print("0");
    lcd.print(String(secs));
    lcd.print(" to halt");
    return(millis() + 1000);
  } else { // time expired
    return(0);
  }
}

long checkTimer(long endMillis) {
  // increment display and check timer to verify its hasnt ended. return next millis to calling routine or 0 to indicate end.
  if (endMillis > millis() ) {
    // calc difference in time from millis to end millis
    long remaining = endMillis - millis();
    int hours = int(remaining/3600000);
    if (hours >0) {
      remaining = remaining - (hours * 3600000);
    } else {
      hours = 0;
    }
    int mins = int(remaining/60000);
    if (mins >0) {
      remaining = remaining - (mins * 60000);
    } else {
      mins = 0;
    }
    int secs = int(remaining/1000);
    lcd.clear();
    lcd.setCursor(0,0);
    if (hours < 10) lcd.print("0");
    lcd.print(String(hours) + ":");
    if (mins < 10) lcd.print("0");
    lcd.print(String(mins) + ":");
    if (secs < 10) lcd.print("0");
    lcd.print(String(secs));
    lcd.setCursor(0,1);
    lcd.print(lcdLine2);
    return(millis() + 1000);
  } else { // time expired
    return(0);
  }
}

void programMode() {
  if(firstRun) {
    lcdLine1 = F("Program Mode");
    lcdLine2 = F("Press 1-7 for   ");
    writeLCD(2000);
    lcdLine1 = lcdLine2;
    lcdLine2 = F("settings, #=OK,");
    writeLCD(2000);
    lcdLine1 = lcdLine2;
    lcdLine2 = F("*=exit.");
    writeLCD(0);
    firstRun = false;
  }
  key = keypad.getKey();
  if(key != NO_KEY) {
    beep(30);
    if(!progSelected) {
      lcdLine2="(#=OK)";
      switch (key) {
        case '1':
          lcdLine1=F("Set default game");
          lcdLine2=gameNames[gameMode-1];
          progMenu = 1;
          break;
        case '2':
          lcdLine1=F("Countdwn strobe?");
          if (cdStrobe) {
            lcdLine2 = "(On)";
          } else {
            lcdLine2 = "(Off)";
          }
          progMenu = 2;
          break;
        case '3':
          lcdLine1=F("Countdown beep?");
          if (cdBeep) {
            lcdLine2 = "(On)";
          } else {
            lcdLine2 = "(Off)";
          }
          progMenu = 3;
          break;
        case '4':
          lcdLine1=F("Alert duration:");
          lcdLine2="(" + String(alertPeriod) + "s)";
          progMenu = 4;
          break;
        case '5':
          lcdLine1=F("Countdown time:");
          lcdLine2= "(" + String(cdPeriod/60) + "m)";
          progMenu = 5;
          break;
        case '6':
          lcdLine1=F("PIN:");
          lcdLine2="("+String(lngPIN)+")";
          progMenu = 6;
          break;
        case '7':
          lcdLine1=F("EOD Mode?");
          if (eodMode) {
            lcdLine2 = "(On)";
          } else {
            lcdLine2 = "(Off)";
          }
          progMenu = 7;
          break;
        case '#':
          if (progMenu != 0) {
            progSelected = true;
          } else {
            lcdLine1=F("Enter 1-7, then");
          }
          break;
        case '*':
          progMode = false;
          gameOn = true;
          firstRun = true;
          break;
        default:
          lcdLine1=F("Enter 1-7, then");
          progMenu = 0;
          progSelected = false;
          break;
      }
      writeLCD(0);
    }
  }
  if (progSelected) {
    // Program option is selected, show initial question and loop back round to get answer. Then set EEPROM and reset program mode.
    switch (progMenu) {
      case 1:
        if (optionShown) {
          gameMode = getLong(1,5,false);
          if (gameMode != 5) {
            lcdLine1 =F("Default game:");
            lcdLine2 = String(gameMode);
            EEPROM.write(0,gameMode);
            writeLCD(2000);
            lcdLine1 = lcdLine2;
            lcdLine2 = gameNames[gameMode-1];
            writeLCD(2000);
          } else {
            // List game modes
            for (int x = 0; x<4; x++) {
              lcdLine1 = "Mode: " + String(x+1);
              lcdLine2 = gameNames[x];
              writeLCD(2000);
            }
          }
          resetMenu();
        } else {
          lcdLine1 = F("What game should");
          lcdLine2 = F("the bomb start");
          writeLCD(2500);
          lcdLine1 = F("with? (1-4)");
          lcdLine2 = F("(5=list):");
          writeLCD(2000);
          optionShown = true;
        }
        break;
      case 2:
        if (optionShown) {
          cdStrobe = getYesNo(cdStrobe);
          lcdLine2 = F("during countdwn.");
          if (cdStrobe) {
            lcdLine1 = F("Strobe On");
            configByte = configByte | 1;
          } else {
            lcdLine1 = F("Strobe Off");
            configByte = bitClear(configByte,0);
          }
          EEPROM.write(1,configByte);
          writeLCD(3000);
          resetMenu();
        } else {
          lcdLine1 = F("Strobe on when");
          lcdLine2 = F("countdown (1/0)?");
          writeLCD(2000);
          optionShown = true;
        }
        break;
      case 3:
        if (optionShown) {
          cdBeep = getYesNo(cdBeep);
          lcdLine2 = F("during countdown");
          if (cdBeep) {
            lcdLine1 = F("Beep Enabled");
            configByte = configByte | 2;
          } else {
            lcdLine1 = F("Beep Disabled");
            configByte = bitClear(configByte,1);
          }
          EEPROM.write(1,configByte);
          writeLCD(2000);
          resetMenu();
        } else {
          lcdLine1 = F("Beep during");
          lcdLine2 = F("countdown (1/0)?");
          writeLCD(2000);
          optionShown = true;
        }
        break;
      case 4:
        if (optionShown) {
          alertPeriod = getLong(1,240,false);
          lcdLine1 = F("Alert Duration:");
          lcdLine2 = String(alertPeriod) + " secs";
          EEPROM.write(2,alertPeriod);
          writeLCD(2000);
          resetMenu();
        } else {
          lcdLine1 = F("Duration of");
          lcdLine2 = F("alert? 0-240(s)");
          writeLCD(2000);
          optionShown = true;
        }
        break;
      case 5:
        if (optionShown) {
          cdPeriod = getLong(1,60,false) * 60;
          lcdLine1 = F("Countdwn set to:");
          lcdLine2 = String(cdPeriod/60) + "minutes";
          EEPROM.write(3,highByte(cdPeriod));
          EEPROM.write(4,lowByte(cdPeriod));
          writeLCD(3000);
          resetMenu();
        } else {
          lcdLine1 = F("Countdown length");
          lcdLine2 = F("1-60 (m):");
          writeLCD(3000);
          optionShown = true;
        }
        break;
      case 6:
        if (optionShown) {
          lngPIN = getLong(1,999999999,false);
          lcdLine1 = "PIN is:";
          lcdLine2 = lngPIN;
          EEPROMWriteLong(5,lngPIN);
          writeLCD(2000);
          resetMenu();
        } else {
          lcdLine1 = F("Set PIN code:");
          lcdLine2 = String(lngPIN);
          writeLCD(2000);
          optionShown = true;
        }
        break;
      case 7:
        if (optionShown) {
          eodMode = getYesNo(eodMode);
          lcdLine2 = F("during countdwn.");
          if (eodMode) {
            lcdLine1 = F("EOD Enabled");
            configByte = configByte | 4;
          } else {
            lcdLine1 = F("EOD Disabled");
            configByte = bitClear(configByte,2);
          }
          EEPROM.write(1,configByte);
          writeLCD(2000);
          resetMenu();
        } else {
          lcdLine1 = F("EOD during");
          lcdLine2 = F("countdown (1/0)?");
          writeLCD(2000);
          optionShown = true;
        }
        break;
    }
  }
}

void beep(int period) {
  digitalWrite(buzzPIN,HIGH);
  delay(period);
  digitalWrite(buzzPIN,LOW);
}

void resetMenu() {
  optionShown = false;
  progSelected = false;
  firstRun = true;
  progMenu = 0;
}

void writeLCD(int pause) {
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print(lcdLine1);
  lcd.setCursor(0,1);
  lcd.print(lcdLine2);
  delay(pause);
}

long getLong(int minV, long maxV, bool useLCD1) {
  // Get int min to max from keypad and return Long
  long result;
  bool finished = false;
  if (!useLCD1) {
    String stringOne = "Range:";
    String stringTwo = "-";
    stringOne = stringOne + String(minV);
    stringTwo = stringTwo + String(maxV);
    lcdLine1 = stringOne + stringTwo;
  }
  lcdLine2 = "";
  writeLCD(0);
  long num = 0;
  key = keypad.getKey();
  while(!finished)
  {
    switch (key)
    {
       case NO_KEY:
          break;
       case '0': case '1': case '2': case '3': case '4':
       case '5': case '6': case '7': case '8': case '9':
          beep(30);
          num = num * 10 + (key - '0');
          lcdLine2 = num;
          writeLCD(0);
          break;
       case '*':
          beep(30);
          num = 0;
          lcdLine2 = "";
          writeLCD(0);
          break;
       case '#':
          if (num >= minV && num <= maxV) {
            beep(30);
            finished = true;  
          } else {
            num = 0;
            lcdLine2 = F("Invalid Entry!");
            writeLCD(0);
            beep(1000);
            lcdLine2 = "";
            writeLCD(0);
            finished = false;
          }
          break;          
    }
    key = keypad.getKey();
  }
  return num;
}

bool getYesNo(bool cVal) {
  // Get 0 or 1 from keyboard and return boolean
  bool result = cVal;
  bool finished = false;
  lcdLine1 = F("1=Yes, 0=No");
  lcdLine2 = "";
  writeLCD(0);
  key = keypad.getKey();
  while(!finished)
  {
    switch (key)
    {
       case NO_KEY:
          break;
       case '0': 
          beep(30);
          lcdLine2 = F("0/No/Off");
          writeLCD(0);
          result = false;
          break;
       case '1':
          beep(30);
          lcdLine2 = F("1/Yes/On");
          writeLCD(0);
          result = true;
          break;
       case '#':
          beep(30);
          finished = true;  
          break;          
    }
    key = keypad.getKey();
  }
  return result;
}

void EEPROMWriteLong(int address, long value) {
    byte four = (value & 0xFF);
    byte three = ((value >> 8) & 0xFF);
    byte two = ((value >> 16) & 0xFF);
    byte one = ((value >> 24) & 0xFF);
    EEPROM.write(address, four);
    EEPROM.write(address + 1, three);
    EEPROM.write(address + 2, two);
    EEPROM.write(address + 3, one);
}

long EEPROMReadLong(long address) {
    long four = EEPROM.read(address);
    long three = EEPROM.read(address + 1);
    long two = EEPROM.read(address + 2);
    long one = EEPROM.read(address + 3);
    return ((four << 0) & 0xFF) + ((three << 8) & 0xFFFF) + ((two << 16) & 0xFFFFFF) + ((one << 24) & 0xFFFFFFFF);
}

