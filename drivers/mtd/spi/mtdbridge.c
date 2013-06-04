/*
 * Driver for mtdbridge flash emulation
 *
 * (C) Copyright 2000-2002
 * Wolfgang Denk, DENX Software Engineering, wd@denx.de.
 * Copyright 2008, Network Appliance Inc.
 * Jason McMullan <mcmullan@netapp.com>
 * Copyright (C) 2004-2007 Freescale Semiconductor, Inc.
 * TsiChung Liew (Tsi-Chung.Liew@freescale.com)
 * Copyright (c) 2008-2009 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#include <common.h>
#include <malloc.h>
#include <asm/io.h>
#include <spi_flash.h>

#include "spi_flash_internal.h"
#define MTDBRIDGE_NSECTORS 65536
#define MTDBRIDGE_NAME "mtdbridge"

#define CMD_MTDBRIDGE_RDSR		0x05	/* Read Status Register */
#define CMD_MTDBRIDGE_WRSR		0x01	/* Write Status Register */
#define CMD_MTDBRIDGE_READ		0x03	/* Read Data Bytes */
#define CMD_MTDBRIDGE_FAST_READ	0x0b	/* Read Data Bytes at Higher Speed */
#define CMD_MTDBRIDGE_BP		0x02	/* Byte Program */
#define CMD_MTDBRIDGE_SE		0x20	/* Sector Erase */

#define MTDBRIDGE_IRQ_REG_ADDR     0x010
#define MTDBRIDGE_MASK_REG_ADDR    0x014
#define MTDBRIDGE_COMMAND_REG_ADDR 0x018
#define MTDBRIDGE_STATUS_REG_ADDR  0x01C
#define MTDBRIDGE_ADDRESS_REG_ADDR 0x020
#define MTDBRIDGE_LENGTH_REG_ADDR  0x040
#define MTDBRIDGE_MAILBOX_RAM_ADDR 0x800

#define MTDBRIDGE_IRQ_COMMAND_BIT  (1 << 0)
#define MTDBRIDGE_IRQ_COMPLETE_BIT (1 << 0)

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

#define MTDBRIDGE_READ(offset) readl(XPAR_LAWO_MTD_BRIDGE_0_BASEADDR + offset)
#define MTDBRIDGE_WRITE(offset, data) writel(data, XPAR_LAWO_MTD_BRIDGE_0_BASEADDR + offset)
#define MTDBRIDGE_GETBUF() ((void *)(XPAR_LAWO_MTD_BRIDGE_0_BASEADDR + MTDBRIDGE_MAILBOX_RAM_ADDR))

struct mtdbridge_spi_flash {
	struct spi_flash flash;
	int wr_ena;
	const char *name;
	u16 nr_sectors;
};

static inline struct mtdbridge_spi_flash *to_mtdbridge_spi_flash(struct spi_flash *flash)
{
	return container_of(flash, struct mtdbridge_spi_flash, flash);
}

static unsigned int read_mtd_sector(volatile unsigned char *dest, unsigned int flashAddr, unsigned int length)
{
	unsigned int rc;
	unsigned long int response_timeout = get_timer(0) + CONFIG_SYS_HZ/2; // 500 mS for iMX to respond to read request
	MTDBRIDGE_WRITE(MTDBRIDGE_IRQ_REG_ADDR, MTDBRIDGE_IRQ_COMPLETE_BIT); // write 1 to clear
	MTDBRIDGE_WRITE(MTDBRIDGE_ADDRESS_REG_ADDR, flashAddr); // Set Flash offset (address)
	MTDBRIDGE_WRITE(MTDBRIDGE_LENGTH_REG_ADDR, length); // Set length
	MTDBRIDGE_WRITE(MTDBRIDGE_COMMAND_REG_ADDR, CMD_MTDBRIDGE_READ); // Start read
	do {
		rc = 0;
		while ((MTDBRIDGE_READ(MTDBRIDGE_IRQ_REG_ADDR) & MTDBRIDGE_IRQ_COMPLETE_BIT) == 0) {
			if (get_timer(0) > response_timeout) {
				rc = MTDBRIDGE_SR_NORESP;
				break; // Wait for MTD bridge to set IRQ complete, or timeout
			}
		}
		if (rc == 0) {
			rc = MTDBRIDGE_READ(MTDBRIDGE_STATUS_REG_ADDR);
		}
	} while ((rc & MTDBRIDGE_SR_OIP) != 0);
	if (rc == 0) {
		rc = length >> 2; // Doing word transfers
		while (rc-- > 0) {
			volatile unsigned long int *src = (volatile unsigned long int *)MTDBRIDGE_GETBUF();
			volatile unsigned long int *dst = (volatile unsigned long int *)dest;
			*dst++ = *src++;
		}
	}
	return rc;
}

static unsigned int write_mtd_sector(volatile unsigned char *source, unsigned int flashAddr, unsigned int length)
{
	unsigned int rc;
	unsigned long int response_timeout = get_timer(0) + CONFIG_SYS_HZ/2; // 500 mS for iMX to respond to read request

	rc = length >> 2; // Doing word transfers
	while (rc-- > 0) {
		volatile unsigned long int *src = (volatile unsigned long int *)source;
		volatile unsigned long int *dst = (volatile unsigned long int *)MTDBRIDGE_GETBUF();
		*dst++ = *src++;
	}
	MTDBRIDGE_WRITE(MTDBRIDGE_IRQ_REG_ADDR, MTDBRIDGE_IRQ_COMPLETE_BIT); // write 1 to clear
	MTDBRIDGE_WRITE(MTDBRIDGE_ADDRESS_REG_ADDR, flashAddr); // Set Flash offset (address)
	MTDBRIDGE_WRITE(MTDBRIDGE_LENGTH_REG_ADDR, length); // Set length
	MTDBRIDGE_WRITE(MTDBRIDGE_COMMAND_REG_ADDR, CMD_MTDBRIDGE_BP); // Start write
	do {
		rc = 0;
		while ((MTDBRIDGE_READ(MTDBRIDGE_IRQ_REG_ADDR) & MTDBRIDGE_IRQ_COMPLETE_BIT) == 0) {
			if (get_timer(0) > response_timeout) {
				rc = MTDBRIDGE_SR_NORESP;
				break; // Wait for MTD bridge to set IRQ complete, or timeout
			}
		}
		if (rc == 0) {
			rc = MTDBRIDGE_READ(MTDBRIDGE_STATUS_REG_ADDR);
		}
	} while ((rc & MTDBRIDGE_SR_OIP) != 0);
	return rc;
}

static unsigned int erase_mtd_sector(unsigned int flashAddr, unsigned int length)
{
	unsigned int rc;
	unsigned long int response_timeout = get_timer(0) + CONFIG_SYS_HZ/2; // 500 mS for iMX to respond to read request

	MTDBRIDGE_WRITE(MTDBRIDGE_IRQ_REG_ADDR, MTDBRIDGE_IRQ_COMPLETE_BIT); // write 1 to clear
	MTDBRIDGE_WRITE(MTDBRIDGE_ADDRESS_REG_ADDR, flashAddr); // Set Flash offset (address)
	MTDBRIDGE_WRITE(MTDBRIDGE_LENGTH_REG_ADDR, length); // Set length
	MTDBRIDGE_WRITE(MTDBRIDGE_COMMAND_REG_ADDR, CMD_MTDBRIDGE_SE); // Start write
	do {
		rc = 0;
		while ((MTDBRIDGE_READ(MTDBRIDGE_IRQ_REG_ADDR) & MTDBRIDGE_IRQ_COMPLETE_BIT) == 0) {
			if (get_timer(0) > response_timeout) {
				rc = MTDBRIDGE_SR_NORESP;
				break; // Wait for MTD bridge to set IRQ complete, or timeout
			}
		}
		if (rc == 0) {
			rc = MTDBRIDGE_READ(MTDBRIDGE_STATUS_REG_ADDR);
		}
	} while ((rc & MTDBRIDGE_SR_OIP) != 0);
	return rc;
}

static int
mtdbridge_enable_writing(struct spi_flash *flash)
{
	to_mtdbridge_spi_flash(flash)->wr_ena = 1;
	return 0;
}

static int
mtdbridge_disable_writing(struct spi_flash *flash)
{
	to_mtdbridge_spi_flash(flash)->wr_ena = 0;
	return 0;
}

static int
mtdbridge_read_fast(struct spi_flash *flash, u32 offset, size_t len, void *buf)
{
	size_t size;
	int rc = 0;
	volatile unsigned char *dest = (volatile unsigned char *)buf;
	while (len > 0 && rc == 0) {
		size = (len < MTDBRIDGE_BUFFER_SIZE) ? len : MTDBRIDGE_BUFFER_SIZE;
		rc = read_mtd_sector(dest, offset, size);
		offset += size;
		dest += size;
	}
	return rc;
}

static int
mtdbridge_write(struct spi_flash *flash, u32 offset, size_t len, const void *buf)
{
	size_t size;
	int rc = 0;
	volatile unsigned char *source = (volatile unsigned char *)buf;
	mtdbridge_enable_writing(flash);
	while (len > 0 && rc == 0) {
		size = (len < MTDBRIDGE_BUFFER_SIZE) ? len : MTDBRIDGE_BUFFER_SIZE;
		rc = write_mtd_sector(source, offset, size);
		offset += size;
		source += size;
	}
	mtdbridge_disable_writing(flash);
	return rc;
}

int
mtdbridge_erase(struct spi_flash *flash, u32 offset, size_t len)
{
	size_t size;
	int rc = 0;
	mtdbridge_enable_writing(flash);
	while (len > 0 && rc == 0) {
		size = (len < MTDBRIDGE_BUFFER_SIZE) ? len : MTDBRIDGE_BUFFER_SIZE;
		rc = erase_mtd_sector(offset, size);
		offset += size;
	}
	mtdbridge_disable_writing(flash);
	return rc;
}

static int
mtdbridge_unlock(struct spi_flash *flash)
{
	int ret;

	ret = mtdbridge_enable_writing(flash);
	return ret;
}

struct spi_flash *
spi_flash_probe_mtdbridge(struct spi_slave *spi, u8 *idcode)
{
	struct mtdbridge_spi_flash *stm;

	(void)idcode;
	stm = malloc(sizeof(*stm));
	if (!stm) {
		debug("SF: Failed to allocate memory\n");
		return NULL;
	}

	stm->nr_sectors = (unsigned short int)MTDBRIDGE_NSECTORS;
	stm->name = MTDBRIDGE_NAME,

	stm->flash.spi = spi;
	stm->flash.name = stm->name;

	stm->flash.write = mtdbridge_write;
	stm->flash.erase = mtdbridge_erase;
	stm->flash.read = mtdbridge_read_fast;
	stm->flash.size = MTDBRIDGE_BUFFER_SIZE * stm->nr_sectors;

	debug("SF: Detected %s with page size %u, total %lu bytes\n",
	      stm->name, MTDBRIDGE_BUFFER_SIZE, stm->flash.size);

	// We will use polled I/O, since we have nothing better to do anyway; mask off interrupt
	MTDBRIDGE_WRITE(MTDBRIDGE_MASK_REG_ADDR, MTDBRIDGE_READ(MTDBRIDGE_MASK_REG_ADDR) & ~MTDBRIDGE_IRQ_COMPLETE_BIT);
	MTDBRIDGE_WRITE(MTDBRIDGE_IRQ_REG_ADDR, MTDBRIDGE_IRQ_COMPLETE_BIT); // write 1 to clear

	/* Flash powers up read-only, so clear BP# bits */
	mtdbridge_unlock(&stm->flash);

	return &stm->flash;
}
