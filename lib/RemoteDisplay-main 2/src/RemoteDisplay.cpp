#include <Arduino.h>
#include "RemoteDisplay.h"

FLASHMEM void RemoteDisplay::init(uint16_t inScreenWidth, uint16_t inScreenHeight, uint16_t inPortStream)
{
    screenWidth = inScreenWidth;
    screenHeight = inScreenHeight;
    portStream = inPortStream;

    sendRemoteScreen = false;
    disableLocalScreen = false;

    Serial.printf("In RemoteDisplay::init, port: %d\n", portStream);

    //Start listening

    //Serial
    REM_SERIALOUT.begin(115'200);
}

FLASHMEM void  RemoteDisplay::registerRefreshCallback(refresh_callback_t inRefreshCallback)
{
    Serial.printf("In RemoteDisplay::registerRefreshCallback\n");

    refreshCallback = inRefreshCallback;
}

FLASHMEM void  RemoteDisplay::registerTouchCallback(touch_callback_t inTouchCallback)
{
    Serial.printf("In RemoteDisplay::registerTouchCallback\n");

    touchCallback = inTouchCallback;
}

FLASHMEM void  RemoteDisplay::registerCommandCallback(command_callback_t inCommandCallback)
{
    Serial.printf("In RemoteDisplay::registerCommandCallback\n");

    commandCallback = inCommandCallback;
}



FLASHMEM void RemoteDisplay::connectRemoteSerial()
{
    Serial.printf("In RemoteDisplay::connectRemoteSerial, blockSize: %d\n", MAX_USB_PACKET_SIZE);

    //Clean up existing connection first
    if (connectionType != SEND_NONE) {
        disconnectRemote();
    }

    serialFailedCount = 0;
    connectionType = SEND_USBSERIAL;
    connectRemote();
}

FLASHMEM void RemoteDisplay::connectRemote()
{
    sendRemoteScreen = true;

    Serial.printf("In RemoteDisplay::connectRemote, sending INIT_REMOTE\n");
    sendHeader(INIT_REMOTE, 0);

    //Send initial full display refresh
    refreshDisplay();

    if (commandCallback) {
        commandCallback(CMD_CONNECT);
    }
}

FLASHMEM void RemoteDisplay::disconnectRemote()
{
    Serial.printf("In RemoteDisplay::disconnectRemote\n");

    sendRemoteScreen = false;
    disableLocalScreen = false;

    if (connectionType == SEND_USBSERIAL) {
        //Cleanup in-flight data
        REM_SERIALOUT.clear();
        REM_SERIALOUT.flush();
    }
    connectionType = SEND_NONE;

    if (commandCallback) {
        commandCallback(CMD_DISCONNECT);
    }
}

FLASHMEM void RemoteDisplay::refreshDisplay()
{
    Serial.printf("In RemoteDisplay::refreshDisplay\n");

    if (refreshCallback) {
        refreshCallback();
    }
}

FASTRUN void RemoteDisplay::sendData(const uint16_t x1, const uint16_t y1, const uint16_t x2, const uint16_t y2, uint8_t * pixelmap)
{
    //Serial.printf("In RemoteDisplay::sendData, area: x1: %ld y1: %ld x2:%ld y2:%ld\n", x1, y1, x2, y2);

    if (sendRemoteScreen == false) {
        return;
    }

    const uint8_t * transBufferEndPtr = transBufferStart + (connectionType == SEND_ETHERNET ? MAX_ETH_PACKET_SIZE : MAX_USB_PACKET_SIZE);

    rlePacket.controlValue = RLE_ESCAPED_8_DATA;
    rlePacket.x = x1;
    rlePacket.y = y1;
    rlePacket.width = (x2 - x1 + 1);
    rlePacket.progressStart = 0;

    uint32_t totalPixels = rlePacket.width * (y2 - y1 + 1);

    sendHeader(RLE_ESCAPED_8_HEADER, totalPixels);

    uint8_t runLength = 1; // Current run length
    uint32_t pixelsInPacket = 0;

    uint8_t * dataBufferPtr = (uint8_t *)transBufferStart;

    uint16_t * pixelPtr = (uint16_t *)pixelmap;
    uint16_t * pixelPtrEnd = pixelPtr + totalPixels;

    // Initialize lastColor with the first pixel color
    uint16_t lastColor = *pixelPtr++;

    // Traverse through the pixel data
    while (pixelPtr < pixelPtrEnd)
    {
        uint16_t color = *pixelPtr;

        bool colorChanged = (pixelPtr == (pixelPtrEnd - 1)) || (color != lastColor);
        bool bufferFull = (dataBufferPtr >= transBufferEndPtr - 3);

        if (colorChanged || (runLength == 255) || bufferFull)
        {
            // Add the current run to the RLE buffer
            *dataBufferPtr++ = lastColor >> 8;
            *dataBufferPtr++ = lastColor;
            *dataBufferPtr++ = runLength;

            pixelsInPacket += runLength;

            // Send the RLE buffer if the buffer is full or if we've processed all pixels
            if (bufferFull == true)
            {
                sendPacket((uint8_t *)&rlePacket, packetHeaderSize + (dataBufferPtr - (uint8_t *)transBufferStart));

                rlePacket.progressStart += pixelsInPacket;
                pixelsInPacket = 0;
                dataBufferPtr = (uint8_t *)transBufferStart;
            }

            // Reset runLength and update lastColor
            runLength = 1;
            lastColor = color;
        }
        else
        {
            // Continue the current run
            runLength++;
        }
        pixelPtr++;
    }
    //Send last
    *dataBufferPtr++ = lastColor >> 8;
    *dataBufferPtr++ = lastColor;
    *dataBufferPtr++ = runLength;

    pixelsInPacket += runLength;
    sendPacket((uint8_t *)&rlePacket, packetHeaderSize + (dataBufferPtr - transBufferStart));

    rlePacket.progressStart += pixelsInPacket;

    // Check if the total number of pixels sent matches the expected totalPixels
    if (rlePacket.progressStart != totalPixels) {
        Serial.printf("Error: Discrepancy: Expected %ld pixels, but sent %ld pixels\n", totalPixels, rlePacket.progressStart);
    } else {
        //Serial.printf("All pixels sent successfully\n");
    }
}

FASTRUN void RemoteDisplay::pollRemoteCommand()
{
    //Serial.printf("In RemoteDisplay::pollRemoteCommand\n");
    //Need to check both network and USBSerial as we could be sitting waiting for a new connection from either source

    int32_t packetSize = 0;
    char incomingPacketBuffer[5]; // 1 byte status + 2x 16-bit integers for X and Y

    //Check network
    packetSize = -1;

    if (packetSize == -1) {
        //Nothing from network, check serial
        uint16_t readAvailable = REM_SERIALOUT.available();
        if (readAvailable > 0) {
            //Read data from the USB port
            packetSize = REM_SERIALOUT.readBytes((char *)incomingPacketBuffer, 5);
            processIncomingCommand(incomingPacketBuffer);
        }
    }
}

FASTRUN void RemoteDisplay::processIncomingCommand(const char * incomingPacketBuffer)
{
    uint8_t remoteStatus = 0;

    // Extract touch data from packet
    remoteStatus = incomingPacketBuffer[0];
    int16_t remoteX = (incomingPacketBuffer[1] << 8) | incomingPacketBuffer[2]; // High byte first
    int16_t remoteY = (incomingPacketBuffer[3] << 8) | incomingPacketBuffer[4]; // High byte first

    switch (remoteStatus) {
        case 0: //Pressed
        case 1: //Released
            lastRemoteTouchX = remoteX;
            lastRemoteTouchY = remoteY;
            lastRemoteTouchState = remoteStatus;
            if (touchCallback) {
                touchCallback(remoteX, remoteY, lastRemoteTouchState);
            }
            break;
        case 2: //Connect ethernet
            //connectRemoteEthernet(udpStream.remoteIP());
            break;
        case 3: //Disconnect
            disconnectRemote();
            break;
        case 4: // Refresh the screen
            refreshDisplay();
            break;
        case 5: //Toggle disable local screen
            disableLocalScreen = !disableLocalScreen;
            if (commandCallback) {
                commandCallback(disableLocalScreen ? CMD_DISABLE_SCREEN : CMD_ENABLE_SCREEN);
            }
            break;
        case 6: //Connect serial
            connectRemoteSerial();
            break;
    }
}

FASTRUN void RemoteDisplay::sendHeader(uint16_t controlValue, uint32_t extraData)
{
    //Serial.printf("In RemoteDisplay::sendHeader, controlValue: 0x%04X, extraData: %ld\n", controlValue, extraData);

    size_t packetSize = 12;
    memcpy(infoBuffer, &controlValue, 2);           // Control value
    memcpy(infoBuffer + 2, &screenWidth, 2);        // Screen width
    memcpy(infoBuffer + 4, &screenHeight, 2);       // Screen height
    memcpy(infoBuffer + 6, &extraData, 4);          // 32-bit extra data

    //Send the packet via udpStream
    sendPacket(infoBuffer, packetSize);
}

FASTRUN void RemoteDisplay::sendPacket(uint8_t * buffer, uint32_t packetSize)
{

    if (connectionType == SEND_USBSERIAL) {

        //Wait for REM_SERIALOUT write to be available
        bool timeout = false;
        uint32_t start = micros();

        //Wait for REM_SERIALOUT write to be available - typically 0-3uS but if remote disconnects suddenly, will timeout here
        while (((uint32_t)REM_SERIALOUT.availableForWrite() < ((packetSize + 8)) && timeout == false)) {
            //Waiting...
            if (micros() > (start + serialTimeoutMicros)) {
                timeout = true;
            }
        };

        //Send the packet via USB, wrapped in delimiters
        if (timeout == false) {
            size_t writeBytes = REM_SERIALOUT.write(serialDelimiter);
            writeBytes += REM_SERIALOUT.write(buffer, packetSize);
            writeBytes += REM_SERIALOUT.write(serialDelimiter);
            REM_SERIALOUT.send_now();
            serialFailedCount = 0;
            //delayMicroseconds(75);
            if (writeBytes != packetSize + 8) {
                Serial.printf("Mismatch on serial.write, packetSize: %ld, wrote: %ld", (packetSize + 8), writeBytes);
            }
        } else {
            serialFailedCount += 1;
            if (serialFailedCount == 100) {
                Serial.printf("Failed to send %d packets, disconnecting remote\n", serialFailedCount);
                disconnectRemote();
            }
        }
    }
}
