#ifndef __SDRAM_H
#define __SDRAM_H

#include <generated/csr.h>

/*-----------------------------------------------------------------------*/
/* Constants                                                             */
/*-----------------------------------------------------------------------*/

int sdrdatabits(void);
int sdrfreq(void);

/*-----------------------------------------------------------------------*/
/* Software/Hardware Control                                             */
/*-----------------------------------------------------------------------*/

void sdrsw(void);
void sdrhw(void);

/*-----------------------------------------------------------------------*/
/* Write Leveling                                                        */
/*-----------------------------------------------------------------------*/
int write_level(void);

/*-----------------------------------------------------------------------*/
/* Read Leveling                                                         */
/*-----------------------------------------------------------------------*/

void read_level(int module);

/*-----------------------------------------------------------------------*/
/* Leveling                                                              */
/*-----------------------------------------------------------------------*/

int sdrlevel(void);

/*-----------------------------------------------------------------------*/
/* Calibration                                                           */
/*-----------------------------------------------------------------------*/

void sdrcal(void);

/*-----------------------------------------------------------------------*/
/* Initialization                                                        */
/*-----------------------------------------------------------------------*/

int sdrinit(void);

/*-----------------------------------------------------------------------*/
/* MPR access                                                            */
/*-----------------------------------------------------------------------*/

void sdrmrwr(char reg, int value);
void sdrmpron(char mpr);
void sdrmproff(void);
void sdrmpr(void);

#endif /* __SDRAM_H */
