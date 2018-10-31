#include <stdio.h>
#include <stdlib.h>
#include <sigutils/sigutils.h>
#include <sigutils/sampling.h>
#include <sigutils/ncqo.h>

#define INPUT_FILE   "ntsc.raw"
#define OUTPUT_FILE  "demod.raw"
#define SAMP_RATE    20e6
#define FREQ_OFFSET  788e3 /* How far is the desired center frequency */

int
main(void)
{
  FILE *fp, *ofp;
  su_ncqo_t nco;
  SUSCOUNT samples = 0;
  SUCOMPLEX x, y;
  
  if ((fp = fopen(INPUT_FILE, "rb")) == NULL) {
    fprintf(stderr, "error: cannot open %s\n", INPUT_FILE);
    exit(EXIT_FAILURE);
  }

  if ((ofp = fopen(OUTPUT_FILE, "wb")) == NULL) {
    fprintf(stderr, "error: cannot open %s for writing\n", OUTPUT_FILE);
    exit(EXIT_FAILURE);
  }

  su_ncqo_init(&nco, SU_ABS2NORM_FREQ(SAMP_RATE, FREQ_OFFSET));
  
  while (fread(&x, sizeof(SUCOMPLEX), 1, fp) == 1) {
    y = x * su_ncqo_read(&nco); /* Take sample from oscillator and mix */
    fwrite(&y, sizeof(SUCOMPLEX), 1, ofp);
    ++samples;
  }

  printf(
    "%d samples read (%g seconds of raw capture)\n",
    samples,
    samples / SAMP_RATE);

  return 0;
}
