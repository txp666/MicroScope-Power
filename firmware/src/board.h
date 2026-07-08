#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "hardware/spi.h"
#include "hardware/i2c.h"

#define ADS8866_SPI_PORT spi0
#define ADS8866_SPI_BAUD_HZ 24000000u
#define ADS8866_PIN_MISO 16u
#define ADS8866_PIN_CS 17u
#define ADS8866_PIN_SCLK 18u
#define ADS8866_PIN_MOSI 19u
#define ADS8866_PIN_SEL 20u
#define ADS8866_REF_MV 2500u

#define POWER_I2C_PORT i2c0
#define POWER_I2C_BAUD_HZ 400000u
#define POWER_PIN_LDO_EN 0u
#define POWER_PIN_LDO_PG 1u
#define POWER_PIN_I2C_SDA 8u
#define POWER_PIN_I2C_SCL 9u

#define ENCODER_PIN_B 4u
#define ENCODER_PIN_KEY 5u
#define ENCODER_PIN_A 6u

#define BUZZER_PIN 13u

#define ADC0_VOLTAGE_PIN 26u
#define ADC0_VOLTAGE_INPUT 0u
#define ADC0_VOLTAGE_REF_MV 3300u
#define ADC0_DIVIDER_TOP_OHM 100000u
#define ADC0_DIVIDER_BOTTOM_OHM 100000u

void app_board_init(void);
void app_board_init_after_usb(void);
void app_board_set_led(bool on);
uint32_t app_board_millis(void);
