#ifndef BEERMAT_CLOCKSPEED_H
#define BEERMAT_CLOCKSPEED_H

#include <Arduino.h>
#include <stdint.h>

uint32_t beermat_set_arm_clock(uint32_t frequency, uint32_t voltage_mv);

#endif // BEERMAT_CLOCKSPEED_H