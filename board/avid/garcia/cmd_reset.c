// Lab X Technologies - Peter McLoone (2011)
// peter.mcloone@labxtechnologies.com
// implement reset command using ICAP

#include <common.h>
#include <config.h>
#include <command.h>
#include "microblaze_fsl.h"

#define RUNTIME_FPGA_BASE (0x00340000)
#define BOOT_FPGA_BASE (0x00000000)

void icap_reset(int resetProduction)
{
	unsigned long int fpga_base;
	u32 val;
	fpga_base = (resetProduction != 0) ? RUNTIME_FPGA_BASE : BOOT_FPGA_BASE;
#ifdef CONFIG_SYS_GPIO
	if ((rdreg32(CONFIG_SYS_GPIO_ADDR) &
			(GARCIA_FPGA_LX100_ID | GARCIA_FPGA_LX150_ID)) == GARCIA_FPGA_LX150_ID) {
		fpga_base = BOOT_FPGA_BASE;
	}
#endif
#ifdef USE_ICAP_FSL
	// It has been empirically determined that ICAP FSL doesn't always work
	// the first time, but if retried enough times it does eventually work.
	// Thus we keep hammering the operation we want and checking for failure
	// until we finally succeed.  Somebody please fix ICAP!! <sigh>

	// Abort anything in progress
	do {
		putfslx(0x0FFFF, 0, FSL_CONTROL); // Control signal aborts, data doesn't matter
		udelay(1000);
		getfsl(val, 0); // Read the ICAP result
	} while ((val & ICAP_FSL_FAILED) != 0);

	do {
		// Synchronize command bytes
        putfsl(0x0FFFF, 0); // Pad words
        putfsl(0x0FFFF, 0);
        putfsl(0x0AA99, 0); // SYNC
        putfsl(0x05566, 0); // SYNC

        // Write the reconfiguration FPGA offset; the base address of the
        // "run-time" FPGA is #defined as a byte address, but the ICAP needs
        // a 16-bit half-word address, so we shift right by one extra bit.
        putfsl(0x03261, 0); // Write GENERAL1
        putfsl(((fpga_base >> 1) & 0x0FFFF), 0); // Multiboot start address[15:0]
        putfsl(0x03281, 0); // Write GENERAL2
        putfsl(((fpga_base >> 17) & 0x0FF), 0); // Opcode 0x00 and address[23:16]

        // Write the fallback FPGA offset (this image)
        putfsl(0x032A1, 0); // Write GENERAL3
        putfsl(((BOOT_FPGA_BASE >> 1) & 0x0FFFF), 0);
        putfsl(0x032C1, 0); // Write GENERAL4
        putfsl(((BOOT_FPGA_BASE >> 17) & 0x0FF), 0);
        putfsl(0x032E1, 0); // Write GENERAL5
        putfsl(0x00, 0); // Value 0 allows u-boot to use production image

        // Write IPROG command
        putfsl(0x030A1, 0); // Write CMD
        putfsl(0x0000E, 0); // IPROG Command
    	// Add some safety noops
    	putfsl(0x02000, 0); // Type 1 NOP
        putfsl(FINISH_FSL_BIT | 0x02000, 0); // Type 1 NOP, and Trigger the FSL peripheral to drain the FIFO into the ICAP
		__udelay (1000);
		getfsl(val, 0); // Read the ICAP result
	} while ((val & ICAP_FSL_FAILED) != 0);
#else
	wrreg32(CONFIG_SYS_ICAP_CR, XPAR_ICAP_CR_ABORT);
	while ((rdreg32(CONFIG_SYS_ICAP_CR) & (XPAR_ICAP_CR_ABORT | XPAR_ICAP_CR_RESET |
			XPAR_ICAP_CR_FIFO_CLEAR | XPAR_ICAP_CR_READ | XPAR_ICAP_CR_WRITE)) != 0)
		;
	// Synchronize command bytes
	wrreg32(CONFIG_SYS_ICAP_WF, 0x0FFFF); // Pad words
	wrreg32(CONFIG_SYS_ICAP_WF, 0x0FFFF);
	wrreg32(CONFIG_SYS_ICAP_WF, 0x0AA99); // SYNC
	wrreg32(CONFIG_SYS_ICAP_WF, 0x05566); // SYNC

    // Write the reconfiguration FPGA offset; the base address of the
    // "run-time" FPGA is #defined as a byte address, but the ICAP needs
    // a 16-bit half-word address, so we shift right by one extra bit.
	wrreg32(CONFIG_SYS_ICAP_WF, 0x03261); // Write GENERAL1
	wrreg32(CONFIG_SYS_ICAP_WF, ((fpga_base >> 1) & 0x0FFFF)); // Multiboot start address[15:0]
	wrreg32(CONFIG_SYS_ICAP_WF, 0x03281); // Write GENERAL2
	wrreg32(CONFIG_SYS_ICAP_WF, ((fpga_base >> 17) & 0x0FF)); // Opcode 0x00 and address[23:16]
    // Write the fallback FPGA offset (this image)
	wrreg32(CONFIG_SYS_ICAP_WF, 0x032A1); // Write GENERAL3
	wrreg32(CONFIG_SYS_ICAP_WF, ((BOOT_FPGA_BASE >> 1) & 0x0FFFF)); // Multiboot start address[15:0]
	wrreg32(CONFIG_SYS_ICAP_WF, 0x032C1); // Write GENERAL4
	wrreg32(CONFIG_SYS_ICAP_WF, ((BOOT_FPGA_BASE >> 17) & 0x0FF)); // Opcode 0x00 and address[23:16]
	wrreg32(CONFIG_SYS_ICAP_WF, 0x032E1); // Write GENERAL5
	wrreg32(CONFIG_SYS_ICAP_WF, 0); // Value 0 allows u-boot to use production image
	// Write IPROG command
	wrreg32(CONFIG_SYS_ICAP_WF, 0x030A1); // Write CMD
	wrreg32(CONFIG_SYS_ICAP_WF, 0x0000E); // IPROG Command
	// Add some safety noops
	wrreg32(CONFIG_SYS_ICAP_WF, 0x02000); // Type 1 NOP
	wrreg32(CONFIG_SYS_ICAP_WF, 0x02000); // Type 1 NOP
	// Trigger the FSL peripheral to drain the FIFO into the ICAP
	wrreg32(CONFIG_SYS_ICAP_CR, XPAR_ICAP_CR_WRITE);
	while ((rdreg32(CONFIG_SYS_ICAP_CR) & XPAR_ICAP_CR_WRITE) != 0)
		;
#endif
	while(1);
	return;
}

int do_reset(cmd_tbl_t *cmdtp, int flag, int argc, char *argv[])
{
	icap_reset(0);
	return 0;
}
