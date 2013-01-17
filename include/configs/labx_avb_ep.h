/*
 * (C) Copyright 2010 Eldridge M. Mount IV, Peter McLoone, Albert M. Hajjar
 *                    eldridge.mount@labxtechnologies.com
 *                    peter.mcloone@labxtechnologies.com
 *                    albert.hajjar@labxtechnologies.com
 *
 * See file CREDITS for list of people who contributed to this
 * project.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#ifndef _LABX_AVB_EP_H_
#define _LABX_AVB_EP_H_

#include "../board/labx/avb_ep/xparameters.h"

#define	CONFIG_MICROBLAZE	1	/* MicroBlaze CPU */
#define	MICROBLAZE_V5		1

/* Location or runtime FPGA on this platform. */
#define RUNTIME_FPGA_BASE 0x280000

// This is the entire firmware update module,
// and includes GPIO-checking.
#define CONFIG_FIRMWARE_UPDATE

// GPIO pins to request a boot
// delay or a firmware update.
#define GPIO_BOOT_DELAY_BIT      2
#define GPIO_FIRMWARE_UPDATE_BIT 3

/* UARTLITE0 is used for MDM. Use UARTLITE1 for Microblaze */

#define	CONFIG_XILINX_UARTLITE
#define	CONFIG_SERIAL_BASE	XPAR_UARTLITE_1_BASEADDR
#define	CONFIG_BAUDRATE		XPAR_UARTLITE_1_BAUDRATE
#define	CONFIG_SYS_BAUDRATE_TABLE	{ CONFIG_BAUDRATE }

/* Ethernet port */
#define CONFIG_SYS_ENET
#define CONFIG_CMD_PING
#define CONFIG_NET_MULTI
#define CONFIG_CMD_NET 

#undef ET_DEBUG

/* Use the Lab X Ethernet driver */
#define CONFIG_LABX_ETHERNET  1

/* Top-level configuration setting to determine whether AVB port 0 or 1
 * is used by U-Boot.  AVB 0 is on top at the card edge, with AVB 1
 * located underneath of it.
 */
#define WHICH_ETH_PORT  0

/* The MDIO divisor is set to produce a 1.5 MHz interface */
#define LABX_ETHERNET_MDIO_DIV  (0x28)

/* Port zero is used for all MDIO operations, regardless of which
 * port is used for communications.
 */
#define LABX_MDIO_ETH_BASEADDR  (XPAR_ETH0_BASEADDR)

#if (WHICH_ETH_PORT == 0)
  /* Use port zero; the base address of the primary register file and the
   * FIFO used for data are specified, as well as the corresponding PHY address.
   */
#  define LABX_PRIMARY_ETH_BASEADDR    (XPAR_ETH0_BASEADDR)
#  define LABX_ETHERNET_PHY_ADDR  (0x08)

#else

  /* Use port one instead */
#  define LABX_PRIMARY_ETH_BASEADDR    (XPAR_ETH1_BASEADDR)
#  define LABX_ETHERNET_PHY_ADDR  (0x09)

#endif /* if(U_BOOT_PORT = 0) */

#define CONFIG_MV88E1116R 1

/* gpio */
#ifdef XPAR_XPS_GPIO_0_BASEADDR
#  define	CONFIG_SYS_GPIO_0		1
#  define	CONFIG_SYS_GPIO_0_ADDR		XPAR_XPS_GPIO_0_BASEADDR
#endif

/* ICAP peripheral controller */
#define FINISH_FSL_BIT (0x80000000)
#define ICAP_FSL_FAILED (0x80000000)

#define USE_ICAP_FSL
#define XPAR_ICAP_CR_ABORT		BIT(4)
#define XPAR_ICAP_CR_RESET		BIT(3)
#define XPAR_ICAP_CR_FIFO_CLEAR	BIT(2)
#define XPAR_ICAP_CR_READ		BIT(1)
#define XPAR_ICAP_CR_WRITE		BIT(0)

#define XPAR_ICAP_SR_CFGERR		BIT(8)
#define XPAR_ICAP_SR_DALIGN		BIT(7)
#define XPAR_ICAP_SR_READ_IP	BIT(6)
#define XPAR_ICAP_SR_IN_ABORT	BIT(5)
#define XPAR_ICAP_SR_DONE		BIT(0)

#ifdef XPAR_XPS_HWICAP_0_BASEADDR
#define	CONFIG_SYS_ICAP_ADDR	XPAR_XPS_HWICAP_0_BASEADDR
#define	CONFIG_SYS_ICAP_GIE		(CONFIG_SYS_ICAP_ADDR + 0x01C)
#define	CONFIG_SYS_ICAP_IPISR	(CONFIG_SYS_ICAP_ADDR + 0x020)
#define	CONFIG_SYS_ICAP_IPIER	(CONFIG_SYS_ICAP_ADDR + 0x028)
#define	CONFIG_SYS_ICAP_WF		(CONFIG_SYS_ICAP_ADDR + 0x100)
#define	CONFIG_SYS_ICAP_RF		(CONFIG_SYS_ICAP_ADDR + 0x104)
#define	CONFIG_SYS_ICAP_SZ		(CONFIG_SYS_ICAP_ADDR + 0x108)
#define	CONFIG_SYS_ICAP_CR		(CONFIG_SYS_ICAP_ADDR + 0x10C)
#define	CONFIG_SYS_ICAP_SR		(CONFIG_SYS_ICAP_ADDR + 0x110)
#define	CONFIG_SYS_ICAP_WFV		(CONFIG_SYS_ICAP_ADDR + 0x114)
#define	CONFIG_SYS_ICAP_RFO		(CONFIG_SYS_ICAP_ADDR + 0x118)
#endif

/* interrupt controller */
#define	CONFIG_SYS_INTC_0		1
#define	CONFIG_SYS_INTC_0_ADDR		XPAR_INTC_0_BASEADDR
#define	CONFIG_SYS_INTC_0_NUM		32

/* timer */
#define	CONFIG_SYS_TIMER_0		1
#define	CONFIG_SYS_TIMER_0_ADDR	XPAR_TMRCTR_0_BASEADDR
#define	CONFIG_SYS_TIMER_0_IRQ	XPAR_INTC_0_TMRCTR_0_VEC_ID
#define	FREQUENCE		XPAR_PROC_BUS_0_FREQ_HZ
#define	CONFIG_SYS_TIMER_0_PRELOAD	( FREQUENCE/1000 )

/* FSL */
/* #define	CONFIG_SYS_FSL_2 */
/* #define	FSL_INTR_2	1 */

/* DDR2 SDRAM, main memory */
#define	CONFIG_SYS_SDRAM_BASE		XPAR_MPMC_0_MPMC_BASEADDR
#define	CONFIG_SYS_SDRAM_SIZE		(XPAR_MPMC_0_MPMC_HIGHADDR - XPAR_MPMC_0_MPMC_BASEADDR + 1)

#define XILINX_RAM_START		CONFIG_SYS_SDRAM_BASE
#define XILINX_RAM_SIZE			CONFIG_SYS_SDRAM_SIZE

#define	CONFIG_SYS_MEMTEST_START	CONFIG_SYS_SDRAM_BASE
#define	CONFIG_SYS_MEMTEST_END		(CONFIG_SYS_SDRAM_BASE + 0x1000)

/* global pointer */
#define	CONFIG_SYS_GBL_DATA_SIZE	128 /* size of global data */
/* start of global data */
#define	CONFIG_SYS_GBL_DATA_OFFSET	(CONFIG_SYS_SDRAM_BASE + CONFIG_SYS_SDRAM_SIZE - CONFIG_SYS_GBL_DATA_SIZE)

/* monitor code */
#define CONFIG_MONITOR_IS_IN_RAM        1
#define	SIZE				0x40000
#define	CONFIG_SYS_MONITOR_LEN		(SIZE - CONFIG_SYS_GBL_DATA_SIZE)
#define	CONFIG_SYS_MONITOR_BASE		(CONFIG_SYS_GBL_DATA_OFFSET - CONFIG_SYS_MONITOR_LEN)
#define	CONFIG_SYS_MONITOR_END		(CONFIG_SYS_MONITOR_BASE + CONFIG_SYS_MONITOR_LEN)
#define	CONFIG_SYS_MALLOC_LEN	        0x100000 	
#define	CONFIG_SYS_MALLOC_BASE		(CONFIG_SYS_MONITOR_BASE - CONFIG_SYS_MALLOC_LEN)

/* stack */
#define	CONFIG_SYS_INIT_SP_OFFSET	CONFIG_SYS_MALLOC_BASE

/* Flash memory is always present on this board */

#define	CONFIG_SYS_FLASH_BASE		XPAR_FLASH_CONTROL_MEM0_BASEADDR                                               
#define	CONFIG_SYS_FLASH_SIZE		(XPAR_FLASH_CONTROL_MEM0_HIGHADDR - XPAR_FLASH_CONTROL_MEM0_BASEADDR + 1)       
#define	CONFIG_SYS_FLASH_CFI		1                                                                                
#define	CONFIG_FLASH_CFI_DRIVER		1                                                                              
#define	CONFIG_SYS_FLASH_EMPTY_INFO	1	                  /* ?empty sector */
#define	CONFIG_SYS_MAX_FLASH_BANKS	1	                  /* max number of memory banks */
#define	CONFIG_SYS_MAX_FLASH_SECT	512	                  /* max number of sectors on one chip */
#define	CONFIG_SYS_FLASH_PROTECTION		                  /* hardware flash protection */
#define CONFIG_SYS_FLASH_USE_BUFFER_WRITE 1 /* Accelerate Flash writes with on-chip buffers */
//#define CONFIG_FLASH_CFI_LEGACY 1
//#define CONFIG_SYS_FLASH_CFI_AMD_RESET 1

/* NOTE - The configuration environment address must align with the environment
 *        variables in ../../board/labx/avb_ep/ub.config.scr!
 *        These definitions locate the environment within the last few
 *        top-boot parameter sectors on the Flash.
 */
#define	CONFIG_ENV_IS_IN_FLASH	1   
#define	CONFIG_ENV_SECT_SIZE	0x20000	 /* 128K */
#define	CONFIG_ENV_ADDR		(CONFIG_SYS_FLASH_BASE + 0x540000)      
#define	CONFIG_ENV_SIZE		0x08000  /* Only 32K actually allocated */

/* If this is defined and zero, the system will auto-boot
 * ("production" mode). If it is defined and > 0, there
 * will be a delay to allow the user to stop auto-boot,
 * if desired. If it is not defined, auto-boot will be
 * compiled out completely. */
#define CONFIG_BOOTDELAY 3

/* Include Lab X pre-boot routines (CRC-checking, FPGA reconfiguration, etc.) */
#define CONFIG_LABX_PREBOOT

/* Data Cache */
#ifdef XPAR_MICROBLAZE_0_USE_DCACHE
	#define CONFIG_DCACHE
#else
	#undef CONFIG_DCACHE
#endif

/* Instruction Cache */
#ifdef XPAR_MICROBLAZE_0_USE_ICACHE
	#define CONFIG_ICACHE
#else
	#undef CONFIG_ICACHE
#endif

/*
 * Command line configuration.
 */
#include <config_cmd_default.h>

#define CONFIG_CMD_ASKENV
#define CONFIG_CMD_IRQ
//#define CONFIG_CMD_MFSL
#define CONFIG_CMD_ECHO

#if defined(CONFIG_DCACHE) || defined(CONFIG_ICACHE)
	#define CONFIG_CMD_CACHE
#else
	#undef CONFIG_CMD_CACHE
#endif

#define CONFIG_CMD_ECHO
#define CONFIG_CMD_FLASH
#define CONFIG_CMD_IMLS
#define CONFIG_CMD_JFFS2

#define CONFIG_CMD_SAVEENV
#define CONFIG_CMD_SAVES

/* JFFS2 partitions */
#define CONFIG_CMD_MTDPARTS	/* mtdparts command line support */
#define CONFIG_MTD_DEVICE	/* needed for mtdparts commands */
#define CONFIG_FLASH_CFI_MTD

/* Miscellaneous configurable options */
#define	CONFIG_SYS_PROMPT	"U-Boot> "
#define	CONFIG_SYS_CBSIZE	512	/* size of console buffer */
#define	CONFIG_SYS_PBSIZE	(CONFIG_SYS_CBSIZE + sizeof(CONFIG_SYS_PROMPT) + 16) /* print buffer size */
#define	CONFIG_SYS_MAXARGS	15	/* max number of command args */
#define	CONFIG_SYS_LONGHELP
#define	CONFIG_SYS_LOAD_ADDR	XILINX_RAM_START /* default load address */

/* Some appropriate defaults for network settings */
#define CONFIG_HOSTNAME		labx_avb-ep
#define CONFIG_IPADDR           192.168.1.1
#define CONFIG_SERVERIP         192.168.1.100

/* TEMPORARY - This is a Xilinx OUI, need to replace */
#define CONFIG_ETHADDR          00:0A:35:00:33:01

/* Permit a single-time overwrite of the ethaddr */
#define CONFIG_OVERWRITE_ETHADDR_ONCE

/* architecture dependent code */
#define	CONFIG_SYS_USR_EXCEP	/* user exception */
#define CONFIG_SYS_HZ	1000

#define CONFIG_CMDLINE_EDITING

/* Use the HUSH parser */
#define CONFIG_SYS_HUSH_PARSER
#ifdef  CONFIG_SYS_HUSH_PARSER
#define CONFIG_SYS_PROMPT_HUSH_PS2 "> "
#endif

/* Flat device tree support */
#define CONFIG_OF_LIBFDT
#define CONFIG_SYS_BOOTMAPSZ	(8 << 20)       /* Initial Memory map for Linux */
#define CONFIG_LMB

#endif	/* __CONFIG_H */
