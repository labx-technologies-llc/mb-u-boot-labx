#include "spi-mailbox.h"
#include "FirmwareUpdate_unmarshal.h"
#include "FirmwareUpdate.h"
#include "xparameters.h"
#include <linux/types.h>
#include <common.h>
#include <u-boot/crc.h>

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0 
#endif

#define MAX_CMD_LENGTH 256


#define FWUPDATE_BUFFER XPAR_DDR2_CONTROL_MPMC_BASEADDR

extern int ReadSPIMailbox(uint8_t *buffer, uint32_t *size);
extern void WriteSPIMailbox(uint8_t *buffer, uint32_t size);
FirmwareUpdate__ErrorCode executeFirmwareUpdate(void);

/**
 * Simple structure to encapsulate firmwar update globals
 */
typedef struct
{
    uint8_t  bUpdateInProgress;
    uint32_t crc;
    uint32_t length;
    uint32_t bytesReceived;
    uint8_t *fwImageBase;
    uint8_t *fwImagePtr;
    string_t cmd;
} FirmwareUpdateCtxt_t;

FirmwareUpdateCtxt_t fwUpdateCtxt;

/**
 * Start a firmware update session.
 *
 * cmd      - String u-boot shell command (i.e. run update_fpga, run update_linux, run update_fpga etc)
 * length   - Size of the fw image that is going to be sent via SendDatacmd
 * crc      - The expected CRC value for the coming FW image
 */
FirmwareUpdate__ErrorCode startFirmwareUpdate(string_t cmd, uint32_t length, uint32_t crc)
{
  if(fwUpdateCtxt.bUpdateInProgress) return e_EC_UPDATE_ALREADY_IN_PROGRESS;
  fwUpdateCtxt.bUpdateInProgress = TRUE;
  fwUpdateCtxt.crc = crc;
  fwUpdateCtxt.length = length;
  fwUpdateCtxt.cmd = cmd;
  fwUpdateCtxt.bytesReceived = 0;
  fwUpdateCtxt.fwImageBase = (uint8_t *)XPAR_DDR2_CONTROL_MPMC_BASEADDR;
  fwUpdateCtxt.fwImagePtr = fwUpdateCtxt.fwImageBase;
  return e_EC_SUCCESS;
}

/**
 * Accept a Data packet for a firmware update. Must be called while we are in the process 
 * of a firmware update (i.e. startFirmwareUpdate() called first. 
 *
 * data - Data that contains the datapacket. 
 */
FirmwareUpdate__ErrorCode sendDataPacket(FirmwareUpdate__FwData *data)
{
  if(!fwUpdateCtxt.bUpdateInProgress) return e_EC_UPDATE_NOT_IN_PROGRESS;
  memcpy(fwUpdateCtxt.fwImagePtr,data->m_data,data->m_size);
  fwUpdateCtxt.bytesReceived+=data->m_size;
  if (fwUpdateCtxt.bytesReceived >= fwUpdateCtxt.length)
  {
    /* We are done with the transfer, start the flash update process */
    return executeFirmwareUpdate();
  }
  else
  {
    fwUpdateCtxt.fwImagePtr+=data->m_size;
  }

  return e_EC_SUCCESS;
}

uint8_t doCrcCheck(void)
{
  uint32_t crc = crc32(fwUpdateCtxt.crc,fwUpdateCtxt.fwImageBase,fwUpdateCtxt.length);
  return (crc == fwUpdateCtxt.crc);
}


FirmwareUpdate__ErrorCode executeFirmwareUpdate(void)
{
  if (doCrcCheck() == FALSE)
    return e_EC_CORRUPT_IMAGE;

  if (run_command(fwUpdateCtxt.cmd,0) < 0)
  {
    return e_EC_NOT_EXECUTED;
  }

  return e_EC_SUCCESS;
}



/**
 * Perform a firmware update throught he SPI mailbox interface
 *
 * Return 1 - Success
 *        0 - No Success
 */
int DoFirmwareUpdate(void)
{
  RequestMessageBuffer_t request;
  ResponseMessageBuffer_t response;
  uint32_t reqSize = sizeof(RequestMessageBuffer_t);


  while (ReadSPIMailbox(request,&reqSize))
  {
    unmarshal(request,response);
  }

  return 1;
}



/**
 * Read the GPIO that tells if a FW update is requested from Host
 * processor
 *
 * Returns - 1 - If GPIO is set
 *           0 - GPIO not set
 **/
int ReadFWUpdateGPIO(void)
{
  /* TODO - Read the GPIO to see if Host is initiating FW Update on us */
  return 0;
}

/**
 * Check to see if a Firmware update is being request, if so 
 * carry out firmware update and return.
 *
 * Returns - Void - Will never return if FW update happens(Should be reset by host)
 *
 */
void CheckFirmwareUpdate(void)
{
  SetupSPIMbox();

  if (ReadFWUpdateGPIO())
  {
    printf("Firmware Update Requested from HOST, starting Firmware Update\n");
    DoFirmwareUpdate();
    printf("Firmware Update Completed, waiting for reset from Host\n");
    while(1);
  }

  printf("No Firmware update requested\n");
}