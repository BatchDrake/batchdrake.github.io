#include <stdio.h>
#include <stdlib.h>
#include <sigutils/sigutils.h>
#include <sigutils/sampling.h>
#include <sigutils/ncqo.h>
#include <sigutils/iir.h>
#include <sigutils/agc.h>

#define INPUT_FILE    "ntsc.raw"
#define OUTPUT_FILE   "demod.raw"
#define SAMP_RATE     20e6
#define FREQ_OFFSET   788e3  /* How far is the desired center frequency */
#define MID_FREQUENCY 1.91e6 /* Where is the spectrum center */ 
#define BANDWIDTH     11.6e6 /* Spectrum center */

#define HSYNC_FREQ      15734.0 /* NTSC line frequency */
#define LINES_PER_FRAME 525     /* Number of lines per NTSC frame */

int
main(void)
{
  FILE *fp, *ofp;
  su_ncqo_t nco1, nco2;
  su_iir_filt_t lpf;
  struct su_agc_params agc_params = su_agc_params_INITIALIZER;
  su_agc_t agc;
  SUSCOUNT samples = 0;
  SUCOMPLEX x, y, prev;
  
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

  agc_params.mag_history_size = SAMP_RATE / HSYNC_FREQ;
  
  agc_params.delay_line_size  =  agc_params.mag_history_size;
  agc_params.fast_rise_t = 0.2 * agc_params.mag_history_size;
  agc_params.fast_fall_t = 0.5 * agc_params.mag_history_size;
  agc_params.slow_rise_t = 1.0 * agc_params.mag_history_size;
  agc_params.slow_fall_t = 1.0 * agc_params.mag_history_size;
  agc_params.hang_max    = LINES_PER_FRAME * agc_params.mag_history_size;

  if (!su_agc_init(&agc, &agc_params)) {
    fprintf(stderr, "error: failed to initialize AGC\n");
    exit(EXIT_FAILURE);
  }
  
  prev = 0;
  while (fread(&x, sizeof(SUCOMPLEX), 1, fp) == 1) {
    x *= su_ncqo_read(&nco1);      /* Center spectrum */
    x = su_iir_filt_feed(&lpf, x); /* Filter centered signal */
    x *= su_ncqo_read(&nco2);      /* Center signal around the black level peak */
    y = carg(x * conj(prev));      /* Perform FM detection */
    prev = x;                      /* Save previous sample */
    y = su_agc_feed(&agc, y);      /* Stabilize amplitude */
    fwrite(&y, sizeof(SUCOMPLEX), 1, ofp);
    ++samples;
  }

  printf(
    "%d samples read (%g seconds of raw capture)\n",
    samples,
    samples / SAMP_RATE);

  return 0;
}
