/*******************************************************************************
Copyright (C) Marvell International Ltd. and its affiliates

This software file (the "File") is owned and distributed by Marvell 
International Ltd. and/or its affiliates ("Marvell") under the following
alternative licensing terms.  Once you have made an election to distribute the
File under one of the following license alternatives, please (i) delete this
introductory statement regarding license alternatives, (ii) delete the two
license alternatives that you have not elected to use and (iii) preserve the
Marvell copyright notice above.

********************************************************************************
Marvell Commercial License Option

If you received this File from Marvell and you have entered into a commercial
license agreement (a "Commercial License") with Marvell, the File is licensed
to you under the terms of the applicable Commercial License.

********************************************************************************
Marvell GPL License Option

If you received this File from Marvell, you may opt to use, redistribute and/or 
modify this File in accordance with the terms and conditions of the General 
Public License Version 2, June 1991 (the "GPL License"), a copy of which is 
available along with the File in the license.txt file or by writing to the Free 
Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 or 
on the worldwide web at http://www.gnu.org/licenses/gpl.txt. 

THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE IMPLIED 
WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE ARE EXPRESSLY 
DISCLAIMED.  The GPL License provides additional details about this warranty 
disclaimer.
********************************************************************************
Marvell BSD License Option

If you received this File from Marvell, you may opt to use, redistribute and/or 
modify this File under the following licensing terms. 
Redistribution and use in source and binary forms, with or without modification, 
are permitted provided that the following conditions are met:

    *   Redistributions of source code must retain the above copyright notice,
	    this list of conditions and the following disclaimer. 

    *   Redistributions in binary form must reproduce the above copyright
        notice, this list of conditions and the following disclaimer in the
        documentation and/or other materials provided with the distribution. 

    *   Neither the name of Marvell nor the names of its contributors may be 
        used to endorse or promote products derived from this software without 
        specific prior written permission. 
    
THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND 
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE 
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR 
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES 
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; 
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON 
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS 
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*******************************************************************************/



#include <common.h>
#include "mvSwitch_6350R.h"                                
                             

/* CAL_ICS constants; ultimately these and the corresponding code
 * should migrate into board-specific init code.
 */
#define REG_PORT(p)		(0x10 + (p))
#define REG_GLOBAL		0x1b
#define REG_GLOBAL2		0x1c

/* 88E6350R ports assigned to the two CPU ports */
#define CAL_ICS_CPU_PORT_0 (5)
#define CAL_ICS_CPU_PORT_1 (6)

/* Register constant definitions for the 88E6350R LinkStreet switch */
#define PHYS_CTRL_REG  (1)
#  define RGMII_MODE_RXCLK_DELAY   (0x8000)
#  define RGMII_MODE_GTXCLK_DELAY  (0x4000)
#  define FLOW_CTRL_FORCE_DISABLED (0x0040)
#  define FLOW_CTRL_FORCE_ENABLED  (0x00C0)
#  define FORCE_LINK_DOWN          (0x0010)
#  define FORCE_LINK_UP            (0x0030)
#  define FORCE_DUPLEX_HALF        (0x0004)
#  define FORCE_DUPLEX_FULL        (0x000C)
#  define FORCE_SPEED_10           (0x0000)
#  define FORCE_SPEED_100          (0x0001)
#  define FORCE_SPEED_1000         (0x0002)
#  define SPEED_AUTO_DETECT        (0x0003)

/* Register settings assigned to the CPU port:
 * Link forced up, 1 Gbps full-duplex
 * Using RGMII delay on switch IND input data
 * Using RGMII delay on switch OUTD output data
 */
 
#define CAL_ICS_CPU_PORT_0_PHYS_CTRL (RGMII_MODE_RXCLK_DELAY  | \
                                         RGMII_MODE_GTXCLK_DELAY | \
                                         FORCE_LINK_UP           | \
                                         FORCE_DUPLEX_FULL       | \
                                         FORCE_SPEED_1000)



/* Register settings assigned to the SFP port:
 * Link forced up, 1 Gbps full-duplex
 * Using RGMII delay on switch IND input data
 * Using RGMII delay on switch OUTD output data
 */
 
#define CAL_ICS_CPU_PORT_1_PHYS_CTRL (RGMII_MODE_RXCLK_DELAY  | \
                                         RGMII_MODE_GTXCLK_DELAY | \
                                         FORCE_LINK_UP           | \
                                         FORCE_DUPLEX_FULL       | \
                                         FORCE_SPEED_1000)


/* Bit-mask of enabled ports  */
#define CAL_ICS_ENABLED_PORTS ((1 << 6) | (1 << 5) | (1 << 1) | (1 << 0))

/* Number of copper 1000Base-TX ports for CAL_ICS */
#define CAL_ICS_COPPER_PORTS (2)

/* E6350R-related */
#define MV_E6350R_MAX_PORTS_NUM					7

#define XPAR_XPS_GPIO_0_BASEADDR 0x820F0000
#define LABX_MDIO_ETH_BASEADDR 0x82050000
#define MDIO_CONTROL_REG      (0x00000000)
#  define PHY_MDIO_BUSY       (0x80000000)
#  define PHY_REG_ADDR_MASK   (0x01F)
#  define PHY_ADDR_MASK       (0x01F)
#  define PHY_ADDR_SHIFT      (5)
#  define PHY_MDIO_READ       (0x0400)
#  define PHY_MDIO_WRITE      (0x0000)
#define MDIO_DATA_REG         (0x00000004)

#define LABX_MAC_REGS_BASE    (0x00001000)
#define MAC_MDIO_CONFIG_REG   (LABX_MAC_REGS_BASE + 0x0014)
#define LABX_ETHERNET_MDIO_DIV  (0x28)
#  define MDIO_DIVISOR_MASK  (0x0000003F)
#  define MDIO_ENABLED       (0x00000040)


/* Performs a register write to a PHY */
void REG_WRITE(int phy_addr, int reg_addr, int phy_data)
{
  unsigned int addr;

  /* Write the data first, then the control register */
  addr = (LABX_MDIO_ETH_BASEADDR + MDIO_DATA_REG);
  *((volatile unsigned int *) addr) = phy_data;
  addr = (LABX_MDIO_ETH_BASEADDR + MDIO_CONTROL_REG);
  *((volatile unsigned int *) addr) = 
    (PHY_MDIO_WRITE | ((phy_addr & PHY_ADDR_MASK) << PHY_ADDR_SHIFT) |
     (reg_addr & PHY_REG_ADDR_MASK));
  while(*((volatile unsigned int *) addr) & PHY_MDIO_BUSY);
}

/* Performs a register read from a PHY */
unsigned int REG_READ(int phy_addr, int reg_addr)
{
  unsigned int addr;
  unsigned int readValue;

  /* Write to the MDIO control register to initiate the read */
  addr = (LABX_MDIO_ETH_BASEADDR + MDIO_CONTROL_REG);
  *((volatile unsigned int *) addr) = 
    (PHY_MDIO_READ | ((phy_addr & PHY_ADDR_MASK) << PHY_ADDR_SHIFT) |
     (reg_addr & PHY_REG_ADDR_MASK));
  while(*((volatile unsigned int *) addr) & PHY_MDIO_BUSY);
  addr = (LABX_MDIO_ETH_BASEADDR + MDIO_DATA_REG);
  readValue = *((volatile unsigned int *) addr);
  return(readValue);
}



static void switchVlanInit(
						               MV_U32 switchCpuPort0,
						               MV_U32 switchCpuPort1,
                           MV_U32 switchMaxPortsNum,
                           MV_U32 switchEnabledPortsMask)
{
  MV_U32 prt;
	MV_U16 reg;

	MV_U16 cpu_vid = 0x1;


	/* Setting  Port default priority for all ports to zero, set default VID=0x1 */
    for(prt=0; prt < switchMaxPortsNum; prt++) {
      if (((1 << prt)& switchEnabledPortsMask)) {
		    reg = REG_READ (REG_PORT(prt),MV_SWITCH_PORT_VID_REG);
		    reg &= ~0xefff;
		    reg |= cpu_vid;
		    REG_WRITE (REG_PORT(prt),
                                 MV_SWITCH_PORT_VID_REG,reg);
        //printf("Yi Cao: set default priority of port %d\n", prt);
        mdelay(2); 
      }
	}

	
  /* 
   *  set Ports VLAN Mapping.
   *	
   */
  
  /* port 0 is mapped to port 5 (CPU Port) */  
  reg = REG_READ ( REG_PORT(0),MV_SWITCH_PORT_VMAP_REG);  
  reg &= ~0x00ff;
  reg |= 0x20;
  REG_WRITE ( REG_PORT(0),
                       MV_SWITCH_PORT_VMAP_REG,reg); 
  //printf("VLAN map for port 0 = 0x%08X\n", reg);
  mdelay(2);   
  /* port 1 is mapped to port 6 (CPU Port) */
  reg = REG_READ ( REG_PORT(1),MV_SWITCH_PORT_VMAP_REG);  
  reg &= ~0x00ff;
  reg |= 0x40;
  REG_WRITE ( REG_PORT(1),
                       MV_SWITCH_PORT_VMAP_REG,reg);                 
  //printf("VLAN map for port 1 = 0x%08X\n", reg);      
  mdelay(2);                       
	/* port 5 (CPU Port) is mapped to port 0*/
	reg = REG_READ ( REG_PORT(switchCpuPort0),MV_SWITCH_PORT_VMAP_REG);
	reg &= ~0x00ff;
	reg |= 0x01;
	REG_WRITE ( REG_PORT(switchCpuPort0),
                         MV_SWITCH_PORT_VMAP_REG,reg);
  //printf("VLAN map for CPU port %d = 0x%08X\n", switchCpuPort0, reg);
  mdelay(2); 
  /* port 6 (CPU Port) is mapped to port 1*/
	reg = REG_READ ( REG_PORT(switchCpuPort1),MV_SWITCH_PORT_VMAP_REG);
	reg &= ~0x00ff;
	reg |= 0x02;
	REG_WRITE ( REG_PORT(switchCpuPort1),
                         MV_SWITCH_PORT_VMAP_REG,reg);
  //printf("VLAN map for CPU port %d = 0x%08X\n", switchCpuPort1, reg);
  mdelay(2); 
    /*enable only appropriate ports to forwarding mode*/
    for(prt=0; prt < switchMaxPortsNum; prt++) {

      if ((1 << prt)& switchEnabledPortsMask) {
        reg = REG_READ ( REG_PORT(prt),MV_SWITCH_PORT_CONTROL_REG);
        reg |= 0x3;
        REG_WRITE ( REG_PORT(prt),MV_SWITCH_PORT_CONTROL_REG,reg);
        //printf("Yi Cao: set forwarding mode of port %d\n", prt);
        mdelay(2); 
      }
    }

	return;
}

static void mv88e6350R_hard_reset(void)
{ 
  unsigned long reg;
  //set the MDIO clock divisor
  *(volatile unsigned int *)(LABX_MDIO_ETH_BASEADDR + MAC_MDIO_CONFIG_REG) = (LABX_ETHERNET_MDIO_DIV & MDIO_DIVISOR_MASK) | MDIO_ENABLED;
  
  /* read the GPIO_TRI register*/
  reg = *((unsigned long *)(XPAR_XPS_GPIO_0_BASEADDR + 0x04));  
  /* set bit 20 as output */
  reg &= ~0x4;
  *((unsigned long *)(XPAR_XPS_GPIO_0_BASEADDR + 0x04)) = reg;
  
  /* read the GPIO_Data register*/
  reg = *((unsigned long *)(XPAR_XPS_GPIO_0_BASEADDR));  
  /* set bit 20 as 1, so PHY_RESET_n=0, reset phy */
  reg |= 0x4;
  *((unsigned long *)(XPAR_XPS_GPIO_0_BASEADDR)) = reg;
  /* delay 10 ms */
  mdelay(10);
  
  reg = *((unsigned long *)(XPAR_XPS_GPIO_0_BASEADDR)); 
  /* set bit 20 as 0, so PHY_RESET_n=1, free up PHY_Reset_n */
  reg &= ~0x4;
  *((unsigned long *)(XPAR_XPS_GPIO_0_BASEADDR)) = reg;
  
  
}


MV_VOID mvEthE6350RSwitchInit()
{
	MV_U32 portIndex;
	MV_U16 saved_g1reg4;
	
	mdelay(1000);
	mv88e6350R_hard_reset();
	mdelay(1000);
 
  REG_WRITE( 
             REG_PORT(CAL_ICS_CPU_PORT_0), 
             PHYS_CTRL_REG,
             CAL_ICS_CPU_PORT_0_PHYS_CTRL);

  mdelay(2); 
                        
	REG_WRITE( 
             REG_PORT(CAL_ICS_CPU_PORT_1), 
             PHYS_CTRL_REG,
             CAL_ICS_CPU_PORT_1_PHYS_CTRL);

	/* Init vlan LAN0-3 <-> CPU port egiga0 */
  //printf("CPU port is on 88E6350R port %d and %d\n", CAL_ICS_CPU_PORT_0, CAL_ICS_CPU_PORT_1);
	mdelay(2); 
  switchVlanInit(
                   CAL_ICS_CPU_PORT_0,
                   CAL_ICS_CPU_PORT_1,
                   MV_E6350R_MAX_PORTS_NUM,
                   CAL_ICS_ENABLED_PORTS);

	/* Disable PPU */
	saved_g1reg4 = REG_READ(REG_GLOBAL,0x4);
	REG_WRITE(REG_GLOBAL,0x4,0x0);


	for(portIndex = 0; portIndex < MV_E6350R_MAX_PORTS_NUM; portIndex++) {
      /* Reset PHYs for all but the port to the CPU */
      if((portIndex != CAL_ICS_CPU_PORT_0) && (portIndex != CAL_ICS_CPU_PORT_1)) {
        //printf("Yi Cao: reset phy of port %d\n", portIndex);
        mdelay(2);
        REG_WRITE(portIndex, 0, 0x9140);
      }
    }

    /* Initialize LED control registers
     *
     * Specifically, Titanium-411 is designed to display per-port status on the
     * front panel using LED columns COL_0 and COL_1:
     *
     * Green -   1 Gbit Link Up
     * Amber - 100 Mbit Link Up
     * Red   -  10 Mbit Link Up
     *
     * Regardless of color, activity is indicated by blinking.
     *
     * On the back panel, LED columns COL_2 and COL_3 reflect status via light
     * pipes co-located above each port, with the exception of switch port 4,
     * which is denoted as "Port 5 - SFP Port" on the enclosure.  The SFP port
     * has a vertical two-LED stack reflecting the same:
     *
     * Green - 1 Gbit Link Up
     * Red   - Any Link Up / Activity (blink)
     *
     * This functionality is enabled through the use of the LED control 
     * registers within the 88E6350R switch chip.
     */
    for(portIndex = 0; portIndex < CAL_ICS_COPPER_PORTS; portIndex++) {
      //printf("Yi Cao: set up the LED of port %d\n",portIndex);
      mdelay(2); 
      REG_WRITE( 
                          REG_PORT(portIndex),
                          MV_SWITCH_PORT_LED_CTRL_REG,
                          MV_SWITCH_LED_WRITE(MV_SWITCH_LED_01_CTRL_REG, 
                                              (MV_SWITCH_LED1_100M_10M_ACT | MV_SWITCH_LED0_1G_100M_ACT)));
      REG_WRITE( 
                          REG_PORT(portIndex),
                          MV_SWITCH_PORT_LED_CTRL_REG,
                          MV_SWITCH_LED_WRITE(MV_SWITCH_LED_23_CTRL_REG, 
                                              (MV_SWITCH_LED3_LINK_ACT | MV_SWITCH_LED2_1G_LINK)));
      REG_WRITE( 
                          REG_PORT(portIndex),
                          MV_SWITCH_PORT_LED_CTRL_REG,
                          MV_SWITCH_LED_WRITE(MV_SWITCH_LED_SPECIAL_CTRL_REG, 
                                              MV_SWITCH_LED_SPECIAL_NONE));
    }

	/* Enable PHY Polling Unit (PPU) */
	saved_g1reg4 |= 0x4000;
	REG_WRITE(REG_GLOBAL,0x4,saved_g1reg4);
}
