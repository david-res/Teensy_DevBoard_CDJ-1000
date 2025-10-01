#ifndef FT5316_H
#define FT5316_H

#include <Arduino.h>
#include <Wire.h>

class FT5316 {
public:
    // Constructor
    FT5316(uint8_t address = 0x38, int intPin = -1);
    
    // Initialization
    bool begin();
    bool begin(int intPin);
    
    // Touch data structure
    struct TouchPoint {
        uint16_t x;
        uint16_t y;
        bool touched;
    };
    
    // Main functions
    bool isTouched();
    TouchPoint getTouch();
    void printTouch();
    
    // Interrupt handling
    bool hasNewTouch();
    void clearTouchFlag();
    static void touchISR();  // Interrupt service routine
    
    // Low-level functions
    uint8_t getNumTouches();
    uint16_t getTouchX();
    uint16_t getTouchY();
    
private:
    uint8_t _address;
    int _intPin;
    static volatile bool _touchFlag;  // Flag set by interrupt
    static FT5316* _instance;        // For static ISR access
    
    // Register addresses
    static const uint8_t FT5316_REG_NUM_TOUCHES = 0x02;
    static const uint8_t FT5316_REG_TOUCH1_XH = 0x03;
    static const uint8_t FT5316_REG_TOUCH1_XL = 0x04;
    static const uint8_t FT5316_REG_TOUCH1_YH = 0x05;
    static const uint8_t FT5316_REG_TOUCH1_YL = 0x06;
    
    // Helper functions
    uint8_t readRegister(uint8_t reg);
    void readMultipleRegisters(uint8_t reg, uint8_t* buffer, uint8_t length);
};

#endif