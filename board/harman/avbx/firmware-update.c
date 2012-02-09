#include "hush.h"
#include "labx-mailbox.h"
#include "FirmwareUpdate_unmarshal.h"
#include "FirmwareUpdate.h"
#include "xparameters.h"
#include <linux/types.h>
#include <common.h>
#include <u-boot/crc.h>
#include "microblaze_fsl.h"

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0 
#endif

//#define _LABXDEBUG

#define FWUPDATE_BUFFER XPAR_DDR2_CONTROL_MPMC_BASEADDR

/* Bit definition which kicks off a dump of the FIFO to the ICAP */
#define FINISH_FSL_BIT (0x80000000)

/* "Magic" value written to the ICAP GENERAL5 register to detect fallback */
#define GENERAL5_MAGIC (0x0ABCD)

AvbDefs__ErrorCode executeFirmwareUpdate(void);

int bootDelay = 0;
int firmwareUpdate = 0;
int executeUpdate = 0;

/**
 * Simple structure to encapsulate firmware update globals
 */
typedef struct {
  uint8_t              bUpdateInProgress;
  uint32_t             length;
  uint32_t             bytesReceived;
  uint8_t             *fwImageBase;
  uint8_t             *fwImagePtr;
  string_t             cmd;
} FirmwareUpdateCtxt_t;

FirmwareUpdateCtxt_t fwUpdateCtxt;

/**
 * Accessor for the ExecutingImageType attribute; this tells the client
 * that we are in the bootloader, not the main image.
 */
AvbDefs__ErrorCode get_ExecutingImageType(FirmwareUpdate__CodeImageType *value) {
  *value = e_CODE_IMAGE_BOOT;
  return(e_EC_SUCCESS);
}

/**
 * Start a firmware update session.
 *
 * @param cmd      - Command to be executed after all data is received
 * @param length   - Length, in bytes, of the data image which will be sent
 */
AvbDefs__ErrorCode startFirmwareUpdate(string_t cmd,
                                       uint32_t length) {
  AvbDefs__ErrorCode returnValue;

  printf("Got startFirmwareUpdate(\"%s\", %d)\n", cmd, length);

  /* Return a distinct error code if this call supercedes an update which
   * was already in progress.
   */
  returnValue = (fwUpdateCtxt.bUpdateInProgress ?
                 e_EC_UPDATE_ALREADY_IN_PROGRESS : e_EC_SUCCESS);

  /* Initialize the firware update context; load the binary image to the "clobber"
   * region, which is at the start of DDR2.
   */
  fwUpdateCtxt.bUpdateInProgress     = TRUE;
  fwUpdateCtxt.length                = length;
  fwUpdateCtxt.cmd                   = cmd;
  fwUpdateCtxt.bytesReceived         = 0;
  fwUpdateCtxt.fwImageBase           = (uint8_t*) XPAR_DDR2_CONTROL_MPMC_BASEADDR;
  fwUpdateCtxt.fwImagePtr            = fwUpdateCtxt.fwImageBase;

  return(returnValue);
}

/**
 * Accept a Data packet for a firmware update. Must be called while we are in the process 
 * of a firmware update (i.e. startFirmwareUpdate() called first. 
 *
 * data - Data that contains the datapacket. 
 */
AvbDefs__ErrorCode sendDataPacket(FirmwareUpdate__FwData *data)
{
  AvbDefs__ErrorCode sendSuccess = e_EC_SUCCESS;

  if(!fwUpdateCtxt.bUpdateInProgress) return e_EC_UPDATE_NOT_IN_PROGRESS;
  memcpy(fwUpdateCtxt.fwImagePtr,data->m_data,data->m_size);
  fwUpdateCtxt.bytesReceived+=data->m_size;

#ifdef _LABXDEBUG
    printf("BLK[%d] : 0x%08X @ 0x%08X, sz %d\n", fwUpdateCtxt.bytesReceived,
         fwUpdateCtxt.fwImagePtr, *((uint32_t *) data->m_data),
         data->m_size);
#endif

  if(fwUpdateCtxt.bytesReceived >= fwUpdateCtxt.length) {

    /* We are done with the transfer, start the flash update process */
    printf("Received all %d bytes of image\n", fwUpdateCtxt.length);

    /* Begin executing the update command */
    executeUpdate = 1;  
 
    /* The firmware update is now no longer in progress */
    fwUpdateCtxt.bUpdateInProgress = FALSE;
  }
  else fwUpdateCtxt.fwImagePtr += data->m_size;

  return(sendSuccess);
}

int doCrcCheck(void)
{
  int returnValue = 0;

  if(image_check_type((image_header_t *)fwUpdateCtxt.fwImageBase, IH_TYPE_KERNEL)) {
    printf("   Verifying Checksum ... ");
    setenv("crcreturn", "0");
    if (!image_check_dcrc ((image_header_t *)fwUpdateCtxt.fwImageBase)) {
      printf("Bad Data CRC - please retry\n");
      setenv("crcreturn", "1");
      goto end;
    }
    printf("OK\n");
    returnValue = 1;
  }
end:
  return(returnValue);
}

AvbDefs__ErrorCode executeFirmwareUpdate(void)
{
  if (doCrcCheck() == FALSE)
    return e_EC_CORRUPT_IMAGE;

  /* Invoke the HUSH parser on the command */
  if(parse_string_outer(fwUpdateCtxt.cmd,
                        (FLAG_PARSE_SEMICOLON | FLAG_EXIT_FROM_LOOP)) != 0) {
    return e_EC_NOT_EXECUTED;
  }

  return e_EC_SUCCESS;
}

AvbDefs__ErrorCode sendCommand(string_t cmd) {
  int returnValue = e_EC_SUCCESS;

  /* Invoke the HUSH parser on the command */
  printf("Mailbox sendCommand: \"%s\", strlen = %d\n", cmd, strlen(cmd));
  if(parse_string_outer(cmd, (FLAG_PARSE_SEMICOLON | FLAG_EXIT_FROM_LOOP)) != 0) {
    returnValue = e_EC_NOT_EXECUTED;
  }

  return(returnValue);
}

AvbDefs__ErrorCode remainInBootloader(void) {
  int returnValue = e_EC_SUCCESS;
 
  firmwareUpdate = 1;
  return(returnValue);
}

AvbDefs__ErrorCode requestBootDelay(void) {
  int returnValue = e_EC_SUCCESS;
 
  bootDelay = 1;
  return(returnValue);
}

/* Statically-allocated request and response buffers for use with IDL */
static RequestMessageBuffer_t request;
static ResponseMessageBuffer_t response;

/**
 * Perform a firmware update through the mailbox interface
 *
 * Return 1 - Success
 *        0 - No Success
 */
int DoFirmwareUpdate(void)
{
  AvbDefs__ErrorCode sendSuccess = e_EC_SUCCESS;
  uint32_t reqSize = sizeof(RequestMessageBuffer_t);
  uint32_t respSize;
  int i;

  /* Continuously read request messages from the host and unmarshal them */
  while (ReadLabXMailbox(request, &reqSize, TRUE)) {
    /* Unmarshal the received request */
    switch(getClassCode_req(request)) {
	
    case k_CC_FirmwareUpdate:
#ifdef _LABXDEBUG
      printf("Length: 0x%02X\n", getLength_req(request));
      printf("CC: 0x%02X\n", getClassCode_req(request));
      printf("SC: 0x%02X\n", getServiceCode_req(request));
      printf("AC: 0x%02X\n", getAttributeCode_req(request));
      printf("Request: [ ");
      for(i = 0; i < reqSize; i++) {
        printf("%02X ", request[i]);
      }
      printf("]\n");
#endif
      FirmwareUpdate__unmarshal(request, response);
      break;

    default:
      // Report a malformed request
      setStatusCode_resp(response, e_EC_INVALID_SERVICE_CODE);
      setLength_resp(response, getPayloadOffset_resp(response));
    }

    /* Write the response out to the mailbox */

      respSize = getLength_resp(response);
      setLength_resp(response, respSize);
      WriteLabXMailbox(response, respSize);

#ifdef _LABXDEBUG
      printf("Response Length: 0x%02X\n", respSize);
      printf("Response Code: 0x%04X\n", getStatusCode_resp(response));
      printf("Response: [ "); 
      for(i = 0; i < respSize; i++) {
        printf("%02X ", response[i]); 
      } 
      printf("]\n");
#endif

      if(executeUpdate) {
       sendSuccess = executeFirmwareUpdate();
       setStatusCode_resp(response, (uint16_t)sendSuccess);
       respSize = getLength_resp(response);
       setLength_resp(response, respSize);
       WriteLabXMailbox(response, respSize);
       TrigAsyncLabXMailbox(); 
       executeUpdate = FALSE;
#ifdef _LABXDEBUG
      printf("Response Length: 0x%02X\n", respSize);
      printf("Response Code: 0x%04X\n", getStatusCode_resp(response));
      printf("Response: [ "); 
      for(i = 0; i < respSize; i++) {
        printf("%02X", response[i]); 
      } 
      printf(" ]\n");
#endif
       break;
      } 

    /* Re-set the max request size for the next iteration */
    reqSize = sizeof(RequestMessageBuffer_t);
  }

  return 1;
}

/**
 * Check to see if a Firmware update is being request, if so 
 * carry out firmware update and return.
 *
 * Returns - Zero if no firmware update was performed and no
 *           boot delay desired, nonzero otherwise
 *
 */
int CheckFirmwareUpdate(void)
{
  int doUpdate = 0;
  int i;
  u16 readValue;

  uint32_t reqSize = sizeof(RequestMessageBuffer_t);
  uint32_t respSize;
  int returnValue = 0;

  // Enable the mailbox.
  SetupLabXMailbox();

  /* Read the GENERAL5 register from the ICAP peripheral to determine
   * whether the golden FPGA is still resident as the result of a re-
   * configuration fallback
   */
  putfslx(0x0FFFF, 0, FSL_CONTROL_ATOMIC);
  udelay(1000);

  /* First determine whether a reconfiguration has already been attempted
   * and failed (e.g. due to a corrupted run-time bitstream).  This is done
   * by checking for the fallback bit in the ICAP status register.
   *
   * Next, examine the GPIO signal from the backplane to see if the host is
   * initiating a firware update or not.  If not, see if a boot delay
   * is being requested by an installed jumper.
   */
  putfslx(0x0FFFF, 0, FSL_ATOMIC); // Pad words
  putfslx(0x0FFFF, 0, FSL_ATOMIC);
  putfslx(0x0AA99, 0, FSL_ATOMIC); // SYNC
  putfslx(0x05566, 0, FSL_ATOMIC); // SYNC

  // Read GENERAL5
  putfslx(0x02AE1, 0, FSL_ATOMIC);

  // Add some safety noops and wait briefly
  putfslx(0x02000, 0, FSL_ATOMIC); // Type 1 NOP
  putfslx(0x02000, 0, FSL_ATOMIC); // Type 1 NOP

  // Trigger the FSL peripheral to drain the FIFO into the ICAP.
  // Wait briefly for the read to occur.
  putfslx(FINISH_FSL_BIT, 0, FSL_ATOMIC);
  udelay(1000);
  getfslx(readValue, 0, FSL_ATOMIC); // Read the ICAP result

  /* Check the mailbox every quarter of a second for a
  total of 1 second to enter into firmware update */
  for (i=0; i<4; ++i) {
	if(ReadLabXMailbox(request, &reqSize, FALSE)) {
#ifdef _LABXDEBUG
          printf("Length: 0x%02X\n", getLength_req(request));
          printf("CC: 0x%02X\n", getClassCode_req(request));
          printf("SC: 0x%02X\n", getServiceCode_req(request));
          printf("AC: 0x%02X\n", getAttributeCode_req(request));
          printf("Request: [ ");
          for(i = 0; i < reqSize; i++) {
            printf("%02X ", request[i]);
          }
          printf("]\n");
#endif
		/* Unmarshal the received request */
		switch(getClassCode_req(request)) {

		case k_CC_FirmwareUpdate:
	  	  FirmwareUpdate__unmarshal(request, response);
		  break;

		default:
	  	// Report a malformed request
		  setStatusCode_resp(response, e_EC_INVALID_SERVICE_CODE);
		  setLength_resp(response, getPayloadOffset_resp(response));
		}

		respSize = getLength_resp(response);
		setLength_resp(response, respSize);
		WriteLabXMailbox(response, respSize);
#ifdef _LABXDEBUG
                printf("Response Length: 0x%02X\n", respSize);
                printf("Response Code: 0x%04X\n", getStatusCode_resp(response));
                printf("Response: [ "); 
                for(i = 0; i < respSize; i++) {
                  printf("%02X ", response[i]); 
                } 
                 printf("]\n");
#endif
    		/* Re-set the max request size for the next iteration */
		reqSize = sizeof(RequestMessageBuffer_t);

		/* Break out of loop, we received a valid request */
                if(getStatusCode_resp(response) == e_EC_SUCCESS) break;
	}
	else {
		udelay(250000);
  	}
  }

  if (readValue == GENERAL5_MAGIC) {
    printf("Run-time FPGA reconfiguration failed\n");
    doUpdate = 1;
  } else if(firmwareUpdate) {
    printf("Firmware Update Requested from HOST\n");
    doUpdate = 1;
  } else if(bootDelay) {
    printf("Boot Delay Requested from HOST\n");
    returnValue = 1;
  } else {
    printf("No Firmware update requested\n");
  }

  // Perform an update if required for any reason
  if(doUpdate) {
    printf("Entering firmware update\n");
    DoFirmwareUpdate();
    printf("Firmware update completed, waiting for reset from host\n");
    while(1);
  }

  // Return, supplying a nonzero value if a boot delay was requested;
  // if a firmware update was requested, we will never get here.
  return(returnValue);
}
