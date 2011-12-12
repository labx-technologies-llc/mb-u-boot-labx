#include "xparameters.h"
#include <linux/types.h>
#include <common.h>
#include <u-boot/crc.h>

/**
 * Check to see if a Firmware update is being request, if so 
 * carry out firmware update and return.
 *
 * Returns - Void - Will never return if FW update happens(Should be reset by host)
 *
 */
void CheckFirmwareUpdate(void)
{
	// TODO: Check for a firmware update here...
}
