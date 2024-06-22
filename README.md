# PicoBalloon Arduino Library for LightAPRS-W-2.0 ported from AG6NS RP2040 Pico Balloon Tracker

This fork refactors the AG6NS RP2040 Pico Balloon Tracker into an Arduino library.

## How to compile & build the source code

Same as the LightAPRS-W-2.0, you need to use [Arduino IDE](https://www.arduino.cc/en/Main/Software) to compile & build this project.

### 1. Install Arduino IDE

Download and install [Arduino IDE](https://www.arduino.cc/en/Main/Software). If you have already installed Arduino, please check for updates. Its version should be at least v2.3.1 or newer.

### 2. Configure Board

- Open the **Tools > Board > Boards Manager...** menu item.
- Type "raspberry pi pico" in the search bar until you see the **Raspberry Pi Pico/RP2040** entry and click on it.
  - For more details about the **Raspberry Pi Pico/RP2040**, please refer to [Arduino-Pico documentation](https://arduino-pico.readthedocs.io/en/latest/index.html).
  - If you use Arduino IDE v1.*, please refer to [here](https://arduino-pico.readthedocs.io/en/latest/install.html#installing-via-arduino-boards-manager).
- Click **Install** .
- After installation is complete, close the **Boards Manager** window.
- Open the **Tools > Board** menu item and select **Raspberry Pi Pico/RP2040 -> Raspberry Pi Pico** from the the list.
- Open the **Tools** menu again to select values below:
  - Debug Level: "None"
  - Debug Port: "Serial"
  - C++ Exceptions: "Disabled"
  - Flash Size: "2MB (no FS)"
  - CPU Speed: "125MHz"
  - IP/Bluetooth Stack: "IPv4 Only"
  - Optimize: "Small (-Os) (standard)"
  - RTTI: "Disabled"
  - Stack Protection: "Disabled"
  - Upload Method: "Default (UF2)"
  - USB Stack: "Pico SDK"

### 3. Copy Libraries & Compile Source Code 

- First download this repository to your computer.
- There are only one Arduino projects for the sample PicoBalloon.ino project.
- You will notice some folders in the "libraries" folder. You have to copy these folders (libraries) into your Arduino libraries folder on your computer. Path to your Arduino libraries:
- **Windows** : This PC\Documents\Arduino\libraries\
- **Mac** : /Users/\<username\>/Documents/Arduino/libraries/

**IMPORTANT :** If you already have folders that have same name, you still need to overwrite them. Otherwise you get a compile error.

- Then open the PicoBallon/PicoBalloon.ino file with Arduino IDE and change your settings (Callsign, SSID, comment, etc.)
- Click **Verify** (If you get compile errors, check the steps above)

### 4. Upload

- First attach an VHF antenna (at least 50cm monopole wire) to your tracker. Radio module may be damaged when not attaching an antenna, since power has nowhere to go. 
- Connect sf-hab.org RP2040 based PicoBalloon Tracker PCB generation 1 board to your computer with a micro USB cable, then you should see a COM port under **Tools->Port** menu item. Select that port. (e.g. "/dev/cu.usbmodem141101")
- Click **Upload**
- If you see an error, you may need to put the tracker board into "Bootloader" mode before uploading:
  - Disconnect the USB cable
  - While shorting the H5 (two thru-hole next to the USB connector, maybe with pins or tweezers), connect the USB cable
  - After your PC recognized the board, release the shorting

#

Robert Graf, K6RGG

if you are insterested in, [here](https://www.instagram.com/kazuterasaki/) is my latest updates
