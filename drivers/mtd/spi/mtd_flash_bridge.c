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

/* MTD bridge register definitions */
#define UART_FIFO_READ_ADDR        0x000
#define UART_FIFO_WRITE_ADDR       0x004
#define UART_STATUS_REG_ADDR       0x008
#define UART_CTRL_REG_ADDR         0x00C
#define MTDBRIDGE_IRQ_REG_ADDR     0x010
#define MTDBRIDGE_MASK_REG_ADDR    0x014
#define MTDBRIDGE_COMMAND_REG_ADDR 0x018
#define MTDBRIDGE_STATUS_REG_ADDR  0x01C
#define MTDBRIDGE_ADDRESS_REG_ADDR 0x020
#define MTDBRIDGE_LENGTH_REG_ADDR  0x024
#define MTDBRIDGE_MAILBOX_RAM_ADDR 0x800

#define UART_STATUS_RX_DATA_BIT    (1 << 0)
#define UART_STATUS_RX_FULL_BIT    (1 << 1)
#define UART_STATUS_TX_EMPTY_BIT   (1 << 2)
#define UART_STATUS_TX_FULL_BIT    (1 << 3)
#define UART_STATUS_INT_EN_BIT     (1 << 4)
#define MTDBRIDGE_IRQ_COMMAND_BIT  (1 << 0)
#define MTDBRIDGE_IRQ_COMPLETE_BIT (1 << 0)
#define MTDBRIDGE_OPCODE_READ      0x03
/* Status Register bits. */
#define	MTDBRIDGE_SR_OIP           1	/* Operation in progress */
#define	MTDBRIDGE_SR_WEL           2	/* Write enable latch */
/* meaning of other SR_* bits may differ between vendors */
#define MTDBRIDGE_SR_NORESP        0x04	/* No response from MTD bridge */
#define MTDBRIDGE_SR_RWERROR       0x08	/* Error in read/write operation on file */
#define	MTDBRIDGE_SR_UNMAPPED      0x10	/* Address specified in command is not mapped to a file */
#define	MTDBRIDGE_SR_RANGE_ERR     0x20	/* Specified block goes beyond mapped area end  */
#define	MTDBRIDGE_SR_RDONLY        0x40	/* Attempt to write or erase a read-only map */
#define MTDBRIDGE_SR_INVALID       0x80	/* Invalid MTD command */
#define MTDBRIDGE_BUFFER_SIZE      2048 /* Size of mtdbridge mailbox RAM */

/* Macro definitions for accessing MTD bridge registers */
#define REG32_READ(addr, offset)        (*(volatile unsigned long int *)(addr + offset))
#define REG32_WRITE(addr, offset, data) (*(volatile unsigned long int *)(addr + offset) = data)

#define MTDBRIDGE_READ(offset) REG32_READ(MTD_BRIDGE_BASEADDR, offset)
#define MTDBRIDGE_WRITE(offset, data) REG32_WRITE(MTD_BRIDGE_BASEADDR, offset, data)
#define MTDBRIDGE_GETBUF() ((void *)(MTD_BRIDGE_BASEADDR + MTDBRIDGE_MAILBOX_RAM_ADDR))

/* Timeout, in milliseconds, associated with MTD bridge operations */
#define MTDBRIDGE_TIMEOUT_MS     500
#define MTDBRIDGE_TIMEOUT_SLICE  100
#define MTDBRIDGE_TIMEOUT_WAITS  ((MTDBRIDGE_TIMEOUT_MS + MTDBRIDGE_TIMEOUT_SLICE - 1) / MTDBRIDGE_TIMEOUT_SLICE)

/* Stubbed out SPI Flash functions */

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

/* Functions performing the actual interaction with the MTD bridge */

static int mtd_bridge_write(struct spi_flash *flash,
                            u32 offset, size_t len, const void *buf) {
  printf("mtd_bridge_write()\n");
	return 0;
}

static int mtd_bridge_read(struct spi_flash *flash,
                           u32 offset, size_t len, void *buf) {
  int timeout;
	int rc = 0;

  // Issue a read command to the MTD bridge:
  //
  // * Clear the "operation complete" IRQ flag bit
  // * Set the Flash offset and length
  // * Trigger the operation by writing the opcode
	MTDBRIDGE_WRITE(MTDBRIDGE_IRQ_REG_ADDR, MTDBRIDGE_IRQ_COMPLETE_BIT);
	MTDBRIDGE_WRITE(MTDBRIDGE_ADDRESS_REG_ADDR, offset);

  /* TEMPORARY - only do 1 KiB to start!! */
	MTDBRIDGE_WRITE(MTDBRIDGE_LENGTH_REG_ADDR, 1024);
	MTDBRIDGE_WRITE(MTDBRIDGE_COMMAND_REG_ADDR, MTDBRIDGE_OPCODE_READ);

	// Poll until a response is received from the MTD bridge daemon, or we time out
  timeout = MTDBRIDGE_TIMEOUT_WAITS;
	while ((MTDBRIDGE_READ(MTDBRIDGE_IRQ_REG_ADDR) & MTDBRIDGE_IRQ_COMPLETE_BIT) == 0) {
    if(!timeout--) {
			rc = MTDBRIDGE_SR_NORESP;
			break;
		}
    mdelay(MTDBRIDGE_TIMEOUT_SLICE);
	}

	// Fetch the response if there was one
	if(rc == 0) rc = MTDBRIDGE_READ(MTDBRIDGE_STATUS_REG_ADDR);

  // Loop waiting if the returned code was "operation in progress", up until
  // the timeout period
  timeout = MTDBRIDGE_TIMEOUT_WAITS;
	while((rc & MTDBRIDGE_SR_OIP) != 0) {
    if(!timeout--) {
			// Break loop with the "operation in progress" status pending
			break;
		}
    mdelay(MTDBRIDGE_TIMEOUT_SLICE);
		rc = MTDBRIDGE_READ(MTDBRIDGE_STATUS_REG_ADDR);
	}

  /* If the read returned without an error code, print some bytes for now */
  if(rc == 0) {
    int word_index;
    int offset = MTDBRIDGE_MAILBOX_RAM_ADDR;
    
    printf("Good read!  First words:\n");
    for(word_index = 0; word_index < 8; word_index++) {
      printf("0x%08X ", MTDBRIDGE_READ(offset));
      offset += 4;
    }
    printf("\n");
  }

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
