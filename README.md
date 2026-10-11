# Juniper-Electrical-Design - FOC Board

The FOC Board is planned to be on the car's steering wheel.
This branch currently contains the schematic and PCB files for the board.
For firmware see [https://github.com/UAlberta-EcoCar/Sally-Dashboard](https://github.com/UAlberta-EcoCar/Sally-Dashboard)

Board Requirements:
* A LCD screen with touch screen capabilities
* An IMU to track steering wheel position
* &gt; 3 Inputs for push buttons
* CAN FD capability 
* Addressable RGB LEDs
* Powered by a 7V and 12V power supply
* Overcurrent protection

MCU Chosen: STM32G491RET6

Link: https://www.digikey.ca/en/products/detail/stmicroelectronics/STM32G491RET6/13592591
* The STM32F4 series was considered, 
 but an MCU with the right stock options, clock speed, protocol support, and pin count was not found.


# Getting Started

Clone the repository:

```sh
git clone --recursive https://github.com/UAlberta-EcoCar/Juniper-Electrical-Design.git
```

Switch to the branch:

```sh
git checkout FOC-Board
```

Onboarding documentation here: [https://ualberta-ecocar.github.io/Documentation/](https://ualberta-ecocar.github.io/Documentation/)
