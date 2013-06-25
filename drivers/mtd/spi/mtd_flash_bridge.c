/*
 * Module mimicking an SPI Flash subsystem, using a "bridge" peripheral
 * to expose Flash memory hosted by another processor.
 *
 * Licensed under the GPL-2 or later.
 */

#include <common.h>
#include <malloc.h>
#include <spi_flash.h>

#include "spi_flash_internal.h"

int spi_flash_cmd(struct spi_slave *spi, u8 cmd, void *response, size_t len)
{
  printf("MTD_BRIDGE: spi_flash_cmd()\n");
  return 0;
}

int spi_flash_cmd_read(struct spi_slave *spi, const u8 *cmd,
                       size_t cmd_len, void *data, size_t data_len) {
  printf("MTD_BRIDGE: spi_flash_cmd_read()\n");
	return 0;
}

int spi_flash_cmd_write(struct spi_slave *spi, const u8 *cmd, size_t cmd_len,
                        const void *data, size_t data_len) {
  printf("MTD_BRIDGE: spi_flash_cmd_write()\n");
	return 0;
}


int spi_flash_read_common(struct spi_flash *flash, const u8 *cmd,
		size_t cmd_len, void *data, size_t data_len)
{
  /* Fake out the SPI call, there is no SPI bus master */
	return(spi_flash_cmd_read(NULL, cmd, cmd_len, data, data_len));
}

// Modeled after Spansion

static int mtd_bridge_write(struct spi_flash *flash,
                            u32 offset, size_t len, const void *buf) {
  printf("mtd_bridge_write()\n");
	return 0;
}

static int mtd_bridge_read(struct spi_flash *flash,
                           u32 offset, size_t len, void *buf) {
  /* Just return erased bytes for the moment */
  memset(buf, 0xFF, len);
  printf("mtd_bridge_read(off %lu, len %lu)\n", offset, len);
	return 0;
}

int mtd_bridge_erase(struct spi_flash *flash, u32 offset, size_t len) {
  printf("mtd_bridge_erase()\n");
	return 0;
}

static int mtd_bridge_read_otp(struct spi_flash *flash,
                               u32 offset, size_t len, void *buf) {
  printf("mtd_bridge_read_otp()\n");
	return 0;
}

static int mtd_bridge_write_otp(struct spi_flash *flash,
                                u32 offset, size_t len, const void *buf) {
  printf("mtd_bridge_write_otp()\n");
	return 0;
}

// End Spansion

struct spi_flash *spi_flash_probe(unsigned int bus, unsigned int cs,
                                  unsigned int max_hz, unsigned int spi_mode)
{
  struct spi_flash *bridged_flash;

	bridged_flash = malloc(sizeof(struct spi_flash));
	if (bridged_flash == NULL) {
		printf("MTD Flash Bridge: Failed to allocate memory\n");
		return NULL;
	}

  // NULL the SPI master, since this isn't actually using an SPI link
	bridged_flash->spi  = NULL;
	bridged_flash->name = "mtd-bridge";

	bridged_flash->write = mtd_bridge_write;
	bridged_flash->erase = mtd_bridge_erase;
	bridged_flash->read = mtd_bridge_read;
	bridged_flash->wotp = mtd_bridge_write_otp;
	bridged_flash->rotp = mtd_bridge_read_otp;

  /* TODO - What should the size be?  This is hard-coded to 16 MiB */
	bridged_flash->size = (16 * 1024 * 1024);

	printf("Created MTD bridge Flash device\n");

	return(bridged_flash);
}

void spi_flash_free(struct spi_flash *flash) {
	free(flash);
}
