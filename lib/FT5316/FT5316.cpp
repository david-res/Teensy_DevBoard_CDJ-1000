#include "FT5316.h"

// Static variables for interrupt handling
volatile bool FT5316::_touchFlag = false;
FT5316* FT5316::_instance = nullptr;

FT5316::FT5316(uint8_t address, int intPin) {
    _address = address;
    _intPin = intPin;
    _instance = this;  // Set static instance for ISR access
}

bool FT5316::begin() {
    Wire.begin();
    
    // Test communication by reading a register
    Wire.beginTransmission(_address);
    uint8_t error = Wire.endTransmission();
    
    if (error == 0) {
        // Device found, add small delay for stability
        delay(50);
        
        // Setup interrupt pin if specified
        if (_intPin >= 0) {
            pinMode(_intPin, INPUT_PULLUP);
            attachInterrupt(digitalPinToInterrupt(_intPin), touchISR, FALLING);
        }
        
        return true;
    }
    return false;
}

bool FT5316::begin(int intPin) {
    _intPin = intPin;
    return begin();
}

uint8_t FT5316::readRegister(uint8_t reg) {
    Wire.beginTransmission(_address);
    Wire.write(reg);
    Wire.endTransmission(false);  // Send restart
    
    Wire.requestFrom(_address, (uint8_t)1);
    if (Wire.available()) {
        return Wire.read();
    }
    return 0;
}

void FT5316::readMultipleRegisters(uint8_t reg, uint8_t* buffer, uint8_t length) {
    Wire.beginTransmission(_address);
    Wire.write(reg);
    Wire.endTransmission(false);  // Send restart
    
    Wire.requestFrom(_address, length);
    for (uint8_t i = 0; i < length && Wire.available(); i++) {
        buffer[i] = Wire.read();
    }
}

uint8_t FT5316::getNumTouches() {
    return readRegister(FT5316_REG_NUM_TOUCHES) & 0x0F;  // Lower 4 bits
}

bool FT5316::isTouched() {
    return getNumTouches() > 0;
}

uint16_t FT5316::getTouchX() {
    uint8_t data[2];
    readMultipleRegisters(FT5316_REG_TOUCH1_XH, data, 2);
    
    // Combine high and low bytes, mask out event flag bits
    uint16_t x = ((data[0] & 0x0F) << 8) | data[1];
    return x;
}

uint16_t FT5316::getTouchY() {
    uint8_t data[2];
    readMultipleRegisters(FT5316_REG_TOUCH1_YH, data, 2);
    
    // Combine high and low bytes, mask out event flag bits
    uint16_t y = ((data[0] & 0x0F) << 8) | data[1];
    return y;
}

FT5316::TouchPoint FT5316::getTouch() {
    TouchPoint point;
    
    if (isTouched()) {
        point.x = getTouchX();
        point.y = getTouchY();
        point.touched = true;
    } else {
        point.x = 0;
        point.y = 0;
        point.touched = false;
    }
    
    return point;
}

void FT5316::printTouch() {
    TouchPoint touch = getTouch();
    
    if (touch.touched) {
        Serial.print("Touch detected - X: ");
        Serial.print(touch.x);
        Serial.print(", Y: ");
        Serial.println(touch.y);
    } else {
        Serial.println("No touch detected");
    }
}

// Interrupt handling functions
bool FT5316::hasNewTouch() {
    return _touchFlag;
}

void FT5316::clearTouchFlag() {
    _touchFlag = false;
}

// Static interrupt service routine
void FT5316::touchISR() {
    _touchFlag = true;
}