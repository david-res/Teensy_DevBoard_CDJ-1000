#include <Arduino.h>
#include "globals.h"
#include "app_stats.h"
#include "../include/device_defines.h"
#if defined(RDI_DEVELOPMENTS_REV3)  
#include "battery/battery.h"
#endif 

#if defined(USE_STATS)

FLASHMEM bool AppStats::readyToReport()
{
    return (millis() - _lastCheckTime >= (nextReportPeriod * 1'000));
}

FLASHMEM void AppStats::report()
{
    // Report all stats generically
    for (int i = 0; i < STAT_COUNT; i++) {
        const StatConfig& config = statConfigs[i];
        const StatData& stat = stats[i];
        
        // Skip stats that should only show when playing
        if (config.showOnlyWhenPlaying && !is_playing) {
            continue;
        }
       
        // Print name and count
        Serial.printf("%s %7ld, avg: %8.2fµS  max: %6.0fµS", config.name, stat.count / nextReportPeriod, 
            stat.count > 0 ? getAvgMicros(stat.count, stat.totalCycles) : 0, stat.maxCycles > 0 ? getMaxMicros(stat.maxCycles) : 0);
        
        // Print rate if configured (for file reads)
        if (config.showRate && stat.byteCount > 0) {
            float rate = (float)(stat.byteCount * (float)F_CPU_ACTUAL) / (float)(stat.totalCycles * 1024.0 * 1024.0);
            Serial.printf("  rate: %0.2fMB/s", rate);
        }
        
        Serial.println();
    }
    
#if defined(RDI_DEVELOPMENTS_REV3)  
    Battery.getUpdates();   
    float currentMA = Battery.getCurrent();
    Serial.printf("Battery current:   %3.2fmA, CPU temp: %0.2f°C\n", 
        currentMA < 0 ? 0 : currentMA, tempmonGetTemp());
#endif
    Serial.println("--");

    // Reset counters and timer
    reset();
}

FASTRUN void AppStats::start(StatType type)
{
    StatData& stat = stats[type];
    
    if (stat.started == true) {
        Serial.println("Overlapping stats.start() without stats.end()");
    }
    
    stat.started = true;
    stat.startCycle = ARM_DWT_CYCCNT;
}

FASTRUN uint32_t AppStats::end(StatType type)
{
    StatData& stat = stats[type];
    
    uint32_t cycles = ARM_DWT_CYCCNT - stat.startCycle;
    
    stat.started = false;
    stat.count++;
    stat.totalCycles += cycles;
    
    // Update max if this cycle count is larger
    if (cycles > stat.maxCycles) {
        stat.maxCycles = cycles;
    }
    
    return cycles;
}

FLASHMEM void AppStats::reset()
{
    // Reset all stats
    for (int i = 0; i < STAT_COUNT; i++) {
        stats[i].count = 0;
        stats[i].totalCycles = 0;
        stats[i].maxCycles = 0;
        stats[i].byteCount = 0;
        // Note: don't reset startCycle or started flag in case measurement is ongoing
    }

    // Reset timer
    _lastCheckTime = millis();
}

void AppStats::addByteCount(StatType type, uint32_t bytes)
{ 
    StatData& stat = stats[type];
    stat.byteCount += bytes;
}
    

// Private helpers

FASTRUN float AppStats::getAvgMicros(uint32_t count, uint32_t totalCycles)
{
    return (float)totalCycles * 1000000.0f / ((float)count * (float)F_CPU_ACTUAL);
}

FASTRUN float AppStats::getMaxMicros(uint32_t totalCycles)
{
    return (float)totalCycles * 1'000'000.0f / (float)F_CPU_ACTUAL;
}
#else
    FLASHMEM bool AppStats::readyToReport() {
        return false;
    };

    FLASHMEM void AppStats::report()
    {
        //
    }
    FLASHMEM void AppStats::start(StatType type)
    {
        //
    }

    FLASHMEM uint32_t AppStats::end(StatType type)
    {
        return 0;
    }

    FLASHMEM void AppStats::reset()
    {
        //
    }

    FLASHMEM void AppStats::addByteCount(StatType type, uint32_t bytes)
    {
        //
    }
#endif