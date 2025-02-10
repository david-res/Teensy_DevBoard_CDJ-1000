
# _RemoteDisplay_ - send Teensy 4.x screen buffers over Ethernet or SerialUSB to display on your desktop
_Version: 0.2.0_

The _RemoteDisplay_ library, in conjunction with the supplied [Windows](https://github.com/egonbeermat/RemoteDisplay/tree/main/clientsoftware/Windows) and [MacOS](https://github.com/egonbeermat/RemoteDisplay/tree/main/clientsoftware/MacOS) client software, provides the ability to remotely display and control your Teensy 4.x screen from your desktop over Ethernet or SerialUSB.

## Introduction

Demo of this in action, streaming from a Teensy 4.x running an LVGL driven display, over Ethernet to a client running on a Mac Mini:

https://youtu.be/TxMsTKo4VVM

If your Teensy 4.x uses a screen buffer and transmits screen updates using a standard pattern of defining an area x, y, w, h, and a pointer to a 16 bit RGB565 color buffer for the pixels, you can use this library to compress and send that buffer to display on your desktop, using the included client software. Additionally, you can link touch controls on the desktop into your code on the Teensy 4.x, allowing full remote operation of your device.

Use this for operating your device easily whilst developing, or to test display code without having a physical screen to display on, or to test different resolutions on the remote display, without having a physical screen that supports that resolution.

Remote client software allows you to zoom and pan on the local view of your screen, and super-impose pixel grids and display crosshairs to pinpoint screen co-ordinates, taking some of the guess work out of screen design efforts.

Tested with [QNEthernet](https://github.com/ssilverman/QNEthernet/) _(recommended)_ and [NativeEthernet](https://github.com/vjmuzik/NativeEthernet/) libraries, and SerialUSB1. _(Note: there is a #define in remoteDisplay.h that needs to be changed to use NativeEthernet)_

## Quick Start

```c++
#include <RemoteDisplay.h>

...

RemoteDisplay remoteDisplay;

...

void setup() {
...
    //Add this to initialize remoteDisplay. udpPortNumber is optional for SerialUSB
    remoteDisplay.init(SCREENWIDTH, SCREENHEIGHT, udpPortNumber);
...
}

void loop() {
...
    //Add this to poll remoteDisplay periodically to receive connect, commands, mouse, etc.
    remoteDisplay.pollRemoteCommand();
...
}

void flushBuffer(uint16_t x1, uint16_t x2, uint16_t y1, uint16_t y2, uint16_t * buffer) {

    //Add this to your existing flushBuffer code to send the x1, y1, x2, y2 rectangle of RGB565 pixels
    if (remoteDisplay.sendRemoteScreen == true) {
        remoteDisplay.sendData(x1, y1, x2, y2, (uint8_t *)buffer);
    }

}
```

## Detailed Setup

For Ethernet connections, this guide assumes you already have Ethernet connectivity and code setup. Refer to the examples and Ethernet library documentation if you haven't already done so. Follow these **6** steps to implement:

**Step 1 (Ethernet):** For Ethernet, uncomment / comment out the appropriate lines at the top of _RemoteDisplay.h_ to enable QNEthernet or NativeEthernet

**Step 1 (SerialUSB):** By default, this requires you enable the dual serial or triple serial build options, so that SerialUSB1 can be used to send data without interfering with your existing Serial read/writes/prints:
- **Arduino IDE** - Select 'Dual Serial' or 'Triple Serial' from the 'Tools...USB Type' menu item
- **PlatformIO** - in platformio.ini, add `build_flag` `-D USB_DUAL_SERIAL`  or  `-D USB_TRIPLE_SERIAL`.
Alternatively, you can skip this, and edit the #define at the top of _RemoteDisplay.h_ to use `USBSerial`


**Step 2:** Include the library and declare an instance of RemoteDisplay:

  ```c++
  #include <RemoteDisplay.h>

  ...

RemoteDisplay remoteDisplay;
  ```

**Step 3:** Initialize the library (after an Ethernet connection is established, for Ethernet), providing the width and height of the desired display, and (optionally) a valid port number to listen for UDP connections:

  ```c++
remoteDisplay.init(SCREENWIDTH, SCREENHEIGHT, udpPortNumber);
  ```

**Step 4:** Register callbacks to be executed when the client software:

- requests full display refresh (essential but optional)
- detects a touch event (optional)
- issues a command (optional)

```c++
void  refreshDisplayCallback() {
	// Called when the client requires a full screen refresh, such as on initial connection

  /* LVGL example:

  lv_area_t area;
  area.x1 = 0; area.y1 = 0; area.x2 = SCREENWIDTH; area.y2 = SCREENHEIGHT;

  lv_obj_invalidate_area(lv_scr_act(), &area); // Invalidate this region on the active screen
  */
}

void  remoteTouchCallback(uint16_t x, uint16_t y, uint8_t action) {
	// Executed when remoteDisplay.readRemoteCommand() detects a touch event.
	// x & y represent the co-ords of the touch, action is either 0 (PRESSED) or 1 (RELEASED)
}

void  commandCallback(uint8_t command) {
  // Executed when client connects, disconnects or enables/disables attached physical screen
  // command is CommandType  - CMD_CONNECT, CMD_DISCONNECT, CMD_DISABLE_SCREEN, CMD_ENABLE_SCREEN
}

...
remoteDisplay.registerRefreshCallback(refreshDisplayCallback);
remoteDisplay.registerTouchCallback(remoteTouchCallback);
remoteDisplay.registerCommandCallback(commandCallback);
```

**Step 5:** In the main loop of your code, you will need to make a call to allow RemoteDisplay to check if it received a command from the client software (touch event, connect, disconnect, send a full display refresh):

  ```c++
void loop() {
...
	remoteDisplay.pollRemoteCommand();
...
}
```
**Step 6:** To transmit buffer updates to the remote client, if the remote client is connected, call `sendData` with parameters x1, y1, x2, y2 (the bounds of the update rectangle) and a pointer to a buffer **only** containing 16 bit RGB565 color values for the area to be updated:

```c++
if (remoteDisplay.sendRemoteScreen == true) {
    remoteDisplay.sendData(x1, y1, x2, y2, (uint8_t *)buffer);
}
```

## Additional Info

If you registered a touch callback, it will be called if the `pollRemoteCommand()` detected a touch event. Alternatively, you can reference the following in your own (polled) touch interface code, and arbitrate between local and remote touches:

`remoteDisplay.lastRemoteTouchState` - set to `RemoteDisplay::PRESSED` or `RemoteDisplay::RELEASED`
`remoteDisplay.lastRemoteTouchX` - X co-ordinate of last touch sent from remote client
`remoteDisplay.lastRemoteTouchY` - Y co-ordinate of last touch sent from remote client

The client software has an interface that provides a mechanism to control if buffer updates are also sent to the physical screen, or not, improving performance by disabling the local screen buffer flush. This interface sets `remoteDisplay.disableLocalScreen` to true or false, and you can check this before sending your buffer updates to the physical screen.

The client software typically initiates the connection to the Teensy 4.x, but you can call `remoteDisplay.connectRemote(IPAddress)` from the Teensy 4.x, supplying it's IP address, to initiate an Ethernet connection. This is useful if you wish to auto-reconnect after a reboot, for instance.

## Performance

RemoteDisplay uses escaped run-length encoding to compress the data before transmission. For performance reasons, it is beneficial to reduce the amount of data transfered, but not at the expense of a complex compression algorithm consuming even more time. RemoteDisplay was tested without compression, with RLE encoding, and with both an 8 bit and 16 bit value for run-length in escaped RLE encoding. In all data scenarios (simple UI, complex UI, images, 24fps full screen video), 8 bit escaped RLE encoding performed the best, achieving the best compression / least overhead for compression, and a significant performance improvement compared to just streaming the raw data. For simple to medium complexity UIs, compression ratios of 70-85% are typical. For MJPEG videos encoded in ffmpeg with quality 5, it achieves around 30-40% compression ratios. The remote client provides statistics on data received, compression ratios and additional information about the encoding, so you can make decisions around changing this, if you wish.

## Limitations

- Only works with 16 bit RGB565 data
- Uses UDP or USBSerial for data communication, which is not secure, nor provides guaranteed packet delivery
- Cannot solve world hunger

## Known issues

- Occasionally suffers from small number of dropped packets over SerialUSB

## Using the examples

To be continued....

## Other notes

To be continued....

## Todo

- Release Android and iOS client software

## References

* [Remote Display for LVGL Development](https://github.com/CubeCoders/LVGLRemoteServer) - the original inspiration for the Teensy-side code. This has been modified with improved compression (RLE escaped), smaller memory utilization, removal of LVGL dependency, serial port communication, and additional message packets to help the client software functions. Client software and features developed independently of this original codebase.

## License

This library is distributed under the "AGPL-3.0-or-later" license. Please contact the author if you wish to inquire about other license options.
---

Copyright (c) 2024-2025 Ian Wall
