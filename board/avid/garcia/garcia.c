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

int do_flash_rom_image(cmd_tbl_t *cmdtp, int flag, int argc, char *argv[])
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

U_BOOT_CMD(flash_rom_image, 2, 0, do_flash_rom_image,
		"Load a Garcia Flash ROM image into Flash memory",
		"Read a flash ROM image, by default named \"garcia.rom\", from a TFTP server at"
		" IP address 192.168.1.100 and write it to the Flash ROM.");

