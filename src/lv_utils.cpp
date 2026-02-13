#include "lv_utils.h"

/*
const char* getKey(uint8_t numericValue) {
    if (numericValue < sizeof(keyLookup) / sizeof(keyLookup[0])) {
        return keyLookup[numericValue].key;
    }
    return "N/A";
}
*/    

uint8_t lookupValue(uint8_t input) {
    switch (input) {
        case 0: return 0;
        case 20: return 1;
        case 40: return 2;
        case 60: return 3;
        case 80: return 4;
        case 100: return 5;
        case 120: return 5;
        default: return 255; // Invalid input
    }
}

// Convert hex string to lv_color_t
lv_color_t hex_string_to_color(const char* hex) {
    if (!hex) {
        return COLOR_GRAY;
    }
    
    if (hex[0] == '#') hex++; // Skip # if present
    
    uint32_t color_val = strtoul(hex, NULL, 16);
    return lv_color_hex(color_val);
}


const char* getKeyColor(const char* keyName) {
    for (const auto& entry : keyLookupColor) {
        if (strcmp(entry.name, keyName) == 0) {
            return entry.color;
        }
    }
    return "#ffffff"; // fallback white
}
/*
lv_color_t getKeyColor(uint8_t numericValue) {
    if (numericValue < sizeof(keyLookupColor) / sizeof(keyLookupColor[0])) {
        return hex_string_to_color(keyLookupColor[numericValue].key);
    }
    return COLOR_GRAY; // Default color for invalid keys
}
*/

const char* formatDuration(uint16_t total_seconds) {
    static char formatted_time[8]; // Static buffer for "MM:SS\0"
    
    int minutes = total_seconds / 60;
    int seconds = total_seconds % 60;
    
    snprintf(formatted_time, sizeof(formatted_time), "%d:%02d", minutes, seconds);
    return formatted_time;
}

FLASHMEM uint32_t get_voltage_mv()
{
    uint32_t dcdc_val = DCDC_REG3;
    uint32_t trg_field_bits = dcdc_val & DCDC_REG3_TRG_MASK;
    uint32_t trg_value = trg_field_bits;

    uint32_t voltage_mv = (trg_value * 25) + 800;

    return voltage_mv;
}

const float flexspi2_clock_speeds[4] = {396.0f, 720.0f, 664.62f, 528.0f};

//Returns speed of EXTMEM clock, in MHz
FLASHMEM float getPSRamSpeed() {
  return flexspi2_clock_speeds[(CCM_CBCMR >> 8) & 0x03] / (float)(((CCM_CBCMR >> 29) & 0x07) + 1);
}


FLASHMEM void setPSRamSpeed(int mhz) {
  //See what the closest setting might be:
  uint8_t clk_save = 0, divider_save = 0;
  int min_delta = mhz;
  for (uint8_t clk = 0; clk < 4; clk++) {
      uint8_t divider = (flexspi2_clock_speeds[clk] + (mhz / 2)) / mhz;
      int delta = abs(mhz - flexspi2_clock_speeds[clk] / divider);
      if ((delta < min_delta) && (divider < 8)) {
          min_delta = delta;
          clk_save = clk;
          divider_save = divider;
      }
  }

  //First turn off FLEXSPI2
  CCM_CCGR7 &= ~CCM_CCGR7_FLEXSPI2(CCM_CCGR_ON);

  divider_save--; // 0 biased

  //Set the clock settings.
  CCM_CBCMR = (CCM_CBCMR & ~(CCM_CBCMR_FLEXSPI2_PODF_MASK | CCM_CBCMR_FLEXSPI2_CLK_SEL_MASK))
              | CCM_CBCMR_FLEXSPI2_PODF(divider_save) | CCM_CBCMR_FLEXSPI2_CLK_SEL(clk_save);

  //Turn FlexSPI2 clock back on
  CCM_CCGR7 |= CCM_CCGR7_FLEXSPI2(CCM_CCGR_ON);

//#if defined(DEBUG)
  Serial.printf("Update FLEXSPI2 speed: %u clk:%u div:%u Actual:%u\n", mhz, clk_save, divider_save,
      flexspi2_clock_speeds[clk_save] / (divider_save + 1));
//#endif
}