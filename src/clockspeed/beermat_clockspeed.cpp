#include <Arduino.h>
#include <stdint.h>
#include "imxrt.h"
#include "wiring.h"
#include "debug/printf.h"
// #include "brownout.h" // Assuming this include is added to provide PMU_REG_CORE

// REMOVED: This is no longer needed because PMU_REG_CORE is a macro in brownout.h
// #define PMU_REG_CORE_ADDR (volatile uint32_t*)(CCM_ANALOG_BASE + 0x140)

// A brief explanation of F_CPU_ACTUAL vs F_CPU
//  https://forum.pjrc.com/threads/57236?p=212642&viewfull=1#post212642
extern volatile uint32_t F_CPU_ACTUAL;
extern volatile uint32_t F_BUS_ACTUAL;

// Define these to increase the voltage when attempting overclocking
// The frequency step is how quickly to increase voltage per frequency
// The datasheet says 1600 is the absolute maximum voltage.  The hardware
// can actually create up to 1575.  But 1300 is the recommended limit.
//  (earlier versions of the datasheet said 1300 was the absolute max)
#define OVERCLOCK_STEPSIZE  28000000
#define OVERCLOCK_MAX_VOLT  1575


// stuff needing wait handshake:
//  CCM_CACRR  ARM_PODF
//  CCM_CBCDR  PERIPH_CLK_SEL
//  CCM_CBCMR  PERIPH2_CLK_SEL
//  CCM_CBCDR  AHB_PODF
//  CCM_CBCDR  SEMC_PODF

FLASHMEM uint32_t beermat_set_arm_clock(uint32_t frequency, uint32_t voltage_mv)
{
	uint32_t cbcdr = CCM_CBCDR; // pg 1021
	uint32_t cbcmr = CCM_CBCMR; // pg 1023
	uint32_t dcdc = DCDC_REG3;

	// compute required voltage, starting with the passed-in value
	uint32_t voltage;
	
	if (voltage_mv != 0) {
		// Use the provided voltage, later clamped by the maximum
		voltage = voltage_mv;
	} else {
		// Calculate default voltage based on frequency bands
		voltage = 1150; // Teensy default = 1.15V

        if (frequency > 528000000) {
                voltage = 1350; // Teensy default 1.25V

#if defined(OVERCLOCK_STEPSIZE) && defined(OVERCLOCK_MAX_VOLT)
                if (frequency > 600000000) {
                    voltage += ((frequency - 600000000) / OVERCLOCK_STEPSIZE) * 25;
                }
#endif
            } else if (frequency <= 24000000) {
                voltage = 950; // 0.95V
            }
	}
	
	// Apply max voltage check after all calculations
	if (voltage > OVERCLOCK_MAX_VOLT) {
		voltage = OVERCLOCK_MAX_VOLT;
	} else if (voltage == 0) {
		// Safety check if user passed 0mV and the default logic didn't override it
		voltage = 1150; 
	}


	// Calculate the PMU/DCDC target code (0.80V base, 25mV step)
	uint32_t dcdc_pmu_code = (voltage - 800) / 25;


	// if voltage needs to increase, do it before switch clock speed
	CCM_CCGR6 |= CCM_CCGR6_DCDC(CCM_CCGR_ON);
	if ((dcdc & DCDC_REG3_TRG_MASK) < DCDC_REG3_TRG(dcdc_pmu_code)) {
		printf("Increasing voltage to %u mV\n", voltage);
		
		// --- PMU SYNCHRONIZATION (Step 1 of 2: Update PMU_REG_CORE) ---
		// Update the PMU's expected VDD_SOC_CTRL (bits 25:21) with the new code
		uint32_t pmu_reg = PMU_REG_CORE; // Direct read using the macro
		pmu_reg &= ~(0x1F << 21); 			// Clear old VDD_SOC_CTRL bits
		pmu_reg |= (dcdc_pmu_code << 21);	// Set new VDD_SOC_CTRL code
		PMU_REG_CORE = pmu_reg;           // Direct write using the macro
		// -------------------------------------------------------------
		
		// Update DCDC Register
		dcdc &= ~DCDC_REG3_TRG_MASK;
		dcdc |= DCDC_REG3_TRG(dcdc_pmu_code);
		DCDC_REG3 = dcdc;
		
		while (!(DCDC_REG0 & DCDC_REG0_STS_DC_OK)) ; // wait voltage settling
	}

	if (!(cbcdr & CCM_CBCDR_PERIPH_CLK_SEL)) {
		printf("need to switch to alternate clock during reconfigure of ARM PLL\n");
		const uint32_t need1s = CCM_ANALOG_PLL_USB1_ENABLE | CCM_ANALOG_PLL_USB1_POWER |
			CCM_ANALOG_PLL_USB1_LOCK | CCM_ANALOG_PLL_USB1_EN_USB_CLKS;
		uint32_t sel, div;
		if ((CCM_ANALOG_PLL_USB1 & need1s) == need1s) {
			printf("USB PLL is running, so we can use 120 MHz\n");
			sel = 0;
			div = 3; // divide down to 120 MHz, so IPG is ok even if IPG_PODF=0
		} else {
			printf("USB PLL is off, use 24 MHz crystal\n");
			sel = 1;
			div = 0;
		}
		if ((cbcdr & CCM_CBCDR_PERIPH_CLK2_PODF_MASK) != CCM_CBCDR_PERIPH_CLK2_PODF(div)) {
			// PERIPH_CLK2 divider needs to be changed
			cbcdr &= ~CCM_CBCDR_PERIPH_CLK2_PODF_MASK;
			cbcdr |= CCM_CBCDR_PERIPH_CLK2_PODF(div);
			CCM_CBCDR = cbcdr;
		}
		if ((cbcmr & CCM_CBCMR_PERIPH_CLK2_SEL_MASK) != CCM_CBCMR_PERIPH_CLK2_SEL(sel)) {
			// PERIPH_CLK2 source select needs to be changed
			cbcmr &= ~CCM_CBCMR_PERIPH_CLK2_SEL_MASK;
			cbcmr |= CCM_CBCMR_PERIPH_CLK2_SEL(sel);
			CCM_CBCMR = cbcmr;
			while (CCM_CDHIPR & CCM_CDHIPR_PERIPH2_CLK_SEL_BUSY) ; // wait
		}
		// switch over to PERIPH_CLK2
		cbcdr |= CCM_CBCDR_PERIPH_CLK_SEL;
		CCM_CBCDR = cbcdr;
		while (CCM_CDHIPR & CCM_CDHIPR_PERIPH_CLK_SEL_BUSY) ; // wait
	} else {
		printf("already running from PERIPH_CLK2, safe to mess with ARM PLL\n");
	}

	// TODO: check if PLL2 running, can 352, 396 or 528 can work? (no need for ARM PLL)

	// DIV_SELECT: 54-108 = official range 648 to 1296 in 12 MHz steps
	uint32_t div_arm = 1;
	uint32_t div_ahb = 1;
	while (frequency * div_arm * div_ahb < 648000000) {
		if (div_arm < 8) {
			div_arm = div_arm + 1;
		} else {
			if (div_ahb < 5) {
				div_ahb = div_ahb + 1;
				div_arm = 1;
			} else {
				break;
			}
		}
	}
	uint32_t mult = (frequency * div_arm * div_ahb + 6000000) / 12000000;
	if (mult > 108) mult = 108;
	if (mult < 54) mult = 54;
	printf("Freq: 12 MHz * %u / %u / %u\n", mult, div_arm, div_ahb);
	frequency = mult * 12000000 / div_arm / div_ahb;

	printf("ARM PLL=%x\n", CCM_ANALOG_PLL_ARM);
	const uint32_t arm_pll_mask = CCM_ANALOG_PLL_ARM_LOCK | CCM_ANALOG_PLL_ARM_BYPASS |
		CCM_ANALOG_PLL_ARM_ENABLE | CCM_ANALOG_PLL_ARM_POWERDOWN |
		CCM_ANALOG_PLL_ARM_DIV_SELECT_MASK;
	if ((CCM_ANALOG_PLL_ARM & arm_pll_mask) != (CCM_ANALOG_PLL_ARM_LOCK
	  | CCM_ANALOG_PLL_ARM_ENABLE | CCM_ANALOG_PLL_ARM_DIV_SELECT(mult))) {
		printf("ARM PLL needs reconfigure\n");
		CCM_ANALOG_PLL_ARM = CCM_ANALOG_PLL_ARM_POWERDOWN;
		// TODO: delay needed?
		CCM_ANALOG_PLL_ARM = CCM_ANALOG_PLL_ARM_ENABLE
			| CCM_ANALOG_PLL_ARM_DIV_SELECT(mult);
		while (!(CCM_ANALOG_PLL_ARM & CCM_ANALOG_PLL_ARM_LOCK)) ; // wait for lock
		printf("ARM PLL=%x\n", CCM_ANALOG_PLL_ARM);
	} else {
		printf("ARM PLL already running at required frequency\n");
	}

	if ((CCM_CACRR & CCM_CACRR_ARM_PODF_MASK) != (div_arm - 1)) {
		CCM_CACRR = CCM_CACRR_ARM_PODF(div_arm - 1);
		while (CCM_CDHIPR & CCM_CDHIPR_ARM_PODF_BUSY) ; // wait
	}

	if ((cbcdr & CCM_CBCDR_AHB_PODF_MASK) != CCM_CBCDR_AHB_PODF(div_ahb - 1)) {
		cbcdr &= ~CCM_CBCDR_AHB_PODF_MASK;
		cbcdr |= CCM_CBCDR_AHB_PODF(div_ahb - 1);
		CCM_CBCDR = cbcdr;
		while (CCM_CDHIPR & CCM_CDHIPR_AHB_PODF_BUSY); // wait
	}

	uint32_t div_ipg = (frequency + 149999999) / 150000000;
	if (div_ipg > 4) div_ipg = 4;
	if ((cbcdr & CCM_CBCDR_IPG_PODF_MASK) != (CCM_CBCDR_IPG_PODF(div_ipg - 1))) {
		cbcdr &= ~CCM_CBCDR_IPG_PODF_MASK;
		cbcdr |= CCM_CBCDR_IPG_PODF(div_ipg - 1);
		// TODO: how to safely change IPG_PODF ??
		CCM_CBCDR = cbcdr;
	}

	//cbcdr &= ~CCM_CBCDR_PERIPH_CLK_SEL;
	//CCM_CBCDR = cbcdr;  // why does this not work at 24 MHz?
	CCM_CBCDR &= ~CCM_CBCDR_PERIPH_CLK_SEL;
	while (CCM_CDHIPR & CCM_CDHIPR_PERIPH_CLK_SEL_BUSY) ; // wait

	F_CPU_ACTUAL = frequency;
	F_BUS_ACTUAL = frequency / div_ipg;
	// This line assumes scale_cpu_cycles_to_microseconds is defined elsewhere, 
	// typically as a global variable in the linker or a macro in an included file.
	// scale_cpu_cycles_to_microseconds = 0xFFFFFFFFu / (uint32_t)(frequency / 1000000u); 

	printf("New Frequency: ARM=%u, IPG=%u\n", frequency, frequency / div_ipg);

	// if voltage needs to decrease, do it after switch clock speed
	if ((dcdc & DCDC_REG3_TRG_MASK) > DCDC_REG3_TRG(dcdc_pmu_code)) {
		printf("Decreasing voltage to %u mV\n", voltage);
		
		// --- PMU SYNCHRONIZATION (Step 2 of 2: Update PMU_REG_CORE) ---
		// Update the PMU's expected VDD_SOC_CTRL (bits 25:21) with the new code
		uint32_t pmu_reg = PMU_REG_CORE; // Direct read using the macro
		pmu_reg &= ~(0x1F << 21); 			// Clear old VDD_SOC_CTRL bits
		pmu_reg |= (dcdc_pmu_code << 21);	// Set new VDD_SOC_CTRL code
		PMU_REG_CORE = pmu_reg;           // Direct write using the macro
		// -------------------------------------------------------------
		
		// Update DCDC Register
		dcdc &= ~DCDC_REG3_TRG_MASK;
		dcdc |= DCDC_REG3_TRG(dcdc_pmu_code);
		DCDC_REG3 = dcdc;
		while (!(DCDC_REG0 & DCDC_REG0_STS_DC_OK)) ; // wait voltage settling
	}

	return frequency;
}