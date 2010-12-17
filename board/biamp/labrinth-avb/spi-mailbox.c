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
  SPI_MBOX_WRITE_REG(SPI_MBOX_CTRL,SPI_MBOX_ENABLE);
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
    uint32_t ctrl_reg = SPI_MBOX_READ_REG(SPI_MBOX_CTRL);
    if (ctrl_reg & SPI_MBOX_HOST2SLAVE)
    {
      uint32_t bufLen = SPI_MBOX_READ_REG(SPI_MBOX_MSG_LENGTH);
      if (*size < bufLen) break;
      *size = bufLen;

      for (idx=0;idx<((*size+3)/4);idx++)
      {
        ((uint32_t *)buffer)[idx] = ((uint32_t *)SPI_MBOX_DATA)[idx];
      }

      SPI_MBOX_WRITE_REG(SPI_MBOX_CTRL,SPI_MBOX_MSG_CONSUMED | SPI_MBOX_ENABLE);
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
  for (idx=0;idx<(size+3)/4;idx++)
  {
    ((uint32_t *)SPI_MBOX_DATA)[idx] = ((uint32_t *)buffer)[idx];
  }
  SPI_MBOX_WRITE_REG(SPI_MBOX_MSG_LENGTH,size);
}
