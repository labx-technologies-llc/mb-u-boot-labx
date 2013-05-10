/* File        : preboot.c
 * Author      : Yuriy Dragunov (yuriy.dragunov@labxtechnologies.com)
 * Description : Pre-boot procedures for U-Boot.
 * Copyright (c) 2012, Lab X Technologies, LLC.  All rights reserved. */

#include <common.h>
#include <config.h>
#include <command.h>
#include <asm/io.h>
#include <image.h>
#include "preboot.h"

#ifdef CONFIG_SPI_FLASH
#include <spi_flash.h>
#endif

#ifdef USE_ICAP_FSL
#include "asm/microblaze_fsl.h"

/* "Magic" value written to the ICAP GENERAL5 register to detect fallback */
#define GENERAL5_MAGIC (0x0ABCD)
#endif

#ifdef CONFIG_LABX_PREBOOT
#ifndef USE_ICAP_FSL
#error "Support for the ICAP module is required for Lab X pre-boot procedures (USE_ICAP_FSL not defined)"
#endif

/* Arrays for variables that hold the locations
 * of all of the images to check CRCs for. These
 * variables need to be defined as part of the
 * U-Boot environment. The last number is whether
 * the image header is stored in flash as part
 * of the image, instead of in its dedicated
 * location (for the Linux kernel, for instance).
 * The third number is the length of that image's
 * partition (as a sanity check, in case the
 * size is wrong because the flash data is wrong.
 * We don't want to CRC-check huge amounts of area. */
static const char *golden_crc_vars[][5] = {
  { "Boot FPGA"     , "bootfpgastart"    , "bootfpgasize"    , "bootfpgahdr"    , 0 },
  { "Golden FDT"    , "goldenfdtstart"   , "goldenfdtsize"   , "goldenfdthdr"   , 0 },
  { "U-Boot"        , "bootstart"        , "bootsize"        , "boothdr"        , 0 },
  { "Golden Kernel" , "goldenkernstart"  , "goldenkernsize"  , 0,          (char*)1 },
  { "Golden Root FS", "goldenrootfsstart", "goldenrootfssize", "goldenrootfshdr", 0 },
  { "Golden ROM FS" , "goldenromfsstart" , "goldenromfssize" , "goldenromfshdr" , 0 }
};
static const unsigned int num_golden_crcs = sizeof(golden_crc_vars) / sizeof(golden_crc_vars[0]);

static const char *crc_vars[][5] = {
  { "FPGA"       , "fpgastart"      , "fpgasize"      , "fpgahdr"      , 0 },
  { "FDT"        , "fdtstart"       , "fdtsize"       , "fdthdr"       , 0 },
  { "Kernel"     , "kernstart"      , "kernsize"      , 0,        (char*)1 },
  { "Root FS"    , "rootfsstart"    , "rootfssize"    , "rootfshdr"    , 0 },
  { "ROM FS"     , "romfsstart"     , "romfssize"     , "romfshdr"     , 0 }
};
static const unsigned int num_crcs = sizeof(crc_vars) / sizeof(crc_vars[0]);

/* Pre-boot function. */
int labx_preboot(int bootdelay) {
  if(labx_is_golden_fpga()) {
#ifdef CONFIG_BOOTDELAY /* If auto-boot is compiled in. */
    /* The auto-boot will reconfigure
     * to the production FPGA. */
    setenv("bootcmd", "reconf 1");

#if CONFIG_BOOTDELAY > 0
    /* Print if we are in development mode. */
    puts("Development build. Not checking CRCs.\n");
#else
    /* Only check CRCs and perform related logic if
     * we are not in development mode (i.e. no boot
     * delay is configured, and we try to boot right
     * away), and if we are to boot right away. */
    if(bootdelay == 0) {
      puts("Checking runtime CRCs...\n");
      if(!check_runtime_crcs()) {
        /* Runtime CRCs failed. The boot command
         * will now boot golden Linux. */
        setenv("bootcmd", "run bootglnx");

        puts("Runtime CRC checks failed. Checking golden CRCs...\n");
        if(!check_golden_crcs()) {
          puts("Golden CRC checks failed. Staying in U-Boot. Please perform a firmware update.\n");
          labx_print_cmdhelp();
          puts("Type 'boot' to try to boot to golden Linux anyways.\n");
          return -1; /* Abort auto-boot (i.e. stay in U-Boot). */
        } else {
          puts("Golden CRC checks passed. Golden Linux will be booted.\n");
          return 0; /* Continue to boot right away. */
        }
      } else {
        puts("Runtime CRC checks passed. Booting Linux.\n");
      }
    } else {
      puts("Production build: boot delay requested, not checking CRCs.\n");
    }
#endif
#endif
    /* Do not affect boot-up mode (delay or not). */
    return 1;
  } else {
    /* In the production FPGA, we always try to boot right away. */
    return 0;
  }
}

void labx_print_cmdhelp(void) {
	puts("Available commands:\n");
	puts("  'checkc', 'checkg', 'checkp', to check all, golden, and production CRCs.\n");
	puts("  'reconf 1' to reconfigure to the production FPGA (no arg for golden).\n");
	puts("  'run bootglnx' to boot golden linux.\n");
}

static int check_crcs(const char *crc_vars[][5], int num) {
  int success = 1;
  char start_var[11], hdr_var[11], *part_size_var;
  unsigned int i, start_off, hdr_off, part_size, crc_in_image;
  const unsigned int hdr_len = sizeof(image_header_t);
  static unsigned char *ddr = NULL;
  image_header_t *hdr_ddr;
#ifdef CONFIG_SPI_FLASH
  static struct spi_flash *spiflash = NULL;
#endif

  // Use the start of DDR memory for temporary storage.
  if(!ddr) {
    if(!(ddr = map_physmem(XPAR_DDR2_CONTROL_MPMC_BASEADDR, XPAR_DDR2_CONTROL_MPMC_HIGHADDR - XPAR_DDR2_CONTROL_MPMC_BASEADDR, MAP_WRBACK))) {
      printf("Failed to map physical memory at 0x%08X\n", XPAR_DDR2_CONTROL_MPMC_BASEADDR);
      return 0;
    } else {
      hdr_ddr = (image_header_t*)ddr;
    }
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

  // Loop over each flash image, copy it and its
  // header from flash to DDR, and perform the CRC
  // check based on the image and the CRC stored
  // within the header.
  for(i = 0; i < num; i++) {
    printf("Checking CRC for %s image... ", crc_vars[i][0]);

    if(getenv_r(crc_vars[i][1], start_var, sizeof(start_var)) == -1) {
      printf("required environment variable \"%s\" not defined.\n", crc_vars[i][1]);
      continue;
    } else { start_off = simple_strtoul(start_var, NULL, 16); }
    
    crc_in_image = (unsigned int)crc_vars[i][4];

    // Header and image locations.
    if(!crc_in_image) {
      if(getenv_r(crc_vars[i][3], hdr_var, sizeof(hdr_var)) == -1) {
        printf("required environment variable \"%s\" not defined.\n", crc_vars[i][3]);
        continue;
      } else { hdr_off = simple_strtoul(hdr_var, NULL, 16); }
    } else {
      hdr_off = start_off;
      start_off += hdr_len;
    }

    // Header.
#ifdef CONFIG_SPI_FLASH
    spi_flash_read(spiflash, hdr_off, hdr_len, ddr);
#else
    memcpy(ddr, hdr_off, hdr_len);
#endif

    // Sanity checks.
    if(!(part_size_var = getenv(crc_vars[i][2]))) {
      printf("required environment variable \"%s\" not defined.\n", crc_vars[i][2]);
      success = 0;
      continue;
    } else { part_size = simple_strtoul(part_size_var, NULL, 16); }
    if(part_size > 0x800000 || hdr_ddr->ih_size > part_size) {
      puts("failed sanity checks: exuberant sizes reported.\n");
      success = 0;
      continue;
    } else if(hdr_ddr->ih_size == 0) {
      puts("failed sanity checks: zero size reported.\n");
      success = 0;
      continue;
    }

    // Data image.
#ifdef CONFIG_SPI_FLASH
    spi_flash_read(spiflash, start_off, hdr_ddr->ih_size, ddr + hdr_len);
#else
    memcpy(ddr + hdr_len, (const void*)(start_off), hdr_ddr->ih_size);
#endif

    // Perform CRC check.
    if(image_check_dcrc(hdr_ddr)) {
      puts("OK\n");
    } else {
      puts("Failed\n");
      success = 0;
    }
  }

  // CRC check status.
  return success;
}

int cmd_check_crcs(struct cmd_tbl_s* cmd_tbl, int flag, int argc, char *argv[]) {
  int success = check_golden_crcs();
  if(success == 0) success = check_runtime_crcs();
  else check_runtime_crcs();
  return !success;
}

int check_runtime_crcs(void) {
  return check_crcs(crc_vars, num_crcs);
}

int cmd_check_runtime_crcs(struct cmd_tbl_s* cmd_tbl, int flag, int argc, char *argv[]) {
  return !check_runtime_crcs();
}

int check_golden_crcs(void) {
  return check_crcs(golden_crc_vars, num_golden_crcs);
}

int cmd_check_golden_crcs(struct cmd_tbl_s* cmd_tbl, int flag, int argc, char *argv[]) {
  return !check_golden_crcs();
}

U_BOOT_CMD(checkg, 1, 1, cmd_check_golden_crcs,
           "checks the golden image CRCs",
           "checks the golden image CRCs");
U_BOOT_CMD(checkp, 1, 1, cmd_check_runtime_crcs,
           "checks the production image CRCs",
           "checks the production image CRCs");
U_BOOT_CMD(checkc, 1, 1, cmd_check_crcs,
           "checks all image CRCs",
           "checks all image CRCs");
#endif /* CONFIG_LABX_PREBOOT */

#ifdef USE_ICAP_FSL
static int read_general_5(void) {
  u16 readValue;

  /* Read the GENERAL5 register from the ICAP peripheral. */
  putfslx(0x0FFFF, 0, FSL_CONTROL_ATOMIC);
  udelay(1000);

  putfslx(0x0FFFF, 0, FSL_ATOMIC); // Pad words
  putfslx(0x0FFFF, 0, FSL_ATOMIC);
  putfslx(0x0AA99, 0, FSL_ATOMIC); // SYNC
  putfslx(0x05566, 0, FSL_ATOMIC); // SYNC

  // Read GENERAL5
  putfslx(0x02AE1, 0, FSL_ATOMIC);

  // Add some safety noops and wait briefly
  putfslx(0x02000, 0, FSL_ATOMIC); // Type 1 NOP
  putfslx(0x02000, 0, FSL_ATOMIC); // Type 1 NOP

  // Trigger the FSL peripheral to drain the FIFO into the ICAP.
  // Wait briefly for the read to occur.
  putfslx(FINISH_FSL_BIT, 0, FSL_ATOMIC);
  udelay(1000);
  getfslx(readValue, 0, FSL_ATOMIC); // Read the ICAP result

  return readValue;
}

int labx_is_fallback_fpga(void) {
  static int is_fallback = -1;

  if(is_fallback == -1) {
    is_fallback = (read_general_5() == GENERAL5_MAGIC);
  }

  return is_fallback;
}

int labx_is_golden_fpga(void) {
  static int is_golden = -1;

  if(is_golden == -1) {
    is_golden = (read_general_5() == 0);
  }

  return is_golden || labx_is_fallback_fpga();
}
#endif /* USE_ICAP_FSL */

