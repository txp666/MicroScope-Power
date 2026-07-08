#include "power_output.h"

#include "board.h"

#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "pico/stdlib.h"

#define DAC60501_ADDR_FIRST 0x48u
#define DAC60501_ADDR_LAST 0x4Bu
#define DAC60501_REG_SYNC 0x02u
#define DAC60501_REG_CONFIG 0x03u
#define DAC60501_REG_GAIN 0x04u
#define DAC60501_REG_DAC_DATA 0x08u
#define DAC60501_RESOLUTION_BITS 12u
#define DAC60501_DEFAULT_FULL_SCALE_MV 2500u
#define DAC60501_DAC_DATA_SHIFT (16u - DAC60501_RESOLUTION_BITS)
#define DAC60501_I2C_TIMEOUT_US 5000u
#define DAC60501_SYNC_ASYNC 0x0000u
#define DAC60501_CONFIG_ACTIVE 0x0000u
#define DAC60501_GAIN_REF_DIV2_GAIN2 0x0101u

static bool g_power_output_ready = false;
static uint8_t g_dac_address = 0u;
static uint16_t g_vdac_mv = POWER_OUTPUT_VDAC_MIN_MV;

static int dac60501_write_reg(uint8_t address, uint8_t reg, uint16_t value) {
  uint8_t data[3] = {
      reg,
      (uint8_t)(value >> 8),
      (uint8_t)(value & 0xffu),
  };

  int written =
      i2c_write_timeout_us(POWER_I2C_PORT, address, data, sizeof(data), false,
                           DAC60501_I2C_TIMEOUT_US);
  return written;
}

static bool dac60501_write_reg_ok(uint8_t address, uint8_t reg,
                                  uint16_t value) {
  return dac60501_write_reg(address, reg, value) == 3;
}

static uint16_t dac60501_mv_to_code12(uint16_t mv) {
  uint32_t clamped_mv = mv;
  if (clamped_mv > DAC60501_DEFAULT_FULL_SCALE_MV) {
    clamped_mv = DAC60501_DEFAULT_FULL_SCALE_MV;
  }

  uint32_t max_code = (1u << DAC60501_RESOLUTION_BITS) - 1u;
  uint32_t code =
      (clamped_mv * max_code + (DAC60501_DEFAULT_FULL_SCALE_MV / 2u)) /
      DAC60501_DEFAULT_FULL_SCALE_MV;
  return (uint16_t)code;
}

static uint16_t dac60501_mv_to_left_aligned_code(uint16_t mv) {
  return (uint16_t)(dac60501_mv_to_code12(mv) << DAC60501_DAC_DATA_SHIFT);
}

static bool dac60501_write_mv(uint8_t address, uint16_t mv) {
  uint16_t dac_data = dac60501_mv_to_left_aligned_code(mv);
  return dac60501_write_reg_ok(address, DAC60501_REG_DAC_DATA, dac_data);
}

static bool dac60501_configure_and_write(uint8_t address, uint16_t mv) {
  if (!dac60501_write_reg_ok(address, DAC60501_REG_SYNC, DAC60501_SYNC_ASYNC)) {
    return false;
  }

  if (!dac60501_write_reg_ok(address, DAC60501_REG_GAIN,
                             DAC60501_GAIN_REF_DIV2_GAIN2)) {
    return false;
  }

  if (!dac60501_write_reg_ok(address, DAC60501_REG_CONFIG,
                             DAC60501_CONFIG_ACTIVE)) {
    return false;
  }

  sleep_ms(1);
  return dac60501_write_mv(address, mv);
}

static bool dac60501_find_configure_and_write(uint16_t mv,
                                              uint8_t *selected_address) {
  for (uint8_t address = DAC60501_ADDR_FIRST; address <= DAC60501_ADDR_LAST;
       ++address) {
    if (dac60501_configure_and_write(address, mv)) {
      *selected_address = address;
      return true;
    }
  }

  return false;
}

bool power_output_set_vdac_mv(uint16_t mv) {
  if (mv > POWER_OUTPUT_VDAC_MAX_MV) {
    mv = POWER_OUTPUT_VDAC_MAX_MV;
  }

  if (g_dac_address != 0u && dac60501_write_mv(g_dac_address, mv)) {
    g_vdac_mv = mv;
    g_power_output_ready = true;
    return true;
  }

  uint8_t selected_address = 0u;
  if (dac60501_find_configure_and_write(mv, &selected_address)) {
    g_dac_address = selected_address;
    g_vdac_mv = mv;
    g_power_output_ready = true;
    return true;
  }

  g_power_output_ready = false;
  return false;
}

void power_output_init(void) {
  gpio_init(POWER_PIN_LDO_EN);
  gpio_set_dir(POWER_PIN_LDO_EN, GPIO_OUT);
  gpio_put(POWER_PIN_LDO_EN, 0);

  gpio_init(POWER_PIN_LDO_PG);
  gpio_set_dir(POWER_PIN_LDO_PG, GPIO_IN);

  i2c_init(POWER_I2C_PORT, POWER_I2C_BAUD_HZ);
  gpio_set_function(POWER_PIN_I2C_SDA, GPIO_FUNC_I2C);
  gpio_set_function(POWER_PIN_I2C_SCL, GPIO_FUNC_I2C);
  gpio_pull_up(POWER_PIN_I2C_SDA);
  gpio_pull_up(POWER_PIN_I2C_SCL);

  sleep_ms(1);

  uint8_t selected_address = 0u;
  if (dac60501_find_configure_and_write(POWER_OUTPUT_VDAC_MIN_MV,
                                        &selected_address)) {
    g_dac_address = selected_address;
    g_vdac_mv = POWER_OUTPUT_VDAC_MIN_MV;
    g_power_output_ready = true;
    sleep_ms(1);
    gpio_put(POWER_PIN_LDO_EN, 1);
  } else {
    g_power_output_ready = false;
    gpio_put(POWER_PIN_LDO_EN, 0);
  }
}

bool power_output_ready(void) { return g_power_output_ready; }

bool power_output_power_good(void) { return gpio_get(POWER_PIN_LDO_PG); }

uint8_t power_output_dac_address(void) { return g_dac_address; }

uint16_t power_output_vdac_mv(void) { return g_vdac_mv; }
