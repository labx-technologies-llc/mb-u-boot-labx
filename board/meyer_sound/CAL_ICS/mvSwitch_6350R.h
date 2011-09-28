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

#ifndef __INCETHSWITCHH
#define __INCETHSWITCHH

typedef char  MV_8;
typedef unsigned char	MV_U8;

typedef int		MV_32;
typedef unsigned int	MV_U32;

typedef short		MV_16;
typedef unsigned short	MV_U16;

typedef void    MV_VOID;

/* Per-port switch registers */
#define MV_SWITCH_PORT_CONTROL_REG				0x04
#define MV_SWITCH_PORT_VMAP_REG					0x06
#define MV_SWITCH_PORT_VID_REG					0x07

/* Port LED control register and indirect registers */
#define MV_SWITCH_PORT_LED_CTRL_REG             0x16
#  define MV_SWITCH_LED_CTRL_UPDATE     0x8000
#  define MV_SWITCH_LED_CTRL_PTR_MASK      0x7
#  define MV_SWITCH_LED_CTRL_PTR_SHIFT      12
#    define MV_SWITCH_LED_01_CTRL_REG       0x0
#      define MV_SWITCH_LED0_1G_100M_ACT   0x001
#      define MV_SWITCH_LED1_100M_10M_ACT  0x010
#      define MV_SWITCH_LED1_By_BLINK_RATE 0x000 

#    define MV_SWITCH_LED_23_CTRL_REG       0x1
#      define MV_SWITCH_LED2_1G_LINK       0x009
#      define MV_SWITCH_LED3_LINK_ACT      0x0A0

#    define MV_SWITCH_LED_RATE_CTRL_REG     0x6
#    define MV_SWITCH_LED_SPECIAL_CTRL_REG  0x7
#      define MV_SWITCH_LED_SPECIAL_NONE  0x000
#  define MV_SWITCH_LED_CTRL_DATA_MASK  0x03FF

#define MV_SWITCH_LED_WRITE(ledReg, regValue)                                   \
    (MV_SWITCH_LED_CTRL_UPDATE |                                                \
     ((ledReg & MV_SWITCH_LED_CTRL_PTR_MASK) << MV_SWITCH_LED_CTRL_PTR_SHIFT) | \
     (regValue & MV_SWITCH_LED_CTRL_DATA_MASK))




/* CAL_ICS board-specific switch init code */
MV_VOID mvEthE6350RSwitchInit();

#endif /* #ifndef __INCETHSWITCHH */
