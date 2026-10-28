# Hardware configuration

## Board stack

- MCU: Waveshare RP2350-PiZero (RP2350B).
- HAT: Waveshare Environment Sensor HAT.
- Physical connection: directly stacked Raspberry Pi-style 2 x 20 header.
- Power: start with USB power to the RP2350-PiZero. Do not assume its 5V/VBUS pin is powered from the battery path.

| Signal | Header physical pin | RP2350 pin |
| --- | ---: | --- |
| I2C SDA | 3 | GPIO2 (I2C1 SDA) |
| I2C SCL | 5 | GPIO3 (I2C1 SCL) |
| 5V | 2 / 4 | VBUS |
| GND | 6 | GND |

## Expected I2C devices

| Device | Address (7-bit) | Function |
| --- | --- | --- |
| TSL25911 | 0x29 | Ambient light / IR |
| BME280 | 0x76 | Temperature, humidity, pressure |
| LTR390 | 0x53 | UV |
| SGP40 | 0x59 | VOC raw signal |
| ICM20948 | 0x68 | 9-axis IMU |

Some Waveshare text lists SGP40 as 0x60; the device's datasheet and I2C examples use 0x59.

## Safety and caveats

1. Verify pin 1 orientation before connecting power.
2. Check the HAT's actual assembled header and component clearance.
3. Do not connect a separate 5V source in parallel with USB unless the supply path has been reviewed.
4. A board-mounted temperature sensor may read higher than ambient due to heat from the MCU/regulators.
5. An I2C ACK alone does not confirm full sensor functionality.

Sources:
- https://www.waveshare.com/wiki/RP2350-PiZero
- https://www.waveshare.com/wiki/Environment_Sensor_HAT
