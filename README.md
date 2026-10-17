# Marauder C5 v2 — ESP32-C5 Wireless Analysis Toolkit
#DFROBOT FİREBEETLE ESP32-C5

A touchscreen-based wireless analysis and hardware monitoring project built around the **ESP32-C5**, **NRF24L01**, **ILI9341 TFT display**, and **XPT2046 touchscreen controller**.

The firmware provides a graphical interface for Wi-Fi network discovery, Bluetooth Low Energy (BLE) scanning, NRF24L01 signal activity monitoring, wireless channel analysis, system diagnostics, and event logging.

> **Project status:** Experimental firmware. Hardware compatibility and individual features must be verified on the target ESP32-C5 board.

## Table of Contents

- [Features](#features)
- [Hardware Requirements](#hardware-requirements)
- [Wiring and Pin Configuration](#wiring-and-pin-configuration)
- [Required Software](#required-software)
- [Libraries and Dependencies](#libraries-and-dependencies)
- [Installation](#installation)
- [Configuration](#configuration)
- [Menu Overview](#menu-overview)
- [How It Works](#how-it-works)
- [Troubleshooting](#troubleshooting)
- [Limitations](#limitations)
- [Legal and Ethical Use](#legal-and-ethical-use)
- [Future Improvements](#future-improvements)
- [License](#license)

## Features

### Wi-Fi Analysis

- **Wi-Fi Scan:** Discover nearby visible wireless networks.
- **Wi-Fi Details:** Display SSIDs, received signal strength (RSSI), channels, and basic security status.
- **Channel Analyzer:** Visualize the number of discovered access points on supported 2.4 GHz channels.
- **Live Network Analysis:** Periodically refresh nearby network information.
- **Signal Graph:** Plot the signal strength of the strongest visible Wi-Fi network over time.

### Bluetooth Low Energy

- **BLE Scan:** Discover advertising BLE devices.
- **BLE Device Details:** Display device names, Bluetooth addresses, and RSSI values.
- **BLE Advertisement Demonstration:** Includes experimental advertising code for predefined beacon data. Transmission compatibility depends on the ESP32-C5 Bluetooth stack and installed library versions.

### NRF24L01 Monitoring

- **NRF Spectrum:** Display an approximate activity graph across NRF24L01 channel settings.
- **NRF Counter:** Count received packets on a configured NRF24L01 channel.
- **Hardware Status:** Check whether the NRF24L01 module initializes successfully.

The NRF24L01 receiver operates in the 2.4 GHz band. Its readings are not a calibrated spectrum measurement and cannot identify every transmitting device.

### System Monitoring and Diagnostics

- CPU frequency
- Free heap memory
- Minimum free heap memory
- Flash size
- System uptime
- Wi-Fi operating status
- BLE and NRF24L01 initialization status
- Basic hardware error checks
- A rolling log buffer for diagnostic events

### Touchscreen Interface

The graphical interface uses an ILI9341 display and an XPT2046 touchscreen controller.

- 18 main-menu buttons
- Color-coded interface elements
- Touchscreen input with debounce handling
- Back navigation
- Adjustable display backlight
- Configurable live-refresh interval
- Optional automatic refresh for live network analysis

## Hardware Requirements

| Component | Purpose |
|---|---|
| ESP32-C5 development board | Main microcontroller |
| NRF24L01 module | 2.4 GHz radio activity monitoring |
| ILI9341 TFT display | Graphical user interface |
| XPT2046 touchscreen controller | Touch input |
| SPI connections | Communication with peripherals |
| USB cable | Programming and serial debugging |
| Jumper wires | Hardware interconnection |
| Suitable power supply | Stable operation of the modules |

An NRF24L01 module with an external antenna may be used if the module's voltage and wiring requirements are respected.

**Important:** ESP32-C5 board pin availability varies. Verify that GPIO pins 2–10 and GPIO pins 23–25 are accessible on your specific development board before wiring the circuit.

## Wiring and Pin Configuration

The following pin assignments are defined in the current firmware.

### Shared SPI Bus

| Signal | ESP32-C5 GPIO |
|---|---:|
| SCK | GPIO 23 |
| MISO | GPIO 25 |
| MOSI | GPIO 24 |

### ILI9341 TFT Display

| Display Signal | ESP32-C5 GPIO |
|---|---:|
| CS | GPIO 4 |
| DC | GPIO 2 |
| RST | GPIO 3 |
| LED / Backlight | GPIO 5 |
| SCK | GPIO 23 |
| MISO | GPIO 25 |
| MOSI | GPIO 24 |

### XPT2046 Touchscreen

| Touch Signal | ESP32-C5 GPIO |
|---|---:|
| CS | GPIO 6 |
| SCK | GPIO 23 |
| MISO | GPIO 25 |
| MOSI | GPIO 24 |

The touchscreen and display share the SPI bus but use separate chip-select pins.

### NRF24L01

| NRF24L01 Signal | ESP32-C5 GPIO |
|---|---:|
| CE | GPIO 9 |
| CSN | GPIO 10 |
| SCK | GPIO 23 |
| MISO | GPIO 25 |
| MOSI | GPIO 24 |

The NRF24L01 shares the SPI bus with the display and touchscreen.

### Electrical Considerations

- Check the voltage requirements of every module before connecting it.
- The NRF24L01 requires a **3.3 V supply**. Do not connect its VCC pin directly to 5 V.
- Use a stable supply with sufficient current capacity for the NRF24L01 module you have.
- A local decoupling capacitor near the NRF24L01 power pins can help reduce supply-related communication problems.
- Connect all module grounds to a common ground.
- Do not assume that a display's backlight pin can safely supply the current required by every TFT module.

The GPIO assignments above describe the firmware configuration, not a guarantee that every ESP32-C5 board exposes those pins.

## Required Software

### 1. Arduino IDE

Install the Arduino IDE:

https://www.arduino.cc/en/software

Arduino IDE 2.x is recommended.

### 2. ESP32 Board Support

Install the Espressif ESP32 platform through the Arduino IDE Boards Manager.

Official Arduino documentation:

https://docs.espressif.com/projects/arduino-esp32/en/latest/installing.html

Select an ESP32-C5-compatible board definition if your installed platform version provides one.

**Compatibility note:** ESP32-C5 support depends on the Espressif Arduino core version and the selected board package. Confirm that your version supports the required Wi-Fi, BLE, SPI, and low-level Wi-Fi APIs before compiling.

## Libraries and Dependencies

Install the following libraries and verify their compatibility with your ESP32-C5 board package.

| Library | Purpose | Source |
|---|---|---|
| Arduino Core for ESP32 | GPIO, Wi-Fi, serial, system APIs | [Espressif Arduino Core](https://github.com/espressif/arduino-esp32) |
| Adafruit GFX Library | Graphics drawing primitives | [GitHub](https://github.com/adafruit/Adafruit-GFX-Library) |
| Adafruit ILI9341 | TFT display driver | [GitHub](https://github.com/adafruit/Adafruit_ILI9341) |
| XPT2046_Touchscreen | Resistive touchscreen input | [GitHub](https://github.com/PaulStoffregen/XPT2046_Touchscreen) |
| RF24 | NRF24L01 radio interface | [GitHub](https://github.com/nRF24/RF24) |
| NimBLE-Arduino | Bluetooth Low Energy scanning and advertising APIs | [GitHub](https://github.com/h2zero/NimBLE-Arduino) |

### Built-in ESP32 and FreeRTOS Headers

The sketch also includes these headers:

| Header | Purpose |
|---|---|
| `Arduino.h` | Core Arduino functionality |
| `SPI.h` | SPI communication |
| `WiFi.h` | Wi-Fi network scanning and status |
| `esp_wifi.h` | Lower-level ESP Wi-Fi APIs |
| `esp_random.h` | Hardware-backed random number generation API |
| `freertos/FreeRTOS.h` | FreeRTOS definitions |
| `freertos/semphr.h` | Mutex and semaphore support |

These headers are provided by the selected ESP32 Arduino platform rather than being separate Arduino IDE libraries.

### Installation Through Library Manager

In Arduino IDE:

1. Open **Sketch → Include Library → Manage Libraries**.
2. Search for `Adafruit GFX Library` and install it.
3. Search for `Adafruit ILI9341` and install it.
4. Search for `XPT2046_Touchscreen` and install the compatible library.
5. Search for `RF24` and install the library by the nRF24 community.
6. Search for `NimBLE-Arduino` and install a release compatible with your ESP32 core.
7. Install the ESP32 board package through Boards Manager.

Some library releases may introduce API changes. If compilation fails in the BLE or Wi-Fi portions, verify the installed library and ESP32 core versions before changing the code.

## Installation

### Step 1 — Clone the Repository

Replace `YOUR_USERNAME` with your GitHub username and `YOUR_REPOSITORY` with your repository name.

```bash
git clone https://github.com/YOUR_USERNAME/YOUR_REPOSITORY.git
cd YOUR_REPOSITORY
```

Alternatively, download the repository as a ZIP file and extract it.

### Step 2 — Open the Firmware

Open the `.ino` file in Arduino IDE.

If necessary, rename the file to match the project directory name, following Arduino sketch naming conventions.

### Step 3 — Connect the Hardware

Connect the ESP32-C5, TFT display, touchscreen, and NRF24L01 using the pin tables above.

Check the power supply and SPI wiring before turning on the board.

### Step 4 — Select the Board

In Arduino IDE:

1. Open **Tools → Board**.
2. Select your compatible ESP32-C5 board.
3. Select the appropriate USB or serial port.
4. Check the board's supported flash and upload settings.

The correct settings depend on the specific development board.

### Step 5 — Compile and Upload

Click **Verify** to compile the sketch.

After resolving any compilation errors, click **Upload** to flash the firmware.

Open **Tools → Serial Monitor** and select **115200 baud** to view startup diagnostics.

### Step 6 — Check Initialization

At startup, the firmware attempts to initialize:

- The TFT display
- The touchscreen controller
- The NRF24L01 radio
- Wi-Fi in station mode
- The NimBLE subsystem
- The diagnostic log mutex

A failed NRF24L01 or BLE initialization is reported through the serial output or the on-screen diagnostic menus.

## Configuration

The most important configuration values are defined near the beginning of the sketch.

### SPI Pins

```cpp
#define SPI_SCK   23
#define SPI_MISO  25
#define SPI_MOSI  24
```

Change these values only if your board's wiring requires different GPIO assignments.

### Display and Touch Pins

```cpp
#define TFT_CS     4
#define TFT_DC     2
#define TFT_RST    3
#define TFT_LED    5
#define TOUCH_CS   6
```

The display rotation is configured in `setup()` using:

```cpp
tft.setRotation(1);
ts.setRotation(1);
```

If the touchscreen coordinates do not align with the display, the touch calibration values and inversion settings may need adjustment.

### NRF24L01 Pins

```cpp
#define NRF_CE     9
#define NRF_CSN    10
```

These must match the physical connections.

### Touchscreen Calibration

```cpp
#define TS_MINX 200
#define TS_MAXX 3800
#define TS_MINY 200
#define TS_MAXY 3800

#define TS_INVERT_X true
#define TS_INVERT_Y true
#define TOUCH_DEBOUNCE_MS 25
```

The calibration values are initial assumptions and may not match every touchscreen panel. Calibrate the actual hardware for reliable button selection.

### Screen Dimensions

```cpp
#define SCREEN_W 320
#define SCREEN_H 240
```

These dimensions correspond to the landscape layout used by the current interface.

### Live Refresh Interval

The default interval is defined as:

```cpp
static uint32_t liveRefreshMs = 5000;
```

The settings menu cycles through 3, 5, and 10 seconds.

## Menu Overview

The main screen contains 18 buttons.

| Menu Item | Description |
|---|---|
| WiFi Tara | Scan nearby Wi-Fi networks |
| BLE Tara | Scan nearby BLE advertisers |
| NRF Spec | Display approximate NRF channel activity |
| NRF Sayac | Count received NRF24L01 packets |
| WiFi Detay | Display Wi-Fi signal and security information |
| Donanim | Show hardware and peripheral status |
| Kanal Anal | Graph discovered Wi-Fi access points by channel |
| Hakkinda | Display project information |
| DOS/Log | Display diagnostic event logs |
| WiFi Beacon | Experimental Wi-Fi beacon transmission code |
| BLE Beacon | Experimental BLE advertising code |
| DeauthDet | Monitor selected Wi-Fi management frames |
| Ayarlar | Configure backlight and refresh behavior |
| Sistem Izle | Display live system resource information |
| Canli Ag | Refresh Wi-Fi network information periodically |
| Sinyal Grafik | Plot the strongest visible Wi-Fi signal |
| BLE Detay | Show BLE names, addresses, and signal levels |
| Hata Kontrol | Run basic initialization and memory checks |

The interface labels are primarily in Turkish because that is how the current firmware is written.

## How It Works

### Wi-Fi Scanning

The firmware switches the Wi-Fi interface to station mode and invokes the ESP32 Wi-Fi scanning API.

Results are displayed with available SSID, RSSI, channel, and basic encryption information.

These scans discover visible nearby networks; they do not provide access to network traffic protected by encryption.

### BLE Scanning

The firmware initializes NimBLE and registers a scanning callback.

Discovered BLE advertisements can be represented by their advertised name, address, and received signal strength.

Some devices do not advertise a name, and some use private or rotating addresses. Therefore, the scan results may not reveal a device's real identity.

### NRF24L01 Monitoring

The spectrum-style screen uses the radio's channel selection and RPD functionality to estimate activity.

This is an approximate activity visualization, not a general-purpose RF spectrum analyzer.

The packet counter listens on channel 76 at a configured data rate. It counts packets received by the module under that configuration; it is not a universal counter for all 2.4 GHz transmissions.

### Event Logging

A fixed-size circular buffer stores recent diagnostic messages.

A FreeRTOS mutex protects log access when it is available. The buffer is held in RAM and is not persistent across reboots.

### System Monitoring

The system monitor periodically displays free heap, minimum free heap, CPU frequency, uptime, and peripheral initialization status.

These checks help identify basic initialization failures but do not constitute a complete hardware or security audit.

## Troubleshooting

### The Display Remains Blank

- Confirm that the selected board exposes the configured GPIO pins.
- Check the TFT power, ground, CS, DC, RST, and SPI connections.
- Verify that the display uses an ILI9341 controller.
- Confirm that the backlight circuit is compatible with the configured LED pin.
- Check the selected ESP32 board package and serial output.

### Touchscreen Buttons Are Misaligned

- Verify the XPT2046 wiring and chip-select pin.
- Adjust the touchscreen calibration values.
- Test the X/Y inversion settings.
- Check that display and touchscreen rotations are consistent.

### NRF24L01 Initialization Fails

- Check CE and CSN connections.
- Verify the shared SPI wiring.
- Confirm that the module receives a stable 3.3 V supply.
- Check for conflicting chip-select signals.
- Test with a compatible RF24 library version.

### BLE Scanning Does Not Work

- Confirm that the installed ESP32 core supports BLE on the selected ESP32-C5 board.
- Check the NimBLE-Arduino version.
- Review serial output for initialization failures.
- Verify that nearby devices are actually advertising BLE packets.

### Compilation Errors

- Confirm that all required libraries are installed.
- Check for incompatible library versions or duplicate installations.
- Verify the selected board definition.
- Review API changes in the ESP32 Wi-Fi and NimBLE libraries.
- Keep the original compiler error output when diagnosing compatibility issues.

## Limitations

- This project is experimental and may require changes for different hardware revisions.
- Pin assignments are board-specific.
- Touchscreen calibration values are not universal.
- NRF24L01 activity measurements are approximate.
- Wi-Fi scans show only networks visible to the device at the time of scanning.
- BLE results depend on advertisement visibility and scan configuration.
- The error-checking menu performs basic status checks rather than comprehensive fault detection.
- Some experimental transmission functions rely on low-level APIs that may be restricted or behave differently across ESP32 core releases.
- The current firmware does not provide a persistent database, network dashboard, or remote management interface.
- The menu labeled `DOS/Log` displays diagnostic logs; it does not implement a general-purpose denial-of-service monitoring platform.

## Legal and Ethical Use

This project is intended for **education, hardware experimentation, wireless protocol learning, and authorized testing**.

Only test wireless systems and devices that you own or have explicit permission to assess.

Wireless transmission and management-frame operations may interfere with other devices or violate local regulations. Keep experimental transmission features disabled unless their operation is lawful, necessary, and authorized.

Do not use the project to disrupt networks, impersonate trusted devices, interfere with communications, or target other people's equipment.

Follow applicable radio-frequency regulations and respect the privacy and security of nearby users.

## Future Improvements

Potential development directions include:

- More reliable touchscreen calibration
- Improved peripheral initialization and error reporting
- Configurable scan duration and refresh intervals
- Persistent diagnostic logs
- Better separation between display, radio, and monitoring modules
- Improved ESP32-C5 core and library compatibility
- Automated hardware testing
- English/Turkish language selection
- More detailed passive signal visualization
