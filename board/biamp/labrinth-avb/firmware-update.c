#include "hush.h"
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

FirmwareUpdate__ErrorCode executeFirmwareUpdate(void);

/**
 * Simple structure to encapsulate firmware update globals
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
  /*  printf("Got startFirmwareUpdate(\"%s\", %d, 0x%08X\n", cmd, length, crc); */
  if(fwUpdateCtxt.bUpdateInProgress) return e_EC_UPDATE_ALREADY_IN_PROGRESS;

  /* Initialize the firware update context; load the binary image to the "clobber"
   * region, which is at the start of DDR2.
   */
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

  /*  printf("BLK[%d] : 0x%08X @ 0x%08X, sz %d\n", fwUpdateCtxt.bytesReceived,
         fwUpdateCtxt.fwImagePtr, *((uint32_t *) data->m_data),
         data->m_size);
  */

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
  uint32_t crc = crc32(0, fwUpdateCtxt.fwImageBase, fwUpdateCtxt.length);
  /* printf("Calculated CRC32 = 0x%08X, supplied CRC32 = 0x%08X\n", crc, fwUpdateCtxt.crc); */
  return (crc == fwUpdateCtxt.crc);
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
  int returnValue;

  /* Invoke the HUSH parser on the command */
  /* printf("SPI sendCommand: \"%s\", strlen = %d\n", cmd, strlen(cmd)); */
  if(parse_string_outer(cmd, (FLAG_PARSE_SEMICOLON | FLAG_EXIT_FROM_LOOP)) != 0) {
    return e_EC_NOT_EXECUTED;
  }

  return e_EC_SUCCESS;
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
#define BP_GPIO_DATA (*((volatile unsigned long*) XPAR_XPS_GPIO_0_BASEADDR))
#define BP_GPIO_BIT  (0x00000001)

/**
 * Read the GPIO that tells if a FW update is requested from Host
 * processor
 *
 * Returns - 1 - If GPIO is set
 *           0 - GPIO not set
 **/
int ReadFWUpdateGPIO(void)
{
  /* Examine the GPIO signal from the backplane to see if the host is
   * initiating a firware update or not
   */
  return((BP_GPIO_DATA & BP_GPIO_BIT) != 0);
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

  u8 macaddr[16];
  char fdtmac0[100];
  char fdtmac1[100];
  int fdt0 = 0;
  int fdt1 = 0;

  if (ReadFWUpdateGPIO())
  {
    printf("Firmware Update Requested from HOST, starting Firmware Update\n");
    DoFirmwareUpdate();
    printf("Firmware Update Completed, waiting for reset from Host\n");
    while(1);
  }

  printf("No Firmware update requested\n");

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

}
