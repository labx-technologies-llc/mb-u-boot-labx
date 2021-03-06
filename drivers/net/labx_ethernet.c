/*
 *
 * Lab X Ethernet driver for u-boot
 * Adapted from the Xilinx TEMAC driver
 *
 * The Lab X Ethernet peripheral implements a FIFO-based interface which
 * largely, directly, mimics the xps_ll_fifo peripheral but without the
 * unnecessary LocalLink adaptation layer.  A future enhancement may add
 * an optional integrated SGDMA controller.
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
 * Copyright (C) 2010 Lab X Technologies, LLC
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
#include "labx_ethernet.h"

DECLARE_GLOBAL_DATA_PTR;

#ifndef LABX_PRIMARY_ETH_BASEADDR
#  error "Missing board definition for Ethernet peripheral base address"
#endif

/* Ensure that there is a definition for the PHY address and MDIO divisor */
#ifndef LABX_ETHERNET_PHY_ADDR
#  error "Missing board definition of MDIO PHY address"
#endif
#ifndef LABX_ETHERNET_MDIO_DIV
#  error "Missing board definition of MDIO clock divisor"
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
#define VLAN_MASK_REG         (0x00000010)
#define MAC_SELECT_REG        (0x00000014)
#define MAC_CONTROL_REG       (0x00000018)
#define   MAC_ADDRESS_LOAD_ACTIVE 0x00000100
#define   MAC_ADDRESS_LOAD_LAST   0x00000200
#define MAC_LOAD_REG          (0x0000001C)
#define REVISION_REG          (0x0000003C)
#  define REVISION_MINOR_MASK  0x0000000F
#  define REVISION_MINOR_SHIFT 0
#  define REVISION_MAJOR_MASK  0x000000F0
#  define REVISION_MAJOR_SHIFT 4
#  define REVISION_MATCH_MASK  0x0000FF00
#  define REVISION_MATCH_SHIFT 8

/* Base address for registers internal to the MAC */
#define LABX_MAC_REGS_BASE    (0x00001000)

/* Base address for the Tx & Rx FIFOs */
#define LABX_FIFO_REGS_BASE   (0x00002000)

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

/* Maximum Ethernet MTU (with VLAN tag extension) */
#define ETHER_MTU		1520

/* PHY register definitions */
#define MII_CTL                 0x00    /* Basic mode control register */
#define MII_STAT                0x01    /* Basic phy status register */
#define MII_PHY_ID_HIGH         0x02    /* PHY ID High byte */
#define MII_PHY_ID_LOW          0x03    /* PHY ID Low byte */
#define MII_ADVERTISE           0x04    /* ANeg Advertise Register */
#define MII_PARTNER_ABILITY     0x05    /* Link Partner Ability register */
#define MII_1GBCTL              0x09    /* 1000BASE-T Control Register */
#define MII_1GBSTAT             0x0A    /* 1000BASE-T Status Register */
#define MII_BCM54XX_EXP_DATA    0x15    /* Expansion register data */
#define MII_BCM54XX_EXP_SEL     0x17    /* Expansion register select */
#define MII_AUXCTL              0x18    /* Auxilary Control Shadow register */
#define MII_AUXSTAT             0x19    /* Auxilary Status Summary register */
#define MII_SHADOW              0x1C    /* Extended Shadow Register */

/* Basic mode control register. */
#define PHY_CTL_RESV            0x003f  /* Unused...                   */
#define PHY_CTL_SPEEDMSB        0x0040  /* MSB of Speed Select         */
#define PHY_CTL_CTST            0x0080  /* Collision test              */
#define PHY_CTL_FULLDPLX        0x0100  /* Full duplex                 */
#define PHY_CTL_ANRESTART       0x0200  /* Auto negotiation restart    */
#define PHY_CTL_ISOLATE         0x0400  /* Disconnect DP83840 from MII */
#define PHY_CTL_PDOWN           0x0800  /* Powerdown the DP83840       */
#define PHY_CTL_ANENABLE        0x1000  /* Enable auto negotiation     */
#define PHY_CTL_SPEEDLSB        0x2000  /* LSB of Speed Select         */
#define PHY_CTL_LOOPBACK        0x4000  /* TXD loopback bits           */
#define PHY_CTL_RESET           0x8000  /* Reset the phy               */
#define PHY_CTL_SPEED10    0
#define PHY_CTL_SPEED100   PHY_CTL_SPEEDLSB
#define PHY_CTL_SPEED1000  PHY_CTL_SPEEDMSB

/* Basic phy status register */
#define PHY_STAT_EXTENDED_CAP   0x0001  /* Extended register capable   */
#define PHY_STAT_JABBER_DET     0x0002  /* Jabber condition detected   */
#define PHY_STAT_LINK_UP        0x0004  /* Link status is "up"         */
#define PHY_STAT_ANEG_CAP       0x0008  /* Auto-negotiation capable    */
#define PHY_STAT_REM_FAULT_DET  0x0010  /* Remote fault detected       */
#define PHY_STAT_ANEG_COMPLETE  0x0020  /* Auto-negotiation complete   */
#define PHY_STAT_PRE_SUP_CAP    0x0040  /* Management frame preamble suppression capable */
#define PHY_STAT_EXT_STAT_CAP   0x0100  /* Extended status in Register 0xF */
#define PHY_STAT_100BTH_CAP     0x0200  /* 100Base-T HDX capable       */
#define PHY_STAT_100BTF_CAP     0x0400  /* 100Base-T FDX capable       */
#define PHY_STAT_10BTH_CAP      0x0800  /* 10Base-T HDX capable        */
#define PHY_STAT_10BTF_CAP      0x1000  /* 10Base-T FDX capable        */
#define PHY_STAT_100BXH_CAP     0x2000  /* 100Base-X HDX capable       */
#define PHY_STAT_100BXF_CAP     0x4000  /* 100Base-X FDX capable       */
#define PHY_STAT_100BT4_CAP     0x8000  /* 100Base-T4 capable          */

/* Advertisement control register. */
#define PHY_ADVERT_SLCT         0x001f  /* Selector bits               */
#define PHY_ADVERT_CSMA         0x0001  /* Only selector supported     */
#define PHY_ADVERT_10BTH        0x0020  /* Try for 10mbps half-duplex  */
#define PHY_ADVERT_10BTF        0x0040  /* Try for 10mbps full-duplex  */
#define PHY_ADVERT_100BTH       0x0080  /* Try for 100mbps half-duplex */
#define PHY_ADVERT_100BTF       0x0100  /* Try for 100mbps full-duplex */
#define PHY_ADVERT_100BT4       0x0200  /* Try for 100mbps 4k packets  */
#define PHY_ADVERT_PAUSE_CAP    0x0400  /* Try for pause               */
#define PHY_ADVERT_PAUSE_ASYM   0x0800  /* Try for asymetric pause     */
#define PHY_ADVERT_RESV         0x1000  /* Unused...                   */
#define PHY_ADVERT_RFAULT       0x2000  /* Say we can detect faults    */
#define PHY_ADVERT_RESV2        0x4000  /* Unused...                   */
#define PHY_ADVERT_NPAGE        0x8000  /* Next page bit               */

/* Link partner capability register. */
#define PHY_PARTNER_CAP_SLCT       0x001f  /* Selector bits                   */
#define PHY_PARTNER_CAP_CSMA       0x0001  /* Only selector accepted          */
#define PHY_PARTNER_CAP_10BTH      0x0020  /* Partner has 10mbps half-duplex  */
#define PHY_PARTNER_CAP_10BTF      0x0040  /* Partner has 10mbps full-duplex  */
#define PHY_PARTNER_CAP_100BTH     0x0080  /* Partner has 100mbps half-duplex */
#define PHY_PARTNER_CAP_100BTF     0x0100  /* Partner has 100mbps full-duplex */
#define PHY_PARTNER_CAP_100BT4     0x0200  /* Partner has 100mbps 4k packets  */
#define PHY_PARTNER_CAP_PAUSE_CAP  0x0400  /* Partner has pause               */
#define PHY_PARTNER_CAP_PAUSE_ASYM 0x0800  /* Partner has asymetric pause     */
#define PHY_PARTNER_CAP_RESV       0x1000  /* Unused...                       */
#define PHY_PARTNER_DET_RFAULT     0x2000  /* Link partner detected fault     */
#define PHY_PARTNER_RECD_LINK      0x4000  /* Link partner received link code word */
#define PHY_PARTNER_CAP_NPAGE      0x8000  /* Link partner has Next Page cap  */

/* Auxilary Status Summary register */
#define MII_AUXSTAT_ANEG_COMPLETE  0x8000
#define MII_AUXSTAT_ANEG_COMPL_ACK 0x4000
#define MII_AUXSTAT_ANEG_ACK_DET   0x2000
#define MII_AUXSTAT_ABILITY_DET    0x1000
#define MII_AUXSTAT_NEXT_PAGE_WAIT 0x0800
#define MII_AUXSTAT_PAR_DET_FAULT  0x0080
#define MII_AUXSTAT_REMOTE_FAULT   0x0040
#define MII_AUXSTAT_ANEG_PG_RECD   0x0020
#define MII_AUXSTAT_ANEG_PARTNER_CAP 0x0010
#define MII_AUXSTAT_NXT_PAGE_PARTNER_CAP 0x0008

/* 1000BASE-T Control register */
#define PHY_1000BT_TEST_MODE_MASK  0xE000  /* Test mode mask                  */
#define PHY_1000BT_MASTER_ENA      0x1000  /* Enable master / slave manual config */
#define PHY_1000BT_MASTER_CONFIG   0x0800  /* PHY configured as master (not slave) */
#define PHY_1000BT_SWITCH          0x0400  /* Repeater / Switch device port    */
#define PHY_1000BT_ADVERT_FULL     0x0200  /* Advertise 1000BASE-T full duplex */
#define PHY_1000BT_ADVERT_FULL     0x0200  /* Advertise 1000BASE-T full duplex */

/* 1000BASE-T Status Register */
#define PHY_1000BT_IDLE_ERRCT_MASK 0x00FF  /* Number of idle errors since last read */
#define PHY_PARTNER_CAP_1000BTH    0x0400  /* Partner has 1000mbps half-duplex */
#define PHY_PARTNER_CAP_1000BTF    0x0800  /* Partner has 1000mbps full-duplex */
#define PHY_1000BT_REMOTE_RECVR_OK 0x1000  /* Remote receiver OK                */
#define PHY_1000BT_LOCAL_RECVR_OK  0x2000  /* Local receiver OK                 */
#define PHY_1000BT_LOCAL_MASTER    0x4000  /* Local transmitter is master       */
#define PHY_1000BT_CONFIG_FAULT    0x8000  /* Master / slave config fault detect */

/* Expansion register select */
#define MII_EXP_ENABLE         0x0F00
#define MII_EXP_SHADOW_MASK    0xFF
#define MII_EXP_RXTX_PKTCOUNT  0
#define MII_EXP_INTSTAT        1
#define MII_EXP_MCLEDSELECT    4
#define MII_EXP_MCLEDFLASH     5
#define MII_EXP_MCLEDBLINK     6

/* Auxilary Control Shadow register */
#define MII_AUX_SHADOW_MASK 7
#define MII_AUX_SHADOW_READSHIFT 12
#define MII_AUX_SELECT_AUXCTL   0
#define MII_AUX_SELECT_10BTREG  1
#define MII_AUX_SELECT_POWERCTL 2
#define MII_AUX_SELECT_MISCTEST 4
#define MII_AUX_SELECT_MISCCTL  7

/* Extended Shadow Register */
#define MII_BCM54XX_SHD_WRITE   0x8000
#define MII_BCM54XX_SHD_VAL(x)  ((x & 0x1f) << 10)
#define MII_BCM54XX_SHD_DATA(x) ((x & 0x3ff) << 0)
#define MII_SHD_SPARECTL1    0x02
#define MII_SHD_CLKALIGN     0x03
#define MII_SHD_SPARECTL2    0x04
#define MII_SHD_SPARECTL3    0x05
#define MII_SHD_LEDSTAT      0x08
#define MII_SHD_LEDCTL       0x09
#define MII_SHD_AUTOPD       0x0A
#define MII_SHD_LEDSEL1      0x0D
#define MII_SHD_LEDSEL2      0x0E
#define MII_SHD_LEDGPIO      0x0F
#define MII_SHD_SERDESCTL    0x13
#define MII_SHD_SGMIISLV     0x15
#define MII_SHD_SGMIIMED     0x18
#define MII_SHD_ANDEBUG      0x1A
#define MII_SHD_AUX1000CTL   0x1B
#define MII_SHD_AUX1000STAT  0x1C
#define MII_SHD_MISC1000STAT 0x1D
#define MII_SHD_CFAUTODET    0x1E
#define MII_SHD_MODECTL      0x1F

/* Upper ID for BCM548x parts */
#define BCM548x_ID_HIGH 0x0143
#define BCM548x_ID_LOW_MASK 0xFFF0

/* Special stuff for MV88E1116R parts */
#define MV881116R_ID_HIGH               0x0141
#define MV881116R_ID_LOW                0x0E40
#define MV881116R_ID_LOW_MASK           0xFFF0
#define MV88E1116_LED_FCTRL_REG		0x000A
#define MV88E1116_COPPER_SPF_REG1       0x0011
#define MV88E1116_MAC_CTRL_REG		0x0015
#define MV88E1116_PGADR_REG		0x0016
#define MV88E1116_RGMII_TXTM_CTRL	0x0010
#define MV88E1116_RGMII_RXTM_CTRL	0x0020
#define MV88E1116_LED_LINK_ACT  	0x021E

/* Special stuff for setting up a BCM5481.  Note that LS nibble of low ID
 * is revision number, and will vary.
 */
#define BCM5481_ID_LOW 0xBCA0
#define BCM5481_RX_SKEW_REGISTER_SEL         0x7007
#define BCM5481_RX_SKEW_ENABLE               0x0100
#define BCM5481_CLOCK_ALIGNMENT_REGISTER_SEL 0x0C00
#define BCM5481_SHADOW_WRITE                 0x8000
#define BCM5481_SPDSEL_LSB                   0x2000
#define BCM5481_SPDSEL_MSB                   0x0040
#define BCM5481_DUPLEX_MODE                  0x0100
#define BCM5481_XMIT_CLOCK_DELAY             0x0200
#define BCM5481_ADV_1000BASE_T_FDX_CAP       0x0200
#define BCM5481_ADV_1000BASE_T_HDX_CAP       0x0100
#define BCM5481_ADV_100BASE_T_FDX_CAP        0x0100
#define BCM5481_ADV_100BASE_T_HDX_CAP        0x0080
#define BCM5481_ADV_100BASE_T4_CAP           0x0200
#define BCM5481_AUTO_NEGOTIATE_ENABLE        0x1000
#define BCM5481_HIGH_PERFORMANCE_ENABLE      0x0040
#define BCM5481_HPE_REGISTER_SELECT          0x0002
#define BCM5481_MII_SHD_MODECTL_RESERVED     0x0008 /* "reserved" bit, write 1 always */
#define BCM5481_AUXCTL_TRANSMIT_NORMAL       0x0400
#define BCM5481_MISCCTL_WRITE_ENA            0x8000
#define BCM5481_MISCCTL_SELECT_MISCCTL       0x7000
#define BCM5481_MISCCTL_RX_PACKET_CTR        0x0800
#define BCM5481_MISCCTL_FORCE_AUTO_MDIX      0x0200
#define BCM5481_MISCCTL_RGMII_RXC_DELAYED    0x0100
#define BCM5481_MISCCTL_RGMII_RX_DV          0x0040
#define BCM5481_MISCCTL_RGMII_OOB_STATUS_DIS 0x0020
#define BCM5481_MISCCTL_ETHERNET_AT_WIRE_SPD 0x0010
#define BCM5481_MISCCTL_MDIO_ALL_PHY         0x0008

/* Special stuff for setting up a BCM5482.  Not that LS nibble of low ID
 * is revision number, and will vary.
 */
#define BCM5482_ID_LOW 0xBCB0

#define MII_BCM54XX_EXP_SEL_SSD 0x0e00  /* Secondary SerDes select */
#define MII_BCM54XX_EXP_SEL_ER  0x0f00  /* Expansion register select */

/* As mentioned above, the Lab X Ethernet hardware mimics the
 * Xilinx LocalLink FIFO peripheral
 */
typedef struct ll_fifo_s {
  int isr;  /* Interrupt Status Register 0x0 */
  int ier;  /* Interrupt Enable Register 0x4 */
  int tdfr; /* Transmit data FIFO reset 0x8 */
  int tdfv; /* Transmit data FIFO Vacancy 0xC */
  int tdfd; /* Transmit data FIFO 32bit wide data write port 0x10 */
  int tlf;  /* Write Transmit Length FIFO 0x14 */
  int rdfr; /* Read Receive data FIFO reset 0x18 */
  int rdfo; /* Receive data FIFO Occupancy 0x1C */
  int rdfd; /* Read Receive data FIFO 32bit wide data read port 0x20 */
  int rlf;  /* Read Receive Length FIFO 0x24 */
} ll_fifo_s;

/* Masks, etc. for use with the register file */
#define RLF_MASK 0x000007FF

/* Interrupt status register mnemonics */
#define FIFO_ISR_RPURE  0x80000000
#define FIFO_ISR_RPORE  0x40000000
#define FIFO_ISR_RPUE   0x20000000
#  define FIFO_ISR_RX_ERR (FIFO_ISR_RPURE | FIFO_ISR_RPORE | FIFO_ISR_RPUE)
#define FIFO_ISR_TPOE   0x10000000
#define FIFO_ISR_TC     0x08000000
#define FIFO_ISR_RC     0x04000000
#  define FIFO_ISR_ALL     0xFC000000

/* "Magic" value for FIFO reset operations, and timeout, in msec */
#define FIFO_RESET_MAGIC    0x000000A5
#define FIFO_RESET_TIMEOUT  500

/* Locate the FIFO structure offset from the Ethernet base address */
ll_fifo_s *ll_fifo = (ll_fifo_s *) (LABX_PRIMARY_ETH_BASEADDR + LABX_FIFO_REGS_BASE);

#if !defined(CONFIG_NET_MULTI)
static struct eth_device *xps_ll_dev = NULL;
#endif

struct labx_eth_private {
  int idx;
  unsigned char dev_addr[6];
};

/* Private data instance to use for the single instance */
static struct labx_eth_private priv_data = {
  .idx      = 0,
  .dev_addr = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00},  
};

/* Performs a register write to a PHY */
static void write_phy_register(unsigned long int phy_addr, unsigned int reg_addr, unsigned int phy_data)
{
  unsigned int addr;

  /* Write the data first, then the control register */
  addr = (LABX_MDIO_ETH_BASEADDR + MDIO_DATA_REG);
  *((volatile unsigned int *) addr) = phy_data;
  addr = (LABX_MDIO_ETH_BASEADDR + MDIO_CONTROL_REG);
  *((volatile unsigned int *) addr) = 
    (PHY_MDIO_WRITE | ((phy_addr & PHY_ADDR_MASK) << PHY_ADDR_SHIFT) |
     (reg_addr & PHY_REG_ADDR_MASK));

  /* Wait for the MDIO unit to go busy, then idle again */
  while((*((volatile uint32_t *) addr) & PHY_MDIO_BUSY) == 0);
  while((*((volatile uint32_t *) addr) & PHY_MDIO_BUSY) != 0);
}

/* Performs a register read from a PHY */
static unsigned int read_phy_register(unsigned long int phy_addr, unsigned int reg_addr)
{
  unsigned int addr;
  unsigned int readValue;

  /* Write to the MDIO control register to initiate the read */
  addr = (LABX_MDIO_ETH_BASEADDR + MDIO_CONTROL_REG);

  *((volatile unsigned int *) addr) = 
    (PHY_MDIO_READ | ((phy_addr & PHY_ADDR_MASK) << PHY_ADDR_SHIFT) |
     (reg_addr & PHY_REG_ADDR_MASK));

  /* Wait for the MDIO unit to go busy, then idle again */
  while((*((volatile uint32_t *) addr) & PHY_MDIO_BUSY) == 0);
  while((*((volatile uint32_t *) addr) & PHY_MDIO_BUSY) != 0);

  addr = (LABX_MDIO_ETH_BASEADDR + MDIO_DATA_REG);
  readValue = *((volatile unsigned int *) addr);
  return(readValue);
}

/*
 * Indirect register access functions for the 1000BASE-T/100BASE-TX/10BASE-T
 * 0x1c shadow registers.
 */
inline unsigned int bcm54xx_shadow_read(unsigned long int phy_addr, unsigned int shadow)
{
  write_phy_register(phy_addr, MII_SHADOW, MII_BCM54XX_SHD_VAL(shadow));
  return MII_BCM54XX_SHD_DATA(read_phy_register(phy_addr, MII_SHADOW));
}

inline void bcm54xx_shadow_write(unsigned long int phy_addr, unsigned int shadow, unsigned int phy_data)
{
  write_phy_register(phy_addr, MII_SHADOW, MII_BCM54XX_SHD_WRITE |
      MII_BCM54XX_SHD_VAL(shadow) | MII_BCM54XX_SHD_DATA(phy_data));
}

inline unsigned int mv88e11x_page_read(unsigned long int phy_addr, unsigned int page, unsigned int reg_addr) 
{
  write_phy_register(phy_addr, MV88E1116_PGADR_REG, page);
  return(read_phy_register(phy_addr, reg_addr));
}

inline void mv88e11x_page_write(unsigned long int phy_addr, unsigned int page, unsigned int reg_addr, unsigned int phy_data) 
{
  write_phy_register(phy_addr, MV88E1116_PGADR_REG, page);
  write_phy_register(phy_addr, reg_addr, phy_data);
}

/* Indirect register access functions for the 0x17 Expansion Registers */
inline unsigned int bxm54xx_exp_read(unsigned long int phy_addr, unsigned int shadow)
{
  unsigned int val;
  write_phy_register(phy_addr, MII_BCM54XX_EXP_SEL, MII_EXP_ENABLE | (shadow & MII_EXP_SHADOW_MASK));
  val = read_phy_register(phy_addr, MII_BCM54XX_EXP_DATA);
  write_phy_register(phy_addr, MII_BCM54XX_EXP_SEL, 0);  /* Restore default value.  It's O.K. if this write fails */
  return val;
}

inline void bcm54xx_exp_write(unsigned long int phy_addr, unsigned int shadow, unsigned int phy_data)
{
  write_phy_register(phy_addr, MII_BCM54XX_EXP_SEL, MII_EXP_ENABLE | (shadow & MII_EXP_SHADOW_MASK));
  write_phy_register(phy_addr, MII_BCM54XX_EXP_DATA, phy_data);
  write_phy_register(phy_addr, MII_BCM54XX_EXP_SEL, 0);  /* Restore default value.  It's O.K. if this write fails. */
}

inline unsigned int bcm54xx_aux_read(unsigned long int phy_addr, unsigned int shadow)
{
  unsigned int addr;
  addr = ((shadow << MII_AUX_SHADOW_READSHIFT) | MII_AUX_SELECT_MISCCTL);
  write_phy_register(phy_addr, MII_AUXCTL, addr);
  return(read_phy_register(phy_addr, MII_AUXCTL));
}

inline void bcm54xx_aux_write(unsigned long int phy_addr, unsigned int shadow, unsigned int phy_data)
{
  unsigned int reg;
  reg = (phy_data & ~MII_AUX_SHADOW_MASK) | shadow;
  write_phy_register(phy_addr, MII_AUXCTL, reg);
}

/* Writes a value to a MAC register */
static void labx_eth_write_mac_reg(int reg_offset, int reg_data)
{
  //printf("REG %04X = %08X (was %08X)\n", reg_offset, reg_data, *(volatile unsigned int *)(LABX_PRIMARY_ETH_BASEADDR + reg_offset));
  /* Apply the register offset to the base address */
  *(volatile unsigned int *)(LABX_PRIMARY_ETH_BASEADDR + reg_offset) = reg_data;
}

/* Writes a value to the MDIO configuration register within the register
 * file of the controller being used for MDIO (may or may not be the same
 * as the primary!
 */
static void labx_eth_write_mdio_config(int config_data)
{
  /* Apply the MDIO register offset to the base address */
  *(volatile unsigned int *)(LABX_MDIO_ETH_BASEADDR + MAC_MDIO_CONFIG_REG) = config_data;
}

/* Reads a value from a MAC register */
int labx_eth_read_mac_reg(int reg_offset)
{
  unsigned int val = *(volatile unsigned int *)(LABX_PRIMARY_ETH_BASEADDR + reg_offset);
  return(val);
}

void mdelay(unsigned int msec)
{
  while(msec--)
    udelay(1000);
}

static unsigned long int phy_addr = LABX_ETHERNET_PHY_ADDR;
static int link = 0;
static int first = 1;

/* setting ll_temac and phy to proper setting */
static int labx_eth_phy_ctrl(void)
{
  unsigned int result;
  unsigned int readback;
  unsigned int id_high;
  unsigned id_low;
  int rc;

  /* Read and report the PHY */
  id_high = read_phy_register(phy_addr, MII_PHY_ID_HIGH);
  id_low = read_phy_register(phy_addr, MII_PHY_ID_LOW);

  if(first) {
    printf("PHY ID at address 0x%02X: 0x%04X%04X\n", phy_addr, id_high, id_low);
    if (id_high == BCM548x_ID_HIGH) // General BCM54xx identifier
    {
        if ((id_low & BCM548x_ID_LOW_MASK) == BCM5481_ID_LOW) { // Special stuff for BCM5481
            printf("BCM5481 PHY setup\n");
            write_phy_register(phy_addr, MII_AUXCTL, BCM5481_RX_SKEW_REGISTER_SEL);
            result = read_phy_register(phy_addr, MII_AUXCTL);
            write_phy_register(phy_addr, MII_AUXCTL,
                    result | BCM5481_SHADOW_WRITE | BCM5481_RX_SKEW_REGISTER_SEL | BCM5481_RX_SKEW_ENABLE);
            write_phy_register(phy_addr, MII_AUXCTL, BCM5481_RX_SKEW_REGISTER_SEL);
            printf("RGMII Receive Clock Skew: %d (0x%04X) => %d\n",
                    ((result & BCM5481_RX_SKEW_ENABLE) != 0), result,
                    ((read_phy_register(phy_addr, MII_AUXCTL)& BCM5481_RX_SKEW_ENABLE) != 0));
            result = read_phy_register(phy_addr, MII_CTL);
            result = (result | (BCM5481_AUTO_NEGOTIATE_ENABLE | BCM5481_DUPLEX_MODE |
    	    	BCM5481_SPDSEL_MSB)) & ~BCM5481_SPDSEL_LSB; // Auto-negotiate, full duplex, and 1000 Mbps
            write_phy_register(phy_addr, MII_CTL, result);
            readback = read_phy_register(phy_addr, MII_CTL);
            printf("Auto-Negotiate Enable: %d (0x%04X) => %d, Duplex Mode %d => %d\n",
    	    	((result & BCM5481_AUTO_NEGOTIATE_ENABLE) != 0), result,
    	    	((readback & BCM5481_AUTO_NEGOTIATE_ENABLE) != 0),
    	    	((result & BCM5481_DUPLEX_MODE) != 0),
    	    	((readback & BCM5481_DUPLEX_MODE) != 0));
            result = read_phy_register(phy_addr, MII_1GBCTL);
            result = (result & ~BCM5481_ADV_1000BASE_T_HDX_CAP) | BCM5481_ADV_1000BASE_T_FDX_CAP;
            write_phy_register(phy_addr, MII_1GBCTL, result);
            readback = read_phy_register(phy_addr, MII_1GBCTL);
            printf("Advertise Half Duplex: %d (0x%04X) => %d, Advertise Full Duplex %d => %d\n",
    	    	((result & BCM5481_ADV_1000BASE_T_HDX_CAP) != 0), result,
    	    	((readback & BCM5481_ADV_1000BASE_T_HDX_CAP) != 0),
    	    	((result & BCM5481_ADV_1000BASE_T_FDX_CAP) != 0),
    	    	((readback & BCM5481_ADV_1000BASE_T_FDX_CAP) != 0));

        } else if ((id_low & BCM548x_ID_LOW_MASK) == BCM5482_ID_LOW) { // Special stuff for BCM5482
            printf("BCM5482 PHY setup\n");
            bcm54xx_shadow_write(phy_addr, MII_SHD_MODECTL,
            	BCM5481_MII_SHD_MODECTL_RESERVED); /* Copper only, not fiber */
            write_phy_register(phy_addr, MII_ADVERTISE, PHY_ADVERT_PAUSE_ASYM |
            	PHY_ADVERT_PAUSE_CAP | PHY_ADVERT_100BTF | PHY_ADVERT_CSMA);
            write_phy_register(phy_addr, MII_1GBCTL, PHY_1000BT_ADVERT_FULL);
            bcm54xx_aux_write(phy_addr, MII_AUX_SELECT_AUXCTL, BCM5481_AUXCTL_TRANSMIT_NORMAL);
            bcm54xx_aux_write(phy_addr, MII_AUX_SELECT_MISCCTL,
                bcm54xx_aux_read(phy_addr, MII_AUX_SELECT_MISCCTL) | BCM5481_RX_SKEW_ENABLE);
            write_phy_register(phy_addr, MII_CTL, PHY_CTL_SPEED1000 | PHY_CTL_ANENABLE | PHY_CTL_ANRESTART | PHY_CTL_FULLDPLX);
        }
    	/* RGMII Transmit Clock Delay: The RGMII transmit timing can be adjusted
		 * by software control. TXD-to-GTXCLK clock delay time can be increased
		 * by approximately 1.9 ns for 1000BASE-T mode, and between 2 ns to 6 ns
		 * when in 10BASE-T or 100BASE-T mode by setting Register 1ch, SV 00011,
		 * bit 9 = 1. Enabling this timing adjustment eliminates the need for
		 * board trace delays as required by the RGMII specification.
    	 */
    	result = bcm54xx_shadow_read(phy_addr, MII_SHD_CLKALIGN);
    	bcm54xx_shadow_write(phy_addr, MII_SHD_CLKALIGN, result | BCM5481_XMIT_CLOCK_DELAY);
    	printf("RGMII Transmit Clock Delay: %d (0x%04X) => %d\n",
    			((result & BCM5481_XMIT_CLOCK_DELAY) != 0), result,
    			((bcm54xx_shadow_read(phy_addr, MII_SHD_CLKALIGN) & BCM5481_XMIT_CLOCK_DELAY) != 0));
    }
    else if (id_high == MV881116R_ID_HIGH && (id_low & MV881116R_ID_LOW_MASK) == MV881116R_ID_LOW) 
    {
        printf("88E1116 PHY setup\n");

        write_phy_register(phy_addr, MII_CTL, PHY_CTL_RESET);
        while ((read_phy_register(phy_addr, MII_CTL) & PHY_CTL_RESET) != 0) {
	        mdelay(10);
        }

        write_phy_register(phy_addr, MII_CTL, PHY_CTL_SPEED1000 | PHY_CTL_ANENABLE | PHY_CTL_ANRESTART | PHY_CTL_FULLDPLX);
        write_phy_register(phy_addr, MII_ADVERTISE, PHY_ADVERT_PAUSE_ASYM |
                PHY_ADVERT_PAUSE_CAP | PHY_ADVERT_100BTF | PHY_ADVERT_CSMA);
        write_phy_register(phy_addr, MII_1GBCTL, PHY_1000BT_ADVERT_FULL);


        result = mv88e11x_page_read(phy_addr, 0x02, MV88E1116_MAC_CTRL_REG);
	mv88e11x_page_write(phy_addr, 0x02, MV88E1116_MAC_CTRL_REG, result | MV88E1116_RGMII_RXTM_CTRL | MV88E1116_RGMII_TXTM_CTRL);                         

	/* Adjust LED Control */
	mv88e11x_page_write(phy_addr, 0x03, MV88E1116_LED_FCTRL_REG, MV88E1116_LED_LINK_ACT);                              

        result = read_phy_register(phy_addr, MII_CTL);
        write_phy_register(phy_addr, MII_CTL, PHY_CTL_RESET | result);
        while ((read_phy_register(phy_addr, MII_CTL) & PHY_CTL_RESET) != 0) {
	        mdelay(10);
        }
        //printf("88E1116 PHY setup complete\n");
    }
  }

  if(!link) {
    int timeout = 50;
    /* Requery the PHY general status register */
    /* Wait up to 5 secs for a link */
    while(timeout--) {
      result = read_phy_register(phy_addr, MII_STAT);
      if((result & PHY_STAT_LINK_UP) != 0) {
        printf("Link up!\n");
        break;
      }
      mdelay(100);
    }
  }

  result = read_phy_register(phy_addr, MII_STAT);
  if((result & PHY_STAT_LINK_UP) == 0) {
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

#ifdef STANDARD_MII_LINK_REGS

  if((read_phy_register(phy_addr, MII_1GBSTAT) & 0x0800) != 0 &&
      (read_phy_register(phy_addr, MII_1GBCTL) & 0x0200) != 0) {
    labx_eth_write_mac_reg(MAC_SPEED_SELECT_REG, MAC_SPEED_1_GBPS);
    printf("1000BASE-T/FD\n");
    rc = 1;
  } else if((read_phy_register(phy_addr, MII_PARTNER_ABILITY) & 0x0100) != 0 &&
      (read_phy_register(phy_addr, MII_ADVERTISE) & 0x0100) != 0) {
    labx_eth_write_mac_reg(MAC_SPEED_SELECT_REG, MAC_SPEED_100_MBPS);
    printf("100BASE-T/FD\n");
    rc = 1;
  } else if((read_phy_register(phy_addr, MII_PARTNER_ABILITY) & 0x0040) != 0 &&
      (read_phy_register(phy_addr, MII_ADVERTISE) & 0x0040) != 0) {
    labx_eth_write_mac_reg(MAC_SPEED_SELECT_REG, MAC_SPEED_10_MBPS);
    printf("10BASE-T/FD\n");
    rc = 1;
  } else {
    printf("Half Duplex not supported\n");
    rc = 0;
  }

#else
  
  if (id_high == MV881116R_ID_HIGH && (id_low & MV881116R_ID_LOW_MASK) == MV881116R_ID_LOW) 
  { 
    result = mv88e11x_page_read(phy_addr, 0x00, MV88E1116_COPPER_SPF_REG1);
    //printf("Result: 0x%04X\n", result);
  }
  else 
  {
    result = read_phy_register(phy_addr, MII_AUXSTAT);
    printf("AUXSTAT = 0x%04X\n", result);
  }

    switch(result & MII_ANEG_LINK_MASK)
    {
      case MII_ANEG_1000BTF:
        labx_eth_write_mac_reg(MAC_SPEED_SELECT_REG, MAC_SPEED_1_GBPS);
        printf("1000BASE-T/Full Duplex link detected\n");
        rc = 1;
        break;
      case MII_ANEG_1000BTH:
        printf("1000BASE-T/Half Duplex link not supported\n");
        rc = 0;
        break;
      case MII_ANEG_100BTF:
        labx_eth_write_mac_reg(MAC_SPEED_SELECT_REG, MAC_SPEED_100_MBPS);
        printf("100BASE-T/Full Duplex link detected\n");
        rc = 1;
        break;
      case MII_ANEG_100BT4:
        labx_eth_write_mac_reg(MAC_SPEED_SELECT_REG, MAC_SPEED_100_MBPS);
        printf("100BASE-T4 link detected\n");
        rc = 0;
        break;
      case MII_ANEG_100BTH:
        printf("100BASE-T/Half Duplex link not supported\n");
        rc = 0;
        break;
      case MII_ANEG_10BTF:
        labx_eth_write_mac_reg(MAC_SPEED_SELECT_REG, MAC_SPEED_10_MBPS);
        printf("10BASE-T/Full Duplex link detected\n");
        rc = 1;
        break;
      case MII_ANEG_10BTH:
        printf("10BASE-T/Half Duplex link not supported\n");
        rc = 0;
        break;
      case MII_ANEG_NO_LINK:
      default:
        printf("No link detected!\n");
        rc = 0;
        break;
    }

  if(first) {
  /* Reset transmit and receive MAC logic after the link speed changes */
    labx_eth_write_mac_reg(MAC_RX_CONFIG_REG, RX_SOFT_RESET);
    labx_eth_write_mac_reg(MAC_TX_CONFIG_REG, TX_SOFT_RESET);
  }

#endif

  return rc;
}

/* Rx buffer is also used by FIFO mode */
static unsigned char rx_buffer[ETHER_MTU] __attribute((aligned(32)));



void debugll(int count)
{
  printf ("%d fifo isr 0x%08x, fifo_ier 0x%08x, fifo_rdfr 0x%08x, fifo_rdfo 0x%08x fifo_rlr 0x%08x\n",count, ll_fifo->isr, \
	  ll_fifo->ier, ll_fifo->rdfr, ll_fifo->rdfo, ll_fifo->rlf);
}


static int labx_eth_send_fifo(unsigned char *buffer, int length)
{
  unsigned int *buf = (unsigned int*) buffer;
  unsigned int len, i, val;

  len = ((length + 3) / 4);

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

  if (ll_fifo->isr & FIFO_ISR_RC) {
    /* One or more packets have been received.  Check the read occupancy register
     * to see how much data is ready to be processed.
     */
    len = ll_fifo->rlf & RLF_MASK;
    len2 = ((len + 3) / 4);

    for (i = 0; i < len2; i++) {
      val = ll_fifo->rdfd;
      *buf++ = val ;
    }

    /* Re-check the occupancy register; if there are still FIFO contents
     * remaining, they are for valid packets.  Not sure if it's okay to call
     * NetReceive() more than once for each invocation, so we'll just leave
     * the ISR flag set instead and let the NetLoop invoke us again.
     */
    if(ll_fifo->rdfo == 0) ll_fifo->isr = FIFO_ISR_RC;

    /* Enqueue the received packet! */
    NetReceive (&rx_buffer[0], len);
  } else if(ll_fifo->isr & FIFO_ISR_RX_ERR) {
    printf("Rx error 0x%08X\n", ll_fifo->isr);

    /* A receiver error has occurred, reset the Rx logic */
    ll_fifo->isr = FIFO_ISR_ALL;
    ll_fifo->rdfr = FIFO_RESET_MAGIC;
  }

    return 0;
}

/* FOO */

#define MAC_MATCH_NONE 0
#define MAC_MATCH_ALL 1

static const u8 MAC_BROADCAST[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
static const u8 MAC_ZERO[6] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

#define NUM_SRL16E_CONFIG_WORDS 8
#define NUM_SRL16E_INSTANCES    12

/* Busy loops until the match unit configuration logic is idle.  The hardware goes 
 * idle very quickly and deterministically after a configuration word is written, 
 * so this should not consume very much time at all.
 */
static void wait_match_config(void) {
  unsigned int addr;
  uint32_t statusWord;
  uint32_t timeout = 10000;

  addr = (LABX_PRIMARY_ETH_BASEADDR + MAC_CONTROL_REG);
  do {
    statusWord = *((volatile unsigned int *) addr);
    if (timeout-- == 0)
      {
        printf("labx_ethernet : wait_match_config timeout!\n");
        break;
      }
  } while(statusWord & MAC_ADDRESS_LOAD_ACTIVE);
}

/* Selects a set of match units for subsequent configuration loads */
typedef enum { SELECT_NONE, SELECT_SINGLE, SELECT_ALL } SelectionMode;
static void select_matchers(SelectionMode selectionMode,
                            uint32_t matchUnit) {
  unsigned int addr;

  addr = (LABX_PRIMARY_ETH_BASEADDR + MAC_SELECT_REG);

  switch(selectionMode) {
  case SELECT_NONE:
    /* De-select all the match units */
    //printk("MAC SELECT %08X\n", 0);
    *((volatile unsigned int *) addr) = 0x00000000;
    break;

  case SELECT_SINGLE:
    /* Select a single unit */
    //printk("MAC SELECT %08X\n", 1 << matchUnit);
    *((volatile unsigned int *) addr) = (1 << matchUnit);
    break;

  default:
    /* Select all match units at once */
    //printk("MAC SELECT %08X\n", 0xFFFFFFFF);
    *((volatile unsigned int *) addr) = 0xFFFFFFFF;
    break;
  }
}

/* Sets the loading mode for any selected match units.  This revolves around
 * automatically disabling the match units' outputs while they're being
 * configured so they don't fire false matches, and re-enabling them as their
 * last configuration word is loaded.
 */
typedef enum { LOADING_MORE_WORDS, LOADING_LAST_WORD } LoadingMode;
static void set_matcher_loading_mode(LoadingMode loadingMode) {
  unsigned int addr;
  uint32_t controlWord;

  addr = (LABX_PRIMARY_ETH_BASEADDR + MAC_CONTROL_REG);

  controlWord = *((volatile unsigned int *) addr);

  if(loadingMode == LOADING_MORE_WORDS) {
    /* Clear the "last word" bit to suppress false matches while the units are
     * only partially cleared out
     */
    controlWord &= ~MAC_ADDRESS_LOAD_LAST;
  } else {
    /* Loading the final word, flag the match unit(s) to enable after the
     * next configuration word is loaded.
     */
    controlWord |= MAC_ADDRESS_LOAD_LAST;
  }
  //printk("CONTROL WORD %08X\n", controlWord);

  *((volatile unsigned int *) addr) = controlWord;
}

/* Clears any selected match units, preventing them from matching any packets */
static void clear_selected_matchers(void) {
  unsigned int addr;
  uint32_t wordIndex;
  
  /* Ensure the unit(s) disable as the first word is load to prevent erronous
   * matches as the units become partially-cleared
   */
  set_matcher_loading_mode(LOADING_MORE_WORDS);

  for(wordIndex = 0; wordIndex < NUM_SRL16E_CONFIG_WORDS; wordIndex++) {
    /* Assert the "last word" flag on the last word required to complete the clearing
     * of the selected unit(s).
     */
    if(wordIndex == (NUM_SRL16E_CONFIG_WORDS - 1)) {
      set_matcher_loading_mode(LOADING_LAST_WORD);
    }

    //printk("MAC LOAD %08X\n", 0);
    addr = (LABX_PRIMARY_ETH_BASEADDR + MAC_LOAD_REG);
    *((volatile unsigned int *) addr) = 0x00000000;
  }
}

/* Loads truth tables into a match unit using the newest, "unified" match
 * architecture.  This is SRL16E based (not cascaded) due to the efficient
 * packing of these primitives into Xilinx LUT6-based architectures.
 */
static void load_unified_matcher(const uint8_t matchMac[6]) {
  unsigned int addr;
  int32_t wordIndex;
  int32_t lutIndex;
  uint32_t configWord = 0x00000000;
  uint32_t matchChunk;
  
  /* All local writes will be to the MAC filter load register */
  addr = (LABX_PRIMARY_ETH_BASEADDR + MAC_LOAD_REG);

  /* In this architecture, all of the SRL16Es are loaded in parallel, with each
   * configuration word supplying two bits to each.  Only one of the two bits can
   * ever be set, so there is just an explicit check for one.
   */
  for(wordIndex = (NUM_SRL16E_CONFIG_WORDS - 1); wordIndex >= 0; wordIndex--) {
    for(lutIndex = (NUM_SRL16E_INSTANCES - 1); lutIndex >= 0; lutIndex--) {
      matchChunk = ((matchMac[5-(lutIndex/2)] >> ((lutIndex&1) << 2)) & 0x0F);
      configWord <<= 2;
      if(matchChunk == (wordIndex << 1)) configWord |= 0x01;
      if(matchChunk == ((wordIndex << 1) + 1)) configWord |= 0x02;
    }
    /* 12 nybbles are packed to the MSB */
    configWord <<= 8;

    /* Two bits of truth table have been determined for each SRL16E, load the
     * word and wait for the configuration to occur.  Be sure to flag the last
     * word to automatically re-enable the match unit(s) as the last word completes.
     */
    if(wordIndex == 0) set_matcher_loading_mode(LOADING_LAST_WORD);
    //printk("MAC LOAD %08X\n", configWord);
    *((volatile unsigned int *) addr) = configWord;
    wait_match_config();
  }
}

static void configure_mac_filter(int unitNum, const u8 mac[6], int mode) {
  //printk("CONFIGURE MAC MATCH %d (%d), %02X:%02X:%02X:%02X:%02X:%02X\n", unitNum, mode,
  //	mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  /* Ascertain that the configuration logic is ready, then select the matcher */
  wait_match_config();
  select_matchers(SELECT_SINGLE, unitNum);

  if (mode == MAC_MATCH_NONE) {
    clear_selected_matchers();
  } else {
    /* Set the loading mode to disable as we load the first word */
    set_matcher_loading_mode(LOADING_MORE_WORDS);
      
    /* Calculate matching truth tables for the LUTs and load them */
    load_unified_matcher(mac);
  }

  /* De-select the match unit */
  select_matchers(SELECT_NONE, 0);
}

/* setup mac addr */
static int labx_eth_addr_setup(struct labx_eth_private * lp)
{
  unsigned int addr;
  uint32_t numMacFilters;
  char * env_p;
  char * end;
  int i;
  
  env_p = getenv("ethaddr");
  if (env_p == NULL) {
    printf("cannot get enviroment for \"ethaddr\".\n");
    return -1;
  }
  
  for (i = 0; i < 6; i++) {
    lp->dev_addr[i] = env_p ? simple_strtoul(env_p, &end, 16) : 0;
    if (env_p) env_p = (*end) ? end + 1 : end;
  }

  /* Determine how many MAC address filters the instance supports, as reported
   * by a field within the revision register
   */
  addr = (LABX_PRIMARY_ETH_BASEADDR + REVISION_REG);
  numMacFilters = ((*((volatile unsigned int *) addr) & REVISION_MATCH_MASK) >>
                   REVISION_MATCH_SHIFT);
  printf("Lab X Ethernet has %d MAC filters\n", numMacFilters);
  
  /* Configure for our unicast MAC address first, and the broadcast MAC
   * address second, provided there are enough MAC address filters.
   */
  if(numMacFilters >= 1) {
    configure_mac_filter(0, lp->dev_addr, MAC_MATCH_ALL);
  }

  if(numMacFilters >= 2) {
    configure_mac_filter(1, MAC_BROADCAST, MAC_MATCH_ALL);
  }
  
  return(0);
}

static void labx_eth_restart(void)
{
  /* Enable the receiver and transmitter */
  labx_eth_write_mac_reg(MAC_RX_CONFIG_REG, RX_ENABLE | RX_VLAN_TAGS_ENABLE);
  labx_eth_write_mac_reg(MAC_TX_CONFIG_REG, TX_ENABLE | TX_VLAN_TAGS_ENABLE);
}


static int labx_eth_init(struct eth_device *dev, bd_t *bis)
{
  struct labx_eth_private *lp = (struct labx_eth_private *)dev->priv;

  if(!first) {
    /* Short-circuit some of the setup; still return no error to permit
     * the client code to keep going
     */
    labx_eth_restart();
    labx_eth_phy_ctrl();
    return(0);
  }

  /* Clear ISR flags and reset both transmit and receive FIFO logic */
  ll_fifo->isr = FIFO_ISR_ALL;
  ll_fifo->tdfr = FIFO_RESET_MAGIC;
  ll_fifo->rdfr = FIFO_RESET_MAGIC;
  //	printf ("fifo isr 0x%08x, fifo_ier 0x%08x, fifo_tdfv 0x%08x, fifo_rdfo 0x%08x fifo_rlf 0x%08x\n", ll_fifo->isr, ll_fifo->ier, ll_fifo->tdfv, ll_fifo->rdfo,ll_fifo->rlf);

  /* Configure the MDIO divisor and enable the interface to the PHY.
   * XILINX_HARD_MAC Note: The hard MDIO controller must be configured or
   * the SGMII autonegotiation won't happen. Even if we never use that MDIO controller.
   * This operation must access the register file of the controller being used
   * for MDIO access, which may or may not be the same as that used for the actual
   * communications! 
   */
  labx_eth_write_mdio_config((LABX_ETHERNET_MDIO_DIV & MDIO_DIVISOR_MASK) |
			      MDIO_ENABLED);
  
  /* Set up the MAC address in the hardware */
  labx_eth_addr_setup(lp);

  /* Enable the receiver and transmitter */
  labx_eth_write_mac_reg(MAC_RX_CONFIG_REG, RX_ENABLE | RX_VLAN_TAGS_ENABLE);
  labx_eth_write_mac_reg(MAC_TX_CONFIG_REG, TX_ENABLE | TX_VLAN_TAGS_ENABLE);

  /* Configure the PHY */
  labx_eth_phy_ctrl();
  first = 0;

  return(0);
}

static void labx_eth_halt(struct eth_device *dev)
{
  labx_eth_write_mac_reg(MAC_RX_CONFIG_REG, RX_DISABLE);
  labx_eth_write_mac_reg(MAC_TX_CONFIG_REG, TX_DISABLE);
}

int labx_eth_send(struct eth_device *dev, volatile void *packet, int length)
{
  if(!link)
    if(labx_eth_phy_ctrl() == 0)
      return 0;

  return(labx_eth_send_fifo((unsigned char *)packet, length));
}

int labx_eth_recv(struct eth_device *dev)
{
  return labx_eth_recv_fifo();
}

int labx_eth_initialize(bd_t *bis)
{
  struct eth_device *dev;
  
  dev = malloc(sizeof(*dev));
  if (dev == NULL)
    hang();
  
  memset(dev, 0, sizeof(*dev));
  sprintf(dev->name, "Lab X Ethernet, eth%d", WHICH_ETH_PORT);
  dev->name[NAMESIZE - 1] = '\0';
  
  dev->iobase =  LABX_PRIMARY_ETH_BASEADDR;
  dev->priv   = &priv_data;
  dev->init   =  labx_eth_init;
  dev->halt   =  labx_eth_halt;
  dev->send   =  labx_eth_send;
  dev->recv   =  labx_eth_recv;
  
  eth_register(dev);
  
  return(0);
}

int do_dump_phy_reg(cmd_tbl_t *cmdtp, int flag, int argc, char *argv[])
{
  unsigned long int val;
  if (argc != 1) {
    cmd_usage(cmdtp);
    return 1;
  }

  if (first) {
    eth_init(gd->bd);
  }
  val = (unsigned long int)read_phy_register(phy_addr, MII_PHY_ID_HIGH) << 16 | read_phy_register(phy_addr, MII_PHY_ID_LOW);
  printf("Ethernet PHY registers for eth%d, ID 0x%08lx:\n"
      "==============================================\n", WHICH_ETH_PORT, val);
  val = read_phy_register(phy_addr, 0x00);
  printf("0x00 - MII Control register:                           0x%04lx\n", val);
  val = read_phy_register(phy_addr, MII_STAT);
  printf("0x01 - MII Status  register:                           0x%04lx\n", val);
  val = read_phy_register(phy_addr, MII_ADVERTISE);
  printf("0x04 - Auto-negotiation Advertisement register:        0x%04lx\n", val);
  val = read_phy_register(phy_addr, MII_PARTNER_ABILITY);
  printf("0x05 - Auto-negotiation Link Partner Ability register: 0x%04lx\n", val);
  val = read_phy_register(phy_addr, 0x06);
  printf("0x06 - Auto-negotiation Expansion register:            0x%04lx\n", val);
  val = read_phy_register(phy_addr, 0x07);
  printf("0x07 - Next page Transmit register:                    0x%04lx\n", val);
  val = read_phy_register(phy_addr, 0x08);
  printf("0x08 - Link Partner Received Next Page register:       0x%04lx\n", val);
  val = read_phy_register(phy_addr, MII_1GBCTL);
  printf("0x09 - 1000Base-T Control register:                    0x%04lx\n", val);
  val = read_phy_register(phy_addr, MII_1GBSTAT);
  printf("0x0A - 1000Base-T Status register:                     0x%04lx\n", val);
  val = read_phy_register(phy_addr, 0x0F);
  printf("0x0F - IEEE Extended Status register:                  0x%04lx\n", val);
#ifdef CONFIG_MV88E1116R 
  val = mv88e11x_page_read(phy_addr, 0x00, MV88E1116_COPPER_SPF_REG1);
  printf("0:0x10 - Copper Specific Control register 1:           0x%04lx\n", val);
  val = mv88e11x_page_read(phy_addr, 0x00, 0x11);
  printf("0:0x11 - Copper Specific Status register 1:            0x%04lx\n", val);
  val = mv88e11x_page_read(phy_addr, 0x00, 0x12);
  printf("0:0x12 - Interrupt Enable register - Copper:           0x%04lx\n", val);
  val = mv88e11x_page_read(phy_addr, 0x00, 0x13);
  printf("0:0x13 - Copper Specific Status register 2:            0x%04lx\n", val);
  val = mv88e11x_page_read(phy_addr, 0x00, 0x14);
  printf("0:0x14 - Copper Specific Control register 3:           0x%04lx\n", val);
  val = mv88e11x_page_read(phy_addr, 0x00, 0x15);
  printf("0:0x15 - Receive Error Counter register:               0x%04lx\n", val);
  val = mv88e11x_page_read(phy_addr, 0x00, 0x1A);
  printf("0:0x1A - Copper Specific Control register 2:           0x%04lx\n", val);
  val = mv88e11x_page_read(phy_addr, 0x02, 0x10);
  printf("2:0x10 - MAC Specific Control register 1:              0x%04lx\n", val);
  val = mv88e11x_page_read(phy_addr, 0x02, 0x12);
  printf("2:0x12 - Interupt Enable register - MAC:               0x%04lx\n", val);
  val = mv88e11x_page_read(phy_addr, 0x02, 0x13);
  printf("2:0x13 - MAC Specific Status register 2:               0x%04lx\n", val);
  val = mv88e11x_page_read(phy_addr, 0x02, 0x15);
  printf("2:0x15 - Control register - MAC:                       0x%04lx\n", val);
  val = mv88e11x_page_read(phy_addr, 0x02, 0x18);
  printf("2:0x18 - RGMII Output Imp Calib Override register:     0x%04lx\n", val);
  val = mv88e11x_page_read(phy_addr, 0x02, 0x19);
  printf("2:0x19 - RGMII Output Imp Target register:             0x%04lx\n", val);
  val = mv88e11x_page_read(phy_addr, 0x03, 0x10);
  printf("3:0x10 - LED[2:0] Function Control register:           0x%04lx\n", val);
  val = mv88e11x_page_read(phy_addr, 0x03, 0x11);
  printf("3:0x11 - LED[2:0] Polarity Control register:           0x%04lx\n", val);
  val = mv88e11x_page_read(phy_addr, 0x05, 0x14);
  printf("5:0x14 - 1000 BASE-T Pair Skew register:               0x%04lx\n", val);
  val = mv88e11x_page_read(phy_addr, 0x05, 0x15);
  printf("5:0x15 - 1000 BASE-T Pair Swap and Polarity register:  0x%04lx\n", val);
  val = mv88e11x_page_read(phy_addr, 0x05, 0x14);
  printf("5:0x14 - 1000 BASE-T Pair Skew register:               0x%04lx\n", val);
#else
  val = read_phy_register(phy_addr, 0x10);
  printf("0x10 - IEEE Extended Control register:                 0x%04lx\n", val);
  val = read_phy_register(phy_addr, 0x11);
  printf("0x11 - PHY Extended Status register:                   0x%04lx\n", val);
  val = read_phy_register(phy_addr, 0x12);
  printf("0x12 - Receive Error Counter register:                 0x%04lx\n", val);
  val = read_phy_register(phy_addr, 0x13);
  printf("0x13 - False Carrier Sense Counter register:           0x%04lx\n", val);
  val = read_phy_register(phy_addr, 0x14);
  printf("0x14 - Receiver NOT_OK Counter register:               0x%04lx\n", val);
  val = bxm54xx_exp_read(phy_addr, MII_EXP_RXTX_PKTCOUNT);
  printf("0x17:0 - Receive/Transmit Packet Counter register:     0x%04lx\n", val);
  val = bxm54xx_exp_read(phy_addr, MII_EXP_INTSTAT);
  printf("0x17:1 - Expansion Interrupt Status register:          0x%04lx\n", val);
  val = bxm54xx_exp_read(phy_addr, MII_EXP_MCLEDSELECT);
  printf("0x17:4 - Multicolor LED Selector register:             0x%04lx\n", val);
  val = bxm54xx_exp_read(phy_addr, MII_EXP_MCLEDFLASH);
  printf("0x17:5 - Multicolor LED Flash Rate Controls register:  0x%04lx\n", val);
  val = bxm54xx_exp_read(phy_addr, MII_EXP_MCLEDBLINK);
  printf("0x17:6 - Multicolor LED Programmable Blink Contrl reg: 0x%04lx\n", val);
  val = bcm54xx_aux_read(phy_addr, MII_AUX_SELECT_AUXCTL);
  printf("0x18:0 - Auxilary Control register:                    0x%04lx\n", val);
  val = bcm54xx_aux_read(phy_addr, MII_AUX_SELECT_10BTREG);
  printf("0x18:1 - 10BaseT register:                             0x%04lx\n", val);
  val = bcm54xx_aux_read(phy_addr, MII_AUX_SELECT_POWERCTL);
  printf("0x18:2 - Power MII Control register:                   0x%04lx\n", val);
  val = bcm54xx_aux_read(phy_addr, MII_AUX_SELECT_MISCTEST);
  printf("0x18:4 - Misc Test register:                           0x%04lx\n", val);
  val = bcm54xx_aux_read(phy_addr, MII_AUX_SELECT_MISCCTL);
  printf("0x18:7 - Misc Control register:                        0x%04lx\n", val);
  val = read_phy_register(phy_addr, MII_AUXSTAT);
  printf("0x19 - Auxilary Status Summary register:               0x%04lx\n", val);
  val = read_phy_register(phy_addr, 0x1A);
  printf("0x1A - Interrupt Status register:                      0x%04lx\n", val);
  val = read_phy_register(phy_addr, 0x1B);
  printf("0x1B - Interrupt Mask register:                        0x%04lx\n", val);
  val = bcm54xx_shadow_read(phy_addr, MII_SHD_SPARECTL1);
  printf("0x1C:2 - Spare Control 1 register:                     0x%04lx\n", val);
  val = bcm54xx_shadow_read(phy_addr, MII_SHD_CLKALIGN);
  printf("0x1C:3 - Clock Alignment Control register:             0x%04lx\n", val);
  val = bcm54xx_shadow_read(phy_addr, MII_SHD_SPARECTL2);
  printf("0x1C:4 - Spare Control 2 register:                     0x%04lx\n", val);
  val = bcm54xx_shadow_read(phy_addr, MII_SHD_SPARECTL3);
  printf("0x1C:5 - Spare Control 3 register:                     0x%04lx\n", val);
  val = bcm54xx_shadow_read(phy_addr, MII_SHD_LEDSTAT);
  printf("0x1C:8 - LED Status register:                          0x%04lx\n", val);
  val = bcm54xx_shadow_read(phy_addr, MII_SHD_LEDCTL);
  printf("0x1C:9 - LED Control register:                         0x%04lx\n", val);
  val = bcm54xx_shadow_read(phy_addr, MII_SHD_AUTOPD);
  printf("0x1C:A - Auto Power-down register:                     0x%04lx\n", val);
  val = bcm54xx_shadow_read(phy_addr, MII_SHD_LEDSEL1);
  printf("0x1C:D - LED Selector 1 register:                      0x%04lx\n", val);
  val = bcm54xx_shadow_read(phy_addr, MII_SHD_LEDSEL2);
  printf("0x1C:E - LED Selector 2 register:                      0x%04lx\n", val);
  val = bcm54xx_shadow_read(phy_addr, MII_SHD_LEDGPIO);
  printf("0x1C:F - LED GPIO Control/Status register:             0x%04lx\n", val);
  val = bcm54xx_shadow_read(phy_addr, MII_SHD_SERDESCTL);
  printf("0x1C:13 - SerDES 100BASE-FX Control register:          0x%04lx\n", val);
  val = bcm54xx_shadow_read(phy_addr, MII_SHD_SGMIISLV);
  printf("0x1C:15 - SGMII Slave register:                        0x%04lx\n", val);
  val = bcm54xx_shadow_read(phy_addr, MII_SHD_SGMIIMED);
  printf("0x1C:18 - SGMII/Media Converter register:              0x%04lx\n", val);
  val = bcm54xx_shadow_read(phy_addr, MII_SHD_ANDEBUG);
  printf("0x1C:1A - Auto-negotiation Debug register:             0x%04lx\n", val);
  val = bcm54xx_shadow_read(phy_addr, MII_SHD_AUX1000CTL);
  printf("0x1C:1B - Auxilary 1000BASE-X Control register:        0x%04lx\n", val);
  val = bcm54xx_shadow_read(phy_addr, MII_SHD_AUX1000STAT);
  printf("0x1C:1C - Auxilary 1000BASE-X Status register:         0x%04lx\n", val);
  val = bcm54xx_shadow_read(phy_addr, MII_SHD_MISC1000STAT);
  printf("0x1C:1D - Misc 1000BASE-X Status register:             0x%04lx\n", val);
  val = bcm54xx_shadow_read(phy_addr, MII_SHD_CFAUTODET);
  printf("0x1C:1E - Copper/Fiber Auto-detect Medium register:    0x%04lx\n", val);
  val = bcm54xx_shadow_read(phy_addr, MII_SHD_MODECTL);
  printf("0x1C:1F - Mode Control register:                       0x%04lx\n", val);
  val = read_phy_register(phy_addr, 0x1D);
  printf("0x1D - PHY Extended Status register:                   0x%04lx\n", val);
  val = read_phy_register(phy_addr, 0x1E);
  printf("0x1E - HCD Sumary register:                            0x%04lx\n", val);
#endif
  return(0);
}

U_BOOT_CMD(phydump, 1, 0, do_dump_phy_reg,
		"Dump Ethernet PHY registers",
		"Dump all PHY registers.  Appropriate for BCM-54xx, may be less so for other PHYs");

int do_read_phy_reg(cmd_tbl_t *cmdtp, int flag, int argc, char *argv[])
{
  unsigned int addr;
  unsigned int val;
  if (argc < 2) {
    cmd_usage(cmdtp);
    return 1;
  }

  if (first) {
    eth_init(gd->bd);
  }
  addr = simple_strtoul(argv[1], NULL, 0);
  val = read_phy_register(phy_addr, addr);
  printf("0x%04x\n", val);
  return(0);
}

U_BOOT_CMD(phyrd, 2, 0, do_read_phy_reg,
		"Read an Ethernet PHY register",
		"Read a value from the Ethernet PHY register specified.  "
		"The value returned is from the specified register of Ethernet PHY 0");

int do_write_phy_reg(cmd_tbl_t *cmdtp, int flag, int argc, char *argv[])
{
  unsigned int addr;
  unsigned int val;
  if (argc < 3) {
    cmd_usage(cmdtp);
    return 1;
  }

  if (first) {
    eth_init(gd->bd);
  }
  addr = simple_strtoul(argv[1], NULL, 0);
  val = simple_strtoul(argv[2], NULL, 0);
  write_phy_register(phy_addr, addr, val);
  // printf("0x%04x => 0x%x\n", val, addr);
  return(0);
}

U_BOOT_CMD(phywr, 3, 0, do_write_phy_reg,
		"Write an Ethernet PHY register",
		"Write a value to the Ethernet PHY register specified.  "
		"The value  is written to the specified register of Ethernet PHY 0");

