#ifndef BIAMP_LABRINTH_SPI_MAILBOX
#define BIAMP_LABRINTH_SPI_MAILBOX
#include "xparameters.h"
#include <linux/types.h> 

/* SPI Mailbox Register Map */
#define SPI_MBOX_BASE              (XPAR_BIAMP_SPI_MAILBOX_0_BASEADDR)

/* Control Register */
#define SPI_MBOX_CTRL              (SPI_MBOX_BASE + 0x00)
#define SPI_MBOX_ENABLE            (0x000000001)
#define SPI_MBOX_MSG_CONSUMED      (0x000000002)         /* Notify host we have consumed a message            */
#define SPI_MBOX_MSG_RDY           (0x000000004)         /* Notifies us that a message from the host is ready */

/* IRQ Mask Register */
#define SPI_MBOX_IRQ_MASK          (SPI_MBOX_BASE + 0x04)

/* IRQ Flags Register */
#define SPI_MBOX_FLAGS             (SPI_MBOX_BASE + 0x08)
#define SPI_MBOX_HOST2SLAVE        0x000000001           /* Notify slave that there is a msg */
#define SPI_MBOX_SLAVE2HOST        0x000000002

/* 
 * Mailbox Message Length Register 
 * On Write - How big message to send to host is.
 * On Read  - How bit incoming message is
 */
#define SPI_MBOX_MSG_LENGTH        (SPI_MBOX_BASE + 0x0C)

/* Mailbox Data Register (Send/Receive) 
 * Send - Write data to this area
 * Rcv  - Read incoming messages from this location
 */
#define SPI_MBOX_DATA              (SPI_MBOX_BASE + 0x0400)

/* Low-level macros for read and write from / to the mailbox */
#define SPI_MBOX_READ_REG(reg) ( *((volatile unsigned long *)reg) )
#define SPI_MBOX_WRITE_REG(reg,val) ( *((volatile unsigned long *)reg) = val )

/* Public functions */
extern void SetupSPIMbox(void);
extern int ReadSPIMailbox(uint8_t *buffer, uint32_t *size);
extern void WriteSPIMailbox(uint8_t *buffer, uint32_t size);

#endif
