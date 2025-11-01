#ifndef _I2S_SYNC_H
#define _I2S_SYNC_H


#include "Arduino.h"
#include "imxrt.h"
#include "Audio.h"


class i2s_sync{
    public:
    typedef void(*CBF)();
    static CBF _callback;
    void begin(CBF callback);
    void stop();
    void startI2SInterrupt();
    void stopI2SInterrupt();
    float sample_rate = 44100.0f;

    private:
    void set_audioClock(int nfact, int32_t nmult, uint32_t ndiv);
    void config_sai();
    

};

#endif