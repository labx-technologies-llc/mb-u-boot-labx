/*
 * Xilinx SPI driver
 *
 * based on bfin_spi.c, by way of altera_spi.c
 * Copyright (c) 2005-2008 Analog Devices Inc.
 * Copyright (c) 2010 Thomas Chou <thomas@wytron.com.tw>
 * Copyright (c) 2010 Graeme Smecher <graeme.smecher@mail.mcgill.ca>
 *
 * Licensed under the GPL-2 or later.
 */
#include <common.h>
#include <asm/io.h>
#include <malloc.h>
#include <spi.h>

#define debug printf

#define XILINX_SPI_RR			0x6c
#define XILINX_SPI_TR			0x68
#define XILINX_SPI_SR			0x64
#define XILINX_SPI_CR			0x60
#define XILINX_SPI_SSR			0x70

#define XILINX_SPI_SR_RX_EMPTY_MSK	0x01

#define XILINX_SPI_CR_DEFAULT		(0x0086)

#if XPAR_XSPI_NUM_INSTANCES > 4
# warning "The xilinx_spi driver will ignore some of your SPI peripherals!"
#endif

static ulong xilinx_spi_base_list[] = {
#ifdef XPAR_FLASH_CONTROL_MEM0_BASEADDR
	XPAR_FLASH_CONTROL_MEM0_BASEADDR,
#endif
#ifdef XPAR_FLASH_CONTROL_MEM1_BASEADDR
	XPAR_FLASH_CONTROL_MEM1_BASEADDR,
#endif
#ifdef XPAR_FLASH_CONTROL_MEM2_BASEADDR
	XPAR_FLASH_CONTROL_MEM2_BASEADDR,
#endif
#ifdef XPAR_FLASH_CONTROL_MEM3_BASEADDR
	XPAR_FLASH_CONTROL_MEM3_BASEADDR,
#endif
};

struct xilinx_spi_slave {
	struct spi_slave slave;
	ulong base;
};
#define to_xilinx_spi_slave(s) container_of(s, struct xilinx_spi_slave, slave)

__attribute__((weak))
int spi_cs_is_valid(unsigned int bus, unsigned int cs)
{
	return bus < ARRAY_SIZE(xilinx_spi_base_list) && cs < 32;
}

__attribute__((weak))
void spi_cs_activate(struct spi_slave *slave)
{
	struct xilinx_spi_slave *xilspi = to_xilinx_spi_slave(slave);
	writel(~(1 << slave->cs), xilspi->base + XILINX_SPI_SSR);
}

__attribute__((weak))
void spi_cs_deactivate(struct spi_slave *slave)
{
	struct xilinx_spi_slave *xilspi = to_xilinx_spi_slave(slave);
	writel(~0, xilspi->base + XILINX_SPI_SSR);
}

void spi_init(void)
{
}

struct spi_slave *spi_setup_slave(unsigned int bus, unsigned int cs,
				  unsigned int max_hz, unsigned int mode)
{
	struct xilinx_spi_slave *xilspi;
	if (!spi_cs_is_valid(bus, cs))
		return NULL;
	xilspi = malloc(sizeof(*xilspi));
	if (!xilspi)
		return NULL;

	xilspi->slave.bus = bus;
	xilspi->slave.cs = cs;
	xilspi->base = xilinx_spi_base_list[bus];
//	debug("%s: bus:%i cs:%i base:%lx\n", __func__,
//		bus, cs, xilspi->base);

	writel(XILINX_SPI_CR_DEFAULT, xilspi->base + XILINX_SPI_CR);

	return &xilspi->slave;
}

void spi_free_slave(struct spi_slave *slave)
{
	struct xilinx_spi_slave *xilspi = to_xilinx_spi_slave(slave);
	free(xilspi);
}

int spi_claim_bus(struct spi_slave *slave)
{
	struct xilinx_spi_slave *xilspi = to_xilinx_spi_slave(slave);

//	debug("%s: bus:%i cs:%i\n", __func__, slave->bus, slave->cs);
	writel(~0, xilspi->base + XILINX_SPI_SSR);
	return 0;
}

void spi_release_bus(struct spi_slave *slave)
{
	struct xilinx_spi_slave *xilspi = to_xilinx_spi_slave(slave);

//	debug("%s: bus:%i cs:%i\n", __func__, slave->bus, slave->cs);
	writel(~0, xilspi->base + XILINX_SPI_SSR);
}

#ifndef CONFIG_XILINX_SPI_IDLE_VAL
# define CONFIG_XILINX_SPI_IDLE_VAL 0xee
#endif

int spi_xfer(struct spi_slave *slave, unsigned int bitlen, const void *dout,
	     void *din, unsigned long flags)
{
	struct xilinx_spi_slave *xilspi = to_xilinx_spi_slave(slave);
	/* assume spi core configured to do 8 bit transfers */
	uint bytes = bitlen / 8;
	const uchar *txp = dout;
	uchar *rxp = din;

	//debug("%s: bus:%i cs:%i bitlen:%i bytes:%i flags:%lx data:%02X\n", __func__,
		//slave->bus, slave->cs, bitlen, bytes, flags, *((uint8_t*)dout));
	if (bitlen == 0)
		goto done;

	if (bitlen % 8) {
		flags |= SPI_XFER_END;
		goto done;
	}

	/* empty read buffer */
	while (!(readl(xilspi->base + XILINX_SPI_SR) &
	    XILINX_SPI_SR_RX_EMPTY_MSK))
		readl(xilspi->base + XILINX_SPI_RR);

	if (flags & SPI_XFER_BEGIN)
		spi_cs_activate(slave);

	while (bytes--) {
		uchar d = txp ? *txp++ : CONFIG_XILINX_SPI_IDLE_VAL;
//		debug("%s: tx:%x ", __func__, d);
		writel(d, xilspi->base + XILINX_SPI_TR);
		while (readl(xilspi->base + XILINX_SPI_SR) &
			 XILINX_SPI_SR_RX_EMPTY_MSK)
			;
		d = readl(xilspi->base + XILINX_SPI_RR);
		if (rxp)
			*rxp++ = d;
//		debug("rx:%x\n", d);
	}
 done:
	if (flags & SPI_XFER_END)
		spi_cs_deactivate(slave);

	return 0;
}
