/*
 * Broadcom Common Firmware Environment (CFE)
 * Board device initialization, File: bcm947xx_devs.c
 *
 * Copyright (C) 2012, Broadcom Corporation
 * All Rights Reserved.
 * 
 * This is UNPUBLISHED PROPRIETARY SOURCE CODE of Broadcom Corporation;
 * the contents of this file may not be disclosed to third parties, copied
 * or duplicated in any form, in whole or in part, without the prior
 * written permission of Broadcom Corporation.
 *
 * $Id: bcm947xx_devs.c 375038 2012-12-17 08:23:00Z $
 */

#include "lib_types.h"
#include "lib_printf.h"
#include "lib_physio.h"
#include "cfe.h"
#include "cfe_error.h"
#include "cfe_iocb.h"
#include "cfe_device.h"
#include "cfe_timer.h"
#ifdef CFG_ROMBOOT
#include "cfe_console.h"
#endif
#include "ui_command.h"
#include "bsp_config.h"
#include "dev_newflash.h"
#include "env_subr.h"
#include "pcivar.h"
#include "pcireg.h"
#include "../../../../../dev/ns16550.h"
#include "net_ebuf.h"
#include "net_ether.h"
#include "net_api.h"

#include <typedefs.h>
#include <osl.h>
#include <bcmutils.h>
#include <hndsoc.h>
#include <siutils.h>
#include <sbchipc.h>
#include <hndpci.h>
#include "bsp_priv.h"
#include <trxhdr.h>
#include <bcmdevs.h>
#include <bcmnvram.h>
#include <hndcpu.h>
#include <hndchipc.h>
#include <hndpmu.h>
#include <epivers.h>
#if CFG_NFLASH
#include <hndnand.h>
#endif
#if CFG_SFLASH
#include <hndsflash.h>
#endif
#include <cfe_devfuncs.h>
#include <cfe_ioctl.h>

#define MAX_WAIT_TIME 20	/* seconds to wait for boot image */
#define MIN_WAIT_TIME 3 	/* seconds to wait for boot image */

#define RESET_DEBOUNCE_TIME	(500*1000)	/* 500 ms */

/* Defined as sih by bsp_config.h for convenience */
si_t *bcm947xx_sih = NULL;

/* Configured devices */
#if (CFG_FLASH || CFG_SFLASH || CFG_NFLASH) && CFG_XIP
#error "XIP and Flash cannot be defined at the same time"
#endif

extern cfe_driver_t ns16550_uart;
#if CFG_FLASH
extern cfe_driver_t newflashdrv;
#endif
#if CFG_SFLASH
extern cfe_driver_t sflashdrv;
#endif
#if CFG_NFLASH
extern cfe_driver_t nflashdrv;
#endif
#if CFG_ET
extern cfe_driver_t bcmet;
#endif
#if CFG_WL
extern cfe_driver_t bcmwl;
#endif
#if CFG_BCM57XX
extern cfe_driver_t bcm5700drv;
#endif

#ifdef CFG_ROMBOOT
#define MAX_SCRIPT_FSIZE	10240
#endif

#define HC_BDINFO_SIZE		65536	/* 64*1024 */
#define HC_BACKUP_SIZE		65536	/* 64*1024 */

/* Reset NVRAM */
static int restore_defaults = 0;
extern char *flashdrv_nvram;

static void
board_console_add(void *regs, uint irq, uint baud_base, uint reg_shift)
{
	physaddr_t base;

	/* The CFE NS16550 driver expects a physical address */
	base = PHYSADDR((physaddr_t) regs);
	cfe_add_device(&ns16550_uart, base, baud_base, &reg_shift);
}

#if CFG_FLASH || CFG_SFLASH || CFG_NFLASH
#if !CFG_SIM
static void
reset_release_wait(void)
{
	int gpio;
	uint32 gpiomask;

	if ((gpio = nvram_resetgpio_init ((void *)sih)) < 0)
		return;

	/* Reset button is active low */
	gpiomask = (uint32)1 << gpio;
	while (1) {
		if (si_gpioin(sih) & gpiomask) {
			OSL_DELAY(RESET_DEBOUNCE_TIME);

			if (si_gpioin(sih) & gpiomask)
				break;
		}
	}
}
#endif /* !CFG_SIM */
#endif /* CFG_FLASH || CFG_SFLASH || CFG_NFLASH */

#define RECOVERY_GPIO_BUTTON		11
#define RECOVERY_SERVER_IP      "192.168.1.88"
#define RECOVERY_DUT_NETIP		"192.168.1.1"
#define RECOVERY_DUT_NETMASK	"255.255.255.0"
#define RECOVERY_IMG_NAME       "recovery.bin"

void recovery_button_init(void)
{
	if (sih == NULL)
	{
		xprintf("si attach failed\n");
		return;
	}

	/*
	 * input mode
	 */
	si_gpioouten(sih, ((uint32) 1 << RECOVERY_GPIO_BUTTON), 0, GPIO_DRV_PRIORITY);
}

int recovery_button_status(void)
{
	if (sih == NULL)
	{
		xprintf("si attach failed\n");
		return -1;
	}

	if (si_gpioin(sih) & ((uint32) 1 << RECOVERY_GPIO_BUTTON))
	{
		return 0;
	}
	else
	{
		/* pressed */
		return 1;
	}

	return 0;
}

int recovery_op(void)
{
    char buf[256];

	if (cfe_finddev("eth0"))
	{
		memset(buf, 0, sizeof(buf));
		sprintf(buf, "ifconfig eth0 -addr=%s -mask=%s", RECOVERY_DUT_NETIP, RECOVERY_DUT_NETMASK);

		ui_docommand(buf);	
	}

	memset(buf, 0, sizeof(buf));
    sprintf(buf, "hccmd %s:%s", RECOVERY_SERVER_IP, RECOVERY_IMG_NAME);

    ui_docommand(buf);

    return 1;
}

#define LED_WLAN2G_GPIO_PIN		4
#define LED_WLAN5G_GPIO_PIN		5
#define LED_INTERNET_GPIO_PIN	6
#define LED_SYS_GPIO_PIN		7

void leds_gpio_init(void)
{
	uint32 mask = 1 << LED_WLAN2G_GPIO_PIN | 1 << LED_WLAN5G_GPIO_PIN | 1 << LED_INTERNET_GPIO_PIN | 1 << LED_SYS_GPIO_PIN;

	if (sih == NULL)
	{
		xprintf("si attach failed\n");
		return;
	}

	/*
	 * output mode
	 */
	si_gpioouten(sih, mask, mask, GPIO_DRV_PRIORITY);	
}

void leds_on(void)
{
	uint32 mask = 1 << LED_WLAN2G_GPIO_PIN | 1 << LED_WLAN5G_GPIO_PIN | 1 << LED_INTERNET_GPIO_PIN | 1 << LED_SYS_GPIO_PIN;

	if (sih == NULL)
	{
		xprintf("si attach failed\n");
		return;
	}

	/*
	 * leds on
	 */
	si_gpioout(sih, mask, mask, GPIO_DRV_PRIORITY);	
}

void leds_off(void)
{
	/*
	 * keep system led on
	 */
	uint32 mask = 1 << LED_WLAN2G_GPIO_PIN | 1 << LED_WLAN5G_GPIO_PIN | 1 << LED_INTERNET_GPIO_PIN;

	if (sih == NULL)
	{
		xprintf("si attach failed\n");
		return;
	}

	/*
	 * leds off
	 */
	si_gpioout(sih, mask, ~mask, GPIO_DRV_PRIORITY);	
}

static void led_control(uint32 gpio, uint32 value)
{
	uint32 mask = 1 << gpio;
	uint32 gpio_value;

	if (sih == NULL)
	{
		xprintf("si attach failed\n");
		return;
	}

	if (value)
	{
		gpio_value = 1 << gpio;
	}
	else
	{
		gpio_value = 0 << gpio;
	}

	/*
	 * output mode
	 */
	si_gpioouten(sih, mask, mask, GPIO_DRV_PRIORITY);

	si_gpioout(sih, mask, gpio_value, GPIO_DRV_PRIORITY);
}

void flash_op_led(uint32 value)
{
	switch(value)
	{
	case 0:
		led_control(LED_SYS_GPIO_PIN, 1);
		led_control(LED_INTERNET_GPIO_PIN, 0);
		led_control(LED_WLAN2G_GPIO_PIN, 0);
		led_control(LED_WLAN5G_GPIO_PIN, 0);
		break;
	case 1:
		led_control(LED_SYS_GPIO_PIN, 0);
		led_control(LED_INTERNET_GPIO_PIN, 1);
		led_control(LED_WLAN2G_GPIO_PIN, 0);
		led_control(LED_WLAN5G_GPIO_PIN, 0);
		break;
	case 2:
		led_control(LED_SYS_GPIO_PIN, 0);
		led_control(LED_INTERNET_GPIO_PIN, 0);
		led_control(LED_WLAN2G_GPIO_PIN, 1);
		led_control(LED_WLAN5G_GPIO_PIN, 0);
		break;
	default:
		led_control(LED_SYS_GPIO_PIN, 0);
		led_control(LED_INTERNET_GPIO_PIN, 0);
		led_control(LED_WLAN2G_GPIO_PIN, 0);
		led_control(LED_WLAN5G_GPIO_PIN, 1);
		break;
	}
}

/*
 *  board_console_init()
 *
 *  Add the console device and set it to be the primary
 *  console.
 *
 *  Input parameters:
 *     nothing
 *
 *  Return value:
 *     nothing
 */
void
board_console_init(void)
{
#if !CFG_MINIMAL_SIZE
	cfe_set_console(CFE_BUFFER_CONSOLE);
#endif

	/* Initialize SB access */
	sih = si_kattach(SI_OSH);
	ASSERT(sih);

	/* Set this to a default value, since nvram_reset needs to use it in OSL_DELAY */
	board_cfe_cpu_speed_upd(sih);

#if !CFG_SIM
	board_pinmux_init(sih);
	/* Check whether NVRAM reset needs be done */
	if (nvram_reset((void *)sih) > 0)
		restore_defaults = 1;
#endif

	/*
	 * init gpio mode for leds and reset button
	 */
	recovery_button_init();
	leds_gpio_init();

	/* Initialize NVRAM access accordingly. In case of invalid NVRAM, load defaults */
	if (nvram_init((void *)sih) > 0)
		restore_defaults = 1;

#if CFG_SIM
	restore_defaults = 0;
#else /* !CFG_SIM */

	if (!restore_defaults)
		board_clock_init(sih);

	board_power_init(sih);
#endif /* !CFG_SIM */

	board_cpu_init(sih);

	/* Initialize UARTs */
	si_serial_init(sih, board_console_add);

	if (cfe_finddev("uart0"))
		cfe_set_console("uart0");
}


#if (CFG_FLASH || CFG_SFLASH || CFG_NFLASH)
#if (CFG_NFLASH || defined(FAILSAFE_UPGRADE) || defined(DUAL_IMAGE))
static int
get_flash_size(char *device_name)
{
	int fd;
	flash_info_t flashinfo;
	int res = -1;

	fd = cfe_open(device_name);
	if ((fd > 0) &&
	    (cfe_ioctl(fd, IOCTL_FLASH_GETINFO,
		(unsigned char *) &flashinfo,
		sizeof(flash_info_t), &res, 0) == 0)) {

		cfe_close(fd);
		return flashinfo.flash_size;
	}

	return -1;
}
#endif /* CFG_NFLASH */

#if defined(FAILSAFE_UPGRADE) || defined(DUAL_IMAGE)
static
int calculate_max_image_size(
	char *device_name,
	int reserved_space_begin,
	int reserved_space_end,
	int *need_commit)
{
	int image_size = 0;
	char *nvram_setting;
	char buf[64];

	*need_commit = 0;

#ifdef DUAL_IMAGE
	if (!nvram_get(IMAGE_BOOT))
#else
	if (!nvram_get(BOOTPARTITION))
#endif
		return 0;

	nvram_setting =  nvram_get(IMAGE_SIZE);

	if (nvram_setting) {
		image_size = atoi(nvram_setting)*1024;
#ifdef CFG_NFLASH
	} else if (device_name[0] == 'n') {
		/* use 32 meg for nand flash */
		image_size = (NFL_BOOT_OS_SIZE - reserved_space_begin)/2;
		image_size = image_size - image_size%(64*1024);
#endif
	} else {
		int flash_size;

		flash_size = get_flash_size(device_name);
		if (flash_size > 0) {
			int available_size =
			        flash_size - (reserved_space_begin + reserved_space_end);

			/* Calculate the 2nd offset with divide the
			 * availabe space by 2
			 * Make sure it is aligned to 64Kb to set
			 * the rootfs search algorithm
			 */
			image_size = available_size/2;
			image_size = image_size - image_size%(128*1024);
		}
	}
	/* 1st image start from bootsz end */
	sprintf(buf, "%d", reserved_space_begin);
	if (!nvram_match(IMAGE_FIRST_OFFSET, buf)) {
		printf("The 1st image start addr  changed, set to %s[%x] (was %s)\n",
			buf, reserved_space_begin, nvram_get(IMAGE_FIRST_OFFSET));
		nvram_set(IMAGE_FIRST_OFFSET, buf);
		*need_commit = 1;
	}
	sprintf(buf, "%d", reserved_space_begin + image_size);
	if (!nvram_match(IMAGE_SECOND_OFFSET, buf)) {
		printf("The 2nd image start addr  changed, set to %s[%x] (was %s)\n",
			buf, reserved_space_begin + image_size, nvram_get(IMAGE_SECOND_OFFSET));
		nvram_set(IMAGE_SECOND_OFFSET, buf);
		*need_commit = 1;
	}

	return image_size;
}
#endif /* FAILSAFE_UPGRADE|| DUAL_IMAGE */

#ifdef CFG_NFLASH
static void
flash_nflash_init(void)
{
	newflash_probe_t fprobe;
	cfe_driver_t *drv;
	hndnand_t *nfl_info;
	int j;
	int max_image_size = 0;
#if defined(DUAL_IMAGE) || defined(FAILSAFE_UPGRADE)
	int need_commit = 0;
#endif

	memset(&fprobe, 0, sizeof(fprobe));

	nfl_info = hndnand_init(sih);
	if (nfl_info == 0) {
		printf("Can't find nandflash! ccrev = %d, chipst= %d \n", sih->ccrev, sih->chipst);
		return;
	}

	drv = &nflashdrv;
	fprobe.flash_phys = nfl_info->phybase;

	j = 0;

	/* kernel in nand flash */
	if (soc_knl_dev((void *)sih) == SOC_KNLDEV_NANDFLASH) {
#if defined(FAILSAFE_UPGRADE) || defined(DUAL_IMAGE)
		max_image_size = calculate_max_image_size("nflash0", 0, 0, &need_commit);
#endif
		/* Because CFE can only boot from the beginning of a partition */
		fprobe.flash_parts[j].fp_size = sizeof(struct trx_header);
		fprobe.flash_parts[j++].fp_name = "trx";
		fprobe.flash_parts[j].fp_size = max_image_size ?
		        max_image_size - sizeof(struct trx_header) : 0;
		fprobe.flash_parts[j++].fp_name = "os";
#if defined(FAILSAFE_UPGRADE) || defined(DUAL_IMAGE)
		if (max_image_size) {
			fprobe.flash_parts[j].fp_size = sizeof(struct trx_header);
			fprobe.flash_parts[j++].fp_name = "trx2";
			fprobe.flash_parts[j].fp_size = max_image_size;
			fprobe.flash_parts[j++].fp_name = "os2";
		}
#endif
		fprobe.flash_nparts = j;

		cfe_add_device(drv, 0, 0, &fprobe);

		/* Because CFE can only boot from the beginning of a partition */
		j = 0;
		fprobe.flash_parts[j].fp_size = max_image_size ?
		        max_image_size : NFL_BOOT_OS_SIZE;
		fprobe.flash_parts[j++].fp_name = "trx";
#if defined(FAILSAFE_UPGRADE) || defined(DUAL_IMAGE)
		if (max_image_size) {
			fprobe.flash_parts[j].fp_size = NFL_BOOT_OS_SIZE - max_image_size;
			fprobe.flash_parts[j++].fp_name = "trx2";
		}
#endif
	}

	fprobe.flash_parts[j].fp_size = 0;
	fprobe.flash_parts[j++].fp_name = "brcmnand";
	fprobe.flash_nparts = j;

	cfe_add_device(drv, 0, 0, &fprobe);

#if defined(FAILSAFE_UPGRADE) || defined(DUAL_IMAGE)
	if (need_commit)
		nvram_commit();
#endif
}
#endif /* CFG_NFLASH */


static void
flash_init(void)
{
	newflash_probe_t fprobe;
	int bootdev;
	uint32 bootsz, *bisz;
	uint32 bdinfosz;
	uint32 backupsz;
	cfe_driver_t *drv = NULL;
	int j = 0;
	int max_image_size = 0;
	int rom_envram_size;
#if defined(DUAL_IMAGE) || defined(FAILSAFE_UPGRADE)
	int need_commit = 0;
#endif
#ifdef CFG_NFLASH
	hndnand_t *nfl_info = NULL;
#endif
#if CFG_SFLASH
	hndsflash_t *sfl_info = NULL;
#endif

	memset(&fprobe, 0, sizeof(fprobe));

	bootdev = soc_boot_dev((void *)sih);
#ifdef CFG_NFLASH
	if (bootdev == SOC_BOOTDEV_NANDFLASH) {
		nfl_info = hndnand_init(sih);
		if (!nfl_info)
			return;

		fprobe.flash_phys = nfl_info->phybase;
		drv = &nflashdrv;
	} else
#endif	/* CFG_NFLASH */
#if CFG_SFLASH
	if (bootdev == SOC_BOOTDEV_SFLASH) {
		sfl_info = hndsflash_init(sih);
		if (!sfl_info)
			return;

		fprobe.flash_phys = sfl_info->phybase;
		drv = &sflashdrv;
	}
	else
#endif
#if CFG_FLASH
	{
		/* This might be wrong, but set pflash
		 * as default if nothing configured
		 */
		chipcregs_t *cc;

		if ((cc = (chipcregs_t *)si_setcoreidx(sih, SI_CC_IDX)) == NULL)
			return;

		fprobe.flash_phys = SI_FLASH2;
		fprobe.flash_flags = FLASH_FLG_BUS16 | FLASH_FLG_DEV16;
		if (!(R_REG(NULL, &cc->flash_config) & CC_CFG_DS))
			fprobe.flash_flags = FLASH_FLG_BUS8 | FLASH_FLG_DEV16;
		drv = &newflashdrv;
	}
#endif /* CFG_FLASH */

	ASSERT(drv);

	/* Default is 256K boot partition */
	bootsz = 256 * 1024;

	/* Do we have a self-describing binary image? */
	bisz = (uint32 *)UNCADDR(fprobe.flash_phys + BISZ_OFFSET);
	if (bisz[BISZ_MAGIC_IDX] == BISZ_MAGIC) {
		int isz = bisz[BISZ_DATAEND_IDX] - bisz[BISZ_TXTST_IDX];

		if (isz > (1024 * 1024))
			bootsz = 2048 * 1024;
		else if (isz > (512 * 1024))
			bootsz = 1024 * 1024;
		else if (isz > (256 * 1024))
			bootsz = 512 * 1024;
		else if (isz <= (128 * 1024))
			bootsz = 128 * 1024;
	}
	printf("Boot partition size = %d(0x%x)\n", bootsz, bootsz);

#if CFG_NFLASH
	if (nfl_info) {
		int flash_size = 0;

		if (bootsz > nfl_info->blocksize) {
			/* Prepare double space in case of bad blocks */
			bootsz = (bootsz << 1);
		} else {
			/* CFE occupies at least one block */
			bootsz = nfl_info->blocksize;
		}

		/* Because sometimes we want to program the entire device */
		fprobe.flash_nparts = 0;
		cfe_add_device(drv, 0, 0, &fprobe);

#if defined(FAILSAFE_UPGRADE) || defined(DUAL_IMAGE)
		max_image_size =
			calculate_max_image_size("nflash0", NFL_BOOT_SIZE, 0, &need_commit);
#endif
		/* Because sometimes we want to program the entire device */
		/* Because CFE can only boot from the beginning of a partition */
		j = 0;
		fprobe.flash_parts[j].fp_size = bootsz;
		fprobe.flash_parts[j++].fp_name = "boot";
		fprobe.flash_parts[j].fp_size = (NFL_BOOT_SIZE - bootsz);
		fprobe.flash_parts[j++].fp_name = "nvram";

		fprobe.flash_parts[j].fp_size = sizeof(struct trx_header);
		fprobe.flash_parts[j++].fp_name = "trx";
		fprobe.flash_parts[j].fp_size = max_image_size ?
		        max_image_size - sizeof(struct trx_header) : 0;
		fprobe.flash_parts[j++].fp_name = "os";
#if defined(FAILSAFE_UPGRADE) || defined(DUAL_IMAGE)
		if (max_image_size) {
			fprobe.flash_parts[j].fp_size = sizeof(struct trx_header);
			fprobe.flash_parts[j++].fp_name = "trx2";
			fprobe.flash_parts[j].fp_size = max_image_size;
			fprobe.flash_parts[j++].fp_name = "os2";
		}
#endif
		if (HC_BDINFO_SIZE >  nfl_info->blocksize)
		{
			bdinfosz = (HC_BDINFO_SIZE << 1);
		}
		else
		{
			bdinfosz = nfl_info->blocksize;
		}
		fprobe.flash_parts[j].fp_size = bdinfosz;
		fprobe.flash_parts[j++].fp_name = "bdinfo";

		if (HC_BACKUP_SIZE >  nfl_info->blocksize)
		{
			backupsz = (HC_BACKUP_SIZE << 1);
		}
		else
		{
			backupsz = nfl_info->blocksize;
		}
		fprobe.flash_parts[j].fp_size = backupsz;
		fprobe.flash_parts[j++].fp_name = "backup";

		fprobe.flash_nparts = j;
		cfe_add_device(drv, 0, 0, &fprobe);

		/* Because CFE can only flash an entire partition */
		j = 0;
		fprobe.flash_parts[j].fp_size = bootsz;
		fprobe.flash_parts[j++].fp_name = "boot";
		fprobe.flash_parts[j].fp_size = (NFL_BOOT_SIZE - bootsz);
		fprobe.flash_parts[j++].fp_name = "nvram";
		fprobe.flash_parts[j].fp_size = max_image_size;
		fprobe.flash_parts[j++].fp_name = "trx";
#if defined(FAILSAFE_UPGRADE) || defined(DUAL_IMAGE)
		if (max_image_size) {
			fprobe.flash_parts[j].fp_size =
				NFL_BOOT_OS_SIZE - NFL_BOOT_SIZE - max_image_size;
			fprobe.flash_parts[j++].fp_name = "trx2";
		}
#endif
		flash_size = get_flash_size("nflash0") - NFL_BOOT_OS_SIZE;
		flash_size -= bdinfosz;
		flash_size -= backupsz;
		if (flash_size > 0) {
			fprobe.flash_parts[j].fp_size = flash_size;
			fprobe.flash_parts[j++].fp_name = "brcmnand";
		}

		fprobe.flash_parts[j].fp_size = bdinfosz;
		fprobe.flash_parts[j++].fp_name = "bdinfo";

		fprobe.flash_parts[j].fp_size = backupsz;
		fprobe.flash_parts[j++].fp_name = "backup";

		fprobe.flash_nparts = j;
		cfe_add_device(drv, 0, 0, &fprobe);

		/* Change nvram device name for NAND boot */
		flashdrv_nvram = "nflash0.nvram";
	} else
#endif /* CFG_NFLASH */
	{
		/* Because sometimes we want to program the entire device */
		fprobe.flash_nparts = 0;
		cfe_add_device(drv, 0, 0, &fprobe);

#ifdef CFG_ROMBOOT
		if (board_bootdev_rom(sih)) {
			rom_envram_size = ROM_ENVRAM_SPACE;
		}
		else
#endif
		{
			rom_envram_size = 0;
		}

#if defined(FAILSAFE_UPGRADE) || defined(DUAL_IMAGE)
		/* If the kernel is not in nand flash, split up the sflash */
		if (soc_knl_dev((void *)sih) != SOC_KNLDEV_NANDFLASH) {
			max_image_size = calculate_max_image_size("flash0",
				bootsz, MAX_NVRAM_SPACE+rom_envram_size, &need_commit);
		}
#endif

		/* Because CFE can only boot from the beginning of a partition */
		j = 0;
		fprobe.flash_parts[j].fp_size = bootsz;
		fprobe.flash_parts[j++].fp_name = "boot";
		fprobe.flash_parts[j].fp_size = sizeof(struct trx_header);
		fprobe.flash_parts[j++].fp_name = "trx";
		fprobe.flash_parts[j].fp_size = max_image_size ?
		        max_image_size - sizeof(struct trx_header) : 0;
		fprobe.flash_parts[j++].fp_name = "os";
#if defined(FAILSAFE_UPGRADE) || defined(DUAL_IMAGE)
		if (max_image_size) {
			fprobe.flash_parts[j].fp_size = sizeof(struct trx_header);
			fprobe.flash_parts[j++].fp_name = "trx2";
			fprobe.flash_parts[j].fp_size = 0;
			fprobe.flash_parts[j++].fp_name = "os2";
		}
#endif
#ifdef CFG_ROMBOOT
		if (rom_envram_size) {
			fprobe.flash_parts[j].fp_size = rom_envram_size;
			fprobe.flash_parts[j++].fp_name = "envram";
		}
#endif
		fprobe.flash_parts[j].fp_size = HC_BDINFO_SIZE;
		fprobe.flash_parts[j++].fp_name = "bdinfo";
		fprobe.flash_parts[j].fp_size = HC_BACKUP_SIZE;
		fprobe.flash_parts[j++].fp_name = "backup";		
		fprobe.flash_parts[j].fp_size = MAX_NVRAM_SPACE;
		fprobe.flash_parts[j++].fp_name = "nvram";
		fprobe.flash_nparts = j;
		cfe_add_device(drv, 0, 0, &fprobe);

		/* Because CFE can only flash an entire partition */
		j = 0;
		fprobe.flash_parts[j].fp_size = bootsz;
		fprobe.flash_parts[j++].fp_name = "boot";
		fprobe.flash_parts[j].fp_size = max_image_size;
		fprobe.flash_parts[j++].fp_name = "trx";
#if defined(FAILSAFE_UPGRADE) || defined(DUAL_IMAGE)
		if (max_image_size) {
			fprobe.flash_parts[j].fp_size = 0;
			fprobe.flash_parts[j++].fp_name = "trx2";
		}
#endif
#ifdef CFG_ROMBOOT
		if (rom_envram_size) {
			fprobe.flash_parts[j].fp_size = rom_envram_size;
			fprobe.flash_parts[j++].fp_name = "envram";
		}
#endif
		fprobe.flash_parts[j].fp_size = HC_BDINFO_SIZE;
		fprobe.flash_parts[j++].fp_name = "bdinfo";	
		fprobe.flash_parts[j].fp_size = HC_BACKUP_SIZE;
		fprobe.flash_parts[j++].fp_name = "backup";			
		fprobe.flash_parts[j].fp_size = MAX_NVRAM_SPACE;
		fprobe.flash_parts[j++].fp_name = "nvram";
		fprobe.flash_nparts = j;
		cfe_add_device(drv, 0, 0, &fprobe);
	}

#if (CFG_FLASH || CFG_SFLASH)
	flash_memory_size_config(sih, (void *)&fprobe);
#endif /* (CFG_FLASH || CFG_SFLASH) */

#ifdef CFG_NFLASH
	/* If boot from sflash, try to init partition for JFFS2 anyway */
	if (nfl_info == NULL)
		flash_nflash_init();
#endif /* CFG_NFLASH */

#if defined(FAILSAFE_UPGRADE) || defined(DUAL_IMAGE)
	if (need_commit)
		nvram_commit();
#endif
}
#endif	/* CFG_FLASH || CFG_SFLASH || CFG_NFLASH */

/*
 *  board_device_init()
 *
 *  Initialize and add other devices.  Add everything you need
 *  for bootstrap here, like disk drives, flash memory, UARTs,
 *  network controllers, etc.
 *
 *  Input parameters:
 *     nothing
 *
 *  Return value:
 *     nothing
 */
void
board_device_init(void)
{
	unsigned int unit;

#if CFG_ET || CFG_WL || CFG_BCM57XX
	void *regs;
#endif

	/* Set by board_console_init() */
	ASSERT(sih);


#if CFG_FLASH || CFG_SFLASH || CFG_NFLASH
	flash_init();
#endif

	mach_device_init(sih);

	for (unit = 0; unit < SI_MAXCORES; unit++) {
#if CFG_ET
		if ((regs = si_setcore(sih, ENET_CORE_ID, unit)))
			cfe_add_device(&bcmet, BCM47XX_ENET_ID, unit, regs);

#if CFG_GMAC
		if ((regs = si_setcore(sih, GMAC_CORE_ID, unit)))
			cfe_add_device(&bcmet, BCM47XX_GMAC_ID, unit, regs);
#endif
#endif /* CFG_ET */
#if CFG_WL
		if ((regs = si_setcore(sih, D11_CORE_ID, unit)))
			cfe_add_device(&bcmwl, BCM4306_D11G_ID, unit, regs);
#endif
#if CFG_BCM57XX
		if ((regs = si_setcore(sih, GIGETH_CORE_ID, unit)))
			cfe_add_device(&bcm5700drv, BCM47XX_GIGETH_ID, unit, regs);
#endif
	}
}

/*
 *  board_device_reset()
 *
 *  Reset devices.  This call is done when the firmware is restarted,
 *  as might happen when an operating system exits, just before the
 *  "reset" command is applied to the installed devices.   You can
 *  do whatever board-specific things are here to keep the system
 *  stable, like stopping DMA sources, interrupts, etc.
 *
 *  Input parameters:
 *     nothing
 *
 *  Return value:
 *     nothing
 */
void
board_device_reset(void)
{
}

/*
 *  board_final_init()
 *
 *  Do any final initialization, such as adding commands to the
 *  user interface.
 *
 *  If you don't want a user interface, put the startup code here.
 *  This routine is called just before CFE starts its user interface.
 *
 *  Input parameters:
 *     nothing
 *
 *  Return value:
 *     nothing
 */
void
board_final_init(void)
{
	char *addr, *mask, *wait_time;
	char buf[512], *cur = buf;
#ifdef CFG_ROMBOOT
	char *laddr = NULL;
#endif
#if !CFG_SIM
	char *boot_cfg = NULL;
	char *go_cmd = "go;";
#endif
	int commit = 0;
	uint32 ncdl;
#if CFG_WL && CFG_WLU && CFG_SIM
	char *ssid;
#endif

	ui_init_bcm947xxcmds();

	/* Force commit of embedded NVRAM */
	commit = restore_defaults;

	/* Set the SDRAM NCDL value into NVRAM if not already done */
	if ((getintvar(NULL, "sdram_ncdl") == 0) &&
	    ((ncdl = si_memc_get_ncdl(sih)) != 0)) {
		sprintf(buf, "0x%x", ncdl);
		nvram_set("sdram_ncdl", buf);
		commit = 1;
	}

	/* Set the bootloader version string if not already done */
	sprintf(buf, "CFE %s", EPI_VERSION_STR);
	if (strcmp(nvram_safe_get("pmon_ver"), buf) != 0) {
		nvram_set("pmon_ver", buf);
		commit = 1;
	}


	/* Set the size of the nvram area if not already done */
	sprintf(buf, "%d", MAX_NVRAM_SPACE);
	if (strcmp(nvram_safe_get("nvram_space"), buf) != 0) {
		nvram_set("nvram_space", buf);
		commit = 1;
	}

#if CFG_FLASH || CFG_SFLASH || CFG_NFLASH
#if !CFG_SIM
	/* Commit NVRAM only if in FLASH */
	if (
#ifdef BCMNVRAMW
		!nvram_inotp() &&
#endif
		commit) {
		printf("Committing NVRAM...");
		nvram_commit();
		printf("done\n");
		if (restore_defaults) {
			printf("Waiting for reset button release...");
			reset_release_wait();
			printf("done\n");
		}
	}

	/* Reboot after restoring defaults */
	if (restore_defaults)
		si_watchdog(sih, 1);
#endif	/* !CFG_SIM */
#else
	if (commit)
		printf("Flash not configured, not commiting NVRAM...\n");
#endif /* CFG_FLASH || CFG_SFLASH || CFG_NFLASH */

	/*
	 * Read the wait_time NVRAM variable and set the tftp max retries.
	 * Assumption: tftp_rrq_timeout & tftp_recv_timeout are set to 1sec.
	 */
	if ((wait_time = nvram_get("wait_time")) != NULL) {
		tftp_max_retries = atoi(wait_time);
		if (tftp_max_retries > MAX_WAIT_TIME)
			tftp_max_retries = MAX_WAIT_TIME;
		else if (tftp_max_retries < MIN_WAIT_TIME)
			tftp_max_retries = MIN_WAIT_TIME;
	}
#ifdef CFG_ROMBOOT
	else if (board_bootdev_rom(sih)) {
		tftp_max_retries = 10;
	}
#endif

	/* Configure network */
	if (cfe_finddev("eth0")) {
		int res;

		if ((addr = nvram_get("lan_ipaddr")) &&
		    (mask = nvram_get("lan_netmask")))
			sprintf(buf, "ifconfig eth0 -addr=%s -mask=%s",
			        addr, mask);
		else
			sprintf(buf, "ifconfig eth0 -auto");

		res = ui_docommand(buf);

#ifdef CFG_ROMBOOT
		/* Try indefinite netboot only while booting from ROM
		 * and we are sure that we dont have valid nvram in FLASH
		 */
		while (board_bootdev_rom(sih) && !addr) {
			char ch;

			cur = buf;
			/* Check if something typed at console */
			if (console_status()) {
				console_read(&ch, 1);
				/* Check for Ctrl-C */
				if (ch == 3) {
					if (laddr)
						MFREE(osh, laddr, MAX_SCRIPT_FSIZE);
					xprintf("Stopped auto netboot!!!\n");
					return;
				}
			}

			if (!res) {
				char *bserver, *bfile, *load_ptr;

				if (!laddr)
					laddr = MALLOC(osh, MAX_SCRIPT_FSIZE);

				if (!laddr) {
					load_ptr = (char *) 0x00008000;
					xprintf("Failed malloc for boot_script, Using :0x%x\n",
						(unsigned int)laddr);
				}
				else {
					load_ptr = laddr;
				}
				bserver = (bserver = env_getenv("BOOT_SERVER"))
					? bserver:"192.168.1.1";

				if ((bfile = env_getenv("BOOT_FILE"))) {
					int len;

					if (((len = strlen(bfile)) > 5) &&
					    !strncmp((bfile + len - 5), "cfesh", 5)) {
						cur += sprintf(cur,
						"batch -raw -tftp -addr=0x%x -max=0x%x %s:%s;",
							(unsigned int)load_ptr,
							MAX_SCRIPT_FSIZE, bserver, bfile);
					}
					if (((len = strlen(bfile)) > 3)) {
						if (!strncmp((bfile + len - 3), "elf", 3)) {
							cur += sprintf(cur,
							"boot -elf -tftp -max=0x5000000 %s:%s;",
							bserver, bfile);
						}
						if (!strncmp((bfile + len - 3), "raw", 3)) {
							cur += sprintf(cur,
							"boot -raw -z -tftp -addr=0x00008000"
							" -max=0x5000000 %s:%s;",
							bserver, bfile);
						}
					}
				}
				else {  /* Make last effort */
					cur += sprintf(cur,
						"batch -raw -tftp -addr=0x%x -max=0x%x %s:%s;",
						(unsigned int)load_ptr, MAX_SCRIPT_FSIZE,
						bserver, "cfe_script.cfesh");
					cur += sprintf(cur,
						"boot -elf -tftp -max=0x5000000 %s:%s;",
						bserver, "boot_file.elf");
					cur += sprintf(cur,
						"boot -raw -z -tftp -addr=0x00008000"
						" -max=0x5000000 %s:%s;",
						bserver, "boot_file.raw");
				}

				ui_docommand(buf);
				cfe_sleep(3*CFE_HZ);
			}

			sprintf(buf, "ifconfig eth0 -auto");
			res = ui_docommand(buf);
		}
#endif /* CFG_ROMBOOT */
	}
#if CFG_WL && CFG_WLU && CFG_SIM
	if ((ssid = nvram_get("wl0_ssid"))) {
		sprintf(buf, "wl join %s", ssid);
		ui_docommand(buf);
	}
#endif

#if !CFG_SIM
	/* Try to run boot_config command if configured.
	 * make sure to leave space for "go" command.
	 */
	if ((boot_cfg = nvram_get("boot_config"))) {
		if (strlen(boot_cfg) < (sizeof(buf) - sizeof(go_cmd)))
			cur += sprintf(cur, "%s;", boot_cfg);
		else
			printf("boot_config too long, skipping to autoboot\n");
	}

	/* Boot image */
	cur += sprintf(cur, go_cmd);
#endif	/* !CFG_SIM */

	/* Startup */
	if (cur > buf)
		env_setenv("STARTUP", buf, ENV_FLG_NORMAL);
}
