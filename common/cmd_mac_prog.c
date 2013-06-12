/*
 * Copyright 2006 Freescale Semiconductor
 * York Sun (yorksun@freescale.com)
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include <common.h>
#include <command.h>

#include <spi_flash.h>
#include <fdt.h>
#include <libfdt.h>

#define MAC_ADDR_BYTES 6
#define MAC_REGION_INSET 0x02
#define MAC_REGION_OFFSET 0x10
#define MAX_MAC_STRING_CHAR 17

#ifdef CONFIG_SPI_OTP
#define OTP_BASE_ADDR 0x114
#define OTP_LOCK_REGION_BASE 0x112
#define OTP_MAX_MAC_ADDR_OFFSET 32
#define OTP_OFFSET_PARAM "base_otp_reg"
#endif

#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif

#ifdef CONFIG_SPI_FLASH
  static struct spi_flash *spiflash = NULL;
#endif

int char_to_hex(char ch) 
{
  	int ret_Value = -1;
  	if((ch >= '0') & (ch <= '9')) {
    		ret_Value = (ch - '0');
  	} else if((ch >= 'A') & (ch <= 'F')){
    		ret_Value = (ch - 'A' + 10);
  	} else if((ch >= 'a') & (ch <= 'f')) {
    		ret_Value = (ch - 'a' + 10);
	}
  	return(ret_Value);
}

#ifdef CONFIG_SPI_OTP
int getFdtBootCmdProperty(const char *propertyName, char *buf, size_t bufLen)
{
	int   i;
	int   err;
	ulong fdt_flash_offset;
	ulong fdt_flash_size;
	void *pfdt = NULL;
	const char *str;		/* used to get string properties */
	const char *path;
	uint8_t hdrbuf[256];

	fdt_flash_offset = simple_strtoul(getenv("fdtstart"), NULL, 0);
	if (fdt_flash_offset == 0) {
		puts("fdtstart not specified in environment\n");
		return -1;
	}		
	err = spi_flash_read(spiflash, fdt_flash_offset, sizeof(hdrbuf), hdrbuf);
	if (err != 0) {
		printf("FDT header read failed: %s\n", fdt_strerror(err));
		return err;
	}
	err = fdt_check_header(hdrbuf);
	if (err != 0 || (fdt_flash_size = fdt_totalsize(hdrbuf)) == 0) {
		printf("FDT header invalid: %s\n", fdt_strerror(err));
		return err;
	}
	pfdt = malloc(fdt_flash_size);
	if (pfdt == NULL) {
		printf("FDT allocation of %lu bytes failed\n", fdt_flash_size);
		return -1;
	}
	err = spi_flash_read(spiflash, fdt_flash_offset, fdt_flash_size, pfdt);
	if (err != 0) {
		printf("FDT read failed: %s\n", fdt_strerror(err));
		goto getFdtBootCmdProperty_err;
	}
	err = fdt_check_header(pfdt);
	if (err != 0) {
		printf("FDT invalid: %s\n", fdt_strerror(err));
		goto getFdtBootCmdProperty_err;
	}
	i = fdt_path_offset (pfdt, "/chosen");
	if (i < 0) {
		puts("FDT node \"/chosen\" not found\n");
		err = -1;
		goto getFdtBootCmdProperty_err;
	}
	path = fdt_getprop(pfdt, i, "bootargs", NULL);
	if (path == NULL) {
		puts("FDT property \"bootargs\" not found\n");
		err = -1;
		goto getFdtBootCmdProperty_err;
	}
	if ((str = strstr (path, propertyName)) == NULL ||
			(str = strchr(str, '=')) == NULL) {
		printf("FDT bootargs property \"%s\" not found\n", propertyName);
		err = -1;
		goto getFdtBootCmdProperty_err;
	}
	while (*str == '=' || *str == ' ') {
		++str;
	}
	for (i = 0; (str[i] > ' ') && (i < (int)bufLen - 1); ++i)
		;
	memcpy(buf, str, i);
	buf[i] = '\0';

getFdtBootCmdProperty_err:
	free(pfdt);
	return (err == 0) ? i : err;
}

#endif

int do_setmac(cmd_tbl_t *cmdtp, int flag, int argc, char *argv[])
{
	int i = 0;
        int j = 0; 
        int ret = 0;
	const char *ethintf = argv[1];
	const char *mac = argv[2];
	uint8_t macbyte[8];
        char otpMacOffsetStr[16];
	uint8_t stored_macbyte[8];
	uint8_t lockbits;
	int skip_colon = TRUE; 
        int idx = 2;
	int ethnum = char_to_hex(ethintf[3]);

	if (argc < 3)
		goto usage;

	if(ethnum > (NUM_ETH_PORTS-1))
	{  
		printf("Interface eth%d exceeds number of available ports\n", ethnum);
		goto fail;
	}
	else if (strlen(argv[2]) != MAX_MAC_STRING_CHAR)
	{
		puts("Invalid MAC address, please use format - xx:xx:xx:xx:xx:xx\n");
		goto fail;
	}

#ifdef CONFIG_SPI_OTP
	puts ("Writing MAC to OTP flash...\n");
#else
	puts ("Writing MAC to flash partition...\n");
#endif

	// Convert colon delimited MAC address into byte array
	for(j = 0; j < MAX_MAC_STRING_CHAR; j++)
	{	
		if(mac[j] == ':')
		{	
			// Skip colon character
			skip_colon = TRUE;
			// Increment MAC byte index
			idx++;
		}
		else
		{
			if(skip_colon)
			{
				macbyte[idx] = char_to_hex(mac[j]);
				skip_colon = FALSE;
			}
			else
			{
				macbyte[idx] <<= 4;
				macbyte[idx] += char_to_hex(mac[j]);
			}
		}
	}

	// Set the first two bytes to 0x00 for identification	
	macbyte[0] = 0x00;
	macbyte[1] = 0x00;

#ifdef CONFIG_SPI_FLASH
  // Probe for flash device.
  if(!spiflash) {
    if(!(spiflash = spi_flash_probe(0, 0, 40000000, 3))) {
      puts("Failed to initialize SPI flash device at 0:0.\n");
      return 0;
    }
  }
#endif
	
#ifdef CONFIG_SPI_OTP
	if (getFdtBootCmdProperty(OTP_OFFSET_PARAM, otpMacOffsetStr, sizeof(otpMacOffsetStr)) > 0) {
		i = (int)simple_strtoul(otpMacOffsetStr, NULL, 0);
		if (i < 0 || i > OTP_MAX_MAC_ADDR_OFFSET) {
			printf(OTP_OFFSET_PARAM " (%d) exceeds max value %d\n", i, OTP_MAX_MAC_ADDR_OFFSET);
			i = 0;
		} else {
			printf("Boot command string " OTP_OFFSET_PARAM "=%d\n", i);
		}
	} else {
		i = 0;
		printf("Boot command string " OTP_OFFSET_PARAM " not found - using offset 0\n");
	}

	ulong otp_addr = (ulong)(OTP_BASE_ADDR + ((i + ethnum) * MAC_REGION_OFFSET));
	ulong otp_lock_addr = (ulong)OTP_LOCK_REGION_BASE + ((i + ethnum) >> 3);
	uint8_t lockmask = 0x01 << ethnum;
	
	ret = spi_flash_read_otp(spiflash, otp_lock_addr, 1, (void*)&lockbits);

	if((lockbits & lockmask) != 0) 
	{ 
		lockmask = lockmask ^ 0xFF;
#else
	ulong flash_addr = (ulong)(LABX_MAC_ADDR_FLASH_LOC + ((i + ethnum) * MAC_REGION_OFFSET));
#endif

		for(i = 0; i < (MAC_ADDR_BYTES+MAC_REGION_INSET); i++) 
		{
#ifdef CONFIG_SPI_OTP
        		ret = spi_flash_write_otp(spiflash, (otp_addr+i), 1, (void*)&macbyte[i]);
#else
        		ret = spi_flash_write_otp(spiflash, (flash_addr+i), 1, (void*)&macbyte[i]);
#endif
		}

		// Read MAC written and verify
#ifdef CONFIG_SPI_OTP
		ret = spi_flash_read_otp(spiflash, otp_addr, (MAC_ADDR_BYTES+MAC_REGION_INSET), (void*)stored_macbyte);
#else
		ret = spi_flash_read_fast(spiflash, flash_addr, (MAC_ADDR_BYTES+MAC_REGION_INSET), (void*)stored_macbyte);
#endif
		
		for(i = 0; i < (MAC_ADDR_BYTES+MAC_REGION_INSET); i++) {
			if(stored_macbyte[i] != macbyte[i]) goto fail;			
		}

#ifdef CONFIG_SPI_OTP
		// Lock MAC address OTP region
		ret = spi_flash_write_otp(spiflash, otp_lock_addr, 1, (void*)&lockmask);  
	} else 
	{
		printf("OTP region for %s is locked\n", ethintf);
		goto fail;
	}
#endif	
	if (ret) {
		printf("SPI flash %s failed\n", argv[0]);
		goto fail;
	}
	else {
		printf("Successfully programmed MAC address for %s\n", ethintf);
	}

	return 0;

usage:
	cmd_usage(cmdtp);
        return 1;
fail:
	return 1;
}

int do_getmac(cmd_tbl_t *cmdtp, int flag, int argc, char *argv[])
{
	int i = 0;
        int ret = 0;
	int ethnum = 0;
	const char *ethintf;
        uint8_t macbyte[6];
        char otpMacOffsetStr[16];
        ulong otp_addr;

	if (argc < 2)
		goto usage;

	ethintf = argv[1]; 
	ethnum = char_to_hex(ethintf[3]);

	if(ethnum > (NUM_ETH_PORTS-1))
	{  
		printf("Interface eth%d exceeds number of available ports\n", ethnum);
		goto fail;
	}
	
#ifdef CONFIG_SPI_FLASH
  // Probe for flash device.
  if(!spiflash) {
    if(!(spiflash = spi_flash_probe(0, 0, 40000000, 3))) {
      puts("Failed to initialize SPI flash device at 0:0.\n");
      return 0;
    }
  }
#endif
	
#ifdef CONFIG_SPI_OTP
	// Read FDT to find MAC address offset in kernel boot args
	
	if (getFdtBootCmdProperty(OTP_OFFSET_PARAM, otpMacOffsetStr, sizeof(otpMacOffsetStr)) > 0) {
		i = (int)simple_strtoul(otpMacOffsetStr, NULL, 0);
		if (i < 0 || i > OTP_MAX_MAC_ADDR_OFFSET) {
			printf(OTP_OFFSET_PARAM " (%d) exceeds max value %d\n", i, OTP_MAX_MAC_ADDR_OFFSET);
			i = 0;
		} else {
			printf("Boot command string " OTP_OFFSET_PARAM "=%d\n", i);
		}
	} else {
		i = 0;
		printf("Boot command string " OTP_OFFSET_PARAM " not found - using offset 0\n");
	}

	otp_addr = (ulong)(OTP_BASE_ADDR + ((i + ethnum) * MAC_REGION_OFFSET) + MAC_REGION_INSET);
	ret = spi_flash_read_otp(spiflash, otp_addr, MAC_ADDR_BYTES, (void*)macbyte); 
#else
	flash_addr = (ulong)(LABX_MAC_ADDR_FLASH_LOC + ((i + ethnum) * MAC_REGION_OFFSET) + MAC_REGION_INSET);
	ret = spi_flash_read_fast(spiflash, flash_addr, MAC_ADDR_BYTES, (void*)macbyte); 
#endif
	printf("eth%d MAC: ", ethnum);
	for(i = 0; i < MAC_ADDR_BYTES; i++) { 
		if(i == (MAC_ADDR_BYTES-1)) {
		  printf("%02X\n", macbyte[i]); 
		} else
		  printf("%02X:", macbyte[i]);
	}

	if (ret) {
		printf("SPI read %s failed\n", argv[0]);
		return 1;
	}

	return 0;

usage:
	cmd_usage(cmdtp);
        return 1;

fail:
	return 1;
}

U_BOOT_CMD(
	setmac,		3,	1,	do_setmac,
#ifdef CONFIG_SPI_OTP
	"Write MAC address to SPI flash OTP area",
	"interface MAC (format: XX:XX:XX:XX:XX:XX) - set permanent MAC for interface\n"
#else
	"Write MAC address to SPI flash config partition",
	"interface MAC (format: XX:XX:XX:XX:XX:XX) - set config based MAC for interface\n"
#endif 
);

U_BOOT_CMD(
	readmac,	2,	1,	do_getmac,
#ifdef CONFIG_SPI_OTP
	"Read MAC address from SPI flash OTP area",
	"interface - read permanent MAC for interface\n"
#else
	"Read MAC address from SPI flash config partition",
	"interface - read config partition MAC for interface\n"
#endif
);
