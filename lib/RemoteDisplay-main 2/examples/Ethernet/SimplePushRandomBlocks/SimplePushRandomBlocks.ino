// SPDX-FileCopyrightText: (c) 2024-2025 Ian Wall <egonbeermat@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

// SimpleHTTPClient implements a simple example to push random small blocks
// to the remote display client software, and respond to touch on the remote
// client by drawing a red block at the touch site

// QNEthernet
#include <QNEthernet.h>
using namespace qindesign::network;

// Where we're at and what we're doing
enum class States {kStart, kConnect, kSendRequest};
States state = States::kStart;
bool gotIP = false;  // Watch for when we get an IP

// Block buffers for demo
uint16_t blockcolorbuf[16] = {0xBBBB, 0xBBBB, 0xBBBB, 0xBBBB, 0xBBBB, 0xBBBB, 0xBBBB, 0xBBBB, 0xBBBB, 0xBBBB, 0xBBBB, 0xBBBB, 0xBBBB, 0xBBBB, 0xBBBB, 0xBBBB};
uint16_t mousecolorbuf[16] = {0xF800, 0xF800, 0xF800, 0xF800, 0xF800, 0xF800, 0xF800, 0xF800, 0xF800, 0xF800, 0xF800, 0xF800, 0xF800, 0xF800, 0xF800, 0xF800};

// Remote Display setup
#include <RemoteDisplay.h>
RemoteDisplay remoteDisplay;

// Screen dimensions to send to remote client
uint16_t SCREENWIDTH = 800;
uint16_t SCREENHEIGHT = 480;

// UDP port to communicate with remote client
uint16_t portNumber = 2347;

// This is registered as a callback, and executed when the client first starts receiving screen
// updates, so it can start with a full image of the screen. It will also be called periodically
// when needed, eg. when 'Highlight updates' is turned off on the client, to refresh the display
void refreshDisplayCallback()
{
  //In this case, just send a bunch of black lines to refresh screen
  uint16_t buf[SCREENWIDTH];
  memset((uint8_t *)buf, 0x00, SCREENWIDTH * 2);

  for (uint16_t y = 0; y < SCREENHEIGHT - 1; y++) {
    remoteDisplay.sendData(0, y, SCREENWIDTH - 1, y, (uint8_t *)buf);
  }

  //This example is for LVGL to execute the refresh by invalidating the whole active screen area
  /*
  lv_area_t area;
  area.x1 = 0; area.y1 = 0; area.x2 = SCREENWIDTH; area.y2 = SCREENHEIGHT;
  lv_obj_invalidate_area(lv_scr_act(), &area);
  */
}

// This is registered as a callback, and executed when remoteDisplay.pollRemoteCommand() detects a touch event,
// action is either 0 (PRESSED) or 1 (RELEASED)
void remoteTouchCallback(uint16_t x, uint16_t y, uint8_t action)
{
  // In this case, jsut send a block to the screen at the touch point
  if (action == RemoteDisplay::PRESSED) {
    remoteDisplay.sendData(x - 1 , y - 1, x + 2, y + 2, (uint8_t *)mousecolorbuf);
  }
}

// Setup
void setup() {

  // Start up Serial
  Serial.begin(115200);
  while (!Serial && millis() < 4000) {
    // Wait for Serial
  }
  Serial.println("Starting...");

  // Watch for address changes
  // It will take a little time to get an IP address, so watch for it
  Ethernet.onAddressChanged([]() {
    IPAddress ip = Ethernet.localIP();
    bool hasIP = (ip != INADDR_NONE);
    if (hasIP) {
      gotIP = true;
      Serial.printf("[Ethernet] Address changed: IP = %u.%u.%u.%u",
                    ip[0], ip[1], ip[2], ip[3]);
      Serial.println();
    } else {
      Serial.println("[Ethernet] Address changed: No IP");
    }
  });

  // Initialize QNEthernet
  if (!Ethernet.begin()) {
    Serial.println("ERROR: Failed to start Ethernet");
    return;
  }
}

// Main program loop
void loop() {
  switch (state) {
    case States::kStart:
      if (gotIP == true) {
        state = States::kConnect;
      }
      break;

    case States::kConnect:
      // Ethernet connected, initialize remote display
      remoteDisplay.init(SCREENWIDTH, SCREENHEIGHT, portNumber);

      // Register callbacks
      remoteDisplay.registerRefreshCallback(refreshDisplayCallback);
      remoteDisplay.registerTouchCallback(remoteTouchCallback);

      state = States::kSendRequest;
      break;

    case States::kSendRequest:
      // Poll remoteDisplay to read commands from the client, such as connect/disconnect, and touch events
      remoteDisplay.pollRemoteCommand();

      // If the client connection was initiated from the client, sendRemoteScreen will be true, and we can send updates
      if (remoteDisplay.sendRemoteScreen == true) {
        uint16_t x = random(SCREENWIDTH - 4);
        uint16_t y = random(SCREENHEIGHT - 4);
        remoteDisplay.sendData(x, y, x + 3, y + 3, (uint8_t *)blockcolorbuf);
      }
      delay(20);
      break;
  }
}