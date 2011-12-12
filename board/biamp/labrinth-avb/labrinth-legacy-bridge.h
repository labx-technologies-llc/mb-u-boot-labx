#ifndef _LABRINTH_LEGACY_BRIDGE_H_
#define _LABRINTH_LEGACY_BRIDGE_H_

#define BRIDGE_BASE (XPAR_BACKPLANE_BRIDGE_BASEADDR)

/* Number of AVB bridge ports */
#define NUM_AVB_BRIDGE_PORTS 2

/* Address range definitions */
#define BRIDGE_REGS_BASE   (0x00000000)
#define LABX_MAC_REGS_BASE (0x00004000)

/* Port selection for within the bridge register space */
#define AVB_PORTS_BASE   (0x00000080)
#define AVB_PORT_0       (0x00000000)
#define AVB_PORT_1       (0x00000040)
#define BRIDGE_PORT_MASK (0x00000040)

/* Macro for addressing a global bridge register */
#define BRIDGE_REG_ADDRESS(base, offset) \
  (base | (offset << 2))

/* Global bridge registers */
#define BRIDGE_CTRL_REG  (0x00000000)
#  define BRIDGE_RX_PORT_0     (0x00000000)
#  define BRIDGE_RX_PORT_1     (0x00000001)
#  define BRIDGE_TX_EN_NONE    (0x00000000)
#  define BRIDGE_TX_EN_PORT_0  (0x00000002)
#  define BRIDGE_TX_EN_PORT_1  (0x00000004)

/* Macro for addressing a per-port bridge register */
#define BRIDGE_PORT_REG_ADDRESS(base, port, offset) \
  (base                               |          \
   AVB_PORTS_BASE                     |          \
   (port & BRIDGE_PORT_MASK)          |          \
   (offset << 2))

/* Per-port packet filter registers */
#define VLAN_MASK_REG        (0x00000000)
#  define VLAN_PRIORITY_ENABLE(priorityLevel) (0x01 << priorityLevel)

#define FILTER_SELECT_REG    (0x00000001)
#  define FILTER_SELECT_NONE (0x00000000)
#  define FILTER_SELECT_ALL  (0xFFFFFFFF)

#define FILTER_CTRL_STAT_REG (0x00000002)
#  define FILTER_LOAD_ACTIVE (0x00000100)
#  define FILTER_LOAD_LAST   (0x00000200)

#define FILTER_LOAD_REG      (0x00000003)
#  define FILTER_LOAD_CLEAR (0x00000000)

/* Address definitions for the Lab X MAC used for the backplane PHY */

#define BP_MAC_REG_ADDRESS(base, offset) \
  (BRIDGE_BASE | LABX_MAC_REGS_BASE | (offset << 2))

#define MAC_RX_CONFIG_REG  (0x00000001)
#  define MAC_RX_RESET (0x80000000)

#define MAC_TX_CONFIG_REG  (0x00000002)
#  define MAC_TX_RESET (0x80000000)

#define MAC_SPEED_CFG_REG  (0x00000004)
#  define MAC_SPEED_10_MBPS  (0x00000000)
#  define MAC_SPEED_100_MBPS (0x00000001)
#  define MAC_SPEED_1_GBPS   (0x00000002)

/* Command to configure an address filter on one of the AVB ports */
#define DISABLE_MAC_FILTER (0)
#define ENABLE_MAC_FILTER  (1)
#define MAC_ADDRESS_BYTES  (6)

/* Number of match units specified in the device tree */
#define NUM_MATCH_UNITS 0x08

/* Command to place the bridge's PHY into one of the test modes defined
 * in phy.h
 */

#define PHY_NORMAL_MODE   0x00
#define PHY_TEST_LOOPBACK 0x01

/* Command to configure the transmit and receive port selections */
#define RX_PORT_0_SELECT  (0)
#define RX_PORT_1_SELECT  (1)
#define TX_PORT_DISABLED  (0)
#define TX_PORT_ENABLED   (1)

/* Low-level macros for read and write from / to the mailbox */
#define BRIDGE_READ_REG(reg) ( *((volatile unsigned long *)reg) )
#define BRIDGE_WRITE_REG(reg,val) ( *((volatile unsigned long *)reg) = val )

#endif 
