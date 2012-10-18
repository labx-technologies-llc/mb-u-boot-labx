/*
 * Command for accessing SPI flash.
 *
 * Copyright (C) 2008 Atmel Corporation
 * Licensed under the GPL-2 or later.
 */

#include <common.h>
#include <spi_flash.h>
#include <malloc.h>
#include <fdt.h>
#include <libfdt.h>

#include <asm/io.h>

#ifndef CONFIG_SF_DEFAULT_SPEED
# define CONFIG_SF_DEFAULT_SPEED	1000000
#endif
#ifndef CONFIG_SF_DEFAULT_MODE
# define CONFIG_SF_DEFAULT_MODE		SPI_MODE_3
#endif

#ifdef CFG_SPI_OTP
#define MAC_ADDR_BYTES 6
#define MAX_MAC_STRING_CHAR 17
#define OTP_BASE_ADDR 0x114
#define OTP_LOCK_REGION_BASE 0x112
#define OTP_REGION_OFFSET 0x010
#define OTP_REGION_INSET 0x02
#define OTP_MAX_MAC_ADDR_OFFSET 32
#define OTP_OFFSET_PARAM "labx_ethernet.base_otp_reg"
#endif

#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif

static struct spi_flash *flash;

static int do_spi_flash_probe(int argc, char *argv[])
{
	unsigned int bus = 0;
	unsigned int cs;
	unsigned int speed = CONFIG_SF_DEFAULT_SPEED;
	unsigned int mode = CONFIG_SF_DEFAULT_MODE;
	char *endp;
	struct spi_flash *new;

	if (argc < 2)
		goto usage;

	cs = simple_strtoul(argv[1], &endp, 0);
	if (*argv[1] == 0 || (*endp != 0 && *endp != ':'))
		goto usage;
	if (*endp == ':') {
		if (endp[1] == 0)
			goto usage;

		bus = cs;
		cs = simple_strtoul(endp + 1, &endp, 0);
		if (*endp != 0)
			goto usage;
	}

	if (argc >= 3) {
		speed = simple_strtoul(argv[2], &endp, 0);
		if (*argv[2] == 0 || *endp != 0)
			goto usage;
	}
	if (argc >= 4) {
		mode = simple_strtoul(argv[3], &endp, 16);
		if (*argv[3] == 0 || *endp != 0)
			goto usage;
	}

	new = spi_flash_probe(bus, cs, speed, mode);
	if (!new) {
		printf("Failed to initialize SPI flash at %u:%u\n", bus, cs);
		return 1;
	}

	if (flash)
		spi_flash_free(flash);
	flash = new;

	printf("%u KiB %s at %u:%u is now current device\n",
			flash->size >> 10, flash->name, bus, cs);

	return 0;

usage:
	puts("Usage: sf probe [bus:]cs [hz] [mode]\n");
	return 1;
}

static int do_spi_flash_read_write(int argc, char *argv[])
{
	unsigned long addr;
	unsigned long offset;
	unsigned long len;
	void *buf;
	char *endp;
	int ret;

	if (argc < 4)
		goto usage;

	addr = simple_strtoul(argv[1], &endp, 16);
	if (*argv[1] == 0 || *endp != 0)
		goto usage;
	offset = simple_strtoul(argv[2], &endp, 16);
	if (*argv[2] == 0 || *endp != 0)
		goto usage;
	len = simple_strtoul(argv[3], &endp, 16);
	if (*argv[3] == 0 || *endp != 0)
		goto usage;

	buf = map_physmem(addr, len, MAP_WRBACK);
	if (!buf) {
		puts("Failed to map physical memory\n");
		return 1;
	}

	if (strcmp(argv[0], "read") == 0)
		ret = spi_flash_read(flash, offset, len, buf);
	else
		ret = spi_flash_write(flash, offset, len, buf);

	unmap_physmem(buf, len);

	if (ret) {
		printf("SPI flash %s failed\n", argv[0]);
		return 1;
	}

	return 0;

usage:
	printf("Usage: sf %s addr offset len\n", argv[0]);
	return 1;
}

static int do_spi_flash_erase(int argc, char *argv[])
{
	unsigned long offset;
	unsigned long len;
	char *endp;
	int ret;

	if (argc < 3)
		goto usage;

	offset = simple_strtoul(argv[1], &endp, 16);
	if (*argv[1] == 0 || *endp != 0)
		goto usage;
	len = simple_strtoul(argv[2], &endp, 16);
	if (*argv[2] == 0 || *endp != 0)
		goto usage;

	ret = spi_flash_erase(flash, offset, len);
	if (ret) {
		printf("SPI flash %s failed\n", argv[0]);
		return 1;
	}

	return 0;

usage:
	puts("Usage: sf erase offset len\n");
	return 1;
}

static int do_spi_flash(cmd_tbl_t *cmdtp, int flag, int argc, char *argv[])
{
	const char *cmd;

	/* need at least two arguments */
	if (argc < 2)
		goto usage;

	cmd = argv[1];

	if (strcmp(cmd, "probe") == 0)
		return do_spi_flash_probe(argc - 1, argv + 1);

	/* The remaining commands require a selected device */
	if (!flash) {
		puts("No SPI flash selected. Please run `sf probe'\n");
		return 1;
	}

	if (strcmp(cmd, "read") == 0 || strcmp(cmd, "write") == 0)
		return do_spi_flash_read_write(argc - 1, argv + 1);
	if (strcmp(cmd, "erase") == 0)
		return do_spi_flash_erase(argc - 1, argv + 1);

	return 0;
usage:
	cmd_usage(cmdtp);
	return 1;
}

#ifdef CFG_SPI_OTP
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
	err = spi_flash_read(flash, fdt_flash_offset, sizeof(hdrbuf), hdrbuf);
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
	err = spi_flash_read(flash, fdt_flash_offset, fdt_flash_size, pfdt);
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

	puts ("Writing MAC to OTP flash...\n");

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

	// Probe SPI flash device
	run_command(getenv("spiprobe"), flag);
	
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

	ulong otp_addr = (ulong)(OTP_BASE_ADDR + ((i + ethnum) * OTP_REGION_OFFSET));
	ulong otp_lock_addr = (ulong)OTP_LOCK_REGION_BASE + ((i + ethnum) >> 3);
	uint8_t lockmask = 0x01 << ethnum;
	
	ret = spi_flash_read_otp(flash, otp_lock_addr, 1, (void*)&lockbits);
	
	if((lockbits & lockmask) != 0) 
	{ 
		lockmask = lockmask ^ 0xFF;
		for(i = 0; i < (MAC_ADDR_BYTES+OTP_REGION_INSET); i++) 
		{
        		ret = spi_flash_write_otp(flash, (otp_addr+i), 1, (void*)&macbyte[i]);
		}

		// Read MAC written to OTP and verify
		ret = spi_flash_read_otp(flash, otp_addr, (MAC_ADDR_BYTES+OTP_REGION_INSET), (void*)stored_macbyte); 
		
		for(i = 0; i < (MAC_ADDR_BYTES+OTP_REGION_INSET); i++) {
			if(stored_macbyte[i] != macbyte[i]) goto fail;			
		}

		// Lock MAC address OTP region
		ret = spi_flash_write_otp(flash, otp_lock_addr, 1, (void*)&lockmask);  
	} else 
	{
		printf("OTP region for %s is locked\n", ethintf);
		goto fail;
	}
	
	if (ret) {
		printf("SPI flash %s failed\n", argv[0]);
		goto fail;
	}
	else {
		printf("Successfully programmed %s and locked OTP region\n", ethintf);
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
	
	// Probe SPI flash device
	run_command(getenv("spiprobe"), flag);
	
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

	otp_addr = (ulong)(OTP_BASE_ADDR + ((i + ethnum) * OTP_REGION_OFFSET) + OTP_REGION_INSET);
	printf("eth%d MAC: ", ethnum);
	ret = spi_flash_read_otp(flash, otp_addr, MAC_ADDR_BYTES, (void*)macbyte); 
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
#endif

U_BOOT_CMD(
	sf,	5,	1,	do_spi_flash,
	"SPI flash sub-system",
	"probe [bus:]cs [hz] [mode]	- init flash device on given SPI bus\n"
	"				  and chip select\n"
	"sf read addr offset len 	- read `len' bytes starting at\n"
	"				  `offset' to memory at `addr'\n"
	"sf write addr offset len	- write `len' bytes from memory\n"
	"				  at `addr' to flash at `offset'\n"
	"sf erase offset len		- erase `len' bytes from `offset'\n"
         
);

#ifdef CFG_SPI_OTP
U_BOOT_CMD(
	setmac,		3,	1,	do_setmac,
	"Write MAC address to SPI flash OTP area",
	"interface MAC (format: XX:XX:XX:XX:XX:XX) - set permanent MAC for interface\n"
);

U_BOOT_CMD(
	readmac,	2,	1,	do_getmac,
	"Read MAC address from SPI flash OTP area",
	"interface - read permanent MAC for interface\n"
);
#endif     
