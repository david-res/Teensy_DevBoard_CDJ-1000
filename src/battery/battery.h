
#ifndef battery_H
#define battery_H

#include "../include/device_defines.h"

#if defined(RDI_DEVELOPMENTS_REV3)

#include "Arduino.h"

class battery {
  public:
    void init();

    void measureSOC(bool start);
    void getUpdates();

    int getVoltage();
    float getCurrent();
    uint8_t getSOC();
    int getTemp();

    bool isCharging();

    static void chargeState_Interrupt();

  private:
    void measureVoltage();
    void measureCurrent();

    float getCycles();
};

extern battery Battery;

#endif // RDI_DEVELOPMENTS_REV3

#endif