// Lab X Technologies - Peter McLoone (2011), Yuriy Dragunov (2012)
// peter.mcloone@labxtechnologies.com
// yuriy.dragunov@labxtechnologies.com
// implement reset command using ICAP

#include <common.h>
#include <config.h>
#include <command.h>

// Assuming this will get brought in.
extern void mdelay(unsigned int msec);

#define BOOT_FPGA_BASE (0x00000000)

#ifndef RUNTIME_FPGA_BASE
#error "RUNTIME_FPGA_BASE not defined for platform"
#elif RUNTIME_FPGA_BASE == BOOT_FPGA_BASE
// If this was a warning, it would be missed in
// the midst of the rest of U-Boot's build output.
#error "RUNTIME_FPGA_BASE and BOOT_FPGA_BASE are the same"
#endif

#ifdef USE_ICAP_FSL
#include "arch/microblaze/include/asm/microblaze_fsl.h"

void icap_reset(int resetProduction)
{
	unsigned long int fpga_base;
	u32 val;

	// ICAP behavior is described (poorly) in Xilinx specification UG380.  Brave
	// souls may look there for detailed guidance on what is being done here.
	fpga_base = (resetProduction != 0) ? RUNTIME_FPGA_BASE : BOOT_FPGA_BASE;
#ifdef CONFIG_SYS_GPIO
	if ((rdreg32(CONFIG_SYS_GPIO_ADDR) &
			(GARCIA_FPGA_LX100_ID | GARCIA_FPGA_LX150_ID)) == GARCIA_FPGA_LX150_ID) {
		fpga_base = BOOT_FPGA_BASE;
	}
#endif

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

#ifndef CONFIG_SPI_FLASH
		// Set the Mode register so that fallback images will be manipulated
		// correctly.  Use bitstream mode instead of physical mode (required
		// for configuration fallback) and set boot mode for BPI
		putfsl(0x03301, 0); // Write MODE_REG
		putfsl(0x02000, 0); // Value 0 allows u-boot to use production image
#endif
		// Write the reconfiguration FPGA offset; the base address of the
		// "run-time" FPGA is #defined as a byte address, but the ICAP needs
		// a 16-bit half-word address, so we shift right by one extra bit.
#ifdef CONFIG_SPI_FLASH
		putfsl(0x03261, 0); // Write GENERAL1
		putfsl(((fpga_base >> 0) & 0x0FFFF), 0); // Multiboot start address[15:0]
		putfsl(0x03281, 0); // Write GENERAL2
		putfsl((((fpga_base >> 16) & 0x0FF) | 0x0300), 0); // Opcode 0x00 and address[23:16]

		// Write the fallback FPGA offset (this image)
		putfsl(0x032A1, 0); // Write GENERAL3
		putfsl(((BOOT_FPGA_BASE >> 0) & 0x0FFFF), 0);
		putfsl(0x032C1, 0); // Write GENERAL4
		putfsl((((BOOT_FPGA_BASE >> 16) & 0x0FF) | 0x0300), 0);
#else
		putfsl(0x03261, 0); // Write GENERAL1
		putfsl(((fpga_base >> 1) & 0x0FFFF), 0); // Multiboot start address[15:0]
		putfsl(0x03281, 0); // Write GENERAL2
		putfsl(((fpga_base >> 17) & 0x0FF), 0); // Opcode 0x00 and address[23:16]

		// Write the fallback FPGA offset (this image)
		putfsl(0x032A1, 0); // Write GENERAL3
		putfsl(((BOOT_FPGA_BASE >> 1) & 0x0FFFF), 0);
		putfsl(0x032C1, 0); // Write GENERAL4
		putfsl(((BOOT_FPGA_BASE >> 17) & 0x0FF), 0);
#endif

		putfsl(0x032E1, 0); // Write GENERAL5
		putfsl((resetProduction ? 1 : 0), 0); // Value 0 allows u-boot to use production image

		// Write IPROG command
		putfsl(0x030A1, 0); // Write CMD
		putfsl(0x0000E, 0); // IPROG Command

		// Add some safety noops
		putfsl(0x02000, 0); // Type 1 NOP
		putfsl(FINISH_FSL_BIT | 0x02000, 0); // Type 1 NOP, and Trigger the FSL peripheral to drain the FIFO into the ICAP
		__udelay (1000);
		getfsl(val, 0); // Read the ICAP result
	} while ((val & ICAP_FSL_FAILED) != 0);

	return;
}

int do_reconfigure(cmd_tbl_t *cmdtp, int flag, int argc, char *argv[]) {
	unsigned int production = 0;

	if(argc > 2) {
		cmd_usage(cmdtp);
		return 1;
	}

	if(argc == 2) production = simple_strtoul(argv[1], NULL, 10);

	printf("Reconfiguring to %s FPGA.\n", (production ? "production" : "golden"));
	mdelay(100); // Let it print.

	icap_reset(production);
	return 0;
}

U_BOOT_CMD(
	reconf, 2, 0, do_reconfigure,
	"system reset (reconfigure the FPGA)",
	"[production] -- non-zero value recongifures to the production FPGA, "
		"otherwise reconfigures to the golden FPGA."
);

int do_reset(cmd_tbl_t *cmdtp, int flag, int argc, char *argv[]) {
	puts("Reconfiguring to golden FPGA.\n"
	"Note: use 'reconf 1' to reconfigure to the production FPGA.\n");
	mdelay(100); // Let it print.

	icap_reset(0);
	return 0;
}

#else

int do_reset(cmd_tbl_t *cmdtp, int flag, int argc, char *argv[]) {
	puts("'reset' command not implemented -- no ICAP.\n");
	return 0;
}

#endif /* USE_ICAP_FSL */

