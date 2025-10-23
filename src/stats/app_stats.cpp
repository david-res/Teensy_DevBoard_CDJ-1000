#include <Arduino.h>
#include "globals.h"
#include "app_stats.h"
#if defined(RDI_DEVELOPMENTS_REV3)  
#include "battery/battery.h"
#endif 

FLASHMEM bool AppStats::readyToReport()
{
    return (millis() - _lastCheckTime >= (nextReportPeriod * 1'000));
}

FLASHMEM void AppStats::report()
{
    // Report data
    Serial.printf("IRQ/s: %ld\n", interruptCounter / nextReportPeriod);
    if (is_playing == true) {
        Serial.printf("timerHandler/s:    %2ld, avg: %8.2fµS, max: %ldµS\n", lvTimerHandlerCount / nextReportPeriod, lvTimerHandlerCount == 0 ? 0 : (float)lvTimerHandlerTime / (float)lvTimerHandlerCount, lvTimerHandlerMax);
        Serial.printf("dynamic render/s:  %2ld, avg: %8.2fµS\n", dynamicRenderCount / nextReportPeriod, dynamicRenderCount == 0 ? 0 : (float)dynamicRenderTime / (float)dynamicRenderCount);
        Serial.printf("dynamic copy/s:    %2ld, avg: %8.2fµS\n", dynamicMemCpyCount / nextReportPeriod, dynamicMemCpyCount == 0 ? 0 : (float)dynamicMemCpyTime / (float)dynamicMemCpyCount);
        Serial.printf("static copy/s:     %2ld, avg: %8.2fµS\n", overviewCopyCount / nextReportPeriod, overviewCopyCount == 0 ? 0 : (float)overviewCopyTime / (float)overviewCopyCount);
        Serial.printf("PlayFile: Read/s:  %2ld, avg: %8.2fµS, rate: %0.2fMB/s\n", playFileReadCount / nextReportPeriod, playFileReadCount == 0 ? 0 : (float)playFileReadTime / (float)playFileReadCount, 
                    playFileReadCount == 0 ? 0 : (float)(playFileReadBytes * 1'000'000.0) / (float)(playFileReadTime * 1024.0 * 1024.0));
        Serial.printf("PlayFile: Seek/s:  %2ld, avg: %8.2fµS\n", playFileSeekCount / nextReportPeriod, playFileSeekCount == 0 ? 0 : (float)playFileSeekTime / (float)playFileSeekCount);
    }
#if defined(RDI_DEVELOPMENTS_REV3)  
    Battery.getUpdates();   
    float currentMA = Battery.getCurrent();
    Serial.printf("Battery current:  %0.2fmA\n", currentMA < 0 ? 0 : currentMA);
#endif
    Serial.println("--");

    // Reset counters and timer
    reset();
}

FASTRUN void AppStats::start()
{
    if (_started == true) {
        Serial.println("Overlapping stats.start() without stats.end()");
    }
    _startTime = micros();
    _started = true;
}

FASTRUN uint32_t AppStats::end()
{
    _started = false;
    return micros() - _startTime;
}

FLASHMEM void AppStats::reset()
{
    // Reset counters
    interruptCounter = 0;

    lvTimerHandlerCount = 0;
    lvTimerHandlerTime = 0;
    lvTimerHandlerMax = 0;

    dynamicRenderTime = 0;
    dynamicRenderCount = 0;
    dynamicMemCpyTime = 0;
    dynamicMemCpyCount = 0;

    overviewCopyTime = 0;
    overviewCopyCount = 0;

    playFileReadCount = 0;
    playFileReadBytes = 0;
    playFileReadTime = 0;

    playFileSeekCount = 0;
    playFileSeekTime = 0;

    // Reset timer
    _lastCheckTime = millis();

}