#include "config.h"

#ifdef CONFIG_FIRMWARE_UPDATE

#include "hush.h"
#include "labx-mailbox.h"
#include "preboot.h"
#include "idl/FirmwareUpdate_unmarshal.h"
#include "idl/FirmwareUpdate.h"
#include "xparameters.h"
#include <linux/types.h>
#include <common.h>
#include <u-boot/crc.h>
#include "asm/microblaze_fsl.h"

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0 
#endif

//#define _LABXDEBUG

#ifndef CONFIG_LABX_PREBOOT
#error "Lab X pre-boot procedures required for firmware update support (CONFIG_LABX_PREBOOT not defined)."
#endif

#define FWUPDATE_BUFFER DDR_CONTROL_MPMC_BASEADDR

/* Constant definitions for event types supported by the AVB platform.
* These are hash values computed from the stream class names.
*/
const uint32_t NULL_EVENT            = 0x00000000;
const uint32_t FIRMWARE_UPDATE_EVENT = 0x846C034D;

FirmwareUpdate__FirmwareUpdateExecutionState *state; 

int bootDelay = FALSE;
int firmwareUpdate = FALSE;
int executeUpdate = FALSE;
int queueEnabled = FALSE;
int eventValid = FALSE;

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
   * region, which is at the start of DDR.
   */
  fwUpdateCtxt.bUpdateInProgress     = TRUE;
  fwUpdateCtxt.length                = length;
  fwUpdateCtxt.cmd                   = cmd;
  fwUpdateCtxt.bytesReceived         = 0;
  fwUpdateCtxt.fwImageBase           = (uint8_t*) DDR_CONTROL_MPMC_BASEADDR;
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
  AvbDefs__ErrorCode returnValue = e_EC_SUCCESS;

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
    executeUpdate = TRUE;  
 
    /* The firmware update is now no longer in progress */
    fwUpdateCtxt.bUpdateInProgress = FALSE;
  }
  else fwUpdateCtxt.fwImagePtr += data->m_size;

  return(returnValue);
}

int doCrcCheck(void) {
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

int executeFirmwareUpdate(void) {

  if (doCrcCheck() == FALSE) {
    *state = UPDATE_CORRUPT_IMAGE;
    return(1);
  }

  /* Invoke the HUSH parser on the command */
  if(parse_string_outer(fwUpdateCtxt.cmd,
                        (FLAG_PARSE_SEMICOLON | FLAG_EXIT_FROM_LOOP)) != 0) {
    *state = UPDATE_NOT_EXECUTED;
    return(1);
  }
   
  *state = UPDATE_SUCCESS;
  return(0);
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
 
  puts("Firmware Update Requested from HOST\n");
  firmwareUpdate = TRUE;
  return(returnValue);
}

AvbDefs__ErrorCode requestBootDelay(void) {
  int returnValue = e_EC_SUCCESS;
 
  puts("Boot Delay Requested from HOST\n");
  bootDelay = TRUE;
  return(returnValue);
}

AvbDefs__ErrorCode get_eventQueueEnabled(uint32_t eventCode,
                                         bool *enabled) {
  eventCode = FIRMWARE_UPDATE_EVENT;
  *enabled = queueEnabled;
  return(e_EC_SUCCESS);
}

AvbDefs__ErrorCode set_eventQueueEnabled(uint32_t eventCode,
                                         bool enabled) {
  printf("Setting event queue for %08X %s\n", eventCode, (enabled ? "enabled" : "disabled"));
  queueEnabled = enabled;
  return(e_EC_SUCCESS);
}

AvbDefs__ErrorCode get_nextQueuedEvent(FirmwareUpdate__GenericEvent *event) { 
  int returnValue = e_EC_SUCCESS;

  if(eventValid) { 
    event->eventCode = FIRMWARE_UPDATE_EVENT;
    event->data.m_data[0] = *state;
    event->data.m_size = 0x01;
    eventValid = FALSE;
    printf("Sending firmware update event status for %08X of 0x%02X\n", event->eventCode, *state);
  } else {
      event->eventCode = NULL_EVENT;
  }

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
  uint32_t reqSize = sizeof(RequestMessageBuffer_t);
  uint32_t respSize;

  /* Continuously read request messages from the host and unmarshal them */
  while (ReadLabXMailbox(request, &reqSize, TRUE)) {
    /* Unmarshal the received request */
    switch(getClassCode_req(request)) {
	
    case k_CC_FirmwareUpdate:
    case k_CC_AvbSystem:
#ifdef _LABXDEBUG
      int i;
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
      executeFirmwareUpdate();
      executeUpdate = FALSE;
      if(queueEnabled) {
        eventValid = TRUE;
        TrigAsyncLabXMailbox();
      }
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
 *           boot delay requested from host, nonzero otherwise
 */
int CheckFirmwareUpdate(void)
{
  int doUpdate = 0;
  int i;

  uint32_t reqSize = sizeof(RequestMessageBuffer_t);
  uint32_t respSize;
  int returnValue = 0;

  /* If this is a fallback FPGA, we wait for
   * a firmware update image unconditionally. */
  if(labx_is_fallback_fpga()) {
    puts("Run-time FPGA reconfiguration failed.\n");
    /* TODO: there is currently no notification provided
     * to the host when a failure to reconfigure to the
     * runtime FPGA occurred and that the host needs to
     * provide a firmware update image. */
    doUpdate = 1;
  }

  if(!doUpdate) {
    /* Check GPIOs, if any are defined for the platform,
       for boot delay and for firmware update mode. */
#if defined(XPAR_XPS_GPIO_0_BASEADDR) && defined(GPIO_FIRMWARE_UPDATE_BIT)
    if(~*((volatile unsigned long*)XPAR_XPS_GPIO_0_BASEADDR) & (0x1 << GPIO_FIRMWARE_UPDATE_BIT)) {
      puts("Firmware update requested via GPIO\n");
      doUpdate = 1;
    }
#endif
  }

  /* Check the mailbox every quarter of a second for a
     total of 1 second to enter into firmware update */
  if(!doUpdate && !bootDelay) {
    /* Enable the mailbox. */
    SetupLabXMailbox();

    puts("Checking for firmware update request from host... ");
    for(i = 0; i < 4; ++i) {
      if(ReadLabXMailbox(request, &reqSize, FALSE)) {
        puts("requested\n");
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
          /* Report a malformed request */
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
      } else {
        udelay(250000);
      }
    }
    if(i == 4) {
      puts("none requested\n");
    }
  }

  /* These variables are checked like this because
   * they can be set asynchronously (such as over IDL). */
  if(firmwareUpdate) {
    doUpdate = 1;
  } else if(bootDelay) {
    returnValue = 1;
  }

  /* Perform an update if required for any reason */
  if(doUpdate) {
    puts("Waiting to receive firmware update image...\n");
    DoFirmwareUpdate();
    puts("Firmware update completed, waiting for reset from host\n");
    while(1);
  }

  /* Return, supplying a nonzero value if a boot delay was requested.
   * If a firmware update was requested, we will never get here. */
  return(returnValue);
}

#endif /* CONFIG_FIRMWARE_UPDATE */
