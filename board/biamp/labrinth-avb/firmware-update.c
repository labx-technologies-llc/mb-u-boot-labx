#include "hush.h"
#include "spi-mailbox.h"
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

#define MAX_CMD_LENGTH 256


#define FWUPDATE_BUFFER XPAR_DDR2_CONTROL_MPMC_BASEADDR

/* Bit definition which kicks off a dump of the FIFO to the ICAP */
#define FINISH_FSL_BIT (0x80000000)

/* "Magic" value written to the ICAP GENERAL5 register to detect fallback */
#define GENERAL5_MAGIC (0x0ABCD)

/* Buffer for constructing command strings */
#define CMD_FORMAT_BUF_SZ (256)
static char *commandBuffer[CMD_FORMAT_BUF_SZ];

FirmwareUpdate__ErrorCode executeFirmwareUpdate(void);

/**
 * Structure storing the revision and CRC32 of each run-time image.
 */
typedef struct {
  uint32_t revision;
  uint32_t crc;
} RuntimeImageRecord;

/* Constant definition for a "blank" run-time image revision in Flash */
#define BLANK_IMAGE_REVISION (0xFFFFFFFF)

/**
 * Simple structure to encapsulate firmware update globals
 */
typedef struct {
  uint8_t             bUpdateInProgress;
  uint32_t            imageIndex;
  uint32_t            length;
  RuntimeImageRecord  imageRecord;
  uint32_t            bytesReceived;
  uint8_t            *fwImageBase;
  uint8_t            *fwImagePtr;
  string_t            cmd;
} FirmwareUpdateCtxt_t;

FirmwareUpdateCtxt_t fwUpdateCtxt;

/**
 * Accessor for the ExecutingImageType attribute; this tells the client
 * that we are in the bootloader, not the main image.
 */
FirmwareUpdate__ErrorCode get_ExecutingImageType(FirmwareUpdate__CodeImageType *value) {
  *value = e_CODE_IMAGE_BOOT;
  return(e_EC_SUCCESS);
}

/**
 * Start a firmware update session.
 *
 * @param image    - Enumerated value identifying which run-time image is
 *                   being updated
 * @param cmd      - String command to invoke after all data has been sent
 * @param length   - Length, in bytes, of the data image which will be sent
 * @param revision - Revision quadlet for the image
 * @param crc      - Expected CRC32 for the data to be checked
 */
FirmwareUpdate__ErrorCode startFirmwareUpdate(FirmwareUpdate__RuntimeImageType image,
                                              string_t cmd,
                                              uint32_t length,
                                              uint32_t revision,
                                              uint32_t crc) {
  printf("Got startFirmwareUpdate(\"%s\", %d, 0x%08X\n", cmd, length, crc);
  if(fwUpdateCtxt.bUpdateInProgress) return e_EC_UPDATE_ALREADY_IN_PROGRESS;

  /* Initialize the firware update context; load the binary image to the "clobber"
   * region, which is at the start of DDR2.
   */
  fwUpdateCtxt.bUpdateInProgress    = TRUE;
  fwUpdateCtxt.imageIndex           = (uint32_t) image;
  fwUpdateCtxt.length               = length;
  fwUpdateCtxt.imageRecord.revision = revision;
  fwUpdateCtxt.imageRecord.crc      = crc;
  fwUpdateCtxt.cmd                  = cmd;
  fwUpdateCtxt.bytesReceived        = 0;
  fwUpdateCtxt.fwImageBase          = (uint8_t*) XPAR_DDR2_CONTROL_MPMC_BASEADDR;
  fwUpdateCtxt.fwImagePtr           = fwUpdateCtxt.fwImageBase;

  /* Sanity-check the image index */
  return(((image >= e_IMAGE_FPGA) & (image <= e_IMAGE_SETTINGSFS)) ? 
         e_EC_SUCCESS : e_EC_NOT_EXECUTED);
}

/**
 * Accept a Data packet for a firmware update. Must be called while we are in the process 
 * of a firmware update (i.e. startFirmwareUpdate() called first. 
 *
 * data - Data that contains the datapacket. 
 */
FirmwareUpdate__ErrorCode sendDataPacket(FirmwareUpdate__FwData *data)
{
  FirmwareUpdate__ErrorCode sendSuccess = e_EC_SUCCESS;

  if(!fwUpdateCtxt.bUpdateInProgress) return e_EC_UPDATE_NOT_IN_PROGRESS;
  memcpy(fwUpdateCtxt.fwImagePtr,data->m_data,data->m_size);
  fwUpdateCtxt.bytesReceived+=data->m_size;

  /*  printf("BLK[%d] : 0x%08X @ 0x%08X, sz %d\n", fwUpdateCtxt.bytesReceived,
         fwUpdateCtxt.fwImagePtr, *((uint32_t *) data->m_data),
         data->m_size);
  */

  if(fwUpdateCtxt.bytesReceived >= fwUpdateCtxt.length) {
    char *envString;
    RuntimeImageRecord *recordPtr;

    /* We are done with the transfer, start the flash update process */
    printf("Received all %d bytes of image %d\n", 
           fwUpdateCtxt.length, fwUpdateCtxt.imageIndex);

    /* Before executing the command, ensure that the image CRC sector does
     * not already have a revision recorded for this specific image.  The host
     * should have erased the sector prior to commencing the update; therefore
     * there should be nothing other than a "blank" revision code.
     *
     * Each image has two 32-bit words recorded as a struct:
     *
     * Revision word
     * CRC32
     */
    envString  = getenv("imagecrcsstart");
    recordPtr  = (RuntimeImageRecord*) simple_strtoul(envString, NULL, 16);
    recordPtr += fwUpdateCtxt.imageIndex;
    printf("Got recordPtr = 0x%08X\n", (uint32_t) recordPtr);

    if(recordPtr->revision == BLANK_IMAGE_REVISION) {
      /* No existing revision, begin executing the update command */
      sendSuccess = executeFirmwareUpdate();

      /* If the Flash update was successful, update the image CRC sector
       * with the revision quadlet and CRC32 for the image.  This is done using
       * the flash subcommand cp.b.
       */
      sprintf(commandBuffer, "cp.b 0x%08X 0x%08X 0x08X",
              (uint32_t) &fwUpdateCtxt.imageRecord,
              (uint32_t) recordPtr,
              sizeof(fwUpdateCtxt.imageRecord));
      commandBuffer[CMD_FORMAT_BUF_SZ - 1] = '\0';

      printf("Writing image record with \"%s\"\n", commandBuffer);
      if(parse_string_outer(commandBuffer,
                            (FLAG_PARSE_SEMICOLON | FLAG_EXIT_FROM_LOOP)) != 0) {
        sendSuccess = e_EC_NOT_EXECUTED;
      }
    } else sendSuccess = e_EC_IMAGE_ALREADY_PRESENT;
  }
  else fwUpdateCtxt.fwImagePtr += data->m_size;

  return(sendSuccess);
}

uint8_t doCrcCheck(void)
{
  uint32_t crc = crc32(0, fwUpdateCtxt.fwImageBase, fwUpdateCtxt.length);
  printf("Calculated CRC32 = 0x%08X, supplied CRC32 = 0x%08X\n", 
         crc, fwUpdateCtxt.imageRecord.crc);
  return(crc == fwUpdateCtxt.imageRecord.crc);
}


FirmwareUpdate__ErrorCode executeFirmwareUpdate(void)
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

FirmwareUpdate__ErrorCode sendCommand(string_t cmd) {
  int returnValue = e_EC_SUCCESS;

  /* Invoke the HUSH parser on the command */
  printf("SPI sendCommand: \"%s\", strlen = %d\n", cmd, strlen(cmd));
  if(parse_string_outer(cmd, (FLAG_PARSE_SEMICOLON | FLAG_EXIT_FROM_LOOP)) != 0) {
    returnValue = e_EC_NOT_EXECUTED;
  }

  return(returnValue);
}

/* Statically-allocated request and response buffers for use with IDL */
static RequestMessageBuffer_t request;
static ResponseMessageBuffer_t response;

/**
 * Perform a firmware update throught he SPI mailbox interface
 *
 * Return 1 - Success
 *        0 - No Success
 */
int DoFirmwareUpdate(void)
{
  uint32_t reqSize = sizeof(RequestMessageBuffer_t);
  uint32_t respSize;

  /* Enable the SPI mailbox, which raises the BP_ATTN signal to indicate to
   * the host that we are ready.
   */
  SetupSPIMbox();

  /* Continuously read request messages from the host and unmarshal them */
  while (ReadSPIMailbox(request, &reqSize)) {
    /* Unmarshal the received request */
    unmarshal(request, response);

    /* Write the response out to the mailbox; before doing so, artificially
     * increase the response length by four to accommodate the four garbage bytes
     * which the mailbox inserts on reads.  The message length was originally defined
     * for Labrinth to include these bytes, and it has stuck.  Only actually
     * send the true size of the message, the dummy bytes are inherently added
     * by the gateware.
     */
    respSize = getLength_resp(response);
    setLength_resp(response, (respSize + MAILBOX_DUMMY_BYTES));
    WriteSPIMailbox(response, respSize);

    /* Re-set the max request size for the next iteration */
    reqSize = sizeof(RequestMessageBuffer_t);
  }

  return 1;
}

/*
 * GPIO definition for reading the backplane GPIO pin
 */
#define BP_GPIO_DATA   (*((volatile unsigned long*) XPAR_XPS_GPIO_0_BASEADDR))
#define BP_GPIO_BIT    (0x00000001)
#define BOOT_DELAY_BIT (0x00000008)

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

  u8 macaddr[16];
  char fdtmac0[100];
  char fdtmac1[100];
  char ubootmac[50];
  int fdt0 = 0;
  int fdt1 = 0;
  int returnValue = 0;
  int doUpdate = 0;
  u16 readValue;

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
  udelay(1000);

  // Trigger the FSL peripheral to drain the FIFO into the ICAP.
  // Wait briefly for the read to occur.
  putfslx(FINISH_FSL_BIT, 0, FSL_ATOMIC);
  udelay(1000);
  getfslx(readValue, 0, FSL_ATOMIC); // Read the ICAP result

  if (readValue == GENERAL5_MAGIC) {
    printf("Run-time FPGA reconfiguration failed\n");
    doUpdate = 1;
  } else if((BP_GPIO_DATA & BP_GPIO_BIT) != 0) {
    printf("Firmware Update Requested from HOST\n");
    doUpdate = 1;
  } else if((BP_GPIO_DATA & BOOT_DELAY_BIT) == 0) {
    printf("Boot delay requested\n");
    returnValue = 1;
  } else printf("No Firmware update requested\n");

  // Perform an update if required for any reason
  if(doUpdate) {
    printf("Entering firmware update\n");
    DoFirmwareUpdate();
    printf("Firmware update completed, waiting for reset from host\n");
    while(1);
  }

  //read from magic location in memory
  memcpy(macaddr, (void *)0x87FE8000, 16);

  //printf("MAC addresses (with prefix) are:\n");
  //printf("%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X\n", macaddr[0], macaddr[1],
  //	 macaddr[2], macaddr[3], macaddr[4], macaddr[5], macaddr[6], macaddr[7]);
  //printf("%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X\n", macaddr[8], macaddr[9],
  //	 macaddr[10], macaddr[11], macaddr[12], macaddr[13], macaddr[14], macaddr[15]);

  //TODO: Bytes 0 and 1 may be updated to some OUI
  //check that result 0 is valid
  if (macaddr[0] == 0 && macaddr[1] == 0 &&
      ((macaddr[2] != 0 && macaddr[2] != 0xFF) ||
       (macaddr[3] != 0 && macaddr[3] != 0xFF) ||
       (macaddr[4] != 0 && macaddr[4] != 0xFF) ||
       (macaddr[5] != 0 && macaddr[5] != 0xFF) ||
       (macaddr[6] != 0 && macaddr[6] != 0xFF) ||
       (macaddr[7] != 0 && macaddr[7] != 0xFF) )) {
    fdt0 = 1;
  }
  //check that result 1 is valid
  if (macaddr[8] == 0 && macaddr[9] == 0 &&
      ((macaddr[10] != 0 && macaddr[10] != 0xFF) ||
       (macaddr[11] != 0 && macaddr[11] != 0xFF) ||
       (macaddr[12] != 0 && macaddr[12] != 0xFF) ||
       (macaddr[13] != 0 && macaddr[13] != 0xFF) ||
       (macaddr[14] != 0 && macaddr[14] != 0xFF) ||
       (macaddr[15] != 0 && macaddr[15] != 0xFF) )) {
    fdt1 = 1;
  }
  
  //only bother loading up the device tree and booting from it if there is a valid address
  if(fdt0 || fdt1) {
    run_command("cp.b 0x87240000 0x88F40000 0x00020000", 0);
    run_command("fdt addr 0x88F40000 0x00020000", 0);
    
    if(fdt0) {
      //special case for fdt0: also modify MAC address in u-boot
      sprintf(ubootmac, "setenv ethaddr %02X:%02X:%02X:%02X:%02X:%02X",
	      macaddr[2], macaddr[3], macaddr[4], macaddr[5], macaddr[6], macaddr[7]);
      run_command(ubootmac, 0);

      sprintf(fdtmac0, "fdt set /plb@0/ethernet@82050000 local-mac-address [%02x %02x %02x %02x %02x %02x]",
	      macaddr[2], macaddr[3], macaddr[4], macaddr[5], macaddr[6], macaddr[7]);
    
      //modify fdt
      run_command(fdtmac0, 0);
    }
    if(fdt1) {
      sprintf(fdtmac1, "fdt set /plb@0/ethernet@82070000 local-mac-address [%02x %02x %02x %02x %02x %02x]",
	      macaddr[10], macaddr[11], macaddr[12], macaddr[13], macaddr[14], macaddr[15]);
      //modify fdt
      run_command(fdtmac1, 0);
    }
    //change the fdstart location
    run_command("set fdtstart 0x88F40000", 0);
  }

  return(returnValue);
}
