//- -----------------------------------------------------------------------------------------------------------------------
// AskSin++
// 2016-10-31 papa Creative Commons - http://creativecommons.org/licenses/by-nc-sa/3.0/de/
// 2021-03-03 jp112sdl Creative Commons - http://creativecommons.org/licenses/by-nc-sa/3.0/de/
// HB-UNI-Sen-EC - HomeMatic EC Sensor
// based on HB-UNI-Sen-PH-ORP by jp112sdl
//- -----------------------------------------------------------------------------------------------------------------------
// ci-test=yes board=328p aes=no

// ========================================================
// Sensor-Typ: genau EINE Zeile einkommentieren
// ========================================================
#define EC_SENSOR_K1          // DFR0300   K=1  (0-20  mS/cm)
//#define EC_SENSOR_K10       // DFR0300-H K=10 (10-100 mS/cm)

#define SENSOR_ONLY
#define HIDE_IGNORE_MSG

#define EI_NOTEXTERNAL
#include <EnableInterrupt.h>
#include <AskSinPP.h>
#include <LowPower.h>
#include <Register.h>
#include <MultiChannelDevice.h>
#include <sensors/Ds18b20.h>
#include <LiquidCrystal_I2C.h>

#if defined(EC_SENSOR_K10)
  #include <DFRobot_EC10.h>
  #define EC_LIB_CLASS  DFRobot_EC10
#elif defined(EC_SENSOR_K1)
  #include <DFRobot_EC.h>
  #define EC_LIB_CLASS  DFRobot_EC
#else
  #error "Bitte EC_SENSOR_K1 oder EC_SENSOR_K10 definieren!"
#endif

#define LCD_ADDRESS          0x27
#define LCD_ROWS             2
#define LCD_COLUMNS          16

#define CONFIG_BUTTON_PIN    8
#define LED_PIN              4
#define DS18B20_PIN          5
#define EC_SIGNAL_PIN        A1

#define REF_VOLTAGE          3300

#define CALIBRATION_MODE_TIMEOUT 600  // seconds

#define PEERS_PER_CHANNEL    6

// ========================================================
// rawEC Schwellenwerte (aus DFRobot Quellcode)
//   rawEC = 1000 * voltage / RES2 / ECREF
//   RES2  = 7500.0/0.66 = 11363.6
//   ECREF = 20.0
//   => rawEC = 1000 * voltage / 11363.6 / 20.0
//            = voltage / 227.27
//
// K=1: 1413 uS/cm => rawEC 0.9..1.9  => voltage  204.. 432 mV
//      12.88 mS   => rawEC 9.0..16.8 => voltage 2045..3818 mV
// K=10: 12.88 mS  => Lib prueft aehnlich intern
// ========================================================
#define RES2  (7500.0f / 0.66f)
#define ECREF 20.0f

// rawEC aus Spannung berechnen (gleiche Formel wie DFRobot Lib)
#define CALC_RAW_EC(v) (1000.0f * (v) / RES2 / ECREF)

// K=1 Schwellenwerte (aus DFRobot_EC.cpp)
#define RAWEC_1413_LOW   0.9f
#define RAWEC_1413_HIGH  1.9f
#define RAWEC_1288_LOW   9.0f
#define RAWEC_1288_HIGH  16.8f

// K=10 Schwellenwerte (aus DFRobot_EC10.cpp, etwas breiter)
#define RAWEC_K10_1288_LOW   8.0f
#define RAWEC_K10_1288_HIGH  16.8f

using namespace as;

// ========================================================
// DeviceInfo - Model 0xFC20
// ========================================================
const struct DeviceInfo PROGMEM devinfo = {
  { 0xFC, 0x20, 0x01 },   // Device ID
  "HBEC000001",            // Device Serial
  { 0xFC, 0x20 },          // Device Model
  0x10,                    // Firmware Version
  0x53,                    // Device Type
  { 0x01, 0x00 }           // Info Bytes
};

typedef AskSin<StatusLed<LED_PIN>, NoBattery, Radio<AvrSPI<10, 11, 12, 13>, 2>> Hal;
Hal hal;

// ========================================================
// List0 - Geraete-Register
// ========================================================
DEFREGISTER(UReg0, MASTERID_REGS, 0x1f, 0x20, 0x21, DREG_BACKONTIME)
class UList0 : public RegList0<UReg0> {
public:
  UList0 (uint16_t addr) : RegList0<UReg0>(addr) {}
  bool Sendeintervall (uint8_t value) const { return this->writeRegister(0x21, value & 0xff); }
  uint8_t Sendeintervall () const { return this->readRegister(0x21, 0); }
  bool Messintervall (uint16_t value) const { return this->writeRegister(0x1f, (value >> 8) & 0xff) && this->writeRegister(0x20, value & 0xff); }
  uint16_t Messintervall () const { return (this->readRegister(0x1f, 0) << 8) + this->readRegister(0x20, 0); }
  void defaults () { clear(); lowBatLimit(22); backOnTime(60); Sendeintervall(18); Messintervall(10); }
};

// ========================================================
// List1 - Kanal-Register
// ========================================================
DEFREGISTER(UReg1, 0x05)
class UList1 : public RegList1<UReg1> {
public:
  UList1 (uint16_t addr) : RegList1<UReg1>(addr) {}
  bool TemperatureOffsetIndex (uint8_t value) const { return this->writeRegister(0x05, value & 0xff); }
  uint8_t TemperatureOffsetIndex () const { return this->readRegister(0x05, 0); }
  void defaults () { clear(); TemperatureOffsetIndex(7); }
};

// ========================================================
// LCD-Klasse
// ========================================================
class LcdType {
public:
  class BacklightAlarm : public Alarm {
    LcdType& lcdDev;
  public:
    BacklightAlarm (LcdType& l) : Alarm(0), lcdDev(l) {}
    virtual ~BacklightAlarm () {}
    void restartTimer(uint8_t sec) {
      sysclock.cancel(*this);
      set(seconds2ticks(sec));
      lcdDev.lcd.backlight();
      sysclock.add(*this);
    }
    virtual void trigger (__attribute__((unused)) AlarmClock& clock) { lcdDev.lcd.noBacklight(); }
  } backlightalarm;

private:
  uint8_t backlightOnTime;
  byte degree[8] = { 0b00111, 0b00101, 0b00111, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000 };

  String tempToStr(int16_t t) {
    String s = (String)((float)t / 10.0);
    s = s.substring(0, s.length() - 1);
    return s;
  }

  String ecToStr(uint16_t e) {
    String s = (String)((float)e / 100.0);
    if (e < 100) s = "  " + s;
    else if (e < 1000) s = " " + s;
    return s;
  }

public:
  LiquidCrystal_I2C lcd;
  LcdType () : backlightalarm(*this), backlightOnTime(10), lcd(LCD_ADDRESS, LCD_COLUMNS, LCD_ROWS) {}
  virtual ~LcdType () {}

  void showMeasureValues(int16_t temperature, uint16_t ec100) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("EC:");
    lcd.print(ecToStr(ec100));
    lcd.print(" mS/cm");
    lcd.setCursor(0, 1);
    lcd.print("T:");
    lcd.print(tempToStr(temperature));
    lcd.write(byte(0));
    lcd.print("C");
    lcd.setCursor(11, 1);
#if defined(EC_SENSOR_K10)
    lcd.print(" K=10");
#else
    lcd.print("  K=1");
#endif
  }

  void showCalibrationMenu(uint8_t step) {
    lcd.clear();
    switch (step) {
      case 0:   // Start
        lcd.setCursor(0, 0); lcd.print(F("EC CALIBRATION"));
        lcd.setCursor(2, 1); lcd.print(F("Press button"));
        break;
      case 10:  // K=1: 1413 uS/cm vorbereiten
        lcd.setCursor(0, 0); lcd.print(F("Put 1413us/cm"));
        lcd.setCursor(2, 1); lcd.print(F("Press button"));
        break;
      case 11:  // K=1: 1413 uS/cm OK
        lcd.setCursor(0, 0); lcd.print(F("1413us READ OK"));
        lcd.setCursor(2, 1); lcd.print(F("Press button"));
        break;
      case 20:  // 12.88 mS/cm vorbereiten
        lcd.setCursor(0, 0); lcd.print(F("Put 12.88ms/cm"));
        lcd.setCursor(2, 1); lcd.print(F("Press button"));
        break;
      case 21:  // 12.88 mS/cm OK
        lcd.setCursor(0, 0); lcd.print(F("12.88ms READ OK"));
        lcd.setCursor(0, 1); lcd.print(F("Saving..."));
        _delay_ms(1000);
        break;
      case 99:  // Fertig
        lcd.setCursor(0, 0); lcd.print(F("Calibration"));
        lcd.setCursor(0, 1); lcd.print(F("Done. Saved."));
        _delay_ms(2000);
        break;
      case 200: // Fehler: 1413 uS/cm nicht erkannt
        lcd.setCursor(0, 0); lcd.print(F("ERR: not 1413us"));
        lcd.setCursor(0, 1); lcd.print(F("Retry! Press btn"));
        _delay_ms(2000);
        break;
      case 201: // Fehler: 12.88 mS/cm nicht erkannt
        lcd.setCursor(0, 0); lcd.print(F("ERR:not 12.88ms"));
        lcd.setCursor(0, 1); lcd.print(F("Retry! Press btn"));
        _delay_ms(2000);
        break;
      case 202: // Fehler: kein Sensor / kein Signal
        lcd.setCursor(0, 0); lcd.print(F("ERR: no signal"));
        lcd.setCursor(0, 1); lcd.print(F("Check probe!"));
        _delay_ms(2000);
        break;
      case 255: // Generischer Fehler
        lcd.setCursor(3, 0); lcd.print(F("CAL FAILED"));
        lcd.setCursor(1, 1); lcd.print(F("STARTING AGAIN"));
        _delay_ms(2000);
        break;
    }
  }

  void initLCD(uint8_t *serial) {
    Wire.begin();
    Wire.beginTransmission(LCD_ADDRESS);
    if (Wire.endTransmission() == 0) {
      lcd.init();
      lcd.createChar(0, degree);
      lcd.backlight();
      lcd.setCursor(0, 0);
      lcd.print(ASKSIN_PLUS_PLUS_IDENTIFIER);
      lcd.setCursor(3, 1);
      lcd.setContrast(200);
      lcd.print((char*)serial);
      if (backlightOnTime > 0) backlightalarm.restartTimer(backlightOnTime);
    } else {
      DPRINT("LCD not found at 0x"); DHEXLN((uint8_t)LCD_ADDRESS);
    }
  }

  void setBackLightOnTime(uint8_t t) {
    backlightOnTime = t;
    if (backlightOnTime == 0) lcd.backlight(); else lcd.noBacklight();
  }
};

LcdType lcd;

// ========================================================
// Message
// ========================================================
class MeasureEventMsg : public Message {
public:
  void init(uint8_t msgcnt, int16_t temp, uint16_t ec100) {
    Message::init(0x0d, msgcnt, 0x53, BIDI | WKMEUP, (temp >> 8) & 0x7f, temp & 0xff);
    pload[0] = (ec100 >> 8) & 0xff;
    pload[1] = ec100 & 0xff;
  }
};

// ========================================================
// EC-Spannung lesen (Mittelwert 10 Samples)
// ========================================================
float readECVoltage() {
  float voltageSum = 0.0f;
  for (uint8_t i = 0; i < 10; i++) {
    _delay_ms(10);
    voltageSum += analogRead(EC_SIGNAL_PIN) / 1024.0f * (float)REF_VOLTAGE;
  }
  return voltageSum / 10.0f;
}

// ========================================================
// Messkanal
// ========================================================
class MeasureChannel : public Channel<Hal, UList1, EmptyList, List4, PEERS_PER_CHANNEL, UList0>, public Alarm {
private:
  MeasureEventMsg msg;
  OneWire         dsWire;
  Ds18b20         ds18b20[1];
  bool            ds18b20_present;
  EC_LIB_CLASS    ecSensor;

  bool            eccalibrationMode;
  uint8_t         eccalibrationStep;
  int16_t         currentTemperature;
  uint16_t        ec;

  uint16_t        measureCount;
  uint32_t        ec_cumulated;
  int32_t         temperature_cumulated;

public:
  MeasureChannel ()
    : Channel(), Alarm(seconds2ticks(3)),
      dsWire(DS18B20_PIN), ds18b20_present(false),
      eccalibrationMode(false), eccalibrationStep(0),
      currentTemperature(0), ec(0),
      measureCount(0), ec_cumulated(0), temperature_cumulated(0)
  {}
  virtual ~MeasureChannel () {}

  int16_t readTemperature() {
    if (ds18b20_present == false) return 250;
    Ds18b20::measure(ds18b20, 1);
    DPRINT(F("Temperature : ")); DDECLN(ds18b20[0].temperature());
    return (ds18b20[0].temperature()) + (-35 + 5 * this->getList1().TemperatureOffsetIndex());
  }

  uint16_t readEC() {
    float voltage = readECVoltage();
    DPRINT(F("EC Voltage (mV) : ")); DDECLN((int)voltage);
    float tempC = (float)currentTemperature / 10.0f;
    float ecVal = ecSensor.readEC(voltage, tempC);
    DPRINT(F("EC (mS/cm)      : ")); DDECLN((int)(ecVal * 100));
    return (uint16_t)(ecVal * 100.0f);
  }

  void disableECCalibrationMode() {
    DPRINTLN(F("Exiting EC Calibration Mode"));
    eccalibrationMode = false;
    sysclock.cancel(*this);
    eccalibrationStep = 0;
    this->changed(true);
    set(millis2ticks(1000));
    sysclock.add(*this);
  }

  void enableECCalibrationMode() {
    DPRINTLN(F("Entering EC Calibration Mode"));
    eccalibrationMode = true;
    sysclock.cancel(*this);
    this->changed(true);
    set(seconds2ticks(CALIBRATION_MODE_TIMEOUT));
    sysclock.add(*this);
    nextCalibrationStep();
  }

  void toggleECCalibrationMode() {
    eccalibrationMode = !eccalibrationMode;
    if (eccalibrationMode) enableECCalibrationMode(); else disableECCalibrationMode();
  }

  bool getCalibrationMode() { return eccalibrationMode; }

  // ------------------------------------------------------------------
  // Kalibrierungs-Ablauf mit Validierung
  // ------------------------------------------------------------------
  void nextCalibrationStep() {
    if (eccalibrationMode != true) { eccalibrationStep = 0; return; }

    DPRINT(F("EC CALIB STEP ")); DDECLN(eccalibrationStep);

#if defined(EC_SENSOR_K10)
    // ================================================================
    // K=10: Single-Point (12.88 mS/cm)
    // Step 0 -> Anzeige "EC CALIBRATION"
    // Step 1 -> Anzeige "Put 12.88ms"
    // Step 2 -> Messen + Validieren + calec
    // ================================================================
    switch (eccalibrationStep) {
      case 0:
        lcd.showCalibrationMenu(0);
        break;
      case 1:
        lcd.showCalibrationMenu(20);
        break;
      case 2:
        {
          float voltage = readECVoltage();
          float rawEC   = CALC_RAW_EC(voltage);
          float tempC   = (float)readTemperature() / 10.0f;

          DPRINT(F("  CAL voltage: ")); DDECLN((int)voltage);
          DPRINT(F("  CAL rawEC  : ")); DDECLN((int)(rawEC * 100));

          // Validierung: kein Signal?
          if (voltage < 5.0f) {
            DPRINTLN(F("  CAL ERR: no signal"));
            lcd.showCalibrationMenu(202);
            eccalibrationStep = 1;  // zurueck zu "Put in 12.88"
            return;
          }

          // Validierung: rawEC im erwarteten Bereich?
          if (rawEC < RAWEC_K10_1288_LOW || rawEC > RAWEC_K10_1288_HIGH) {
            DPRINTLN(F("  CAL ERR: not 12.88 mS/cm range"));
            lcd.showCalibrationMenu(201);
            eccalibrationStep = 1;  // zurueck zu "Put in 12.88"
            return;
          }

          // OK - Kalibrierung durchfuehren
          char cmdEnter[] = "enterec";
          ecSensor.calibration(voltage, tempC, cmdEnter);
          _delay_ms(100);
          char cmdCal[] = "calec";
          ecSensor.calibration(voltage, tempC, cmdCal);
          _delay_ms(100);
          char cmdExit[] = "exitec";
          ecSensor.calibration(voltage, tempC, cmdExit);

          lcd.showCalibrationMenu(21);
          lcd.showCalibrationMenu(99);
          disableECCalibrationMode();
          return;
        }
    }
    eccalibrationStep++;

#elif defined(EC_SENSOR_K1)
    // ================================================================
    // K=1: Two-Point (1413 uS/cm + 12.88 mS/cm)
    // Step 0 -> Anzeige "EC CALIBRATION"
    // Step 1 -> Anzeige "Put 1413us"
    // Step 2 -> Messen + Validieren 1413 + enterec + calec
    // Step 3 -> Anzeige "Put 12.88ms"
    // Step 4 -> Messen + Validieren 12.88 + calec + exitec
    // ================================================================
    switch (eccalibrationStep) {
      case 0:
        lcd.showCalibrationMenu(0);
        break;

      // --- Punkt 1: 1413 uS/cm ---
      case 1:
        lcd.showCalibrationMenu(10);
        break;
      case 2:
        {
          float voltage = readECVoltage();
          float rawEC   = CALC_RAW_EC(voltage);
          float tempC   = (float)readTemperature() / 10.0f;

          DPRINT(F("  CAL voltage: ")); DDECLN((int)voltage);
          DPRINT(F("  CAL rawEC  : ")); DDECLN((int)(rawEC * 100));

          // Validierung: kein Signal?
          if (voltage < 5.0f) {
            DPRINTLN(F("  CAL ERR: no signal"));
            lcd.showCalibrationMenu(202);
            eccalibrationStep = 1;
            return;
          }

          // Validierung: rawEC im 1413 uS/cm Bereich?
          if (rawEC < RAWEC_1413_LOW || rawEC > RAWEC_1413_HIGH) {
            DPRINT(F("  CAL ERR: rawEC ")); DDEC((int)(rawEC*100));
            DPRINTLN(F(" not in 1413us range (0.9-1.9)"));
            lcd.showCalibrationMenu(200);
            eccalibrationStep = 1;  // zurueck zu "Put in 1413"
            return;
          }

          // OK - enterec + calec (Lib erkennt 1413 automatisch)
          char cmdEnter[] = "enterec";
          ecSensor.calibration(voltage, tempC, cmdEnter);
          _delay_ms(100);
          char cmdCal[] = "calec";
          ecSensor.calibration(voltage, tempC, cmdCal);

          lcd.showCalibrationMenu(11);  // "1413us READ OK"
        }
        break;

      // --- Punkt 2: 12.88 mS/cm ---
      case 3:
        lcd.showCalibrationMenu(20);
        break;
      case 4:
        {
          float voltage = readECVoltage();
          float rawEC   = CALC_RAW_EC(voltage);
          float tempC   = (float)readTemperature() / 10.0f;

          DPRINT(F("  CAL voltage: ")); DDECLN((int)voltage);
          DPRINT(F("  CAL rawEC  : ")); DDECLN((int)(rawEC * 100));

          // Validierung: kein Signal?
          if (voltage < 5.0f) {
            DPRINTLN(F("  CAL ERR: no signal"));
            lcd.showCalibrationMenu(202);
            eccalibrationStep = 3;  // zurueck zu "Put in 12.88"
            return;
          }

          // Validierung: rawEC im 12.88 mS/cm Bereich?
          if (rawEC < RAWEC_1288_LOW || rawEC > RAWEC_1288_HIGH) {
            DPRINT(F("  CAL ERR: rawEC ")); DDEC((int)(rawEC*100));
            DPRINTLN(F(" not in 12.88ms range (9.0-16.8)"));
            lcd.showCalibrationMenu(201);
            eccalibrationStep = 3;  // zurueck zu "Put in 12.88"
            return;
          }

          // OK - calec + exitec (Lib erkennt 12.88 automatisch)
          char cmdCal[] = "calec";
          ecSensor.calibration(voltage, tempC, cmdCal);
          _delay_ms(100);
          char cmdExit[] = "exitec";
          ecSensor.calibration(voltage, tempC, cmdExit);

          lcd.showCalibrationMenu(21);
          lcd.showCalibrationMenu(99);
          disableECCalibrationMode();
          return;
        }
    }
    eccalibrationStep++;
#endif
  }

  void run() {
    measureCount++;
    DPRINT(F("Messung #")); DDECLN(measureCount);
    set(seconds2ticks(max(5, device().getList0().Messintervall())));

    currentTemperature = readTemperature();
    ec = readEC();

    float voltage = readECVoltage();
    ecSensor.calibration(voltage, (float)currentTemperature / 10.0f);

    lcd.showMeasureValues(currentTemperature, ec);

    ec_cumulated += ec;
    temperature_cumulated += currentTemperature;

    if (measureCount >= device().getList0().Sendeintervall()) {
      msg.init(device().nextcount(),
        (ds18b20_present == true) ? temperature_cumulated / measureCount : -400,
        ec_cumulated / measureCount);
      device().broadcastEvent(msg);
      measureCount = 0;
      ec_cumulated = 0;
      temperature_cumulated = 0;
    }
    sysclock.add(*this);
  }

  virtual void trigger (__attribute__ ((unused)) AlarmClock& clock) {
    if (eccalibrationMode == false) run(); else disableECCalibrationMode();
  }

  void setup(Device<Hal, UList0>* dev, uint8_t number, uint16_t addr) {
    Channel::setup(dev, number, addr);
    pinMode(EC_SIGNAL_PIN, INPUT);
    ds18b20_present = (Ds18b20::init(dsWire, ds18b20, 1) == 1);
    DPRINT(F("DS18B20: ")); DPRINTLN(ds18b20_present == true ? "OK" : "FAIL");
    ecSensor.begin();
    sysclock.add(*this);
  }

  void configChanged() {
    DPRINT(F("*Temperature Offset : ")); DDECLN(this->getList1().TemperatureOffsetIndex());
  }

  uint8_t status () const { return 0; }
  uint8_t flags ()  const { return eccalibrationMode ? 0x01 << 1 : 0x00; }
};

// ========================================================
// Device-Typ
// ========================================================
class UType : public MultiChannelDevice<Hal, MeasureChannel, 1, UList0> {
public:
  typedef MultiChannelDevice<Hal, MeasureChannel, 1, UList0> TSDevice;
  UType(const DeviceInfo& info, uint16_t addr) : TSDevice(info, addr) {}
  virtual ~UType () {}
  virtual void configChanged () {
    TSDevice::configChanged();
    DPRINT(F("*Messintervall   : ")); DDECLN(this->getList0().Messintervall());
    DPRINT(F("*Sendeintervall  : ")); DDECLN(this->getList0().Sendeintervall());
    uint8_t bOn = this->getList0().backOnTime();
    DPRINT(F("*LCD Backlight   : ")); DDECLN(bOn);
    lcd.setBackLightOnTime(bOn);
  }
};

UType sdev(devinfo, 0x20);

// ========================================================
// Calib-Button
// ========================================================
class CalibButton : public StateButton<HIGH, LOW, INPUT_PULLUP> {
  UType& device;
public:
  typedef StateButton<HIGH, LOW, INPUT_PULLUP> ButtonType;
  CalibButton (UType& dev, uint8_t longpresstime = 3) : device(dev) {
    this->setLongPressTime(seconds2ticks(longpresstime));
  }
  virtual ~CalibButton () {}
  virtual void state (uint8_t s) {
    uint8_t old = ButtonType::state();
    ButtonType::state(s);
    if (s == ButtonType::released) {
      if (device.channel(1).getCalibrationMode() == true)
        device.channel(1).nextCalibrationStep();
      else
        device.startPairing();
    }
    else if (s == ButtonType::pressed) {
      lcd.backlightalarm.restartTimer(sdev.getList0().backOnTime());
    }
    else if (s == ButtonType::longreleased) {
      device.channel(1).toggleECCalibrationMode();
    }
    else if (s == ButtonType::longpressed) {
      if (old == ButtonType::longpressed) {
        if (device.getList0().localResetDisable() == false) device.reset();
      } else {
        device.led().set(LedStates::key_long);
      }
    }
  }
};

CalibButton calibBtn(sdev);

// ========================================================
// setup + loop
// ========================================================
void setup () {
  DINIT(57600, ASKSIN_PLUS_PLUS_IDENTIFIER);
  sdev.init(hal);
  buttonISR(calibBtn, CONFIG_BUTTON_PIN);
  sdev.initDone();
  uint8_t serial[11]; sdev.getDeviceSerial(serial); serial[10] = 0;
  lcd.initLCD(serial);
}

void loop() {
  bool worked = hal.runready();
  bool poll   = sdev.pollRadio();
  if (worked == false && poll == false) {
    hal.activity.savePower<Idle<false, true>>(hal);
  }
}
