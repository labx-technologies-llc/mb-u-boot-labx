#include "xparameters.h"
#include <linux/types.h>
#include <common.h>
#include <u-boot/crc.h>
#include "microblaze_fsl.h"

extern void icap_reset(int resetProduction);

#define FINISH_FSL_BIT (0x80000000)

static int isProductionBoot(void)
{
	unsigned short int val;
	// First find out if we had to use a fallback image
	// Synchronize command bytes
	putfslx(0x0FFFF, 0, FSL_ATOMIC); // Pad words
	putfslx(0x0FFFF, 0, FSL_ATOMIC);
	putfslx(0x0AA99, 0, FSL_ATOMIC); // SYNC
	putfslx(0x05566, 0, FSL_ATOMIC); // SYNC

	// Read the boot status register; we want 0s in bits FALLBACK_0 and FALLBACK_1.
	putfslx(0x02C01, 0, FSL_ATOMIC); // Read BOOTSTS
	// Fill up the FIFO with noops
	putfslx(0x02000, 0, FSL_ATOMIC); // Type 1 NOP
	putfslx(0x02000, 0, FSL_ATOMIC); // Type 1 NOP
	putfslx(0x02000, 0, FSL_ATOMIC); // Type 1 NOP
	putfslx(0x02000, 0, FSL_ATOMIC); // Type 1 NOP
	putfslx(0x02000, 0, FSL_ATOMIC); // Type 1 NOP
	putfslx(0x02000, 0, FSL_ATOMIC); // Type 1 NOP
	putfslx(0x02000, 0, FSL_ATOMIC); // Type 1 NOP

	// Write IPROG command
	putfslx(0x030A1, 0, FSL_ATOMIC); // Write CMD
	putfslx(0x0000E, 0, FSL_ATOMIC); // IPROG Command
	putfslx(0x02000, 0, FSL_ATOMIC); // Type 1 NOP

	// Trigger the FSL peripheral to drain the FIFO into the ICAP
	putfslx(FINISH_FSL_BIT, 0, FSL_ATOMIC);
	__udelay (1000);
	getfslx(val, 0, FSL_ATOMIC); // Read the ICAP result
	// FALLBACK_0 is Bit 1 and FALLBACK_1 is bit 7
	if ((val & 0x82) != 0) {
		printf("Booted from fallback image.  Boot status register = 0x%x\n", val);
		return 0;
	}

	// Next find out if the primary image was the production image
	// Synchronize command bytes
	putfslx(0x0FFFF, 0, FSL_ATOMIC); // Pad words
	putfslx(0x0FFFF, 0, FSL_ATOMIC);
	putfslx(0x0AA99, 0, FSL_ATOMIC); // SYNC
	putfslx(0x05566, 0, FSL_ATOMIC); // SYNC

	// Read the reconfiguration FPGA offset; we only need to read
	// the upper register and see if it is 0.
	putfslx(0x02A81, 0, FSL_ATOMIC); // Read GENERAL2
	// Fill up the FIFO with noops
	putfslx(0x02000, 0, FSL_ATOMIC); // Type 1 NOP
	putfslx(0x02000, 0, FSL_ATOMIC); // Type 1 NOP
	putfslx(0x02000, 0, FSL_ATOMIC); // Type 1 NOP
	putfslx(0x02000, 0, FSL_ATOMIC); // Type 1 NOP
	putfslx(0x02000, 0, FSL_ATOMIC); // Type 1 NOP
	putfslx(0x02000, 0, FSL_ATOMIC); // Type 1 NOP
	putfslx(0x02000, 0, FSL_ATOMIC); // Type 1 NOP

	// Write IPROG command
	putfslx(0x030A1, 0, FSL_ATOMIC); // Write CMD
	putfslx(0x0000E, 0, FSL_ATOMIC); // IPROG Command
	putfslx(0x02000, 0, FSL_ATOMIC); // Type 1 NOP

	// Trigger the FSL peripheral to drain the FIFO into the ICAP
	putfslx(FINISH_FSL_BIT, 0, FSL_ATOMIC);
	__udelay (1000);
	getfslx(val, 0, FSL_ATOMIC); // Read the ICAP result
	printf("FPGA boot image at address 0x%04xxxxx\n", (val << 1));
	__udelay (10000);
	return (val != 0);
}

int isICAPUpdateRequested(void)
{
	int is_update = 0;
	unsigned short int val;
	// Read the reconfiguration FPGA offset; we only need to read
	// the upper register and see if it is 0.
	putfslx(0x02AE1, 0, FSL_ATOMIC); // Read GENERAL5
	// Fill up the FIFO with noops
	putfslx(0x02000, 0, FSL_ATOMIC); // Type 1 NOP
	putfslx(0x02000, 0, FSL_ATOMIC); // Type 1 NOP
	putfslx(0x02000, 0, FSL_ATOMIC); // Type 1 NOP
	putfslx(0x02000, 0, FSL_ATOMIC); // Type 1 NOP
	putfslx(0x02000, 0, FSL_ATOMIC); // Type 1 NOP
	putfslx(0x02000, 0, FSL_ATOMIC); // Type 1 NOP
	putfslx(0x02000, 0, FSL_ATOMIC); // Type 1 NOP

	// Write IPROG command
	putfslx(0x030A1, 0, FSL_ATOMIC); // Write CMD
	putfslx(0x0000E, 0, FSL_ATOMIC); // IPROG Command
	putfslx(0x02000, 0, FSL_ATOMIC); // Type 1 NOP

	// Trigger the FSL peripheral to drain the FIFO into the ICAP
	putfslx(FINISH_FSL_BIT, 0, FSL_ATOMIC);
	__udelay (1000);
	getfslx(val, 0, FSL_ATOMIC); // Read the ICAP result
	if (val == 1) {
		printf("Update triggered by ICAP GENERAL5 == 1\n");
		return 1;
	}
	return (0);
}

/**
 * Check to see if a Firmware update is being request, if so 
 * signal firmware update and return.
 *
 * Returns - 0 if production boot, 1 if golden boot with update request, or
 * no return but immediate reboot to production if golden boot with no update request
 *
 */
int CheckFirmwareUpdate(void)
{
	unsigned long int gpioReg;
	int updateRequired = 0;
	gpioReg = rdreg32(CONFIG_SYS_GPIO_ADDR);

	if (((gpioReg >> 16) & 0xffff) < 0x200B) { // We can't do FPGA reads before this version
		updateRequired = isICAPUpdateRequested();
	}
	if (!updateRequired && ((gpioReg & GARCIA_FPGA_GPIO_PUSHBUTTON) == 0)) {
		printf("Update triggered by pushbutton\n");
		updateRequired = 1;
	}
	if ((gpioReg & (GARCIA_FPGA_LX100_ID | GARCIA_FPGA_LX150_ID)) !=
					GARCIA_FPGA_LX150_ID && // An LX-150 must not load production because it has no production image
			!updateRequired &&              // We stay with golden image to do updates
			(((gpioReg >> 16) & 0xffff) < 0x200B ||  // We can't do FPGA reads before this version
			!isProductionBoot())) {          // We're booted into golden image
		icap_reset(1); // "Golden" boot immediately loads production boot
	}

	// OK, now we're production boot and know if we need to do an update
	return (updateRequired != 0); // "Golden" boot with an update request
}
