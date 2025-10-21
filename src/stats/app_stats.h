#ifndef APP_STATS_H
#define APP_STATS_H

#include <Arduino.h>
#include "../include/device_defines.h"

const uint32_t nextReportPeriod = 5; // Number of seconds between reports

class AppStats
{
public:
    bool readyToReport();
    void report();
    void start();
    uint32_t end();
    void reset();

    volatile uint32_t interruptCounter = 0;

    uint32_t waveformRenderTime = 0;
    uint32_t waveformRenderCount = 0;

    uint32_t playFileReadCount = 0;
    uint32_t playFileReadBytes = 0;
    uint32_t playFileReadTime = 0;

    uint32_t playFileSeekCount = 0;
    uint32_t playFileSeekTime = 0;
    
private:
    bool _started;
    uint32_t _startTime;
    uint32_t _lastCheckTime = 0;
};

#endif //APP_STATS_H