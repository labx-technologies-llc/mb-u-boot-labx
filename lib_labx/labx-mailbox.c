#include "labx-mailbox.h"

#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif

uint8_t bPollingLabXMbox = FALSE;
uint8_t bWaitingForMsg = FALSE;

void SetupLabXMailbox(void)
{
  /* Clear the message ready flag and reset / enable the mailbox */
  LABX_MBOX_WRITE_REG(SUPRV_CONTROL_REG, LABX_MBOX_DISABLE);
  LABX_MBOX_WRITE_REG(SUPRV_IRQ_MASK_REG, NO_IRQS);
  LABX_MBOX_WRITE_REG(SUPRV_CONTROL_REG, LABX_MBOX_ENABLE);
}

/**
 * Reads a message out of the LabX Mailbox. Controlled by
 * global variable bPollingLabXMbox which can be used to
 * force return
 *
 * Paramaters:
 *       buffer     - Buffer to read data into
 *       size       - [IN]  - Size of input buffer
 *                  - [OUT] - Number of bytes read
 *       pollForMsg - [IN]  - Flag indicating if a continuous 
 *                            loop is used to wait for a msg
 *
 * Returns:
 *       TRUE  - Message was read
 *       FALSE - No message was read
 */
int ReadLabXMailbox(uint8_t *buffer, uint32_t *size, uint8_t pollForMsg)
{
  int bStatus = FALSE;
  int idx;

  bWaitingForMsg = TRUE;
  while(bWaitingForMsg)
  {
    /* Use the IRQ flags register to determine when a message has been received,
     * despite the fact that we don't actually use interrupts.  Write the flags
     * back each time to clear any pending events prior to servicing.
     */
    uint32_t flags_reg = LABX_MBOX_READ_REG(SUPRV_IRQ_FLAGS_REG);
    LABX_MBOX_WRITE_REG(SUPRV_IRQ_FLAGS_REG, flags_reg);
    if(flags_reg & SUPRV_IRQ_0)
    {
      /* A message has been received from the host, obtain its length */
      uint32_t bufLen = LABX_MBOX_READ_REG(SUPRV_MSG_LEN_REG);
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
        ((uint32_t *) buffer)[idx] = ((uint32_t *) LABX_MBOX_DATA)[idx];
      }

      bStatus = TRUE;
      bWaitingForMsg = FALSE;
    }
    
    if(!pollForMsg && !bStatus)
    {
      bStatus = FALSE;
      bWaitingForMsg = FALSE;
    }
  }
  return bStatus;
}

/**
 * Writes a message out to the LabX mailbox
 *
 * buffer - Pointer to data buffer to copy message from
 * size   - size of buffer to write
 */
void WriteLabXMailbox(uint8_t *buffer, uint32_t size)
{
  int widx = 0; 
  int bidx = 0;
  uint32_t requestWord;

  /* Write the response words into the data buffer */
  for(widx = 0; widx < (size + 3) / 4; widx++) {
    requestWord = buffer[bidx+3] | (buffer[bidx+2]<<8) | (buffer[bidx+1]<<16) | (buffer[bidx]<<24); 
    ((uint32_t *) LABX_MBOX_DATA)[widx] = requestWord;
    bidx+=4;
  }
  
  /* Commit the response message to the host */
  LABX_MBOX_WRITE_REG(HOST_MSG_LEN_REG, size);
}

void TrigAsyncLabXMailbox(void)
{
  LABX_MBOX_WRITE_REG(SUPRV_TRIG_ASYNC_REG, SUPRV_IRQ_1);
}
