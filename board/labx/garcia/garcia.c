/*
 * (C) Copyright 2007 Michal Simek
 *
 * Michal  SIMEK <monstr@monstr.eu>
 *
 * See file CREDITS for list of people who contributed to this
 * project.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

/* This is a board specific file.  It's OK to include board specific
 * header files */

#include <common.h>
#include <config.h>
#include <asm/microblaze_intc.h>
#include <asm/asm.h>
#include <net.h>
#include <netdev.h>
#include "microblaze_fsl.h"

void gpio_init (int is_update)
{
#ifdef CONFIG_SYS_GPIO
	if (is_update) {
		wrreg32(CONFIG_SYS_GPIO_ADDR, GARCIA_FPGA_POWER_LED_B |
				GARCIA_FPGA_STATUS_LED_B | GARCIA_FPGA_STATUS_LED_FLASH);
	} else {
		wrreg32(CONFIG_SYS_GPIO_ADDR, GARCIA_FPGA_POWER_LED_B |
				GARCIA_FPGA_STATUS_LED_A | GARCIA_FPGA_STATUS_LED_B);
	}
#endif
	return;
}

#ifdef CONFIG_SYS_FSL_2
void fsl_isr2 (void *arg) {
	volatile int num;
	*((unsigned int *)(CONFIG_SYS_GPIO_0_ADDR + 0x4)) =
	    ++(*((unsigned int *)(CONFIG_SYS_GPIO_0_ADDR + 0x4)));
	GET (num, 2);
	NGET (num, 2);
	puts("*");
}

int fsl_init2 (void) {
	puts("fsl_init2\n");
	install_interrupt_handler (FSL_INTR_2, fsl_isr2, NULL);
	return 0;
}
#endif

extern int labx_eth_initialize(bd_t *bis);
int board_eth_init(bd_t *bis)
{
  /* This board has two or four Lab X Ethernet / LocalLink MACs.  Initialize
   * one of them for use with U-Boot.
   */
  return(labx_eth_initialize(bis));
}

int do_garcia_flash_rom_image(cmd_tbl_t *cmdtp, int flag, int argc, char *argv[])
{
	int rc = 0;
	int i;
	const char * const script_list[] = {
			"echo Starting Garcia Flash image load",
			"echo --------------------------------",
			"set autoload no",
			"set ethaddr 00:0a:35:00:44:01",
			"setenv ipaddr 192.168.1.1",
			"setenv serverip 192.168.1.100",
			"setenv netkargs labx_eth_llink.macaddr=0x00,0x0a,0x35,0x00,0x44,0x01",
			"ping 192.168.1.100",
			NULL,
			"protect off 0x87000000 +0x1000000",
			"erase 0x87000000 +0x1000000",
			"cp.b 0x88001000 0x87000000 0x1000000",
			"echo Flash load complete",
			"echo -------------------"};
	const char default_rom_image[] = "garcia.rom";
	char tftpcmd[80];
    strcpy(tftpcmd, "tftp 0x88001000 ");
	if (argc < 2) {
		strcat(tftpcmd, default_rom_image);
	} else {
		strcat(tftpcmd, argv[1]);
	}
	for (i = 0; i < sizeof(script_list)/sizeof(script_list[0]); i++) {
		if (script_list[i] != NULL) {
			rc = run_command(script_list[i], flag);
		} else {
			rc = run_command(tftpcmd, flag);
		}
	}
	return(rc);

}

U_BOOT_CMD(flash_garcia_image, 2, 0, do_garcia_flash_rom_image,
		"Load a Garcia Flash ROM image into Flash memory",
		"Read a flash ROM image, by default named \"garcia.rom\", from a TFTP server at"
		" IP address 192.168.1.100 and write it to the Flash ROM.");

int do_read_icap5(cmd_tbl_t *cmdtp, int flag, int argc, char *argv[])
{
	unsigned long int val;
	int attempt_count = 0;

#ifdef USE_ICAP_FSL
	// It has been empirically determined that ICAP FSL doesn't always work
	// the first time, but if retried enough times it does eventually work.
	// Thus we keep hammering the operation we want and checking for failure
	// until we finally succeed.  Somebody please fix ICAP!! <sigh>

	do {
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
		  ++attempt_count;
	} while ((val & ICAP_FSL_FAILED) != 0 && attempt_count < 10000);
#else
	// Synchronize command bytes
	wrreg32(CONFIG_SYS_ICAP_WF, 0x0FFFF); // Pad words
	wrreg32(CONFIG_SYS_ICAP_WF, 0x0FFFF);
	wrreg32(CONFIG_SYS_ICAP_WF, 0x0AA99); // SYNC
	wrreg32(CONFIG_SYS_ICAP_WF, 0x05566); // SYNC

	wrreg32(CONFIG_SYS_ICAP_WF, 0x02AE1); // Read GENERAL5
	// Add some safety noops
	wrreg32(CONFIG_SYS_ICAP_WF, 0x02000); // Type 1 NOP
	wrreg32(CONFIG_SYS_ICAP_WF, 0x02000); // Type 1 NOP
	// Trigger the FSL peripheral to drain the FIFO into the ICAP
	wrreg32(CONFIG_SYS_ICAP_CR, XPAR_ICAP_CR_WRITE);
	while ((rdreg32(CONFIG_SYS_ICAP_CR) & XPAR_ICAP_CR_WRITE) != 0)
		;
	// Read back the ICAP result
	wrreg32(CONFIG_SYS_ICAP_SZ, 1); // Read 1 word
	wrreg32(CONFIG_SYS_ICAP_CR, XPAR_ICAP_CR_READ);
	while ((rdreg32(CONFIG_SYS_ICAP_CR) & XPAR_ICAP_CR_READ) != 0)
		;
	val = rdreg32(CONFIG_SYS_ICAP_RF);
#endif
  printf("ICAP GP5 register value is 0x%04lx (FSL read value 0x%08lx, attempt count %d)\n", val & 0xFFFF, val, attempt_count);
  return 0;
}

U_BOOT_CMD(rg5, 2, 0, do_read_icap5,
		"Read ICAP General Purpose register 5.",
		"Issue a command to the ICAP peripheral to read General Purpose Register 5.");

int do_read_idreg(cmd_tbl_t *cmdtp, int flag, int argc, char *argv[])
{
	unsigned long int val;
	unsigned long int id;
	int attempt_count = 0;

#ifdef USE_ICAP_FSL
	// It has been empirically determined that ICAP FSL doesn't always work
	// the first time, but if retried enough times it does eventually work.
	// Thus we keep hammering the operation we want and checking for failure
	// until we finally succeed.  Somebody please fix ICAP!! <sigh>

	do {
		// Synchronize command bytes
		putfsl(0x0FFFF, 0); // Pad words
		putfsl(0x0FFFF, 0);
		putfsl(0x0AA99, 0); // SYNC
		putfsl(0x05566, 0); // SYNC

		// Read the reconfiguration FPGA offset; we only need to read
		// the upper register and see if it is 0.
		putfsl(0x029C1, 0); // Read ID
		// Add some safety noops
		putfsl(0x02000, 0); // Type 1 NOP
		putfsl(FINISH_FSL_BIT | 0x02000, 0); // Type 1 NOP, and Trigger the FSL peripheral to drain the FIFO into the ICAP
		__udelay (1000);
		  getfsl(val, 0); // Read the ICAP result
		  ++ attempt_count;
	} while ((val & ICAP_FSL_FAILED) != 0 && attempt_count < 10000);
	do {
		// Synchronize command bytes
		putfsl(0x0FFFF, 0); // Pad words
		putfsl(0x0FFFF, 0);
		putfsl(0x0AA99, 0); // SYNC
		putfsl(0x05566, 0); // SYNC

		// Read the reconfiguration FPGA offset; we only need to read
		// the upper register and see if it is 0.
		putfsl(0x029C1, 0); // Read ID
		// Add some safety noops
		putfsl(0x02000, 0); // Type 1 NOP
		putfsl(FINISH_FSL_BIT | 0x02000, 0); // Type 1 NOP, and Trigger the FSL peripheral to drain the FIFO into the ICAP
		__udelay (1000);
		  getfsl(id, 0); // Read the ICAP result
		  ++ attempt_count;
	} while ((val & ICAP_FSL_FAILED) != 0 && attempt_count < 10000);
	id = (id & 0xFFFF) | ((val & 0xFFFF) << 16);
#else
	// Synchronize command bytes
	wrreg32(CONFIG_SYS_ICAP_WF, 0x0FFFF); // Pad words
	wrreg32(CONFIG_SYS_ICAP_WF, 0x0FFFF);
	wrreg32(CONFIG_SYS_ICAP_WF, 0x0AA99); // SYNC
	wrreg32(CONFIG_SYS_ICAP_WF, 0x05566); // SYNC

	wrreg32(CONFIG_SYS_ICAP_WF, 0x029C1); // Read ID
	// Add some safety noops
	wrreg32(CONFIG_SYS_ICAP_WF, 0x02000); // Type 1 NOP
	wrreg32(CONFIG_SYS_ICAP_WF, 0x02000); // Type 1 NOP
	// Trigger the FSL peripheral to drain the FIFO into the ICAP
	wrreg32(CONFIG_SYS_ICAP_CR, XPAR_ICAP_CR_WRITE);
	while ((rdreg32(CONFIG_SYS_ICAP_CR) & XPAR_ICAP_CR_WRITE) != 0)
		;
	// Read back the ICAP result
	wrreg32(CONFIG_SYS_ICAP_SZ, 1); // Read 1 word
	wrreg32(CONFIG_SYS_ICAP_CR, XPAR_ICAP_CR_READ);
	while ((rdreg32(CONFIG_SYS_ICAP_CR) & XPAR_ICAP_CR_READ) != 0)
		;
	val = rdreg32(CONFIG_SYS_ICAP_RF);
#endif
  printf("ICAP ID is 0x%08lx (FSL read value 0x%08lx, attempt count %d [expect 2])\n", id, val, attempt_count);
  return 0;
}

U_BOOT_CMD(ridr, 2, 0, do_read_idreg,
		"Read ICAP ID register.",
		"Issue a command to the ICAP peripheral to read its ID register.");

int do_read_icap(cmd_tbl_t *cmdtp, int flag, int argc, char *argv[])
{
	unsigned long int val;
#ifdef USE_ICAP_FSL
	getfsl(val, 0); // Read the ICAP result
#else
	val = rdreg32(CONFIG_SYS_ICAP_RF);
#endif
	printf("ICAP FSL register read value is 0x%08lx)\n", val);
	return 0;
}

U_BOOT_CMD(ricap, 2, 0, do_read_icap,
		"Raw read of ICAP readback register.",
		"Issue a single fsl read instruction to the ICAP peripheral, "
		"returning the current value in the readback register.");

int do_write_icap5(cmd_tbl_t *cmdtp, int flag, int argc, char *argv[])
{
  unsigned long int val;
  unsigned long int arg;
  int attempt_count = 0;

  if (argc < 2) {
    cmd_usage(cmdtp);
    return 1;
  }

  arg = simple_strtoul(argv[1], NULL, 16);

#ifdef USE_ICAP_FSL
	// It has been empirically determined that ICAP FSL doesn't always work
	// the first time, but if retried enough times it does eventually work.
	// Thus we keep hammering the operation we want and checking for failure
	// until we finally succeed.  Somebody please fix ICAP!! <sigh>

	do {
	  putfsl(0x0FFFF, 0); // Pad words
	  putfsl(0x0FFFF, 0);
	  putfsl(0x0AA99, 0); // SYNC
	  putfsl(0x05566, 0); // SYNC

	  // Write the supplied value to ICAP GP5.
	  putfsl(0x032E1, 0); // Write GENERAL5
	  putfsl(arg, 0);

	  // Add some safety noops
	  putfsl(0x02000, 0); // Type 1 NOP
	  putfsl(FINISH_FSL_BIT | 0x02000, 0); // Type 1 NOP, and Trigger the FSL peripheral to drain the FIFO into the ICAP
		__udelay (1000);
		getfsl(val, 0); // Read the ICAP result
		++attempt_count;
	} while ((val & ICAP_FSL_FAILED) != 0 && attempt_count < 10000);
#else
	// Synchronize command bytes
	wrreg32(CONFIG_SYS_ICAP_WF, 0x0FFFF); // Pad words
	wrreg32(CONFIG_SYS_ICAP_WF, 0x0FFFF);
	wrreg32(CONFIG_SYS_ICAP_WF, 0x0AA99); // SYNC
	wrreg32(CONFIG_SYS_ICAP_WF, 0x05566); // SYNC

	// Read the reconfiguration FPGA offset; we only need to read
	// the upper register and see if it is 0.
	wrreg32(CONFIG_SYS_ICAP_WF, 0x032E1); // Write GENERAL5
	wrreg32(CONFIG_SYS_ICAP_WF, val);
	// Add some safety noops
	wrreg32(CONFIG_SYS_ICAP_WF, 0x02000); // Type 1 NOP
	wrreg32(CONFIG_SYS_ICAP_WF, 0x02000); // Type 1 NOP
	// Trigger the FSL peripheral to drain the FIFO into the ICAP
	wrreg32(CONFIG_SYS_ICAP_CR, XPAR_ICAP_CR_WRITE);
	while ((rdreg32(CONFIG_SYS_ICAP_CR) & XPAR_ICAP_CR_WRITE) != 0)
		;
#endif
	  printf ("## Wrote 0x%08lX to ICAP GP5 (FSL read value 0x%08lx, attempt count %d)\n", arg, val, attempt_count);

  return 0;
}

U_BOOT_CMD(wg5, 2, 0, do_write_icap5,
		"Write ICAP General Purpose register 5 with <value>.",
		"Issue a command to the ICAP peripheral to write General Purpose Register 5.");

int do_abort_icap(cmd_tbl_t *cmdtp, int flag, int argc, char *argv[])
{
  unsigned long int val;
  int attempt_count = 0;

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
	  ++attempt_count;
	} while ((val & ICAP_FSL_FAILED) != 0 && attempt_count < 10000);
#else
	wrreg32(CONFIG_SYS_ICAP_CR, XPAR_ICAP_CR_ABORT);
	while ((rdreg32(CONFIG_SYS_ICAP_CR) & (XPAR_ICAP_CR_ABORT | XPAR_ICAP_CR_RESET |
			XPAR_ICAP_CR_FIFO_CLEAR | XPAR_ICAP_CR_READ | XPAR_ICAP_CR_WRITE)) != 0)
		;
#endif
	printf ("## Issued Abort to ICAP peripheral, attempt count %d\n", attempt_count);
  return 0;
}

U_BOOT_CMD(aicap, 2, 0, do_abort_icap,
		"Abort ICAP peripheral.",
		"Issue an Abort command to the ICAP peripheral, "
		"resetting it to initialization idle state.");

