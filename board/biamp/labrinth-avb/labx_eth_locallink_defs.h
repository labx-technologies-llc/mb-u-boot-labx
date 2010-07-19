/**
 * File        : labx_eth_locallink_defs.h
 * Author      : Eldridge M. Mount IV (eldridge.mount@labxtechnologies.com)
 * Description : Provides hardware details necessary for the Lab X Ethernet
 *               LocalLink network driver.
 *
 *               This particular incarnation is for the Biamp Labrinth-AVB
 *               audio board
 */

#ifndef _LABX_ETH_LOCALLINK_DEFS_H_
#define _LABX_ETH_LOCALLINK_DEFS_H_

/* Lab X Ethernet LocalLink hardware; this interfaces between the Lab X soft
 * tri-mode Ethernet MAC (or shimmed up to a Xilinx hard TEMAC) and a FIFO
 * peripheral on the PLB.  A PLB slave interface also allows host control of the 
 * MAC registers and MDIO logic for PHY management.
 */
#define LABX_PRIMARY_ETH_BASEADDR  (XPAR_ETH0_BASEADDR)

/* Operate in FIFO mode on this platform */
#define LABX_ETH_LOCALLINK_FIFO_MODE  (1)
#define XILINX_LLTEMAC_FIFO_BASEADDR  (XPAR_ETH0_FIFO_BASEADDR)

/* Define the address of the PHY we will use on the MDIO bus and the MDIO
 * clock divisor to be used.
 */
#define LABX_ETH_LOCALLINK_PHY_ADDR  (0x01)
#define LABX_ETH_LOCALLINK_MDIO_DIV  (0x28)

#endif
