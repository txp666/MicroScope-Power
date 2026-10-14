#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct { uint32_t dr; } spi_hw_t;
typedef struct { spi_hw_t hw; } spi_inst_t;
extern spi_inst_t *spi0;
#define SPI_CPOL_0 0
#define SPI_CPHA_0 0
#define SPI_MSB_FIRST 0
bool spi_is_readable(spi_inst_t *spi);
bool spi_is_writable(spi_inst_t *spi);
spi_hw_t *spi_get_hw(spi_inst_t *spi);
uint32_t spi_init(spi_inst_t *spi, uint32_t baud);
void spi_set_format(spi_inst_t *spi, unsigned bits, unsigned cpol, unsigned cpha, unsigned order);
uint32_t spi_set_baudrate(spi_inst_t *spi, uint32_t baud);
