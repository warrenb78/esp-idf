| Supported Targets | ESP32-S2 | ESP32-S3 |
| ----------------- | -------- | -------- |

# USB Camera Example

## Overview

This example demonstrates how to:

- Capture video from a USB camera using the `libuvc` library.
- Stream the video over WiFi by hosting a TCP server.

The example enumerates connected camera, negotiates selected resolution together with `FPS` and starts capturing video.
`frame_callback` function is then invoked after receiving each frame. User can process received frame according to his needs.

Optionally, captured video can be visualized on computer with help of `player.py` script located in this example.
After setting `Enable streaming` menuconfig option, example will create TCP server upon start, and wait until `player.py` connects to server.
Once connection is established, example streams each received frame to computer for visualization.

**Notice** that `libuvc` selects highest possible `dwMaxPayloadTransferSize` by default, so example has to manually overwrite this value to 512 bytes (maximum transfer size supported by ESP32-S2/S3).

(See the README.md file in the upper level 'examples' directory for more information about examples.)

## How to Use Example

### Hardware Required

This example requires any ESP32-S2 or ESP32-S3 with external PSRAM and exposed USB connector attached to USB camera.
*ESP module without external PSRAM will fail to initialize*

### Configure the project

Following configuration is needed for streaming video:

Open the project configuration menu (`idf.py menuconfig`).

In the `Example Connection Configuration` menu:

* Set the Wi-Fi configuration.
    * Set `WiFi SSID`.
    * Set `WiFi Password`.

In the `Example Configuration` menu:

* Set the Example configuration
    * `Enable streaming`

* Select one of UVC Protocol Mode
    * `Auto`
    * `Custom`

Optional: If you need, change the other options according to your requirements.

Additionally, `player.py` python script makes use of `opencv-python` and `numpy` packages,
not included in `idf-env` environment. Run following commands to install:
* `pip install opencv-python`
* `pip install numpy`

#### UVC Protocol Mode: Auto

When protocol mode set to Auto, the example tries to make three attempts to negotiatiate
the protocol with following parameters:

1 Attempt: 640x480, 15 FPS, MJPEG
2 Attempt: 320x240, 30 FPS, MJPEG
3 Attempt: 320x240, first available FPS, MJPEG

If all three attempts result in an error, the example displays the error message and
suggests to try another USB UVC Device.

#### UVC Protocol Mode: Custom

When protocol mode set to Custom, the example tries to negotiate protocol with
configured parameters: Attempts, Width, Heighs, FPS, Frame Coding format.
After all attemts result in an error, the example displays the error message and
suggests to try another USB UVC Device.


### Build and Flash

Build the project and flash it to the board, then run the monitor tool to view the serial output:

Run `idf.py set-target esp32s2` to set target chip.

Run `idf.py -p PORT flash monitor` to build, flash and monitor the project.

(To exit the serial monitor, type ``Ctrl-]``.)

See the Getting Started Guide for all the steps to configure and use the ESP-IDF to build projects.

## Known limitations

Having only Full Speed USB peripheral and hardware limited MPS (maximum packet size) to 512 bytes, ESP32-S2/S3 is capable of reading about 0.5 MB of data per second. When connected to Full Speed USB host, cameras normally provide resolution no larger than 640x480 pixels.
Following two supported formats are the most common (both encoded in MJPEG):
 * 320x240  30 FPS
 * 640x480  15 FPS

## Tested cameras
 * Logitech C980
 * CANYON CNE-CWC2

## Example Output

```
...
I (1186) example: Waiting for USB UVC device connection ...
I (1606) example: Device found
DEVICE CONFIGURATION (0c45:6340/	S) ---
Status: idle
VideoControl:
	bcdUVC: 0x0100
VideoStreaming(1):
	bEndpointAddress: 129
	Formats:
	MJPEGFormat(1)
		  bits per pixel: 0
		  GUID: 4d4a5047000000000000000000000000 (MJPG)
		  default frame: 1
		  aspect ratio: 0x0
		  interlace flags: 00
		  copy protect: 00
			FrameDescriptor(1)
			  capabilities: 00
			  size: 640x480
			  bit rate: 24576000-147456000
			  max frame size: 614400
			  default interval: 1/30
			  interval[0]: 1/30
			  interval[1]: 1/25
			  interval[2]: 1/20
			  interval[3]: 1/15
			  interval[4]: 1/10
			  interval[5]: 1/5
			FrameDescriptor(2)
			  capabilities: 00
			  size: 352x288
			  bit rate: 8110080-48660480
			  max frame size: 202752
			  default interval: 1/30
			  interval[0]: 1/30
			  interval[1]: 1/25
			  interval[2]: 1/20
			  interval[3]: 1/15
			  interval[4]: 1/10
			  interval[5]: 1/5
			FrameDescriptor(3)
			  capabilities: 00
			  size: 320x240
			  bit rate: 6144000-36864000
			  max frame size: 153600
			  default interval: 1/30
			  interval[0]: 1/30
			  interval[1]: 1/25
			  interval[2]: 1/20
			  interval[3]: 1/15
			  interval[4]: 1/10
			  interval[5]: 1/5
			FrameDescriptor(4)
			  capabilities: 00
			  size: 176x144
			  bit rate: 2027520-12165120
			  max frame size: 50688
			  default interval: 1/30
			  interval[0]: 1/30
			  interval[1]: 1/25
			  interval[2]: 1/20
			  interval[3]: 1/15
			  interval[4]: 1/10
			  interval[5]: 1/5
			FrameDescriptor(5)
			  capabilities: 00
			  size: 160x120
			  bit rate: 1536000-9216000
			  max frame size: 38400
			  default interval: 1/30
			  interval[0]: 1/30
			  interval[1]: 1/25
			  interval[2]: 1/20
			  interval[3]: 1/15
			  interval[4]: 1/10
			  interval[5]: 1/5
			StillFrameDescriptor
			  bEndPointAddress: 00
			  wWidth(1) = 640
			  wHeight(1) = 480
			  wWidth(2) = 352
			  wHeight(2) = 288
			  wWidth(3) = 320
			  wHeight(3) = 240
			  wWidth(4) = 176
			  wHeight(4) = 144
			  wWidth(5) = 160
			  wHeight(5) = 120
END DEVICE CONFIGURATION
I (1796) example: Negotiate streaming profile 640x480, fps 15 ...
I (1816) example: Negotiation complete.
bmHint: 0001
bFormatIndex: 1
bFrameIndex: 1
dwFrameInterval: 666666
wKeyFrameRate: 0
wPFrameRate: 0
wCompQuality: 0
wCompWindowSize: 0
wDelay: 23469
dwMaxVideoFrameSize: 614400
dwMaxPayloadTransferSize: 512
bInterfaceNumber: 1
I (1836) example: Streaming...
I (4016) example: fps: 10, bytes per second: 69920

...

I (44916) example: fps: 9, bytes per second: 62928
E (45626) USBH: Device 1 gone
I (45636) example: Done streaming.
```
