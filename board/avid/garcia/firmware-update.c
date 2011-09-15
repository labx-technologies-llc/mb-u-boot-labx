#include "xparameters.h"
#include <linux/types.h>
#include <common.h>
#include <u-boot/crc.h>
#include "microblaze_fsl.h"

extern void icap_reset(int resetProduction);

#define FINISH_FSL_BIT (0x80000000)

static int isProductionBoot(void)
{
	unsigned long int val;
	// First find out if we had to use a fallback image
	// Synchronize command bytes
	putfsl(0x0FFFF, 0); // Pad words
	putfsl(0x0FFFF, 0);
	putfsl(0x0AA99, 0); // SYNC
	putfsl(0x05566, 0); // SYNC

	// Read the boot status register; we want 0s in bits FALLBACK_0 and FALLBACK_1.
	putfsl(0x02C01, 0); // Read BOOTSTS
	// Add some safety noops
	putfsl(0x02000, 0); // Type 1 NOP
    putfsl(FINISH_FSL_BIT | 0x02000, 0); // Type 1 NOP, and Trigger the FSL peripheral to drain the FIFO into the ICAP
	__udelay (1000);
	getfsl(val, 0); // Read the ICAP result
	// FALLBACK_0 is Bit 1 and FALLBACK_1 is bit 7
	if ((val & 0x82) != 0) {
		printf("Booted from fallback image.  Boot status register = 0x%lx\n", val);
		return 0;
	}

	// Next find out if the primary image was the production image
	// Synchronize command bytes
	putfsl(0x0FFFF, 0); // Pad words
	putfsl(0x0FFFF, 0);
	putfsl(0x0AA99, 0); // SYNC
	putfsl(0x05566, 0); // SYNC

	// Read the reconfiguration FPGA offset; we only need to read
	// the upper register and see if it is 0.
	putfsl(0x02A81, 0); // Read GENERAL2
	// Add some safety noops
	putfsl(0x02000, 0); // Type 1 NOP
    putfsl(FINISH_FSL_BIT | 0x02000, 0); // Type 1 NOP, and Trigger the FSL peripheral to drain the FIFO into the ICAP
	__udelay (1000);
	getfsl(val, 0); // Read the ICAP result
	printf("FPGA boot image at address 0x%04lxxxxx, ICAP 0x%08lx\n", ((val << 1) & 0xFFFF), val);
	__udelay (5000);
	val &= 0xFFFF;
	return (val != 0);
}

int isICAPUpdateRequested(void)
{
	unsigned long int val;

	// Synchronize command bytes
	putfsl(0x0FFFF, 0); // Pad words
	putfsl(0x0FFFF, 0);
	putfsl(0x0AA99, 0); // SYNC
	putfsl(0x05566, 0); // SYNC

	// Read the reconfiguration FPGA offset; we only need to read
	// the upper register and see if it is 0.
	putfsl(0x02AE1, 0); // Read GENERAL5
	// Add some safety noops
	putfsl(0x02000, 0); // Type 1 NOP
    putfsl(FINISH_FSL_BIT | 0x02000, 0); // Type 1 NOP, and Trigger the FSL peripheral to drain the FIFO into the ICAP
	__udelay (1000);
	getfsl(val, 0); // Read the ICAP result
	val &= 0xFFFF;
	return (val == 1);
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
	// Escape mechanism!  If both jumpers are on, we always go to golden uboot!
	if ((gpioReg & (GARCIA_FPGA_GPIO_JUMPER_1 | GARCIA_FPGA_GPIO_JUMPER_2)) == 0) {
		printf("Update triggered by escape jumpers 1 and 2!\n");
		updateRequired = 1;
	}

	if (!updateRequired && isICAPUpdateRequested()) {
		printf("Update triggered by ICAP GENERAL5 == 1\n");
		updateRequired = 1;
	}
	if (!updateRequired && ((gpioReg & GARCIA_FPGA_GPIO_PUSHBUTTON) == 0)) {
		printf("Update triggered by pushbutton\n");
		updateRequired = 1;
	}
	if ((gpioReg & (GARCIA_FPGA_LX100_ID | GARCIA_FPGA_LX150_ID)) !=
					GARCIA_FPGA_LX150_ID && // An LX-150 must not load production because it has no production image
			!updateRequired &&              // We stay with golden image to do updates
			!isProductionBoot()) {          // We're booted into golden image
		printf("ICAP reset to production image\n");
		__udelay(2000);
		icap_reset(1); // "Golden" boot immediately loads production boot
	}

	// OK, now we're production boot and know if we need to do an update
	return (updateRequired != 0); // "Golden" boot with an update request
}
