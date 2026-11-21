# Juniper-Electrical-Design - Sensor Fusion Board

The Sensor Fusion Board is the central sensor hub for Juniper, the 2026-2027 EcoCar Urban Concept car. It collects data from the car's sensors, combines it (for example, radar targets with vehicle speed for blind-spot detection), and shares the results with the rest of the car over CAN FD. This branch currently contains the schematic, PCB and STM32CubeMX configuration for the board.

Board Requirements:

- Two 24 GHz radar modules (HLK-LD2451) for left/right blind-spot detection
- Wheel-speed inputs for vehicle speed
- IMU for acceleration and yaw
- Current/voltage monitoring of the fuel cell and battery
- Analog inputs for the hydrogen sensor and temperatures
- CAN FD capability
- USB-C for programming, debugging and data logging
- Blind-spot warning outputs (LEDs and buzzer)

MCU Chosen: STM32G474RET6 (LQFP64)

- Chosen for its 3x FDCAN controllers, plenty of UARTs for the radars and GPS, fast ADCs, and the team's existing experience with the Nucleo-G474RE.

# Repository Structure

| Folder | Contents |
|---|---|
| `Hardware/` | KiCad 10 schematic and PCB |
| `Software/` | STM32CubeMX configuration (`Software.ioc`) |

# Getting Started

Clone the repository:

```sh
git clone --recursive https://github.com/UAlberta-EcoCar/Juniper-Electrical-Design.git
```

Switch to this branch:

```sh
git checkout Sensor-Fusion
```

Open `Hardware/Sensor-Fusion-Board.kicad_pro` in KiCad 10.

# Status

- [x] MCU, USB and power schematic
- [ ] Connectors and peripherals
- [ ] PCB layout
- [ ] Firmware

Onboarding documentation here: [https://ualberta-ecocar.github.io/Documentation/](https://ualberta-ecocar.github.io/Documentation/)
