// This file is Copyright (c) 2013-2014 Sebastien Bourdeauducq <sb@m-labs.hk>
// This file is Copyright (c) 2013-2020 Florent Kermarrec <florent@enjoy-digital.fr>
// This file is Copyright (c) 2018 Chris Ballance <chris.ballance@physics.ox.ac.uk>
// This file is Copyright (c) 2018 Dolu1990 <charles.papon.90@gmail.com>
// This file is Copyright (c) 2019 Gabriel L. Somlo <gsomlo@gmail.com>
// This file is Copyright (c) 2018 Jean-François Nguyen <jf@lambdaconcept.fr>
// This file is Copyright (c) 2018 Sergiusz Bazanski <q3k@q3k.org>
// This file is Copyright (c) 2018 Tim 'mithro' Ansell <me@mith.ro>
// License: BSD

#include <generated/csr.h>
#include <generated/mem.h>

#include <stdio.h>
#include <stdlib.h>
#include <memtest.h>
#include <lfsr.h>

#ifdef CSR_SDRAM_BASE
#include <generated/sdram_phy.h>
#endif
#include <generated/mem.h>
#include <system.h>

#include "sdram.h"

#ifdef CSR_SDRAM_BASE

__attribute__((unused)) static void cdelay(int i)
{
#ifndef CONFIG_SIM_DISABLE_DELAYS
	while(i > 0) {
		__asm__ volatile(CONFIG_CPU_NOP);
		i--;
	}
#endif
}

/*-----------------------------------------------------------------------*/
/* Constants                                                             */
/*-----------------------------------------------------------------------*/

#define DFII_ADDR_SHIFT CONFIG_CSR_ALIGNMENT/8

#define CSR_DATA_BYTES CONFIG_CSR_DATA_WIDTH/8

#define DFII_PIX_DATA_BYTES DFII_PIX_DATA_SIZE*CSR_DATA_BYTES

int sdrdatabits(void) {
	return SDRAM_PHY_DATABITS;
}

int sdrfreq(void) {
	return SDRAM_PHY_XDR*SDRAM_PHY_PHASES*CONFIG_CLOCK_FREQUENCY;
}

/*-----------------------------------------------------------------------*/
/* Software/Hardware Control                                             */
/*-----------------------------------------------------------------------*/

#define DFII_CONTROL_SOFTWARE (DFII_CONTROL_CKE|DFII_CONTROL_ODT|DFII_CONTROL_RESET_N)
#define DFII_CONTROL_HARDWARE (DFII_CONTROL_SEL)

void sdrsw(void)
{
	unsigned int previous;
	previous = sdram_dfii_control_read();
	if (previous != DFII_CONTROL_SOFTWARE) {
		sdram_dfii_control_write(DFII_CONTROL_SOFTWARE);
		printf("Switching SDRAM to software control.\n");
	}
}

void sdrhw(void)
{
	unsigned int previous;
	previous = sdram_dfii_control_read();
	if (previous != DFII_CONTROL_HARDWARE) {
		sdram_dfii_control_write(DFII_CONTROL_HARDWARE);
		printf("Switching SDRAM to hardware control.\n");
	}
}

#ifdef CSR_DDRPHY_BASE

/*-----------------------------------------------------------------------*/
/* Write Leveling                                                        */
/*-----------------------------------------------------------------------*/

#ifdef SDRAM_PHY_WRITE_LEVELING_CAPABLE

int sdrwl_delays[16];

static void sdrwlon(void)
{
	sdram_dfii_pi0_address_write(DDRX_MR1 | (1 << 7));
	sdram_dfii_pi0_baddress_write(1);
	command_p0(DFII_COMMAND_RAS|DFII_COMMAND_CAS|DFII_COMMAND_WE|DFII_COMMAND_CS);

#ifdef SDRAM_PHY_DDR4_RDIMM
	sdram_dfii_pi0_address_write((DDRX_MR1 | (1 << 7)) ^ 0x2BF8) ;
	sdram_dfii_pi0_baddress_write(1 ^ 0xF);
	command_p0(DFII_COMMAND_RAS|DFII_COMMAND_CAS|DFII_COMMAND_WE|DFII_COMMAND_CS);
#endif

	ddrphy_wlevel_en_write(1);
}

static void sdrwloff(void)
{
	sdram_dfii_pi0_address_write(DDRX_MR1);
	sdram_dfii_pi0_baddress_write(1);
	command_p0(DFII_COMMAND_RAS|DFII_COMMAND_CAS|DFII_COMMAND_WE|DFII_COMMAND_CS);

#ifdef SDRAM_PHY_DDR4_RDIMM
	sdram_dfii_pi0_address_write(DDRX_MR1 ^ 0x2BF8);
	sdram_dfii_pi0_baddress_write(1 ^ 0xF);
	command_p0(DFII_COMMAND_RAS|DFII_COMMAND_CAS|DFII_COMMAND_WE|DFII_COMMAND_CS);
#endif

	ddrphy_wlevel_en_write(0);
}

static void write_delay_rst(int module) {
#ifdef SDRAM_PHY_WRITE_LEVELING_REINIT
	int i;
#endif

	/* sel module */
	ddrphy_dly_sel_write(1 << module);

	/* rst delay */
	ddrphy_wdly_dq_rst_write(1);
	ddrphy_wdly_dqs_rst_write(1);
#ifdef SDRAM_PHY_WRITE_LEVELING_REINIT
	for(i=0; i<ddrphy_half_sys8x_taps_read(); i++)
		ddrphy_wdly_dqs_inc_write(1);
#endif

	/* unsel module */
	ddrphy_dly_sel_write(0);
}

static void write_delay_inc(int module) {
	/* sel module */
	ddrphy_dly_sel_write(1 << module);

	/* inc delay */
	ddrphy_wdly_dq_inc_write(1);
	ddrphy_wdly_dqs_inc_write(1);

	/* unsel module */
	ddrphy_dly_sel_write(0);
}

static int write_level_scan(int *delays, int loops, int show)
{
	int i, j, k;

	int err_ddrphy_wdly;

	unsigned char taps_scan[SDRAM_PHY_DELAYS];

	int one_window_active;
	int one_window_start, one_window_best_start;
	int one_window_count, one_window_best_count;

	unsigned char buf[DFII_PIX_DATA_BYTES];

	int ok;

	err_ddrphy_wdly = SDRAM_PHY_DELAYS - ddrphy_half_sys8x_taps_read();

	sdrwlon();
	cdelay(100);
	for(i=0;i<SDRAM_PHY_MODULES;i++) {
		if (show)
			printf("  m%d: |", i);

		/* rst delay */
		write_delay_rst(i);

		/* scan write delay taps */
		for(j=0;j<err_ddrphy_wdly;j++) {
			int zero_count = 0;
			int one_count = 0;
			int show_iter = show;
#if SDRAM_PHY_DELAYS > 32
			show_iter = (j%16 == 0) && show;
#endif
			for (k=0; k<loops; k++) {
				ddrphy_wlevel_strobe_write(1);
				cdelay(10);
				csr_rd_buf_uint8(sdram_dfii_pix_rddata_addr[0],
						 buf, DFII_PIX_DATA_BYTES);
				if (buf[SDRAM_PHY_MODULES-1-i] != 0)
					one_count++;
				else
					zero_count++;
			}
			if (one_count > zero_count)
				taps_scan[j] = 1;
			else
				taps_scan[j] = 0;
			if (show_iter)
				printf("%d", taps_scan[j]);
			write_delay_inc(i);
			cdelay(10);
		}
		if (show)
			printf("|");

		/* find longer 1 window and set delay at the 0/1 transition */
		one_window_active = 0;
		one_window_start = 0;
		one_window_count = 0;
		one_window_best_start = 0;
		one_window_best_count = -1;
		delays[i] = -1;
		for(j=0;j<err_ddrphy_wdly;j++) {
			if (one_window_active) {
				if ((taps_scan[j] == 0) | (j == err_ddrphy_wdly - 1)) {
					one_window_active = 0;
					one_window_count = j - one_window_start;
					if (one_window_count > one_window_best_count) {
						one_window_best_start = one_window_start;
						one_window_best_count = one_window_count;
					}
				}
			} else {
				if (taps_scan[j]) {
					one_window_active = 1;
					one_window_start = j;
				}
			}
		}

		/* rst delay */
		write_delay_rst(i);

		/* use forced delay if configured */
		if (sdrwl_delays[i] >= 0) {
			delays[i] = sdrwl_delays[i];

			/* configure write delay */
			for(j=0; j<delays[i]; j++)
				write_delay_inc(i);
		/* succeed only if the start of a 1s window has been found */
		} else if (one_window_best_count > 0 && one_window_best_start > 0) {
			delays[i] = one_window_best_start;

			/* configure write delay */
			for(j=0; j<delays[i]; j++)
				write_delay_inc(i);
		}
		if (show) {
			if (delays[i] == -1)
				printf(" delay: -\n");
			else
				printf(" delay: %02d\n", delays[i]);
		}
	}

	sdrwloff();

	ok = 1;
	for(i=SDRAM_PHY_MODULES-1;i>=0;i--) {
		if(delays[i] < 0)
			ok = 0;
	}

	return ok;
}

static void write_level_cdly_range(unsigned int *best_error, int *best_cdly,
		int cdly_start, int cdly_stop, int cdly_step)
{
	int cdly;
	int cdly_actual = 0;
	int delays[SDRAM_PHY_MODULES];

	/* scan through the range */
	ddrphy_cdly_rst_write(1);
	for (cdly = cdly_start; cdly < cdly_stop; cdly += cdly_step) {
		/* increment cdly to current value */
		while (cdly_actual < cdly) {
			ddrphy_cdly_inc_write(1);
			cdelay(10);
			cdly_actual++;
		}

		/* write level using this delay */
		if (write_level_scan(delays, 8, 0)) {
			/* use the mean of delays for error calulation */
			int delay_mean = 0;
			for (int i=0; i < SDRAM_PHY_MODULES; ++i) {
				delay_mean += delays[i];
			}
			delay_mean /= SDRAM_PHY_MODULES;

			/* we want it to be at the start */
			int ideal_delay = 4*SDRAM_PHY_DELAYS/32;
			int error = ideal_delay - delay_mean;
			if (error < 0)
				error *= -1;

			if (error < *best_error) {
				*best_cdly = cdly;
				*best_error = error;
			}
			printf("1");
		} else {
			printf("0");
		}
	}
}

int write_level(void)
{
	int delays[SDRAM_PHY_MODULES];
	unsigned int best_error = ~0u;
	int best_cdly = -1;
	int cdly_range_start;
	int cdly_range_end;
	int cdly_range_step;

#ifndef SDRAM_PHY_CMD_DELAY
	printf("  Cmd/Clk scan:\n");

	/* Center write leveling by varying cdly. Searching through all possible
	 * values is slow, but we can use a simple optimization method of iterativly
	 * scanning smaller ranges with decreasing step */
	cdly_range_start = 0;
	cdly_range_end = SDRAM_PHY_DELAYS;
	if (SDRAM_PHY_DELAYS > 32)
		cdly_range_step = SDRAM_PHY_DELAYS/8;
	else
		cdly_range_step = 1;
	while (cdly_range_step > 0) {
		printf("  |");
		write_level_cdly_range(&best_error, &best_cdly,
				cdly_range_start, cdly_range_end, cdly_range_step);

		/* small optimization - stop if we have zero error */
		if (best_error == 0)
			break;

		/* use best result as the middle of next range */
		cdly_range_start = best_cdly - cdly_range_step;
		cdly_range_end = best_cdly + cdly_range_step + 1;
		if (cdly_range_start < 0)
			cdly_range_start = 0;
		if (cdly_range_end > 512)
			cdly_range_end = 512;

		cdly_range_step /= 4;
	}
	printf("| best: %d\n", best_cdly);
#else
	best_cdly = SDRAM_PHY_CMD_DELAY;
#endif
	printf("  Setting Cmd/Clk delay to %d taps.\n", best_cdly);
	/* set working or forced delay */
	if (best_cdly >= 0) {
		ddrphy_cdly_rst_write(1);
		for (int i = 0; i < best_cdly; ++i) {
			ddrphy_cdly_inc_write(1);
			cdelay(10);
		}
	}

	printf("  Data scan:\n");

	/* re-run write leveling the final time */
	if (!write_level_scan(delays, 128, 1))
		return 0;

	return best_cdly >= 0;
}

#endif /*  SDRAM_PHY_WRITE_LEVELING_CAPABLE */

/*-----------------------------------------------------------------------*/
/* Read Leveling                                                         */
/*-----------------------------------------------------------------------*/

static void read_delay_rst(int module) {
	/* sel module */
	ddrphy_dly_sel_write(1 << module);

	/* rst delay */
	ddrphy_rdly_dq_rst_write(1);

	/* unsel module */
	ddrphy_dly_sel_write(0);

#ifdef SDRAM_PHY_ECP5DDRPHY
	/* Sync all DQSBUFM's, By toggling all dly_sel (DQSBUFM.PAUSE) lines. */
	ddrphy_dly_sel_write(0xFF);
	ddrphy_dly_sel_write(0);
#endif
}

static void read_delay_inc(int module) {
	/* sel module */
	ddrphy_dly_sel_write(1 << module);

	/* inc delay */
	ddrphy_rdly_dq_inc_write(1);

	/* unsel module */
	ddrphy_dly_sel_write(0);

#ifdef SDRAM_PHY_ECP5DDRPHY
	/* Sync all DQSBUFM's, By toggling all dly_sel (DQSBUFM.PAUSE) lines. */
	ddrphy_dly_sel_write(0xFF);
	ddrphy_dly_sel_write(0);
#endif
}

static void read_bitslip_rst(char m)
{
	/* sel module */
	ddrphy_dly_sel_write(1 << m);

	/* inc delay */
	ddrphy_rdly_dq_bitslip_rst_write(1);

	/* unsel module */
	ddrphy_dly_sel_write(0);
}


static void read_bitslip_inc(char m)
{
	/* sel module */
	ddrphy_dly_sel_write(1 << m);

	/* inc delay */
	ddrphy_rdly_dq_bitslip_write(1);

	/* unsel module */
	ddrphy_dly_sel_write(0);
}

static int read_level_scan(int module, int bitslip)
{
	unsigned int prv;
	unsigned char prs[SDRAM_PHY_PHASES][DFII_PIX_DATA_BYTES];
	unsigned char tst[DFII_PIX_DATA_BYTES];
	int p, i;
	int score;

	/* Generate pseudo-random sequence */
	prv = 42;
	for(p=0;p<SDRAM_PHY_PHASES;p++)
		for(i=0;i<DFII_PIX_DATA_BYTES;i++) {
			prv = lfsr(32, prv);
			prs[p][i] = prv;
		}

	/* Activate */
	sdram_dfii_pi0_address_write(0);
	sdram_dfii_pi0_baddress_write(0);
	command_p0(DFII_COMMAND_RAS|DFII_COMMAND_CS);
	cdelay(15);

	/* Write test pattern */
	for(p=0;p<SDRAM_PHY_PHASES;p++)
		csr_wr_buf_uint8(sdram_dfii_pix_wrdata_addr[p],
				 prs[p], DFII_PIX_DATA_BYTES);
	sdram_dfii_piwr_address_write(0);
	sdram_dfii_piwr_baddress_write(0);
	command_pwr(DFII_COMMAND_CAS|DFII_COMMAND_WE|DFII_COMMAND_CS|DFII_COMMAND_WRDATA);

	/* Calibrate each DQ in turn */
	sdram_dfii_pird_address_write(0);
	sdram_dfii_pird_baddress_write(0);
	score = 0;

	printf("  m%d, b%d: |", module, bitslip);
	read_delay_rst(module);
	for(i=0;i<SDRAM_PHY_DELAYS;i++) {
		int working = 1;
		int show = 1;
#if SDRAM_PHY_DELAYS > 32
		show = (i%16 == 0);
#endif
#ifdef SDRAM_PHY_ECP5DDRPHY
		ddrphy_burstdet_clr_write(1);
#endif
		command_prd(DFII_COMMAND_CAS|DFII_COMMAND_CS|DFII_COMMAND_RDDATA);
		cdelay(15);
		for(p=0;p<SDRAM_PHY_PHASES;p++) {
			/* read back test pattern */
			csr_rd_buf_uint8(sdram_dfii_pix_rddata_addr[p],
					 tst, DFII_PIX_DATA_BYTES);
			/* verify bytes matching current 'module' */
			if (prs[p][  SDRAM_PHY_MODULES-1-module] != tst[  SDRAM_PHY_MODULES-1-module] ||
			    prs[p][2*SDRAM_PHY_MODULES-1-module] != tst[2*SDRAM_PHY_MODULES-1-module])
				working = 0;
		}
#ifdef SDRAM_PHY_ECP5DDRPHY
		if (((ddrphy_burstdet_seen_read() >> module) & 0x1) != 1)
			working = 0;
#endif
		if (show)
			printf("%d", working);
		score += working;
		read_delay_inc(module);
	}
	printf("| ");

	/* Precharge */
	sdram_dfii_pi0_address_write(0);
	sdram_dfii_pi0_baddress_write(0);
	command_p0(DFII_COMMAND_RAS|DFII_COMMAND_WE|DFII_COMMAND_CS);
	cdelay(15);

	return score;
}

void read_level(int module)
{
	unsigned int prv;
	unsigned char prs[SDRAM_PHY_PHASES][DFII_PIX_DATA_BYTES];
	unsigned char tst[DFII_PIX_DATA_BYTES];
	int p, i;
	int working;
	int delay, delay_min, delay_max;

	printf("delays: ");

	/* Generate pseudo-random sequence */
	prv = 42;
	for(p=0;p<SDRAM_PHY_PHASES;p++)
		for(i=0;i<DFII_PIX_DATA_BYTES;i++) {
			prv = lfsr(32, prv);
			prs[p][i] = prv;
		}

	/* Activate */
	sdram_dfii_pi0_address_write(0);
	sdram_dfii_pi0_baddress_write(0);
	command_p0(DFII_COMMAND_RAS|DFII_COMMAND_CS);
	cdelay(15);

	/* Write test pattern */
	for(p=0;p<SDRAM_PHY_PHASES;p++)
		csr_wr_buf_uint8(sdram_dfii_pix_wrdata_addr[p],
				 prs[p], DFII_PIX_DATA_BYTES);
	sdram_dfii_piwr_address_write(0);
	sdram_dfii_piwr_baddress_write(0);
	command_pwr(DFII_COMMAND_CAS|DFII_COMMAND_WE|DFII_COMMAND_CS|DFII_COMMAND_WRDATA);

	/* Calibrate each DQ in turn */
	sdram_dfii_pird_address_write(0);
	sdram_dfii_pird_baddress_write(0);

	/* Find smallest working delay */
	delay = 0;
	read_delay_rst(module);
	while(1) {
#ifdef SDRAM_PHY_ECP5DDRPHY
		ddrphy_burstdet_clr_write(1);
#endif
		command_prd(DFII_COMMAND_CAS|DFII_COMMAND_CS|DFII_COMMAND_RDDATA);
		cdelay(15);
		working = 1;
		for(p=0;p<SDRAM_PHY_PHASES;p++) {
			/* read back test pattern */
			csr_rd_buf_uint8(sdram_dfii_pix_rddata_addr[p],
					 tst, DFII_PIX_DATA_BYTES);
			/* verify bytes matching current 'module' */
			if (prs[p][  SDRAM_PHY_MODULES-1-module] != tst[  SDRAM_PHY_MODULES-1-module] ||
			    prs[p][2*SDRAM_PHY_MODULES-1-module] != tst[2*SDRAM_PHY_MODULES-1-module])
				working = 0;
		}
#ifdef SDRAM_PHY_ECP5DDRPHY
		if (((ddrphy_burstdet_seen_read() >> module) & 0x1) != 1)
			working = 0;
#endif
		if(working)
			break;
		delay++;
		if(delay >= SDRAM_PHY_DELAYS)
			break;
		read_delay_inc(module);
	}
	delay_min = delay;

	/* Get a bit further into the working zone */
#if SDRAM_PHY_DELAYS > 32
	for(i=0;i<16;i++) {
		delay += 1;
		read_delay_inc(module);
	}
#else
	delay++;
	read_delay_inc(module);
#endif

	/* Find largest working delay */
	while(1) {
#ifdef SDRAM_PHY_ECP5DDRPHY
		ddrphy_burstdet_clr_write(1);
#endif
		command_prd(DFII_COMMAND_CAS|DFII_COMMAND_CS|DFII_COMMAND_RDDATA);
		cdelay(15);
		working = 1;
		for(p=0;p<SDRAM_PHY_PHASES;p++) {
			/* read back test pattern */
			csr_rd_buf_uint8(sdram_dfii_pix_rddata_addr[p],
					 tst, DFII_PIX_DATA_BYTES);
			/* verify bytes matching current 'module' */
			if (prs[p][  SDRAM_PHY_MODULES-1-module] != tst[  SDRAM_PHY_MODULES-1-module] ||
			    prs[p][2*SDRAM_PHY_MODULES-1-module] != tst[2*SDRAM_PHY_MODULES-1-module])
				working = 0;
		}
#ifdef SDRAM_PHY_ECP5DDRPHY
		if (((ddrphy_burstdet_seen_read() >> module) & 0x1) != 1)
			working = 0;
#endif
		if(!working)
			break;
		delay++;
		if(delay >= SDRAM_PHY_DELAYS)
			break;
		read_delay_inc(module);
	}
	delay_max = delay;

	if (delay_min >= SDRAM_PHY_DELAYS)
		printf("-");
	else
		printf("%02d+-%02d", (delay_min+delay_max)/2, (delay_max-delay_min)/2);

	/* Set delay to the middle */
	read_delay_rst(module);
	for(i=0;i<(delay_min+delay_max)/2;i++)
		read_delay_inc(module);

	/* Precharge */
	sdram_dfii_pi0_address_write(0);
	sdram_dfii_pi0_baddress_write(0);
	command_p0(DFII_COMMAND_RAS|DFII_COMMAND_WE|DFII_COMMAND_CS);
	cdelay(15);
}
#endif /* CSR_DDRPHY_BASE */

#endif /* CSR_SDRAM_BASE */

#ifdef CSR_SDRAM_BASE

#if defined(SDRAM_PHY_WRITE_LEVELING_CAPABLE) || defined(SDRAM_PHY_READ_LEVELING_CAPABLE)

static void read_leveling(void)
{
	int module;
	int bitslip;
	int score;
	int best_score;
	int best_bitslip;

	for(module=0; module<SDRAM_PHY_MODULES; module++) {
		/* scan possible read windows */
		best_score = 0;
		best_bitslip = 0;
		for(bitslip=0; bitslip<SDRAM_PHY_BITSLIPS; bitslip++) {
			/* compute score */
			score = read_level_scan(module, bitslip);
			read_level(module);
			printf("\n");
			if (score > best_score) {
				best_bitslip = bitslip;
				best_score = score;
			}
			/* exit */
			if (bitslip == SDRAM_PHY_BITSLIPS-1)
				break;
			/* increment bitslip */
			read_bitslip_inc(module);
		}

		/* select best read window */
		printf("  best: m%d, b%02d ", module, best_bitslip);
		read_bitslip_rst(module);
		for (bitslip=0; bitslip<best_bitslip; bitslip++)
			read_bitslip_inc(module);

		/* re-do leveling on best read window*/
		read_level(module);
		printf("\n");
	}
}

/*-----------------------------------------------------------------------*/
/* Leveling                                                              */
/*-----------------------------------------------------------------------*/

int _write_level_cdly_scan = 1;

int sdrlevel(void)
{
	int module;
	sdrsw();

	for(module=0; module<SDRAM_PHY_MODULES; module++) {
#ifdef SDRAM_PHY_WRITE_LEVELING_CAPABLE
		write_delay_rst(module);
#endif
		read_delay_rst(module);
		read_bitslip_rst(module);
	}

#ifdef SDRAM_PHY_WRITE_LEVELING_CAPABLE
	printf("Write leveling:\n");
	if (_write_level_cdly_scan) {
		write_level();
	} else {
		/* use only the current cdly */
		int delays[SDRAM_PHY_MODULES];
		write_level_scan(delays, 128, 1);
	}
#endif

#ifdef SDRAM_PHY_READ_LEVELING_CAPABLE
	printf("Read leveling:\n");
	read_leveling();
#endif

	return 1;
}
#endif

/*-----------------------------------------------------------------------*/
/* Calibration                                                           */
/*-----------------------------------------------------------------------*/

void sdrcal(void)
{
#ifdef CSR_DDRPHY_BASE
#if CSR_DDRPHY_EN_VTC_ADDR
	ddrphy_en_vtc_write(0);
#endif
#if defined(SDRAM_PHY_WRITE_LEVELING_CAPABLE) || defined(SDRAM_PHY_READ_LEVELING_CAPABLE)
	sdrlevel();
#endif
#if CSR_DDRPHY_EN_VTC_ADDR
	ddrphy_en_vtc_write(1);
#endif
#endif
	sdrhw();
}

/*-----------------------------------------------------------------------*/
/* Initialization                                                        */
/*-----------------------------------------------------------------------*/

int sdrinit(void)
{
#ifdef SDRAM_PHY_WRITE_LEVELING_CAPABLE
	int i;
	for (i=0; i<16; i++) sdrwl_delays[i] = -1; /* disable forced delays */
#endif
	printf("Initializing SDRAM @0x%08x...\n", MAIN_RAM_BASE);

#if CSR_DDRPHY_RST_ADDR
	ddrphy_rst_write(1);
	cdelay(1000);
	ddrphy_rst_write(0);
	cdelay(1000);
#endif

#ifdef CSR_DDRCTRL_BASE
	ddrctrl_init_done_write(0);
	ddrctrl_init_error_write(0);
#endif
	sdrsw();
	init_sequence();
	sdrcal();
	if(!memtest((unsigned int *) MAIN_RAM_BASE, MAIN_RAM_SIZE)) {
#ifdef CSR_DDRCTRL_BASE
		ddrctrl_init_done_write(1);
		ddrctrl_init_error_write(1);
#endif
		return 0;
	}
#ifdef CSR_DDRCTRL_BASE
	ddrctrl_init_done_write(1);
#endif

	return 1;
}

/*-----------------------------------------------------------------------*/
/* MPR access                                                            */
/*-----------------------------------------------------------------------*/

#ifdef SDRAM_PHY_MODULES

#define MPR0_SEL (0 << 0)
#define MPR1_SEL (1 << 0)
#define MPR2_SEL (2 << 0)
#define MPR3_SEL (3 << 0)

#define MPR_ENABLE (1 << 2)

#define MPR_READ_SERIAL    (0 << 11)
#define MPR_READ_PARALLEL  (1 << 11)
#define MPR_READ_STAGGERED (2 << 11)

void sdrmrwr(char reg, int value) {
	sdram_dfii_pi0_address_write(value);
	sdram_dfii_pi0_baddress_write(reg);
	command_p0(DFII_COMMAND_RAS|DFII_COMMAND_CAS|DFII_COMMAND_WE|DFII_COMMAND_CS);
}

void sdrmpron(char mpr)
{
	sdrmrwr(3, MPR_READ_SERIAL | MPR_ENABLE | mpr);
}

void sdrmproff(void)
{
	sdrmrwr(3, 0);
}

void sdrmpr(void)
{
	int module, phase;
	printf("Read SDRAM MPR...\n");

	/* rst phy */
	for(module=0; module<SDRAM_PHY_MODULES; module++) {
#ifdef SDRAM_PHY_WRITE_LEVELING_CAPABLE
		write_delay_rst(module);
#endif
		read_delay_rst(module);
		read_bitslip_rst(module);
	}

	/* software control */
	sdrsw();

	printf("Reads with MPR0 (0b01010101) enabled...\n");
	sdrmpron(MPR0_SEL);
	command_prd(DFII_COMMAND_CAS|DFII_COMMAND_CS|DFII_COMMAND_RDDATA);
	cdelay(15);
	for (module=0; module < SDRAM_PHY_MODULES; module++) {
		printf("m%d: ", module);
		for(phase=0; phase<SDRAM_PHY_PHASES; phase++) {
			printf("%d", MMPTR(sdram_dfii_pix_rddata_addr[phase]+4*(SDRAM_PHY_MODULES-module-1)) & 0x1);
			printf("%d", MMPTR(sdram_dfii_pix_rddata_addr[phase]+4*(2*SDRAM_PHY_MODULES-module-1)) & 0x1);
		}
		printf("\n");
	}
	sdrmproff();

	/* hardware control */
	sdrhw();
}

#endif

#endif
