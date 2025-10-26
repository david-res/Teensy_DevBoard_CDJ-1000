#ifndef APP_STATS_H
#define APP_STATS_H

#include <Arduino.h>
#include "../include/device_defines.h"

// Add new stats by adding to enum StatType, constexpr statConfigs. 
// Then use in code with appStat.start(ENUM) and appStats.end(ENUM)

const uint32_t nextReportPeriod = 5; // Number of seconds between reports

enum StatType {
    ISR_I2S,
    ISR_LCD,
    LV_TIMER_HANDLER,
    DYNAMIC_RENDER,
    BEAT_GRID_RENDER,
    DYNAMIC_MEMCPY,
    OVERVIEW_COPY,
    PLAYFILE_READ,
    PLAYFILE_SEEK,
    MAIN_LOOP,
    STAT_COUNT  // Don't remove, keep as last item, allows automatic count of enum values
};

class AppStats
{
public:
    bool readyToReport();
    void report();
    void start(StatType type);
    uint32_t end(StatType type);
    void reset();

    // Extra accessor for byte count
    void addByteCount(StatType type, uint32_t bytes);

private:
    struct StatData {
        volatile uint32_t count = 0;
        volatile uint32_t totalCycles = 0;
        volatile uint32_t maxCycles = 0;
        volatile uint32_t byteCount = 0;
        uint32_t startCycle = 0;
        bool started = false;
    };

    struct StatConfig {
        const char* name;
        bool showOnlyWhenPlaying;
        bool showRate;  // For playfile read rate calculation
    };

    StatData stats[STAT_COUNT];
    uint32_t _lastCheckTime = 0;

    // Configuration for each stat type - ADD NEW STATS HERE!
    // Must match order of StatType enum above
    static constexpr StatConfig statConfigs[STAT_COUNT] = {
        // name,                  showOnlyWhenPlaying, showRate
        {"I2S ISR/s:       ",     false,               false},  // ISR_I2S
        {"LCD ISR/s:       ",     false,               false},  // ISR_LCD
        {"timerHandler/s:  ",     true,                false},  // LV_TIMER_HANDLER
        {"dynamic render/s:",     true,                false},  // DYNAMIC_RENDER
        {"Beat render/s:   ",     true,                false},  // BEAT_GRID_RENDER
        {"dynamic copy/s:  ",     true,                true},   // DYNAMIC_MEMCPY
        {"static copy/s:   ",     true,                false},  // OVERVIEW_COPY
        {"PlayFile: Read/s:",     true,                true},   // PLAYFILE_READ
        {"PlayFile: Seek/s:",     true,                false},  // PLAYFILE_SEEK
        {"Main Loop/s:     ",     true,                false},  // MAIN_LOOP
    };

    float getAvgMicros(uint32_t count, uint32_t totalCycles);
    float getMaxMicros(uint32_t totalCycles);
};

#endif //APP_STATS_H