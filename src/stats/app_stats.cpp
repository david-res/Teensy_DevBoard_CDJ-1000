#include <Arduino.h>
#include "app_stats.h"

FLASHMEM bool AppStats::readyToReport()
{
    return (millis() - _lastCheckTime >= (nextReportPeriod * 1'000));
}

FLASHMEM void AppStats::report()
{
    // Report data
    Serial.printf("IRQ/s: %ld\n", interruptCounter / nextReportPeriod);
    Serial.printf("dynamicWaveform/s: %ld, avg: %0.2fµS\n", waveformRenderCount / nextReportPeriod, waveformRenderCount == 0 ? 0 : (float)waveformRenderTime / (float)waveformRenderCount);
    Serial.printf("PlayFile: Read/s: %ld, avg: %0.2fµS, rate: %0.2fMB/s\n", playFileReadCount / nextReportPeriod, playFileReadCount == 0 ? 0 : (float)playFileReadTime / (float)playFileReadCount, 
                playFileReadCount == 0 ? 0 : (float)(playFileReadBytes * 1'000'000.0) / (float)(playFileReadTime * 1024.0 * 1024.0));
    Serial.printf("PlayFile: Seek/s: %ld, avg: %0.2fµS\n", playFileSeekCount / nextReportPeriod, playFileSeekCount == 0 ? 0 : (float)playFileSeekTime / (float)playFileSeekCount);
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

    waveformRenderTime = 0;
    waveformRenderCount = 0;

    playFileReadCount = 0;
    playFileReadBytes = 0;
    playFileReadTime = 0;

    playFileSeekCount = 0;
    playFileSeekTime = 0;

    // Reset timer
    _lastCheckTime = millis();

}