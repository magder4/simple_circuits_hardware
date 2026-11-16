# Simple Circuits Hardware

## Build the daughterboard firmware

1. Install the **Raspberry Pi Pico** extension by Raspberry Pi in VS Code.
2. Open `Daughterboard_fw/daughterboard_test` as the VS Code project folder.
3. If the Pico tools are not installed, use **Import Project** in the Pico sidebar, select this folder, and let the extension install the SDK and toolchain. This project is configured for the original **Pico** and SDK **2.3.1**.
4. In the Pico sidebar, run **Configure CMake**, then **Compile Project**. Wait for the build to succeed.
5. The generated firmware is `Daughterboard_fw/daughterboard_test/build/daughterboard_test.uf2`, relative to the repository root.

## Flash the firmware over USB

This writes the program into the Pico's persistent flash memory. No debug probe is required.

1. Disconnect the Pico from USB.
2. Hold the Pico's **BOOTSEL** button while reconnecting it with a USB data cable.
3. Release BOOTSEL when the **RPI-RP2** drive appears in Finder.
4. Copy the newly compiled `daughterboard_test.uf2` from the project's `build` folder onto **RPI-RP2**. 
5. The drive automatically disappears as the Pico restarts and runs the firmware. 
6. To verify persistence, unplug the Pico and reconnect it **without holding BOOTSEL** or starting a debugger. The saved program should start automatically whenever the board receives power.

**RPI-RP2 is a virtual programming drive.** Previously copied firmware files will not appear when you enter BOOTSEL mode again. The firmware remains stored internally until it is replaced or erased. Rebuild and repeat these steps whenever you change the firmware.

## OLED troubleshooting

During normal operation, the onboard LED changes state about once per second. If OLED initialization or updating fails, it changes state every quarter second. This indicates a display communication error, rather than lost firmware.

The firmware expects the OLED at I2C address `0x3C`, with **SDA on GPIO 18** (physical pin 24) and **SCL on GPIO 19** (physical pin 25), plus power and a shared ground.

The default build has USB serial output disabled. Reading errors through the Pico's USB port requires enabling USB stdio in CMake, rebuilding, and flashing that updated firmware.
