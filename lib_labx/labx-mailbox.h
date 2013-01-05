#ifndef _LABX_MAILBOX_H_
#define _LABX_MAILBOX_H_
#include "xparameters.h"
#include <linux/types.h>

/* LabXMailbox Register Map */
#ifdef XPAR_LABX_SPI_MAILBOX_0_BASEADDR
#define LABX_MBOX_BASE              (XPAR_LABX_SPI_MAILBOX_0_BASEADDR)
#endif
#ifdef XPAR_UHI_MAILBOX0_BASEADDR
#define LABX_MBOX_BASE              (XPAR_UHI_MAILBOX0_BASEADDR)
#endif

#ifndef LABX_MBOX_BASE
#error No Lab X Mailbox base address defined! (No suitable definition found for LABX_MBOX_BASE)
#endif

/* Maximum number of mailboxes and instance count */
#define MAX_MAILBOX_DEVICES 1 

/* Macros for determining sub-addresses for address ranges and individual registers.
 * These are affected by the amount of address space devoted to packet template and 
 * microcode storage, which is hardware-configurable.
 */

#define REGISTER_RANGE      (0x0)
#define MSG_RAM_RANGE       (0x800)

/* Global control registers */
#define SUPRV_CONTROL_REG        (LABX_MBOX_BASE + 0x00)
#  define LABX_MBOX_DISABLE      (0x00000000)
#  define LABX_MBOX_ENABLE	 (0x00000001)

#define SUPRV_IRQ_MASK_REG   	 (LABX_MBOX_BASE + 0x04)
#define SUPRV_IRQ_FLAGS_REG    	 (LABX_MBOX_BASE + 0x08)
#define NO_IRQS      		 (0x00000000)
#define SUPRV_IRQ_0    	         (0x00000001)
#define SUPRV_IRQ_1    	         (0x00000002)
#define ALL_IRQS     		 (0xFFFFFFFF)

#define SUPRV_MSG_LEN_REG        (LABX_MBOX_BASE + 0x0C)
#define HOST_MSG_LEN_REG         (LABX_MBOX_BASE + 0x0C)  

#define SUPRV_TRIG_ASYNC_REG     (LABX_MBOX_BASE + 0x14)

/* Mailbox Data Register (Send/Receive) 
 * Send - Write data to this area
 * Rcv  - Read incoming messages from this location
 */
#define LABX_MBOX_DATA              (LABX_MBOX_BASE + 0x0800)

/* Low-level macros for read and write from / to the bridge */
#define LABX_MBOX_READ_REG(reg) ( *((volatile unsigned long *)reg) )
#define LABX_MBOX_WRITE_REG(reg,val) ( *((volatile unsigned long *)reg) = val )

/* Public functions */
extern void SetupLabXMailbox(void);
extern int ReadLabXMailbox(uint8_t *buffer, uint32_t *size, uint8_t pollForMsg);
extern void WriteLabXMailbox(uint8_t *buffer, uint32_t size);
extern void TrigAsyncLabXMailbox(void);
#endif
