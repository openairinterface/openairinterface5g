/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "SIMULATION/TOOLS/sim.h"
#include "PHY/TOOLS/tools_defs.h"
#include "assertions.h"

// NEW code with lookup table for sin/cos based on delay profile (TO BE TESTED)

double **cos_lut=NULL,* *sin_lut=NULL;
static int freq_channel_init = 0;
static int n_samples_max = 0;


//#if 1

void term_freq_channel(void)
{
  for (int f = -(n_samples_max >> 1); f <= n_samples_max >> 1; f++) {
    const int idx = f + (n_samples_max >> 1);
    if (cos_lut[idx])
      free_and_zero(cos_lut[idx]);
    if (sin_lut[idx])
      free_and_zero(sin_lut[idx]);
  }
  free_and_zero(cos_lut);
  free_and_zero(sin_lut);
  freq_channel_init = 0;
  n_samples_max = 0;
}

int init_freq_channel(channel_desc_t *desc,uint16_t nb_rb,int16_t n_samples,int scs) {
  double delta_f,freq;  // 90 kHz spacing
  double delay;
  int16_t f;
  uint8_t l;

  if ((n_samples&1)==0) {
    fprintf(stderr, "freq_channel_init: n_samples has to be odd\n");
    return(-1);
  }

  cos_lut = (double **)malloc(n_samples*sizeof(double *));
  sin_lut = (double **)malloc(n_samples*sizeof(double *));
  delta_f = nb_rb*12*scs*1000/(n_samples-1);

  for (f=-(n_samples>>1); f<=(n_samples>>1); f++) {
    freq=delta_f*(double)f*1e-6;// due to the fact that delays is in mus
    cos_lut[f+(n_samples>>1)] = (double *)malloc((int)desc->nb_taps*sizeof(double));
    sin_lut[f+(n_samples>>1)] = (double *)malloc((int)desc->nb_taps*sizeof(double));

    for (l=0; l<(int)desc->nb_taps; l++) {
      if (desc->nb_taps==1)
        delay = desc->delays[l];
      else
        delay = desc->delays[l]+NB_SAMPLES_CHANNEL_OFFSET/desc->sampling_rate;

      cos_lut[f+(n_samples>>1)][l] = cos(2*M_PI*freq*delay);
      sin_lut[f+(n_samples>>1)][l] = sin(2*M_PI*freq*delay);
      //      printf("values cos:%f, sin:%f\n", cos_lut[f+(n_samples>>1)][l], sin_lut[f+(n_samples>>1)][l]);
    }
  }

  return(0);
}

int freq_channel(channel_desc_t *desc,uint16_t nb_rb,int16_t n_samples,int scs) {
  int16_t f,f2,d;
  uint8_t aarx,aatx,l;
  double *clut,*slut;

  // do some error checking
  // n_samples has to be a odd number because we assume the spectrum is symmetric around the DC and includes the DC
  if ((n_samples&1)==0) {
    fprintf(stderr, "freq_channel: n_samples has to be odd\n");
    return(-1);
  }

  // printf("no of taps:%d,",(int)desc->nb_taps);

  if (freq_channel_init == 0) {
    // we are initializing the lut for the largets possible n_samples=12*nb_rb+1
    // if called with n_samples<12*nb_rb+1, we decimate the lut
    n_samples_max=12*nb_rb+1;

    if (init_freq_channel(desc,nb_rb,n_samples_max,scs)==0)
      freq_channel_init=1;
    else
      return(-1);
  }

  d=(n_samples_max-1)/(n_samples-1);
  //  printf("no_samples=%d, n_samples_max=%d, d=%d,nb_taps %d\n",n_samples,n_samples_max,d,desc->nb_taps);
  start_meas(&desc->interp_freq);
  allocCast3D(chF, struct complexd, desc->chF, desc->nb_tx, desc->nb_rx, desc->channelF_len, false);
  allocCast3D(a, struct complexd, desc->a, desc->nb_taps, desc->nb_tx, desc->nb_rx, true);

  for (f=-n_samples_max/2,f2=-n_samples/2; f<n_samples_max/2; f+=d,f2++) {
    clut = cos_lut[n_samples_max/2+f];
    slut = sin_lut[n_samples_max/2+f];

    for (aarx=0; aarx<desc->nb_rx; aarx++) {
      for (aatx=0; aatx<desc->nb_tx; aatx++) {
        AssertFatal(n_samples / 2 + f2 < desc->channelF_len,
                    "reading past chF %d (n_samples %d, f2 %d)\n",
                    n_samples / 2 + f2,
                    n_samples,
                    f2);
        chF[aatx][aarx][n_samples / 2 + f2] = (struct complexd){};
        for (l=0; l<(int)desc->nb_taps; l++) {
          chF[aatx][aarx][n_samples / 2 + f2].r += a[l][aatx][aarx].r * clut[l] + a[l][aatx][aarx].i * slut[l];
          chF[aatx][aarx][n_samples / 2 + f2].i += -a[l][aatx][aarx].r * slut[l] + a[l][aatx][aarx].i * clut[l];
        }
      }
    }
  }

  stop_meas(&desc->interp_freq);
  return(0);
}

/* Frequency response of an optional interferer (NULL when absent), with the dimensions of the useful link */
static struct complexd *interferer_chF(const channel_desc_t *desc, channel_desc_t *desc_i)
{
  if (!desc_i)
    return NULL;
  AssertFatal(desc_i->nb_tx == desc->nb_tx && desc_i->nb_rx == desc->nb_rx && desc_i->channelF_len == desc->channelF_len,
              "interferer channel %dx%d (%d) differs from useful channel %dx%d (%d)\n",
              desc_i->nb_tx,
              desc_i->nb_rx,
              desc_i->channelF_len,
              desc->nb_tx,
              desc->nb_rx,
              desc->channelF_len);
  allocCast3D(chF, struct complexd, desc_i->chF, desc_i->nb_tx, desc_i->nb_rx, desc_i->channelF_len, false);
  return &chF[0][0][0];
}

double compute_pbch_sinr(channel_desc_t *desc,
                         channel_desc_t *desc_i1,
                         channel_desc_t *desc_i2,
                         double snr_dB,double snr_i1_dB,
                         double snr_i2_dB,
                         uint16_t nb_rb) {
  double avg_sinr,snr=pow(10.0,.1*snr_dB),snr_i1=pow(10.0,.1*snr_i1_dB),snr_i2=pow(10.0,.1*snr_i2_dB);
  uint16_t f;
  uint8_t aarx,aatx;
  double S;
  struct complexd S_i1;
  struct complexd S_i2;
  avg_sinr=0.0;

  allocCast3D(chF, struct complexd, desc->chF, desc->nb_tx, desc->nb_rx, desc->channelF_len, false);
  struct complexd(*chF1)[desc->nb_rx][desc->channelF_len] = (void *)interferer_chF(desc, desc_i1);
  struct complexd(*chF2)[desc->nb_rx][desc->channelF_len] = (void *)interferer_chF(desc, desc_i2);

  //  printf("nb_rb %d\n",nb_rb);
  for (f=(nb_rb-6); f<(nb_rb+6); f++) {
    S = 0.0;
    S_i1.r =0.0;
    S_i1.i =0.0;
    S_i2.r =0.0;
    S_i2.i = 0.0;
    for (aarx=0; aarx<desc->nb_rx; aarx++) {
      for (aatx=0; aatx<desc->nb_tx; aatx++) {
        S += squaredMod(chF[aatx][aarx][f]);
        //  printf("%d %d chF[%d] => (%f,%f)\n",aarx,aatx,f,chF[aatx][aarx][f].x,chF[aatx][aarx][f].y);

        if (desc_i1) {
          S_i1.r += chF[aatx][aarx][f].r * chF1[aatx][aarx][f].r + chF[aatx][aarx][f].i * chF1[aatx][aarx][f].i;
          S_i1.i += chF[aatx][aarx][f].r * chF1[aatx][aarx][f].i - chF[aatx][aarx][f].i * chF1[aatx][aarx][f].r;
        }

        if (desc_i2) {
          S_i2.r += chF[aatx][aarx][f].r * chF2[aatx][aarx][f].r + chF[aatx][aarx][f].i * chF2[aatx][aarx][f].i;
          S_i2.r += chF[aatx][aarx][f].r * chF2[aatx][aarx][f].i - chF[aatx][aarx][f].i * chF2[aatx][aarx][f].r;
        }
      }
    }

    //    printf("snr %f f %d : S %f, S_i1 %f, S_i2 %f\n",snr,f-nb_rb,S,snr_i1*sqrt(S_i1.x*S_i1.x + S_i1.y*S_i1.y),snr_i2*sqrt(S_i2.x*S_i2.x + S_i2.y*S_i2.y));
    avg_sinr += (snr*S/(desc->nb_tx+snr_i1*sqrt(S_i1.r*S_i1.r + S_i1.i*S_i1.i)+snr_i2*sqrt(S_i2.r*S_i2.r + S_i2.i*S_i2.i)));
  }

  //  printf("avg_sinr %f (%f,%f,%f)\n",avg_sinr/12.0,snr,snr_i1,snr_i2);
  return(10*log10(avg_sinr/12.0));
}


double compute_sinr(channel_desc_t *desc,
                    channel_desc_t *desc_i1,
                    channel_desc_t *desc_i2,
                    double snr_dB,double snr_i1_dB,
                    double snr_i2_dB,
                    uint16_t nb_rb) {
  double avg_sinr,snr=pow(10.0,.1*snr_dB),snr_i1=pow(10.0,.1*snr_i1_dB),snr_i2=pow(10.0,.1*snr_i2_dB);
  uint f;
  uint aarx, aatx;
  double S;
  struct complexd S_i1;
  struct complexd S_i2;
  DevAssert( nb_rb > 0 );
  avg_sinr=0.0;
  allocCast3D(chF, struct complexd, desc->chF, desc->nb_tx, desc->nb_rx, desc->channelF_len, false);
  struct complexd(*chF1)[desc->nb_rx][desc->channelF_len] = (void *)interferer_chF(desc, desc_i1);
  struct complexd(*chF2)[desc->nb_rx][desc->channelF_len] = (void *)interferer_chF(desc, desc_i2);
  //  printf("nb_rb %d\n",nb_rb);
  for (f=0; f<2*nb_rb; f++) {
    S = 0.0;
    S_i1.r =0.0;
    S_i1.i =0.0;
    S_i2.r =0.0;
    S_i2.i =0.0;

    for (aarx=0; aarx<desc->nb_rx; aarx++) {
      for (aatx=0; aatx<desc->nb_tx; aatx++) {
        S += (chF[aatx][aarx][f].r * chF[aatx][aarx][f].r + chF[aatx][aarx][f].i * chF[aatx][aarx][f].i);

        if (desc_i1) {
          S_i1.r += (chF[aatx][aarx][f].r * chF1[aatx][aarx][f].r + chF[aatx][aarx][f].i * chF1[aatx][aarx][f].i);
          S_i1.i += (chF[aatx][aarx][f].r * chF1[aatx][aarx][f].i - chF[aatx][aarx][f].i * chF1[aatx][aarx][f].r);
        }

        if (desc_i2) {
          S_i2.r += (chF[aatx][aarx][f].r * chF2[aatx][aarx][f].r + chF[aatx][aarx][f].i * chF2[aatx][aarx][f].i);
          S_i2.i += (chF[aatx][aarx][f].r * chF2[aatx][aarx][f].i - chF[aatx][aarx][f].i * chF2[aatx][aarx][f].r);
        }
      }
    }

    //        printf("f %d : S %f, S_i1 %f, S_i2 %f\n",f-nb_rb,snr*S,snr_i1*sqrt(S_i1.x*S_i1.x + S_i1.y*S_i1.y),snr_i2*sqrt(S_i2.x*S_i2.x + S_i2.y*S_i2.y));
    avg_sinr += (snr*S/(desc->nb_tx+snr_i1*sqrt(S_i1.r*S_i1.r + S_i1.i*S_i1.i)+snr_i2*sqrt(S_i2.r*S_i2.r + S_i2.i*S_i2.i)));
  }

  //  printf("avg_sinr %f (%f,%f,%f)\n",avg_sinr/12.0,snr,snr_i1,snr_i2);
  return(10*log10(avg_sinr/(nb_rb*2)));
}

int pbch_polynomial_degree=6;
double pbch_awgn_polynomial[7]= {-7.2926e-05, -2.8749e-03, -4.5064e-02, -3.5301e-01, -1.4655e+00, -3.6282e+00, -6.6907e+00};

void load_pbch_desc(FILE *pbch_file_fd) {
  int i, ret;
  char dummy[25];
  ret = fscanf(pbch_file_fd,"%d",&pbch_polynomial_degree);

  if (ret < 0) {
    printf("fscanf failed: %s\n", strerror(errno));
    exit(EXIT_FAILURE);
  }

  if (pbch_polynomial_degree>6) {
    printf("Illegal degree for pbch interpolation polynomial %d\n",pbch_polynomial_degree);
    exit(-1);
  }

  printf("PBCH polynomial : ");

  for (i=0; i<=pbch_polynomial_degree; i++) {
    ret = fscanf(pbch_file_fd,"%24s",dummy);

    if (ret < 0) {
      printf("fscanf failed: %s\n", strerror(errno));
      exit(EXIT_FAILURE);
    }

    pbch_awgn_polynomial[i] = strtod(dummy,NULL);
    printf("%f ",pbch_awgn_polynomial[i]);
  }

  printf("\n");
}

double pbch_bler(double sinr) {
  int i;
  double log10_bler=pbch_awgn_polynomial[pbch_polynomial_degree];
  double sinrpow=sinr;
  double bler=0.0;

  //  printf("log10bler %f\n",log10_bler);
  if (sinr<-10.0)
    bler= 1.0;
  else if (sinr>=0.0)
    bler=0.0;
  else  {
    for (i=1; i<=pbch_polynomial_degree; i++) {
      //    printf("sinrpow %f\n",sinrpow);
      log10_bler += (pbch_awgn_polynomial[pbch_polynomial_degree-i]*sinrpow);
      sinrpow *= sinr;
      //    printf("log10bler %f\n",log10_bler);
    }

    bler = pow(10.0,log10_bler);
  }

  //printf ("sinr %f bler %f\n",sinr,bler);
  return(bler);
}

