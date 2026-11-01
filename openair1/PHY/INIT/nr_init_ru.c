/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include "PHY/phy_extern.h"
#include "assertions.h"
#include <math.h>
#include "openair1/PHY/defs_RU.h"
#include "openair1/PHY/defs_nr_common.h"
#include "openair1/PHY/defs_gNB.h"

void nr_phy_init_RU(RU_t *ru)
{
  NR_DL_FRAME_PARMS *fp = ru->nr_frame_parms;

  LOG_D(PHY, "Initializing RU signal buffers (if_south %s) nb_tx %d, nb_rx %d\n", ru_if_types[ru->if_south], ru->nb_tx, ru->nb_rx);

  // copy configuration from gNB[0] in to RU, assume that all gNB instances sharing RU use the same configuration
  // (at least the parts that are needed by the RU, numerology and PRACH)

  int nb_tx_streams = ru->nb_tx;
  int nb_rx_streams = ru->nb_rx;
  LOG_I(NR_PHY, "nb_tx_streams %d, nb_rx_streams %d\n", nb_tx_streams, nb_rx_streams);

  if ((nb_tx_streams > fp->nb_antennas_tx) || (nb_rx_streams > fp->nb_antennas_rx))
    LOG_W(NR_PHY, "There could be unused baseband ports because of fewer logical ports.\n");

  if (ru->if_south <= REMOTE_IF5) { // this means REMOTE_IF5 or LOCAL_RF, so allocate memory for time-domain signals 
    // Time-domain signals
    ru->common.txdata = (int32_t**)malloc16(nb_tx_streams * sizeof(int32_t*));
    ru->common.rxdata = (int32_t**)malloc16(nb_rx_streams * sizeof(int32_t*));

    for (int i = 0; i < nb_tx_streams; i++) {
      // Allocate 10 subframes of I/Q TX signal data (time) if not
      ru->common.txdata[i] = (int32_t*)malloc16_clear((ru->sf_extension + fp->samples_per_frame) * sizeof(int32_t));
      LOG_D(PHY,
            "[INIT] common.txdata[%d] = %p (%lu bytes,sf_extension %d)\n",
            i,
            ru->common.txdata[i],
            (ru->sf_extension + fp->samples_per_frame) * sizeof(int32_t),
            ru->sf_extension);
      ru->common.txdata[i] = &ru->common.txdata[i][ru->sf_extension];

      LOG_D(PHY, "[INIT] common.txdata[%d] = %p \n", i, ru->common.txdata[i]);
    }
    for (int i = 0; i < nb_rx_streams; i++) {
      ru->common.rxdata[i] = (int32_t*)malloc16_clear(fp->samples_per_frame * sizeof(int32_t));
    }
  } // IF5 or local RF
  else {
    ru->common.txdata = (int32_t**)NULL;
    ru->common.rxdata = (int32_t**)NULL;
  }
  if (ru->function != NGFI_RRU_IF5) { // we need to do RX/TX RU processing
    LOG_D(PHY, "nb_tx %d\n", ru->nb_tx);
    // allocate IFFT input buffers (TX)
    ru->common.txdataF_BF = (int32_t **)malloc16(nb_tx_streams * sizeof(int32_t*));
    LOG_D(PHY, "[INIT] common.txdata_BF= %p (%lu bytes)\n", ru->common.txdataF_BF, nb_tx_streams * sizeof(int32_t *));
    for (int i = 0; i < nb_tx_streams; i++) {
      ru->common.txdataF_BF[i] =
          (int32_t *)malloc16_clear(fp->samples_per_slot_wCP * sizeof(int32_t));
      LOG_D(PHY, "txdataF_BF[%d] %p for RU %d\n", i, ru->common.txdataF_BF[i], ru->idx);
    }
    // allocate FFT output buffers (RX)
    ru->common.rxdataF = (int32_t**)malloc16(nb_rx_streams * sizeof(int32_t*));
    for (int i = 0; i < nb_rx_streams; i++) {
      // allocate 4 slots of I/Q signal data (frequency)
      int size = RU_RX_SLOT_DEPTH * fp->symbols_per_slot * fp->ofdm_symbol_size;
      ru->common.rxdataF[i] = (int32_t*)malloc16_clear(sizeof(**ru->common.rxdataF) * size);
      LOG_D(PHY, "rxdataF[%d] %p for RU %d\n", i, ru->common.rxdataF[i], ru->idx);
    }

    AssertFatal(ru->num_gNB <= NUMBER_OF_gNB_MAX, "gNB instances %d > %d\n", ru->num_gNB,NUMBER_OF_gNB_MAX);

    LOG_D(PHY, "[INIT] %s() ru->num_gNB:%d \n", __FUNCTION__, ru->num_gNB);
  } // !=IF5
}

void nr_phy_free_RU(RU_t *ru)
{
  LOG_D(PHY, "Freeing RU signal buffers (if_south %s) nb_tx %d\n", ru_if_types[ru->if_south], ru->nb_tx);
  int nb_tx_streams = ru->nb_tx;
  int nb_rx_streams = ru->nb_rx;

  if (ru->if_south <= REMOTE_IF5) { // this means REMOTE_IF5 or LOCAL_RF, so free memory for time-domain signals
    // Hack: undo what is done at allocation
    for (int i = 0; i < nb_tx_streams; i++) {
      int32_t *p = &ru->common.txdata[i][-ru->sf_extension];
      free_and_zero(p);
    }
    free_and_zero(ru->common.txdata);

    for (int i = 0; i < nb_rx_streams; i++)
      free_and_zero(ru->common.rxdata[i]);
    free_and_zero(ru->common.rxdata);
  } // else: IF5 or local RF -> nothing to free()

  if (ru->function != NGFI_RRU_IF5) { // we need to do RX/TX RU processing
    // free IFFT input buffers (TX)
    for (int i = 0; i < nb_tx_streams; i++)
      free_and_zero(ru->common.txdataF_BF[i]);
    free_and_zero(ru->common.txdataF_BF);

    // free FFT output buffers (RX)
    for (int i = 0; i < nb_rx_streams; i++)
      free_and_zero(ru->common.rxdataF[i]);
    free_and_zero(ru->common.rxdataF);
  }

  PHY_VARS_gNB *gNB0 = ru->gNB_list[0];
  gNB0->num_RU--;
  DevAssert(gNB0->num_RU >= 0);
}
