#include "../include/device_defines.h"

#if defined(RDI_DEVELOPMENTS_REV3)

#define DEVICE_PIN_BAT_CHRG                   22                // Battery charging pin
#define DEVICE_PIN_LCD_TOUCH_RST              23                // RST pin for LCD and Touch glass
#define DEVICE_PIN_BACKLIGHT                  24                // LCD BACKLIGHT PWM PIN
#define DEVICE_PIN_POWER_BUTTON               25                // SIDE POWER BUTTON PIN
#define DEVICE_PIN_FAST_OFF                   17                // TURN OFF DEVICE PIN

#define CONFIG_SHTDWN_HOLD_TIME               1000              // Time in milliseconds that you must hold the power button for the device to shutdown.

#define CONFIG_BAT_CHARGE_THRESHOLD           100               // Threshold in mA for if the device is being charged or not.

#include "battery.h"

#include "MAX17055.h"
#include "circular_buffer.h"
#include "globals.h"

#include "lvgl.h"

battery Battery;
MAX17055 lipo;

// VARIABLES
bool PIN_INTERRUPT_CHARGING = false;

DMAMEM bool _shouldResetGauge = false;
DMAMEM uint16_t _previousCycles = 0;
DMAMEM Circular_Buffer <int, 4> _mV;
DMAMEM Circular_Buffer <int, 4> _current;
DMAMEM uint8_t _SOC = 0;

FLASHMEM void battery::init() {
  lipo.begin();

  chargeState_Interrupt();

  measureVoltage();
  measureCurrent();

  measureSOC(true);
}

FLASHMEM void battery::measureVoltage() {
  _mV.push_back(lipo.getVoltageCell());
}

FLASHMEM void battery::measureCurrent() {
  _current.push_back(lipo.getCurrent_mA());
}

FLASHMEM void battery::measureSOC(bool start) {
  uint8_t SOC_Raw = lipo.getRepSOC();

  int SOC_Buf = map(SOC_Raw, 20, 90, 0, 100);
  SOC_Buf = constrain(SOC_Buf, 0, 100);

  int mv = 3400;
  if (start) {
    mv = 3500;
  }

  if (SOC_Buf < 1 && _mV.mean() >= mv) {
    SOC_Buf = 1;
  }

  _SOC = SOC_Buf;

  /*
  Serial.println(SOC_Raw);
  Serial.println(getCurrent());
  Serial.println(_mV.mean());
  Serial.println(tempmonGetTemp());
  Serial.println(getTemp());
  Serial.println(getCycles());
  Serial.println();
  */
}

FLASHMEM int battery::getVoltage() {
  return _mV.mean();
}

FLASHMEM float battery::getCurrent() {
  return _current.mean();
}

FLASHMEM uint8_t battery::getSOC() {
  return _SOC;
}

FLASHMEM int battery::getTemp() {
  return lipo.getTemp();
}

FLASHMEM float battery::getCycles() {
  return lipo.getChargeCycles();
}

FLASHMEM bool battery::isCharging() {
  if (PIN_INTERRUPT_CHARGING && getCurrent() > CONFIG_BAT_CHARGE_THRESHOLD) {
    return true;
  } else {
    return false;
  }
}

FLASHMEM void battery::getUpdates() {
  measureVoltage();
  measureCurrent();
  measureSOC(false);

  if (!isCharging() && (millis() > 8000) && (getSOC() < 1)) {
    //POWER_fastPowerDown(); // Auto shutdown if battery low
  }
}

// USES THE DOCK DETECT PIN TO SEE IF CHARGE OR NOT.
FLASHMEM void battery::chargeState_Interrupt() {
  if (digitalRead(DEVICE_PIN_BAT_CHRG) == HIGH) {
    PIN_INTERRUPT_CHARGING = true;
  } else {
    PIN_INTERRUPT_CHARGING = false;
  }
}
#endif // RDI_DEVELOPMENTS_REV3