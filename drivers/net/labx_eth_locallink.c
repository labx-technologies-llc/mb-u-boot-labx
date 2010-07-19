/*
 *
 * Lab X Ethernet / LocalLink driver for u-boot
 * Adapted from the Xilinx TEMAC driver
 *
 * Authors: Yoshio Kashiwagi kashiwagi@co-nss.co.jp
 *          Eldridge M. Mount IV (eldridge.mount@labxtechnologies.com)
 *
 * Copyright (C) 2008 Michal Simek <monstr@monstr.eu>
 * June 2008 Microblaze optimalization, FIFO mode support
 *
 * Copyright (C) 2008 Nissin Systems Co.,Ltd.
 * March 2008 created
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 */

#include <config.h>
#include <common.h>
#include <net.h>
#include <malloc.h>
#include <asm/processor.h>

/* Include a board-specific header file to parameterize the hardware details
 * for the interface.  Ensure that it is providing all the necessary defines.
 */
#include <labx_eth_locallink_defs.h>
#ifndef LABX_PRIMARY_ETH_BASEADDR
#  error "Missing board definition for Ethernet MAC base address"
#endif
#ifndef LABX_ETH_LOCALLINK_SDMA_MODE
#  ifndef LABX_ETH_LOCALLINK_FIFO_MODE
#    error "Missing board definition for either SDMA or FIFO mode"
#  endif
#else
  /* SDMA mode, make certain the SDMA controller base address is defined */
#  ifndef LABX_ETH_LOCALLINK_SDMA_CTRL_BASEADDR
#    error "SDMA mode requires board definition of SDMA controller base address"
#  endif
#endif

/* Ensure that there is a definition for the PHY address and MDIO divisor */
#ifndef LABX_ETH_LOCALLINK_PHY_ADDR
#  error "Missing board definition of MDIO PHY address"
#endif
#ifndef LABX_ETH_LOCALLINK_MDIO_DIV
#  error "Missing board definition of MDIO clock divisor"
#endif

#ifdef LABX_ETH_LOCALLINK_SDMA_MODE
	/* SDMA registers definition */
	#define TX_NXTDESC_PTR		(LABX_ETH_LOCALLINK_SDMA_CTRL_BASEADDR + 0x00)
	#define TX_CURBUF_ADDR		(LABX_ETH_LOCALLINK_SDMA_CTRL_BASEADDR + 0x04)
	#define TX_CURBUF_LENGTH	(LABX_ETH_LOCALLINK_SDMA_CTRL_BASEADDR + 0x08)
	#define TX_CURDESC_PTR		(LABX_ETH_LOCALLINK_SDMA_CTRL_BASEADDR + 0x0c)
	#define TX_TAILDESC_PTR		(LABX_ETH_LOCALLINK_SDMA_CTRL_BASEADDR + 0x10)
	#define TX_CHNL_CTRL		(LABX_ETH_LOCALLINK_SDMA_CTRL_BASEADDR + 0x14)
	#define TX_IRQ_REG		(LABX_ETH_LOCALLINK_SDMA_CTRL_BASEADDR + 0x18)
	#define TX_CHNL_STS		(LABX_ETH_LOCALLINK_SDMA_CTRL_BASEADDR + 0x1c)

	#define RX_NXTDESC_PTR		(LABX_ETH_LOCALLINK_SDMA_CTRL_BASEADDR + 0x20)
	#define RX_CURBUF_ADDR		(LABX_ETH_LOCALLINK_SDMA_CTRL_BASEADDR + 0x24)
	#define RX_CURBUF_LENGTH	(LABX_ETH_LOCALLINK_SDMA_CTRL_BASEADDR + 0x28)
	#define RX_CURDESC_PTR		(LABX_ETH_LOCALLINK_SDMA_CTRL_BASEADDR + 0x2c)
	#define RX_TAILDESC_PTR		(LABX_ETH_LOCALLINK_SDMA_CTRL_BASEADDR + 0x30)
	#define RX_CHNL_CTRL		(LABX_ETH_LOCALLINK_SDMA_CTRL_BASEADDR + 0x34)
	#define RX_IRQ_REG		(LABX_ETH_LOCALLINK_SDMA_CTRL_BASEADDR + 0x38)
	#define RX_CHNL_STS		(LABX_ETH_LOCALLINK_SDMA_CTRL_BASEADDR + 0x3c)

	#define DMA_CONTROL_REG		(LABX_ETH_LOCALLINK_SDMA_CTRL_BASEADDR + 0x40)
#endif

/* 
 * Lab X Tri-mode MAC register definitions 
 * The first half of the address space maps to registers used for
 * PHY control and interrupts.  The second half is passed through to the
 * actual MAC registers.
 */
#define MDIO_CONTROL_REG      (0x00000000)
#  define PHY_MDIO_BUSY       (0x80000000)
#  define PHY_REG_ADDR_MASK   (0x01F)
#  define PHY_ADDR_MASK       (0x01F)
#  define PHY_ADDR_SHIFT      (5)
#  define PHY_MDIO_READ       (0x0400)
#  define PHY_MDIO_WRITE      (0x0000)
#define MDIO_DATA_REG         (0x00000004)
#  define PHY_DATA_MASK       (0x0000FFFF)
#define INT_MASK_REG          (0x00000008)
#  define PHY_IRQ_FALLING     (0x00000000)
#  define PHY_IRQ_RISING      (0x80000000)
#define INT_FLAGS_REG         (0x0000000C)
#  define MDIO_IRQ_MASK       (0x00000001)
#  define PHY_IRQ_MASK        (0x00000002)
#define LABX_MAC_REGS_BASE    (0x00000200)

/* Note - Many of the Rx and Tx config register bits are read-only, and simply
 *        represent the fixed behavior of the simple MAC.  In the future, these
 *        functions may be fleshed out in the hardware.
 */
#ifdef XILINX_HARD_MAC
#define MAC_RX_CONFIG_REG     (LABX_MAC_REGS_BASE + 0x0040)
#else
#define MAC_RX_CONFIG_REG     (LABX_MAC_REGS_BASE + 0x0004)
#endif
#  define RX_SOFT_RESET          (0x80000000)
#  define RX_JUMBO_FRAME_ENABLE  (0x40000000)
#  define RX_IN_BAND_FCS_ENABLE  (0x20000000)
#  define RX_DISABLE             (0x00000000)
#  define RX_ENABLE              (0x10000000)
#  define RX_VLAN_TAGS_ENABLE    (0x08000000)
#  define RX_HALF_DUPLEX_MODE    (0x04000000)
#  define RX_DISABLE_LTF_CHECK   (0x02000000)
#  define RX_DISABLE_CTRL_CHECK  (0x01000000)

#ifdef XILINX_HARD_MAC
#define MAC_TX_CONFIG_REG     (LABX_MAC_REGS_BASE + 0x0080)
#else
#define MAC_TX_CONFIG_REG     (LABX_MAC_REGS_BASE + 0x0008)
#endif
#  define TX_SOFT_RESET          (0x80000000)
#  define TX_JUMBO_FRAME_ENABLE  (0x40000000)
#  define TX_IN_BAND_FCS_ENABLE  (0x20000000)
#  define TX_DISABLE             (0x00000000)
#  define TX_ENABLE              (0x10000000)
#  define TX_VLAN_TAGS_ENABLE    (0x08000000)
#  define TX_HALF_DUPLEX_MODE    (0x04000000)
#  define TX_IFG_ADJUST_ENABLE   (0x02000000)

#ifdef XILINX_HARD_MAC
#define MAC_SPEED_SELECT_REG  (LABX_MAC_REGS_BASE + 0x0100)
#else
#define MAC_SPEED_SELECT_REG  (LABX_MAC_REGS_BASE + 0x0010)
#endif
#  define MAC_SPEED_1_GBPS    (0x80000000)
#  define MAC_SPEED_100_MBPS  (0x40000000)
#  define MAC_SPEED_10_MBPS   (0x00000000)

#ifdef XILINX_HARD_MAC
#define MAC_MDIO_CONFIG_REG   (LABX_MAC_REGS_BASE + 0x0140)
#else
#define MAC_MDIO_CONFIG_REG   (LABX_MAC_REGS_BASE + 0x0014)
#endif
#  define MDIO_DIVISOR_MASK  (0x0000003F)
#  define MDIO_ENABLED       (0x00000040)

/* XPS_LL_TEMAC indirect registers offset definition */

#if 0 // Removing!
#define RCW1	0x5200 /*0x240*/
#define TC	0x5400 /*0x280*/
#define FCC	0x5600 /*0x2c0*/
#define PHYC	XXXXXX /*0x320*/
#define MC	0x5A00 /*0x340*/
#define UAW0	XXXXXX /*0x380*/
#define UAW1	XXXXXX /*0x384*/
#define MAW0	XXXXXX /*0x388*/
#define MAW1	XXXXXX /*0x38c*/
#define AFM	0x2008 /*0x390*/
#define TIS	XXXXXX /*0x3a0*/
#define TIE	XXXXXX /*0x3a4*/
#define MIIMWD	XXXXXX /*0x3b0*/
#define MIIMAI	XXXXXX /*0x3b4*/
#define MIIBASE 0x6000


#define MDIO_ENABLE_MASK	0x40
#define MDIO_CLOCK_DIV_MASK	0x3F
#define MDIO_CLOCK_DIV_100MHz	0x28

#endif

/* Maximum Ethernet MTU (with VLAN tag extension) */
#define ETHER_MTU		1520

/* PHY register definitions */
#define MII_BMCR            0x00        /* Basic mode control register */
#define MII_ADVERTISE       0x04
#define MII_EXADVERTISE 	0x09

/* Basic mode control register. */
#define BMCR_RESV               0x003f  /* Unused...                   */
#define BMCR_SPEED1000          0x0040  /* MSB of Speed (1000)         */
#define BMCR_CTST               0x0080  /* Collision test              */
#define BMCR_FULLDPLX           0x0100  /* Full duplex                 */
#define BMCR_ANRESTART          0x0200  /* Auto negotiation restart    */
#define BMCR_ISOLATE            0x0400  /* Disconnect DP83840 from MII */
#define BMCR_PDOWN              0x0800  /* Powerdown the DP83840       */
#define BMCR_ANENABLE           0x1000  /* Enable auto negotiation     */
#define BMCR_SPEED100           0x2000  /* Select 100Mbps              */
#define BMCR_LOOPBACK           0x4000  /* TXD loopback bits           */
#define BMCR_RESET              0x8000  /* Reset the DP83840           */

/* Advertisement control register. */
#define ADVERTISE_SLCT          0x001f  /* Selector bits               */
#define ADVERTISE_CSMA          0x0001  /* Only selector supported     */
#define ADVERTISE_10HALF        0x0020  /* Try for 10mbps half-duplex  */
#define ADVERTISE_1000XFULL     0x0020  /* Try for 1000BASE-X full-duplex */
#define ADVERTISE_10FULL        0x0040  /* Try for 10mbps full-duplex  */
#define ADVERTISE_1000XHALF     0x0040  /* Try for 1000BASE-X half-duplex */
#define ADVERTISE_100HALF       0x0080  /* Try for 100mbps half-duplex */
#define ADVERTISE_1000XPAUSE    0x0080  /* Try for 1000BASE-X pause    */
#define ADVERTISE_100FULL       0x0100  /* Try for 100mbps full-duplex */
#define ADVERTISE_1000XPSE_ASYM 0x0100  /* Try for 1000BASE-X asym pause */
#define ADVERTISE_100BASE4      0x0200  /* Try for 100mbps 4k packets  */
#define ADVERTISE_PAUSE_CAP     0x0400  /* Try for pause               */
#define ADVERTISE_PAUSE_ASYM    0x0800  /* Try for asymetric pause     */
#define ADVERTISE_RESV          0x1000  /* Unused...                   */
#define ADVERTISE_RFAULT        0x2000  /* Say we can detect faults    */
#define ADVERTISE_LPACK         0x4000  /* Ack link partners response  */
#define ADVERTISE_NPAGE         0x8000  /* Next page bit               */

/* 1000BASE-T Control register */
#define ADVERTISE_1000FULL      0x0200  /* Advertise 1000BASE-T full duplex */
#define ADVERTISE_1000HALF      0x0100  /* Advertise 1000BASE-T half duplex */

#define ADVERTISE_FULL (ADVERTISE_100FULL | ADVERTISE_10FULL | \
                        ADVERTISE_CSMA)
#define ADVERTISE_ALL (ADVERTISE_10HALF | ADVERTISE_10FULL | \
                       ADVERTISE_100HALF | ADVERTISE_100FULL)

#ifdef LABX_ETH_LOCALLINK_SDMA_MODE

/* CDMAC descriptor status bit definitions */
#define BDSTAT_ERROR_MASK	0x80
#define BDSTAT_INT_ON_END_MASK	0x40
#define BDSTAT_STOP_ON_END_MASK	0x20
#define BDSTAT_COMPLETED_MASK	0x10
#define BDSTAT_SOP_MASK		0x08
#define BDSTAT_EOP_MASK		0x04
#define BDSTAT_CHANBUSY_MASK	0x02
#define BDSTAT_CHANRESET_MASK	0x01

/* SDMA Buffer Descriptor */
typedef struct cdmac_bd_t {
  struct cdmac_bd_t *next_p;
  unsigned char *phys_buf_p;
  unsigned long buf_len;
  unsigned char stat;
  unsigned char app1_1;
  unsigned short app1_2;
  unsigned long app2;
  unsigned long app3;
  unsigned long app4;
  unsigned long app5;
} cdmac_bd __attribute((aligned(32))) ;

static cdmac_bd	tx_bd;
static cdmac_bd	rx_bd;

#endif

#ifdef LABX_ETH_LOCALLINK_FIFO_MODE
typedef struct ll_fifo_s {
  int isr; /* Interrupt Status Register 0x0 */
  int ier; /* Interrupt Enable Register 0x4 */
  int tdfr; /* Transmit data FIFO reset 0x8 */
  int tdfv; /* Transmit data FIFO Vacancy 0xC */
  int tdfd; /* Transmit data FIFO 32bit wide data write port 0x10 */
  int tlf; /* Write Transmit Length FIFO 0x14 */
  int rdfr; /* Read Receive data FIFO reset 0x18 */
  int rdfo; /* Receive data FIFO Occupancy 0x1C */
  int rdfd; /* Read Receive data FIFO 32bit wide data read port 0x20 */
  int rlf; /* Read Receive Length FIFO 0x24 */
  int llr; /* Read LocalLink reset 0x28 */
} ll_fifo_s;

ll_fifo_s *ll_fifo = (ll_fifo_s *) (XILINX_LLTEMAC_FIFO_BASEADDR);
#endif


static unsigned char tx_buffer[ETHER_MTU] __attribute((aligned(32)));
static unsigned char rx_buffer[ETHER_MTU] __attribute((aligned(32)));

#if !defined(CONFIG_NET_MULTI)
static struct eth_device *xps_ll_dev = NULL;
#endif

struct labx_eth_private {
  int idx;
  unsigned char dev_addr[6];
};

/* Performs a register write to a PHY */
static void write_phy_register(int emac, int phy_addr, int reg_addr, int phy_data)
{
  unsigned int addr;

  /* Write the data first, then the control register */
  addr = (LABX_PRIMARY_ETH_BASEADDR + MDIO_DATA_REG);
  *((volatile unsigned int *) addr) = phy_data;
  addr = (LABX_PRIMARY_ETH_BASEADDR + MDIO_CONTROL_REG);
  *((volatile unsigned int *) addr) = 
    (PHY_MDIO_WRITE | ((phy_addr & PHY_ADDR_MASK) << PHY_ADDR_SHIFT) |
     (reg_addr & PHY_REG_ADDR_MASK));
  while(*((volatile unsigned int *) addr) & PHY_MDIO_BUSY);
}

/* Performs a register read from a PHY */
static unsigned int read_phy_register(int emac, int phy_addr, int reg_addr)
{
  unsigned int addr;
  unsigned int readValue;

  /* Write to the MDIO control register to initiate the read */
  addr = (LABX_PRIMARY_ETH_BASEADDR + MDIO_CONTROL_REG);
  *((volatile unsigned int *) addr) = 
    (PHY_MDIO_READ | ((phy_addr & PHY_ADDR_MASK) << PHY_ADDR_SHIFT) |
     (reg_addr & PHY_REG_ADDR_MASK));
  while(*((volatile unsigned int *) addr) & PHY_MDIO_BUSY);
  addr = (LABX_PRIMARY_ETH_BASEADDR + MDIO_DATA_REG);
  readValue = *((volatile unsigned int *) addr);
  return(readValue);
}

/* Writes a value to a MAC register */
static void labx_eth_write_mac_reg(int emac, int reg_offset, int reg_data)
{
  //printf("REG %04X = %08X (was %08X)\n", reg_offset, reg_data, *(volatile unsigned int *)(LABX_PRIMARY_ETH_BASEADDR + reg_offset));
  /* Apply the register offset to the base address */
  *(volatile unsigned int *)(LABX_PRIMARY_ETH_BASEADDR + reg_offset) = reg_data;
}

/* Reads a value from a MAC register */
int labx_eth_read_mac_reg(int emac, int reg_offset)
{
	unsigned int val = *(volatile unsigned int *)(LABX_PRIMARY_ETH_BASEADDR + reg_offset);
	return(val);
}

void mdelay(unsigned int msec)
{
	while(msec--)
		udelay(1000);
}

static int phy_addr = LABX_ETH_LOCALLINK_PHY_ADDR;
static int link = 0;
static int first = 1;

/* setting ll_temac and phy to proper setting */
static int labx_eth_phy_ctrl(void)
{
  int i;
  unsigned int result;

  if(first) {
    unsigned int id_high;
    unsigned id_low;

    /* Read and report the PHY */
    id_high = read_phy_register(0, phy_addr, 2);
    id_low = read_phy_register(0, phy_addr, 3);
    printf("PHY ID: 0x%04X%04X\n", id_high, id_low);
  }

  if(!link) {
    int timeout = 50;
    /* Requery the PHY general status register */
    /* Wait up to 5 secs for a link */
    while(timeout--) {
      result = read_phy_register(0, phy_addr, 1);
      if(result & 0x24) {
        printf("Link up!\n");
        break;
      }
      mdelay(100);
    }
  }
  
  result = read_phy_register(0, phy_addr, 1);
  if((result & 0x24) != 0x24) {
    printf("No link!\n");
    if(link) {
      link = 0;
      printf("Link has gone down\n");
    }
    return 0;
  }
  if(link == 0) {
    link = 1;
  } else {
    return 1;
  }
  
  result = read_phy_register(0, phy_addr, 10);
  if((result & 0x0800) == 0x0800) {
    labx_eth_write_mac_reg(0, MAC_SPEED_SELECT_REG, MAC_SPEED_1_GBPS);
    printf("1000BASE-T/FD\n");
    return 1;
  }
  result = read_phy_register(0, phy_addr, 5);
  if((result & 0x0100) == 0x0100) {
    labx_eth_write_mac_reg(0, MAC_SPEED_SELECT_REG, MAC_SPEED_100_MBPS);
    printf("100BASE-T/FD\n");
  } else if((result & 0x0040) == 0x0040) {
    labx_eth_write_mac_reg(0, MAC_SPEED_SELECT_REG, MAC_SPEED_10_MBPS);
    printf("10BASE-T/FD\n");
  } else {
    printf("Half Duplex not supported\n");
  }
  return 1;
}

#ifdef LABX_ETH_LOCALLINK_SDMA_MODE
/* bd init */
static void labx_eth_bd_init()
{
	memset((void *)&tx_bd, 0, sizeof(cdmac_bd));
	memset((void *)&rx_bd, 0, sizeof(cdmac_bd));

	rx_bd.phys_buf_p = &rx_buffer[0];
	rx_bd.next_p = &rx_bd;
	rx_bd.buf_len = ETHER_MTU;
	invalidate_dcache_range(&rx_bd, sizeof(cdmac_bd));


	*(unsigned int *)RX_CURDESC_PTR = &rx_bd;
	*(unsigned int *)RX_TAILDESC_PTR = &rx_bd;

	*(unsigned int *)RX_NXTDESC_PTR = &rx_bd; // setup first fd


	tx_bd.phys_buf_p = &tx_buffer[0];
	tx_bd.next_p = &tx_bd;
	invalidate_dcache_range(&tx_bd, sizeof(cdmac_bd));
	*(unsigned int *)TX_CURDESC_PTR = &tx_bd;
}
#endif

#ifdef LABX_ETH_LOCALLINK_SDMA_MODE
static int labx_eth_send_sdma(unsigned char *buffer, int length)
{
	int i;

	if(labx_eth_phy_ctrl() == 0)
		return 0;

	memcpy(tx_buffer, buffer, length);
	invalidate_dcache_range(tx_buffer, length);

	tx_bd.stat = BDSTAT_SOP_MASK | BDSTAT_EOP_MASK | BDSTAT_STOP_ON_END_MASK;
	tx_bd.buf_len = length;
	invalidate_dcache_range(&tx_bd, sizeof(cdmac_bd));

	// Wait for DMA to complete if one is active
	while (*(volatile unsigned int*)(TX_CHNL_STS) & 0x00000002);

	*(volatile unsigned int *)TX_CURDESC_PTR = &tx_bd;
	*(volatile unsigned int *)TX_TAILDESC_PTR = &tx_bd;	// DMA start

	do {
		invalidate_dcache_range(&tx_bd, sizeof(cdmac_bd));

		if ((*(volatile unsigned int*)(TX_CHNL_STS)) & 0x00000080)
		{
			printf("TX DMA Error\n");
			for (i=0; i<0x44; i+=4)
			{
				printf("SDMA REG %08X: %08x\n", (TX_NXTDESC_PTR+i),
					*(volatile unsigned int*)(TX_NXTDESC_PTR+i));
			}

			// Reset and reinitialize the DMA engine
			*(volatile unsigned int*)(DMA_CONTROL_REG) = 0x00000001;
			while (*(volatile unsigned int*)(DMA_CONTROL_REG) & 0x00000001);

			labx_eth_bd_init();
			break;
		}

		if (((*(volatile unsigned int*)(TX_CHNL_STS)) & 0x00000002) == 0)
		{
			invalidate_dcache_range(&tx_bd, sizeof(cdmac_bd));
            //			printf("Exit loop %08X\n", (volatile int)tx_bd.stat);
			break;
		}
	} while(!(((volatile unsigned int)tx_bd.stat) & BDSTAT_COMPLETED_MASK));

	return length;
}


static int labx_eth_recv_sdma()
{
	int length;
	int i;

	invalidate_dcache_range(&rx_bd, sizeof(cdmac_bd));

	if ((*(volatile unsigned int*)(RX_CHNL_STS)) & 0x00000080)
	{
		printf("RX DMA Error\n");
		for (i=0; i<0x44; i+=4)
		{
			printf("SDMA REG %08X: %08x\n", (TX_NXTDESC_PTR+i),
				*(volatile unsigned int*)(TX_NXTDESC_PTR+i));
		}

		// Reset and reinitialize the DMA engine
		*(volatile unsigned int*)(DMA_CONTROL_REG) = 0x00000001;
		while (*(volatile unsigned int*)(DMA_CONTROL_REG) & 0x00000001);

		labx_eth_bd_init();
	}
	
//	if(!((volatile unsigned int)rx_bd.stat & BDSTAT_COMPLETED_MASK)) {
	if (((*(volatile unsigned int*)(RX_CHNL_STS)) & 0x00000010) == 0) {
		return 0;
	}

	invalidate_dcache_range(&rx_bd, sizeof(cdmac_bd));

//	printf("RX CH STS: %08x, %08x, %08x, %08x, %08x\n", *(volatile unsigned int*)(RX_CHNL_STS), (int)rx_bd.stat, rx_bd.app2, rx_bd.app3, rx_bd.app4);

	length = rx_bd.app5;
	invalidate_dcache_range(rx_bd.phys_buf_p, length);

//	*(volatile unsigned int*)&rx_bd.next_p = &rx_bd;
	*(volatile unsigned int*)&rx_bd.buf_len = ETHER_MTU;
	*(volatile unsigned int*)&rx_bd.stat = 0;
	*(volatile unsigned int*)&rx_bd.app5 = 0;

	invalidate_dcache_range(&rx_bd, sizeof(cdmac_bd));
#if 0	
	if (length == 0) length = 1500;
	printf("recv_sdma (%d)", length);
	for (i = 0; i<length; i++)
	{
		if (i%8 == 0) printf("\n");
		printf("%02X ", rx_bd.phys_buf_p[i]);
	}
	printf("\n");
#endif
	if(length > 0) {
		NetReceive(rx_bd.phys_buf_p, length);
	}

	*(volatile unsigned int *)RX_CURDESC_PTR = &rx_bd;
	*(volatile unsigned int *)RX_TAILDESC_PTR = &rx_bd;
	*(volatile unsigned int *)RX_NXTDESC_PTR = &rx_bd;

	return length;
}
#endif


#ifdef LABX_ETH_LOCALLINK_FIFO_MODE
void debugll(int count)
{
  	printf ("%d fifo isr 0x%08x, fifo_ier 0x%08x, fifo_rdfr 0x%08x, fifo_rdfo 0x%08x fifo_rlr 0x%08x\n",count, ll_fifo->isr, \
	ll_fifo->ier, ll_fifo->rdfr, ll_fifo->rdfo, ll_fifo->rlf);
}


static int labx_eth_send_fifo(unsigned char *buffer, int length)
{
    unsigned int *buf = (unsigned int*) buffer;
	unsigned int len, i, val;

	len = length/4 +1;

	for (i = 0; i < len; i++) {
		val = *buf++;
		ll_fifo->tdfd = val;
	}

	ll_fifo->tlf = length;

	return length;
}

static int labx_eth_recv_fifo(void)
{
	int len, len2, i, val;
	int *buf = (int*) &rx_buffer[0];


	if (ll_fifo->isr & 0x04000000 ) {
		/* reset isr */
		ll_fifo->isr = 0xffffffff;

		len = ll_fifo->rlf & 0x7FF;
		len2 = (len/4) + 1;

		for (i = 0; i < len2; i++) {
			val = ll_fifo->rdfd;
			*buf++ = val ;
		}

		NetReceive (&rx_buffer[0], len);
	}

	return 0;
}
#endif


/* setup mac addr */
static int labx_eth_addr_setup(struct labx_eth_private * lp)
{
  char * env_p;
  char * end;
  int i, val;
  
  env_p = getenv("ethaddr");
  if (env_p == NULL) {
    printf("cannot get enviroment for \"ethaddr\".\n");
    return -1;
  }
  
  for (i = 0; i < 6; i++) {
    lp->dev_addr[i] = env_p ? simple_strtoul(env_p, &end, 16) : 0;
    if (env_p) env_p = (*end) ? end + 1 : end;
  }
  
  /* The following code is somewhat pointless, as the MAC operates without
   * any address filtering.  This allows each hardware module behind it to
   * determine which traffic it is concerned with.  What needs to be done is
   * to equip the labx_eth_locallink EDK core with the ability to filter unicast
   * and multicast traffic, at which point this code may be modified to do so.
   */
  
#if 0
  /* set up unicast MAC address filter */
  val = ((lp->dev_addr[3] << 24) | (lp->dev_addr[2] << 16) |
         (lp->dev_addr[1] << 8) | (lp->dev_addr[0] ));
  labx_eth_write_mac_reg(0, FILTER_0_AW0, val);
  val = (lp->dev_addr[5] << 8) | lp->dev_addr[4] ;
  labx_eth_write_mac_reg(0, FILTER_0_AW1, val);
  labx_eth_write_mac_reg(0, FILTER_0_EN0, 0xFFFFFFFF);
  labx_eth_write_mac_reg(0, FILTER_0_EN1, 0x0000FFFF);
  
  /* setup broadcast MAC address filter */
  labx_eth_write_mac_reg(0, FILTER_1_AW0, 0xFFFFFFFF);
  labx_eth_write_mac_reg(0, FILTER_1_AW1, 0x0000FFFF);
  labx_eth_write_mac_reg(0, FILTER_1_EN0, 0xFFFFFFFF);
  labx_eth_write_mac_reg(0, FILTER_1_EN1, 0x0000FFFF);
#endif
  
	return(0);
}

static void labx_eth_restart(void)
{
#ifdef LABX_ETH_LOCALLINK_SDMA_MODE
  /* Initialize SDMA descriptors */
  labx_eth_bd_init();
#endif

  /* Enable the receiver and transmitter */
  labx_eth_write_mac_reg(0, MAC_RX_CONFIG_REG, RX_ENABLE | RX_VLAN_TAGS_ENABLE);
  labx_eth_write_mac_reg(0, MAC_TX_CONFIG_REG, TX_ENABLE | TX_VLAN_TAGS_ENABLE);
}

static void labx_eth_init(struct eth_device *dev, bd_t *bis)
{
  struct labx_eth_private *lp = (struct labx_eth_private *)dev->priv;

  if(!first)
  {
    labx_eth_restart();
    return 0;
  }

#ifdef LABX_ETH_LOCALLINK_SDMA_MODE
  /* Initialize SDMA descriptors */
  labx_eth_bd_init();
#endif

#ifdef LABX_ETH_LOCALLINK_FIFO_MODE
  // Set fifo length
  ll_fifo->tdfr = 0x000000a5;
  ll_fifo->rdfr = 0x000000a5;
//	printf ("fifo isr 0x%08x, fifo_ier 0x%08x, fifo_tdfv 0x%08x, fifo_rdfo 0x%08x fifo_rlf 0x%08x\n", ll_fifo->isr, ll_fifo->ier, ll_fifo->tdfv, ll_fifo->rdfo,ll_fifo->rlf);
#endif

  /* Configure the MDIO divisor and enable the interface to the PHY */
  /* XILINX_HARD_MAC Note: The hard MDIO controller must be configured or */
  /* the SGMII autonegotiation won't happen. Even if we never use that MDIO controller. */
  labx_eth_write_mac_reg(0, MAC_MDIO_CONFIG_REG, 
                         ((LABX_ETH_LOCALLINK_MDIO_DIV & MDIO_DIVISOR_MASK) |
                          MDIO_ENABLED));
  
  /* Set up the MAC address in the hardware */
  labx_eth_addr_setup(lp);

  /* Enable the receiver and transmitter */
  labx_eth_write_mac_reg(0, MAC_RX_CONFIG_REG, RX_ENABLE | RX_VLAN_TAGS_ENABLE);
  labx_eth_write_mac_reg(0, MAC_TX_CONFIG_REG, TX_ENABLE | TX_VLAN_TAGS_ENABLE);

  /* Configure the PHY */
  labx_eth_phy_ctrl();
  first = 0;
}

static int labx_eth_halt(void)
{
  labx_eth_write_mac_reg(0, MAC_RX_CONFIG_REG, RX_DISABLE);
  labx_eth_write_mac_reg(0, MAC_TX_CONFIG_REG, TX_DISABLE);

#ifdef LABX_ETH_LOCALLINK_SDMA_MODE
  *(unsigned int *)DMA_CONTROL_REG = 0x00000001;
  while(*(volatile unsigned int *)DMA_CONTROL_REG & 1);
#endif
}

int labx_eth_send(struct eth_device *dev, volatile void *packet, int length)
{
#ifdef LABX_ETH_LOCALLINK_SDMA_MODE
	return labx_eth_send_sdma((unsigned char *)packet, length);
#endif
#ifdef LABX_ETH_LOCALLINK_FIFO_MODE
	return labx_eth_send_fifo((unsigned char *)packet, length);
#endif
}

int labx_eth_recv(struct eth_device *dev)
{
#ifdef LABX_ETH_LOCALLINK_SDMA_MODE
	return labx_eth_recv_sdma();
#endif
#ifdef LABX_ETH_LOCALLINK_FIFO_MODE
	return labx_eth_recv_fifo();
#endif
}

int labx_eth_initialize(bd_t *bis)
{
  struct eth_device *dev;
  
  dev = malloc(sizeof(*dev));
  if (dev == NULL)
    hang();
  
  memset(dev, 0, sizeof(*dev));
  sprintf(dev->name, "Lab X LocalLink Ethernet");
  
  dev->iobase = LABX_PRIMARY_ETH_BASEADDR;
  dev->priv = 0;
  dev->init = labx_eth_init;
  dev->halt = labx_eth_halt;
  dev->send = labx_eth_send;
  dev->recv = labx_eth_recv;
  
  eth_register(dev);
  
  return 0;
}
