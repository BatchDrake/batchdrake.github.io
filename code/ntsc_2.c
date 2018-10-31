#include <stdio.h>
#include <stdlib.h>
#include <sigutils/sigutils.h>
#include <sigutils/sampling.h>
#include <sigutils/ncqo.h>
#include <sigutils/iir.h>

#define INPUT_FILE    "ntsc.raw"
#define OUTPUT_FILE   "demod.raw"
#define SAMP_RATE     20e6
#define FREQ_OFFSET   788e3  /* How far is the desired center frequency */
#define MID_FREQUENCY 1.91e6 /* Where is the spectrum center */ 
#define BANDWIDTH     11.6e6 /* Spectrum center */

int
main(void)
{
  FILE *fp, *ofp;
  su_ncqo_t nco1, nco2;
  su_iir_filt_t lpf;
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

  su_ncqo_init(&nco1, SU_ABS2NORM_FREQ(SAMP_RATE, FREQ_OFFSET - MID_FREQUENCY));
  su_ncqo_init(&nco2, SU_ABS2NORM_FREQ(SAMP_RATE, MID_FREQUENCY));

  if (!su_iir_bwlpf_init(&lpf, 5, SU_ABS2NORM_FREQ(SAMP_RATE, BANDWIDTH) / 2)) {
    fprintf(stderr, "error: cannot initialize lowpass filter\n");
    exit(EXIT_FAILURE);
  }
  
  while (fread(&x, sizeof(SUCOMPLEX), 1, fp) == 1) {
    y = x * su_ncqo_read(&nco1); /* Center spectrum */
    y = su_iir_filt_feed(&lpf, y); /* Filter centered signal */
    y = y * su_ncqo_read(&nco2); /* Center signal around the black level peak */
    fwrite(&y, sizeof(SUCOMPLEX), 1, ofp);
    ++samples;
  }

  printf(
    "%d samples read (%g seconds of raw capture)\n",
    samples,
    samples / SAMP_RATE);

  return 0;
}
