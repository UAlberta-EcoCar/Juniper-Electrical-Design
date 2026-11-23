# Juniper-Electrical-Design - Wiper Board

The Wiper Board drives the windshield wiper on Juniper, the 2026-2027 EcoCar Urban Concept car. Shell Eco-marathon rules (2027 Chapter I, Art. 52) require Urban Concept cars to run in light rain, with an electric wiper like a production car's, worked by its own switch within easy reach of the driver. The board runs a 12 V self-parking wiper motor, reads the driver's dash switch and the park sensors, and reports its status over CAN FD. This branch contains the Rev1 board as built, the Rev2 schematic in progress, and the STM32 firmware.

Board Requirements:

- Drive a 12 V wiper motor (about 30 W) with a hardware current limit (set to about 3.5 A)
- Dedicated driver wiper switch input (off / slow / fast) on a harness connector
- Hall-sensor inputs for wiper park and end-of-sweep detection
- Reverse-polarity and transient protection on the 12 V input
- CAN FD for commands and status
- RGB status LEDs (WS2812B) and an optional OLED debug display
- USB-C and SWD for programming and debugging
- Low idle power, since all electrical energy counts on the joulemeter

MCU Chosen: STM32G491KEU6 (UFQFPN32)

- Chosen for its FDCAN controller, timers and ADCs in a small package, and the team's experience with the STM32G4 family.

Motor Driver: STMicroelectronics STSPIN948 (VFQFPN48)

- Dual full-bridge, 4.5 A rms per bridge, up to 58 V, with built-in current-sense amplifiers, an adjustable current limiter and overcurrent, thermal and undervoltage protection. The design follows ST's [EVSPIN948](https://www.st.com/en/evaluation-tools/evspin948.html) evaluation board. Bridge A drives the wiper motor; bridge B is a spare.

# Repository Structure

| Folder | Contents |
|---|---|
| `Hardware/Rev1/` | Rev1 board as built: KiCad 9 schematic, PCB, BOM and production files |
| `Hardware/Rev2/` | Rev2 schematic (work in progress) |
| `firmware/` | STM32CubeIDE project and CubeMX configuration (`Wiper-Board-Firmwire.ioc`); Rev2 configuration in `v2Firmwire/` |

# Getting Started

Clone the repository:

```sh
git clone --recursive https://github.com/UAlberta-EcoCar/Juniper-Electrical-Design.git
```

Switch to this branch:

```sh
git checkout Wiper-Board
```

Open `Hardware/Rev2/defogger_and_wiper.kicad_pro` in KiCad 9. The schematic uses a 25 mil grid; set the connection grid and the wire grid override to 25 mil.

Open `firmware/` as an existing project in STM32CubeIDE to build and flash the firmware.

# Rev2 Pin Map

| Pin | Function |
|---|---|
| PA6 / PA7 | PWM1A / PWM1B to the driver (TIM16_CH1 / TIM17_CH1) |
| PA8 / PA9 | PHA / PHB (direction) |
| PB4 / PB3 | EN/nFAULT A / B (open-drain, 39 k pull-up) |
| PA10 | nSTBY (100 k pull-down: driver sleeps until firmware wakes it) |
| PA3 / PB0 | Motor current A / B (0.5 V/A) |
| PA4 / PA5 | Wiper switch inputs BTN1 / BTN2 (dash connector J9 and on-board buttons) |
| PA0 / PA2 | Hall park sensors (J7) |
| PB5 / PB6 | FDCAN2 RX / TX |
| PA15 / PB7 | I2C1 SCL / SDA (OLED) |
| PB8 | WS2812B data (TIM8_CH2, via 74AHCT1G125; 10 k pull-down keeps BOOT0 low) |
| PA11 / PA12 | USB |
| PA13 / PA14 | SWD |
| PF0 | 8 MHz oscillator input (HSE bypass) |

# Bring-up Notes

- Driver mode switches MODE1 / MODE2 / MODE3 = LOW / HIGH / LOW (dual independent full-bridge). Power-cycle after any change.
- Rev1 only: SCL sits on PB8 (BOOT0), so the board boots into the ST bootloader after a power cycle. Program option bytes nSWBOOT0 = 0 and nBOOT0 = 1, or run it with the debugger attached. Fixed in Rev2.
- Rev1 only: the REF dividers (5.6 k / 1 k) limit the motor to about 1 A. Rev2 uses 910 ohm (about 3.5 A).

# Status

- [x] Rev1 board built and brought up
- [ ] Rev2 schematic
- [ ] Rev2 PCB layout
- [ ] Rev2 firmware

Onboarding documentation here: [https://ualberta-ecocar.github.io/Documentation/](https://ualberta-ecocar.github.io/Documentation/)
