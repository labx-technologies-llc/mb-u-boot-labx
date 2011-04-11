#include "spi-mailbox.h"

#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif


uint8_t bPollingSPIMbox=FALSE;
uint8_t bWaitingForSPIMsg=FALSE;

void SetupSPIMbox(void)
{
  /* Clear the message ready flag and reset / enable the mailbox */
  SPI_MBOX_WRITE_REG(SPI_MBOX_CTRL,  SPI_MBOX_DISABLE);
  SPI_MBOX_WRITE_REG(SPI_MBOX_IRQ_MASK,  SPI_MBOX_NO_IRQS);
  SPI_MBOX_WRITE_REG(SPI_MBOX_CTRL,  SPI_MBOX_ENABLE);
}

/**
 * Reads a message out of the SPI Mailbox. Controlled by
 * global variable bPollingSPIMbox which can be used to
 * force return
 *
 * Paramaters:
 *       buffer - Buffer to read data into
 *       size   - [IN]  - Size of input buffer
 *              - [OUT] - Number of bytes read
 *
 * Returns:
 *       TRUE  - Message was read
 *       FALSE - No message was read
 */
int ReadSPIMailbox(uint8_t *buffer, uint32_t *size)
{
  int bStatus = FALSE;
  int idx;

  bWaitingForSPIMsg = TRUE;
  while(bWaitingForSPIMsg)
  {
    /* Use the IRQ flags register to determine when a message has been received,
     * despite the fact that we don't actually use interrupts.
     */
    uint32_t flags_reg = SPI_MBOX_READ_REG(SPI_MBOX_FLAGS);
    if(flags_reg & SPI_MBOX_HOST2SLAVE)
    {
      /* A message has been received from the host, obtain its length */
      uint32_t bufLen = SPI_MBOX_READ_REG(SPI_MBOX_MSG_LENGTH);
      if (*size < bufLen) {
        /* We received a message longer than the requested message size; bail
         * out and return FALSE as an indication
         */
        break;
      }

      /* Received a message, inform the caller of its length */
      *size = bufLen;

      /* Retrieve and buffer the message words for the client */
      for (idx = 0; idx < ((*size + 3) / 4); idx++) {
        ((uint32_t *) buffer)[idx] = ((uint32_t *) SPI_MBOX_DATA)[idx];
      }

      SPI_MBOX_WRITE_REG(SPI_MBOX_CTRL, (SPI_MBOX_MSG_CONSUMED | SPI_MBOX_ENABLE));
      bStatus = TRUE;
      bWaitingForSPIMsg = FALSE;
    }
  }
  return bStatus;
}

/**
 * Writes a message out to the SPI mailbox
 *
 * buffer - Pointer to data buffer to copy message from
 * size   - size of buffer to write
 */
void WriteSPIMailbox(uint8_t *buffer, uint32_t size)
{
  int idx;

  /* Write the response words into the data buffer */
  for(idx = 0; idx < (size + 3) / 4; idx++) {
    ((uint32_t *) SPI_MBOX_DATA)[idx] = ((uint32_t *) buffer)[idx];
  }

  /* Commit the response message to the host */
  SPI_MBOX_WRITE_REG(SPI_MBOX_MSG_LENGTH, size);
}
