/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief primitives used by eNB for BCH, RACH, ULSCH, DLSCH scheduling
 */

#include "assertions.h"

#include "LAYER2/MAC/mac.h"
#include "LAYER2/MAC/mac_extern.h"

#include "LAYER2/MAC/mac_proto.h"
#include "common/utils/LOG/log.h"
#include "nfapi/oai_integration/vendor_ext.h"
#include "common/utils/LOG/vcd_signal_dumper.h"
#include "UTIL/OPT/opt.h"

#include "RRC/LTE/rrc_extern.h"
#include "RRC/L2_INTERFACE/openair_rrc_L2_interface.h"

//#include "LAYER2/MAC/pre_processor.c"

#include "intertask_interface.h"
#include "executables/softmodem-common.h"
#include "T.h"

#define ENABLE_MAC_PAYLOAD_DEBUG
#define DEBUG_eNB_SCHEDULER 1
extern uint16_t frame_cnt;

#include "common/ran_context.h"
#include "SCHED/sched_common.h"
#include "openair2/LAYER2/MAC/mac_extern.h"
/*
 * If the CQI is low, then scheduler will use a higher aggregation level and lower aggregation level otherwise
 * this is also dependent to transmission mode, where an offset could be defined
 */
// the follwoing three tables are calibrated for TXMODE 1 and 2
static const uint8_t cqi2fmt0_agg[MAX_SUPPORTED_BW][CQI_VALUE_RANGE] = {
    {3, 3, 3, 2, 2, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0}, // 1.4_DCI0_CRC_Size= 37 bits
    //{3, 3, 3, 3, 2, 2, 2, 2, 1, 1, 1, 1, 0, 0, 0, 0}, // 5_DCI0_CRC_SIZE = 41
    {3, 3, 3, 3, 2, 2, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0}, // 5_DCI0_CRC_SIZE = 41
    {3, 3, 3, 3, 2, 2, 2, 1, 1, 1, 1, 0, 0, 0, 0, 0}, // 10_DCI0_CRC_SIZE = 43
    {3, 3, 3, 3, 2, 2, 2, 2, 1, 1, 1, 1, 0, 0, 0, 0} // 20_DCI0_CRC_SIZE = 44
};

static const uint8_t cqi2fmt1x_agg[MAX_SUPPORTED_BW][CQI_VALUE_RANGE] = {
    {3, 3, 3, 2, 2, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0}, // 1.4_DCI0_CRC_Size < 38 bits
    {3, 3, 3, 3, 2, 2, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0}, // 5_DCI0_CRC_SIZE  < 43
    {3, 3, 3, 3, 2, 2, 2, 1, 1, 1, 1, 0, 0, 0, 0, 0}, // 10_DCI0_CRC_SIZE  < 47
    {3, 3, 3, 3, 2, 2, 2, 2, 1, 1, 1, 1, 0, 0, 0, 0} // 20_DCI0_CRC_SIZE  < 55
};

static const uint8_t cqi2fmt2x_agg[MAX_SUPPORTED_BW][CQI_VALUE_RANGE] = {
    {3, 3, 3, 2, 2, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0}, // 1.4_DCI0_CRC_Size= 47 bits
    {3, 3, 3, 3, 2, 2, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0}, // 5_DCI0_CRC_SIZE = 55
    {3, 3, 3, 3, 2, 2, 2, 1, 1, 1, 1, 0, 0, 0, 0, 0}, // 10_DCI0_CRC_SIZE = 59
    {3, 3, 3, 3, 3, 2, 2, 2, 2, 2, 1, 1, 1, 1, 0, 0} // 20_DCI0_CRC_SIZE = 64
};

extern RAN_CONTEXT_t RC;
eNB_DLSCH_INFO eNB_dlsch_info[NUMBER_OF_eNB_MAX][MAX_NUM_CCs][MAX_MOBILES_PER_ENB]; // eNBxUE = 8x8
//------------------------------------------------------------------------------
int
choose(int n,
       int k)
//------------------------------------------------------------------------------
{
  int res = 1;
  int res2 = 1;
  int i;

  if (k > n)
    return (0);

  if (n == k)
    return (1);

  for (i = n; i > k; i--)
    res *= i;

  for (i = 2; i <= (n - k); i++)
    res2 *= i;

  return (res / res2);
}

//------------------------------------------------------------------------------
// Patented algorithm from Yang et al, US Patent 2009, "Channel Quality Indexing and Reverse Indexing"
void reverse_index(int N,
                   int M,
                   int r,
                   int *v)
//------------------------------------------------------------------------------
{
  int BaseValue = 0;
  int IncreaseValue, ThresholdValue;
  int sumV;
  int i;
  r = choose(N, M) - 1 - r;
  memset((void *) v, 0, M * sizeof(int));
  sumV = 0;
  i = M;

  while (i > 0 && r > 0) {
    IncreaseValue = choose(N - M + 1 - sumV - v[i - 1] + i - 2,
                           i - 1);
    ThresholdValue = BaseValue + IncreaseValue;

    if (r >= ThresholdValue) {
      v[i - 1]++;
      BaseValue = ThresholdValue;
    } else {
      r -= BaseValue;
      sumV += v[--i];
      BaseValue = 0;
    }
  }
}

//------------------------------------------------------------------------------
int
to_prb(int dl_Bandwidth)
//------------------------------------------------------------------------------
{
  int prbmap[6] = { 6, 15, 25, 50, 75, 100 };
  AssertFatal(dl_Bandwidth < 6, "dl_Bandwidth is 0..5\n");
  return (prbmap[dl_Bandwidth]);
}

//------------------------------------------------------------------------------
int
to_rbg(int dl_Bandwidth)
//------------------------------------------------------------------------------
{
  int rbgmap[6] = { 6, 8, 13, 17, 19, 25 };
  AssertFatal(dl_Bandwidth < 6, "dl_Bandwidth is 0..5\n");
  return (rbgmap[dl_Bandwidth]);
}

//------------------------------------------------------------------------------
int
get_phich_resource_times6(COMMON_channels_t *cc)
//------------------------------------------------------------------------------
{
  int phichmap[4] = { 1, 3, 6, 12 };
  AssertFatal(cc != NULL, "cc is null\n");
  AssertFatal(cc->mib != NULL, "cc->mib is null\n");
  int phich_Resource = (int) cc->mib->message.phich_Config.phich_Resource;
  AssertFatal(phich_Resource >= 0 && phich_Resource < 4, "phich_Resource %d not in 0..3\n",
              phich_Resource);
  return (phichmap[phich_Resource]);
}

//------------------------------------------------------------------------------
uint16_t
mac_computeRIV(uint16_t N_RB_DL,
               uint16_t RBstart,
               uint16_t Lcrbs)
//------------------------------------------------------------------------------
{
  if (Lcrbs <= (1 + (N_RB_DL >> 1))) {
    return (N_RB_DL * (Lcrbs - 1)) + RBstart;
  }

  return (N_RB_DL * (N_RB_DL + 1 - Lcrbs)) + (N_RB_DL - 1 - RBstart);
}

//------------------------------------------------------------------------------
uint8_t
getQm(uint8_t mcs)
//------------------------------------------------------------------------------
{
  if (mcs < 10)      return (2);
  else if (mcs < 17) return (4);

  return (6);
}

//------------------------------------------------------------------------------
void
get_Msg3alloc(COMMON_channels_t *cc,
              sub_frame_t       current_subframe,
              frame_t           current_frame,
              frame_t           *frame,
              sub_frame_t       *subframe)
//------------------------------------------------------------------------------
{
  // Fill in other TDD Configuration!!!!
  int subframeAssignment;

  if (cc->tdd_Config == NULL) { // FDD
    *subframe = current_subframe + 6;

    if (*subframe > 9) {
      *subframe = *subframe - 10;
      *frame = (current_frame + 1) & 1023;
    } else {
      *frame = current_frame;
    }
  } else {      // TDD
    subframeAssignment = (int) cc->tdd_Config->subframeAssignment;

    if (subframeAssignment == 1) {
      switch (current_subframe) {
        case 0:
          *subframe = 7;
          *frame = current_frame;
          break;

        case 4:
          *subframe = 2;
          *frame = (current_frame + 1) & 1023;
          break;

        case 5:
          *subframe = 2;
          *frame = (current_frame + 1) & 1023;
          break;

        case 9:
          *subframe = 7;
          *frame = (current_frame + 1) & 1023;
          break;
      }
    } else if (subframeAssignment == 3) {
      switch (current_subframe) {
        case 0:
        case 5:
        case 6:
          *subframe = 2;
          *frame = (current_frame + 1) & 1023;
          break;

        case 7:
          *subframe = 3;
          *frame = (current_frame + 1) & 1023;
          break;

        case 8:
          *subframe = 4;
          *frame = (current_frame + 1) & 1023;
          break;

        case 9:
          *subframe = 2;
          *frame = (current_frame + 2) & 1023;
          break;
      }
    } else if (subframeAssignment == 4) {
      switch (current_subframe) {
        case 0:
        case 4:
        case 5:
        case 6:
          *subframe = 2;
          *frame = (current_frame + 1) & 1023;
          break;

        case 7:
          *subframe = 3;
          *frame = (current_frame + 1) & 1023;
          break;

        case 8:
        case 9:
          *subframe = 2;
          *frame = (current_frame + 2) & 1023;
          break;
      }
    } else if (subframeAssignment == 5) {
      switch (current_subframe) {
        case 0:
        case 4:
        case 5:
        case 6:
          *subframe = 2;
          *frame = (current_frame + 1) & 1023;
          break;

        case 7:
        case 8:
        case 9:
          *subframe = 2;
          *frame = (current_frame + 2) & 1023;
          break;
      }
    }
  }

  return;
}

//------------------------------------------------------------------------------
void
get_Msg3allocret(COMMON_channels_t *cc,
                 sub_frame_t current_subframe,
                 frame_t current_frame,
                 frame_t *frame,
                 sub_frame_t *subframe)
//------------------------------------------------------------------------------
{
  int subframeAssignment;

  if (cc->tdd_Config == NULL) { //FDD
    /* always retransmit in n+8 */
    *subframe = current_subframe + 8;

    if (*subframe > 9) {
      *subframe = *subframe - 10;
      *frame = (current_frame + 1) & 1023;
    } else {
      *frame = current_frame;
    }
  } else {
    subframeAssignment = (int) cc->tdd_Config->subframeAssignment;

    if (subframeAssignment == 1) {
      // original PUSCH in 2, PHICH in 6 (S), ret in 2
      // original PUSCH in 3, PHICH in 9, ret in 3
      // original PUSCH in 7, PHICH in 1 (S), ret in 7
      // original PUSCH in 8, PHICH in 4, ret in 8
      *frame = (current_frame + 1) & 1023;
    } else if (subframeAssignment == 3) {
      // original PUSCH in 2, PHICH in 8, ret in 2 next frame
      // original PUSCH in 3, PHICH in 9, ret in 3 next frame
      // original PUSCH in 4, PHICH in 0, ret in 4 next frame
      *frame = (current_frame + 1) & 1023;
    } else if (subframeAssignment == 4) {
      // original PUSCH in 2, PHICH in 8, ret in 2 next frame
      // original PUSCH in 3, PHICH in 9, ret in 3 next frame
      *frame = (current_frame + 1) & 1023;
    } else if (subframeAssignment == 5) {
      // original PUSCH in 2, PHICH in 8, ret in 2 next frame
      *frame = (current_frame + 1) & 1023;
    }
  }

  return;
}

//------------------------------------------------------------------------------
uint8_t
subframe2harqpid(COMMON_channels_t *cc,
                 frame_t frame,
                 sub_frame_t subframe)
//------------------------------------------------------------------------------
{
  AssertFatal(cc != NULL, "cc is null\n");
  uint8_t ret = 255;

  if (cc->tdd_Config == NULL) { // FDD
    ret = (((frame << 1) + subframe) & 7);
  } else {
    switch (cc->tdd_Config->subframeAssignment) {
      case 1:
        if (subframe == 2 || subframe == 3 || subframe == 7 || subframe == 8) {
          switch (subframe) {
            case 2:
            case 3:
              ret = (subframe - 2);
              break;

            case 7:
            case 8:
              ret = (subframe - 5);
              break;

            default:
              AssertFatal(1 == 0, "subframe2_harq_pid, Illegal subframe %d for TDD mode %d\n",
                          subframe,
                          (int) cc->tdd_Config->subframeAssignment);
              break;
          }
        }

        break;

      case 2:
        AssertFatal(subframe == 2 || subframe == 7, "subframe2_harq_pid, Illegal subframe %d for TDD mode %d\n",
                    subframe,
                    (int) cc->tdd_Config->subframeAssignment);
        ret = (subframe / 7);
        break;

      case 3:
        AssertFatal(subframe > 1 && subframe < 5, "subframe2_harq_pid, Illegal subframe %d for TDD mode %d\n",
                    subframe,
                    (int) cc->tdd_Config->subframeAssignment);
        ret = (subframe - 2);
        break;

      case 4:
        AssertFatal(subframe > 1 && subframe < 4, "subframe2_harq_pid, Illegal subframe %d for TDD mode %d\n",
                    subframe,
                    (int) cc->tdd_Config->subframeAssignment);
        ret = (subframe - 2);
        break;

      case 5:
        AssertFatal(subframe == 2, "subframe2_harq_pid, Illegal subframe %d for TDD mode %d\n",
                    subframe,
                    (int) cc->tdd_Config->subframeAssignment);
        ret = (subframe - 2);
        break;

      default:
        AssertFatal(1 == 0, "subframe2_harq_pid, Unsupported TDD mode %d\n",
                    (int) cc->tdd_Config->subframeAssignment);
        break;
    }
  }

  return ret;
}

//------------------------------------------------------------------------------
uint8_t
get_Msg3harqpid(COMMON_channels_t *cc,
                frame_t frame,
                sub_frame_t current_subframe)
//------------------------------------------------------------------------------
{
  uint8_t ul_subframe = 0;
  uint32_t ul_frame = 0;

  if (cc->tdd_Config == NULL) { // FDD
    ul_subframe = (current_subframe > 3) ? (current_subframe - 4) : (current_subframe + 6);
    ul_frame = (current_subframe > 3) ? ((frame + 1) & 1023) : frame;
  } else {
    switch (cc->tdd_Config->subframeAssignment) {
      case 1:
        switch (current_subframe) {
          case 9:
          case 0:
            ul_subframe = 7;
            break;

          case 5:
          case 7:
            ul_subframe = 2;
            break;
        }

        break;

      case 3:
        switch (current_subframe) {
          case 0:
          case 5:
          case 6:
            ul_subframe = 2;
            break;

          case 7:
            ul_subframe = 3;
            break;

          case 8:
            ul_subframe = 4;
            break;

          case 9:
            ul_subframe = 2;
            break;
        }

        break;

      case 4:
        switch (current_subframe) {
          case 0:
          case 5:
          case 6:
          case 8:
          case 9:
            ul_subframe = 2;
            break;

          case 7:
            ul_subframe = 3;
            break;
        }

        break;

      case 5:
        ul_subframe = 2;
        break;

      default:
        LOG_E(PHY, "get_Msg3_harq_pid: Unsupported TDD configuration %d\n",
              (int) cc->tdd_Config->subframeAssignment);
        AssertFatal(1 == 0, "get_Msg3_harq_pid: Unsupported TDD configuration");
        break;
    }
  }

  return (subframe2harqpid(cc,
                           ul_frame,
                           ul_subframe));
}

//------------------------------------------------------------------------------
uint32_t
pdcchalloc2ulframe(COMMON_channels_t *ccP,
                   uint32_t frame,
                   uint8_t n)
//------------------------------------------------------------------------------
{
  uint32_t ul_frame = (frame + (n >= 6 ? 1 : 0));

  if (ccP->tdd_Config) {
    if (ccP->tdd_Config->subframeAssignment == 1) {
      if (n == 1 || n == 6) {
        ul_frame = (frame + (n == 1 ? 0 : 1));
      }
    } else if (ccP->tdd_Config->subframeAssignment == 6) {
      if (n == 0 || n == 1 || n == 5 || n == 6) {
        ul_frame = (frame + (n >= 5 ? 1 : 0));
      } else if (n == 9) {
        ul_frame = (frame + 1);
      }
    }
  }

  LOG_D(PHY, "frame %d subframe %d: PUSCH frame = %d\n",
        frame,
        n,
        ul_frame);
  return ul_frame;
}

//------------------------------------------------------------------------------
uint8_t
pdcchalloc2ulsubframe(COMMON_channels_t *ccP,
                      uint8_t n)
//------------------------------------------------------------------------------
{
  uint8_t ul_subframe;

  if (ccP->tdd_Config && ccP->tdd_Config->subframeAssignment == 1 && (n == 1 || n == 6))  // tdd_config 0,1 SF 1,5
    ul_subframe = ((n + 6) % 10);
  else if (ccP->tdd_Config && ccP->tdd_Config->subframeAssignment == 6 && (n == 0 || n == 1 || n == 5 || n == 6))
    ul_subframe = ((n + 7) % 10);
  else if (ccP->tdd_Config && ccP->tdd_Config->subframeAssignment == 6 && n == 9) // tdd_config 6 SF 9
    ul_subframe = ((n + 5) % 10);
  else
    ul_subframe = ((n + 4) % 10);

  LOG_D(PHY, "subframe %d: PUSCH subframe = %d\n",
        n,
        ul_subframe);
  return ul_subframe;
}

//------------------------------------------------------------------------------
int
is_UL_sf(COMMON_channels_t *ccP,
         sub_frame_t subframeP)
//------------------------------------------------------------------------------
{
  // if FDD return dummy value
  if (ccP->tdd_Config == NULL)
    return 0;

  switch (ccP->tdd_Config->subframeAssignment) {
    case 1:
      switch (subframeP) {
        case 0:
        case 4:
        case 5:
        case 9:
          return 0;

        case 2:
        case 3:
        case 7:
        case 8:
          return 1;

        default:
          return 0;
      }

      break;

    case 3:
      if (subframeP <= 1 || subframeP >= 5)
        return 0;

      return 1;

    case 4:
      if (subframeP <= 1 || subframeP >= 4)
        return 0;

      return 1;

    case 5:
      if (subframeP <= 1 || subframeP >= 3)
        return 0;

      return 1;

    default:
      AssertFatal(1 == 0,  "subframe %d Unsupported TDD configuration %d\n",
                  subframeP,
                  (int) ccP->tdd_Config->subframeAssignment);
      break;
  }

  return 0;
}

//------------------------------------------------------------------------------
int
is_S_sf(COMMON_channels_t *ccP,
        sub_frame_t subframeP)
//------------------------------------------------------------------------------
{
  // if FDD return dummy value
  if (ccP->tdd_Config == NULL)
    return 0;

  switch (subframeP) {
    case 1:
      return 1;

    case 6:
      if (ccP->tdd_Config->subframeAssignment == 0 || ccP->tdd_Config->subframeAssignment == 1 ||
          ccP->tdd_Config->subframeAssignment == 2 || ccP->tdd_Config->subframeAssignment == 6)
        return 1;

      break;

    default:
      break;
  }

  return 0;
}

//------------------------------------------------------------------------------
uint8_t
ul_subframe2_k_phich(COMMON_channels_t *cc,
                     sub_frame_t ul_subframe)
//------------------------------------------------------------------------------
{
  if(cc->tdd_Config) { //TODO fill other tdd config
    switch(cc->tdd_Config->subframeAssignment) {
      case 0:
        break;

      case 1:
        if(ul_subframe == 2 || ul_subframe == 7)
          return 4;
        else if(ul_subframe == 3 || ul_subframe == 8)
          return 6;

        return 255;

      case 2:
      case 3:
      case 4:
      case 5:
        break;
    }
  }

  return 4; //idk  sf_ahead?
}

//------------------------------------------------------------------------------
uint16_t
get_pucch1_absSF(COMMON_channels_t *cc,
                 uint16_t dlsch_absSF)
//------------------------------------------------------------------------------
{
  uint16_t sf, f, nextf;
  LTE_TDD_Config_t *tdd_Config = cc->tdd_Config;

  if (tdd_Config == NULL) { //FDD n+4
    return (dlsch_absSF + 4) % 10240;
  }

  sf = dlsch_absSF % 10;
  f = dlsch_absSF / 10;
  nextf = (f + 1) & 1023;

  switch (tdd_Config->subframeAssignment) {
    case 0:
      if (sf == 0 || sf == 5)
        return ((10 * f) + sf + 4) % 10240; // ACK/NAK in SF 4,9 same frame

      if (sf == 6)
        return ((10 * nextf) + 2) % 10240;  // ACK/NAK in SF 2 next frame

      if (sf == 1)
        return ((10 * f) + 7) % 10240;      // ACK/NAK in SF 7 same frame

      break;

    case 1:
      if (sf == 5 || sf == 6)
        return ((10 * nextf) + 2) % 10240;  // ACK/NAK in SF 2 next frame

      if (sf == 9)
        return ((10 * nextf) + 3) % 10240;  // ACK/NAK in SF 3 next frame

      if ((sf == 0) || (sf == 1))
        return ((10 * f) + 7) % 10240;      // ACK/NAK in SF 7 same frame

      if (sf == 4)
        return ((10 * f) + 8) % 10240;      // ACK/NAK in SF 8 same frame

      break;

    case 2:
      if (sf == 4 || sf == 5 || sf == 6 || sf == 8)
        return ((10 * nextf) + 2) % 10240;  // ACK/NAK in SF 2 next frame

      if (sf == 9)
        return ((10 * nextf) + 7) % 10240;  // ACK/NAK in SF 7 next frame

      if (sf == 0 || sf == 1 || sf == 3)
        return ((10 * f) + 7)% 10240;       // ACK/NAK in SF 7 same frame

      break;

    case 3:
      if (sf == 5 || sf == 6 || sf == 7 || sf == 8 || sf == 9)
        return ((10 * nextf) + ((sf - 1) >> 1)) % 10240;  // ACK/NAK in 2,3,4 resp. next frame

      if (sf == 1)
        return ((10 * nextf) + 2) % 10240;                // ACK/NAK in 2 next frame

      if (sf == 0)
        return ((10 * f) + 4) % 10240;                    // ACK/NAK in 4 same frame

      break;

    case 4:
      if (sf == 6 || sf == 7 || sf == 8 || sf == 9)
        return ((10 * nextf) + 3) % 10240;  // ACK/NAK in SF 3 next frame
      else if (sf == 0 || sf == 1 || sf == 4 || sf == 5)
        return ((10 * nextf) + 2) % 10240;  // ACK/NAK in SF 2 next frame

      break;

    case 5:
      if (sf == 0 || sf == 1 || sf == 3 || sf == 4 || sf == 5 || sf == 6 || sf == 7 || sf == 8)
        return ((10 * nextf) + 2) % 10240;        // ACK/NAK in SF 3 next frame

      if (sf == 9)
        return ((10 * (1 + nextf)) + 2) % 10240;  // ACK/NAK in SF 2 next frame

      break;

    case 6:
      if (sf == 5 || sf == 6)
        return ((10 * f) + sf + 7) % 10240; // ACK/NAK in SF 2,3 next frame

      if (sf == 9)
        return ((10 * nextf) + 4) % 10240;  // ACK/NAK in SF 4 next frame

      if (sf == 1 || sf == 0)
        return ((10 * f) + sf + 7) % 10240; // ACK/NAK in SF 7 same frame

      break;

    default:
      AssertFatal(1 == 0, "Illegal TDD subframe Assigment %ld\n",
                  tdd_Config->subframeAssignment);
      return 0;
  }

  AssertFatal(1 == 0, "Shouldn't get here\n");
}

//------------------------------------------------------------------------------
void
get_srs_pos(COMMON_channels_t *cc,
            uint16_t isrs,
            uint16_t *psrsPeriodicity,
            uint16_t *psrsOffset)
//------------------------------------------------------------------------------
{
  if (cc->tdd_Config) { // TDD
    AssertFatal(isrs >= 10, "2 ms SRS periodicity not supported");

    if (isrs > 9 && isrs < 15) {
      *psrsPeriodicity = 5;
      *psrsOffset = isrs - 10;
    } else if (isrs > 14 && isrs < 25) {
      *psrsPeriodicity = 10;
      *psrsOffset = isrs - 15;
    } else if (isrs > 24 && isrs < 45) {
      *psrsPeriodicity = 20;
      *psrsOffset = isrs - 25;
    } else if (isrs > 44 && isrs < 85) {
      *psrsPeriodicity = 40;
      *psrsOffset = isrs - 45;
    } else if (isrs > 84 && isrs < 165) {
      *psrsPeriodicity = 80;
      *psrsOffset = isrs - 85;
    } else if (isrs > 164 && isrs < 325) {
      *psrsPeriodicity = 160;
      *psrsOffset = isrs - 165;
    } else if (isrs > 324 && isrs < 645) {
      *psrsPeriodicity = 320;
      *psrsOffset = isrs - 325;
    }

    AssertFatal(isrs <= 644, "Isrs out of range %d>644\n", isrs);
  }       // TDD
  else {      // FDD
    if (isrs < 2) {
      *psrsPeriodicity = 2;
      *psrsOffset = isrs;
    } else if (isrs > 1 && isrs < 7) {
      *psrsPeriodicity = 5;
      *psrsOffset = isrs - 2;
    } else if (isrs > 6 && isrs < 17) {
      *psrsPeriodicity = 10;
      *psrsOffset = isrs - 7;
    } else if (isrs > 16 && isrs < 37) {
      *psrsPeriodicity = 20;
      *psrsOffset = isrs - 17;
    } else if (isrs > 36 && isrs < 77) {
      *psrsPeriodicity = 40;
      *psrsOffset = isrs - 37;
    } else if (isrs > 76 && isrs < 157) {
      *psrsPeriodicity = 80;
      *psrsOffset = isrs - 77;
    } else if (isrs > 156 && isrs < 317) {
      *psrsPeriodicity = 160;
      *psrsOffset = isrs - 157;
    } else if (isrs > 316 && isrs < 637) {
      *psrsPeriodicity = 320;
      *psrsOffset = isrs - 317;
    }

    AssertFatal(isrs <= 636, "Isrs out of range %d>636\n", isrs);
  }

  return;
}

//------------------------------------------------------------------------------
/*
* Get some CSI (CQI/PMI/RI) parameters for SFN and subframe number calculation
* with periodic report.
*/
void
get_csi_params(COMMON_channels_t *cc,
               struct LTE_CQI_ReportPeriodic *cqi_ReportPeriodic,
               uint16_t *Npd,
               uint16_t *N_OFFSET_CQI,
               int *H)
//------------------------------------------------------------------------------
{
  AssertFatal(cqi_ReportPeriodic != NULL, "cqi_ReportPeriodic is null!\n");
  uint16_t cqi_PMI_ConfigIndex = cqi_ReportPeriodic->choice.setup.cqi_pmi_ConfigIndex;
  uint8_t Jtab[6] = { 0, 2, 2, 3, 4, 4 };

  if (cc->tdd_Config == NULL) { //FDD
    if (cqi_PMI_ConfigIndex <= 1) { // 2 ms CQI_PMI period
      *Npd = 2;
      *N_OFFSET_CQI = cqi_PMI_ConfigIndex;
    } else if (cqi_PMI_ConfigIndex <= 6) {  // 5 ms CQI_PMI period
      *Npd = 5;
      *N_OFFSET_CQI = cqi_PMI_ConfigIndex - 2;
    } else if (cqi_PMI_ConfigIndex <= 16) { // 10ms CQI_PMI period
      *Npd = 10;
      *N_OFFSET_CQI = cqi_PMI_ConfigIndex - 7;
    } else if (cqi_PMI_ConfigIndex <= 36) { // 20 ms CQI_PMI period
      *Npd = 20;
      *N_OFFSET_CQI = cqi_PMI_ConfigIndex - 17;
    } else if (cqi_PMI_ConfigIndex <= 76) { // 40 ms CQI_PMI period
      *Npd = 40;
      *N_OFFSET_CQI = cqi_PMI_ConfigIndex - 37;
    } else if (cqi_PMI_ConfigIndex <= 156) {  // 80 ms CQI_PMI period
      *Npd = 80;
      *N_OFFSET_CQI = cqi_PMI_ConfigIndex - 77;
    } else if (cqi_PMI_ConfigIndex <= 316) {  // 160 ms CQI_PMI period
      *Npd = 160;
      *N_OFFSET_CQI = cqi_PMI_ConfigIndex - 157;
    } else if (cqi_PMI_ConfigIndex > 317) {
      if (cqi_PMI_ConfigIndex <= 349) {         // 32 ms CQI_PMI period
        *Npd = 32;
        *N_OFFSET_CQI = cqi_PMI_ConfigIndex - 318;
      } else if (cqi_PMI_ConfigIndex <= 413) {  // 64 ms CQI_PMI period
        *Npd = 64;
        *N_OFFSET_CQI = cqi_PMI_ConfigIndex - 350;
      } else if (cqi_PMI_ConfigIndex <= 541) {  // 128 ms CQI_PMI period
        *Npd = 128;
        *N_OFFSET_CQI = cqi_PMI_ConfigIndex - 414;
      }
    }
  } else {  // TDD
    if (cqi_PMI_ConfigIndex == 0) { // all UL subframes
      *Npd = 1;
      *N_OFFSET_CQI = 0;
    } else if (cqi_PMI_ConfigIndex <= 6) {  // 5 ms CQI_PMI period
      *Npd = 5;
      *N_OFFSET_CQI = cqi_PMI_ConfigIndex - 1;
    } else if (cqi_PMI_ConfigIndex <= 16) { // 10ms CQI_PMI period
      *Npd = 10;
      *N_OFFSET_CQI = cqi_PMI_ConfigIndex - 6;
    } else if (cqi_PMI_ConfigIndex <= 36) { // 20 ms CQI_PMI period
      *Npd = 20;
      *N_OFFSET_CQI = cqi_PMI_ConfigIndex - 16;
    } else if (cqi_PMI_ConfigIndex <= 76) { // 40 ms CQI_PMI period
      *Npd = 40;
      *N_OFFSET_CQI = cqi_PMI_ConfigIndex - 36;
    } else if (cqi_PMI_ConfigIndex <= 156) {  // 80 ms CQI_PMI period
      *Npd = 80;
      *N_OFFSET_CQI = cqi_PMI_ConfigIndex - 76;
    } else if (cqi_PMI_ConfigIndex <= 316) {  // 160 ms CQI_PMI period
      *Npd = 160;
      *N_OFFSET_CQI = cqi_PMI_ConfigIndex - 156;
    }
  }

  // get H
  if (cqi_ReportPeriodic->choice.setup.cqi_FormatIndicatorPeriodic.present == LTE_CQI_ReportPeriodic__setup__cqi_FormatIndicatorPeriodic_PR_subbandCQI) {
    *H = 1 + (Jtab[cc->mib->message.dl_Bandwidth] * cqi_ReportPeriodic->choice.setup.cqi_FormatIndicatorPeriodic.choice.subbandCQI.k);
  } else {
    *H = 1;
  }

  return;
}

//------------------------------------------------------------------------------
uint8_t
get_dl_cqi_pmi_size_pusch(COMMON_channels_t *cc,
                          uint8_t tmode,
                          uint8_t ri,
                          LTE_CQI_ReportModeAperiodic_t *cqi_ReportModeAperiodic)
//------------------------------------------------------------------------------
{
  int Ntab[6] = { 0, 4, 7, 9, 10, 13 };
  int N = Ntab[cc->mib->message.dl_Bandwidth];
  int Ltab_uesel[6] = { 0, 6, 9, 13, 15, 18 };
  int L = Ltab_uesel[cc->mib->message.dl_Bandwidth];
  AssertFatal(cqi_ReportModeAperiodic != NULL, "cqi_ReportPeriodic is null!\n");

  switch (*cqi_ReportModeAperiodic) {
    case LTE_CQI_ReportModeAperiodic_rm12:
      AssertFatal(tmode == 4 || tmode == 6 || tmode == 8 || tmode == 9 || tmode == 10, "Illegal TM (%d) for CQI_ReportModeAperiodic_rm12\n",
                  tmode);
      AssertFatal(cc->p_eNB <= 4, "only up to 4 antenna ports supported here\n");

      if (ri == 1 && cc->p_eNB == 2)
        return (4 + (N << 1));

      if (ri == 2 && cc->p_eNB == 2)
        return (8 + N);

      if (ri == 1 && cc->p_eNB == 4)
        return (4 + (N << 2));

      if (ri > 1 && cc->p_eNB == 4)
        return (8 + (N << 2));

      break;

    case LTE_CQI_ReportModeAperiodic_rm20:
      // Table 5.2.2.6.3-1 (36.212)
      AssertFatal(tmode == 1 || tmode == 2 || tmode == 3 || tmode == 7 || tmode == 9 || tmode == 10, "Illegal TM (%d) for CQI_ReportModeAperiodic_rm20\n",
                  tmode);
      AssertFatal(tmode != 9 && tmode != 10, "TM9/10 will be handled later for CQI_ReportModeAperiodic_rm20\n");
      return (4 + 2 + L);

    case LTE_CQI_ReportModeAperiodic_rm22:
      // Table 5.2.2.6.3-2 (36.212)
      AssertFatal(tmode == 4 || tmode == 6 || tmode == 8 || tmode == 9 || tmode == 10, "Illegal TM (%d) for CQI_ReportModeAperiodic_rm22\n",
                  tmode);
      AssertFatal(tmode != 9 && tmode != 10, "TM9/10 will be handled later for CQI_ReportModeAperiodic_rm22\n");

      if (ri == 1 && cc->p_eNB == 2)
        return (4 + 2 + 0 + 0 + L + 4);

      if (ri == 2 && cc->p_eNB == 2)
        return (4 + 2 + 4 + 2 + L + 2);

      if (ri == 1 && cc->p_eNB == 4)
        return (4 + 2 + 0 + 0 + L + 8);

      if (ri >= 2 && cc->p_eNB == 4)
        return (4 + 2 + 4 + 2 + L + 8);

      break;

    case LTE_CQI_ReportModeAperiodic_rm30:
      // Table 5.2.2.6.2-1 (36.212)
      AssertFatal(tmode == 1 || tmode == 2 || tmode == 3 || tmode == 7 || tmode == 8 || tmode == 9 || tmode == 10,
                  "Illegal TM (%d) for CQI_ReportModeAperiodic_rm30\n",
                  tmode);
      AssertFatal(tmode != 8 && tmode != 9 && tmode != 10, "TM8/9/10 will be handled later for CQI_ReportModeAperiodic_rm30\n");
      return (4 + (N << 1));

    case LTE_CQI_ReportModeAperiodic_rm31:
      // Table 5.2.2.6.2-2 (36.212)
      AssertFatal(tmode == 4 || tmode == 6 || tmode == 8 || tmode == 9 || tmode == 10, "Illegal TM (%d) for CQI_ReportModeAperiodic_rm31\n",
                  tmode);
      AssertFatal(tmode != 8 && tmode != 9 && tmode != 10, "TM8/9/10 will be handled later for CQI_ReportModeAperiodic_rm31\n");

      if (ri == 1 && cc->p_eNB == 2)
        return (4 + (N << 1) + 0 + 0 + 2);

      if (ri == 2 && cc->p_eNB == 2)
        return (4 + (N << 1) + 4 + (N << 1) + 1);

      if (ri == 1 && cc->p_eNB == 4)
        return (4 + (N << 1) + 0 + 0 + 4);

      if (ri >= 2 && cc->p_eNB == 4)
        return (4 + (N << 1) + 4 + (N << 1) + 4);

      break;

    case LTE_CQI_ReportModeAperiodic_rm32_v1250:
      AssertFatal(tmode == 4 || tmode == 6 || tmode == 8 || tmode == 9 || tmode == 10, "Illegal TM (%d) for CQI_ReportModeAperiodic_rm32\n",
                  tmode);
      AssertFatal(1 == 0, "CQI_ReportModeAperiodic_rm32_v1250 not supported yet\n");
      break;

    case LTE_CQI_ReportModeAperiodic_rm10_v1310:

      // Table 5.2.2.6.1-1F/G (36.212)
      if (ri == 1)
        return 4;   // F

      return 7;   // G

    case LTE_CQI_ReportModeAperiodic_rm11_v1310:
      // Table 5.2.2.6.1-1H (36.212)
      AssertFatal(tmode == 4 || tmode == 6 || tmode == 8 || tmode == 9 || tmode == 10,  "Illegal TM (%d) for CQI_ReportModeAperiodic_rm11\n",
                  tmode);
      AssertFatal(cc->p_eNB <= 4, "only up to 4 antenna ports supported here\n");

      if (ri == 1 && cc->p_eNB == 2)
        return (4 + 0 + 2);

      if (ri == 2 && cc->p_eNB == 2)
        return (4 + 4 + 1);

      if (ri == 1 && cc->p_eNB == 4)
        return (4 + 0 + 4);

      if (ri > 1 && cc->p_eNB == 4)
        return (4 + 4 + 4);

      break;
  }

  AssertFatal(1 == 0, "Shouldn't get here\n");
  return 0;
}

//------------------------------------------------------------------------------
uint8_t
get_rel8_dl_cqi_pmi_size(UE_sched_ctrl_t *sched_ctl,
                         int CC_idP,
                         COMMON_channels_t *cc,
                         uint8_t tmode,
                         struct LTE_CQI_ReportPeriodic *cqi_ReportPeriodic)
//------------------------------------------------------------------------------
{
  int no_pmi = 0;
  //    Ltab[6] = {0,log2(15/4/2),log2(25/4/2),log2(50/6/3),log2(75/8/4),log2(100/8/4)};
  uint8_t Ltab[6] = { 0, 1, 2, 2, 2, 2 };
  uint8_t ri = sched_ctl->periodic_ri_received[CC_idP];
  AssertFatal(cqi_ReportPeriodic != NULL, "cqi_ReportPeriodic is null!\n");
  AssertFatal(cqi_ReportPeriodic->present != LTE_CQI_ReportPeriodic_PR_NOTHING, "cqi_ReportPeriodic->present == CQI_ReportPeriodic_PR_NOTHING!\n");
  AssertFatal(cqi_ReportPeriodic->choice.setup.cqi_FormatIndicatorPeriodic.present != LTE_CQI_ReportPeriodic__setup__cqi_FormatIndicatorPeriodic_PR_NOTHING,
              "cqi_ReportPeriodic->cqi_FormatIndicatorPeriodic.choice.setup.present == CQI_ReportPeriodic__setup__cqi_FormatIndicatorPeriodic_PR_NOTHING!\n");

  switch (tmode) {
    case 1:
    case 2:
    case 5:
    case 6:
    case 7:
      no_pmi = 1;
      break;

    default:
      no_pmi = 0;
      break;
  }

  if (cqi_ReportPeriodic->choice.setup.cqi_FormatIndicatorPeriodic.present == LTE_CQI_ReportPeriodic__setup__cqi_FormatIndicatorPeriodic_PR_widebandCQI ||
      sched_ctl->feedback_cnt[CC_idP] == 0) {
    // send wideband report every opportunity if wideband reporting mode is selected, else every H opportunities
    if (no_pmi == 1) return 4;

    if (cc->p_eNB == 2 && ri == 1) return 6;

    if (cc->p_eNB == 2 && ri == 2) return 8;

    if (cc->p_eNB == 4 && ri == 1) return 8;

    if (cc->p_eNB == 4 && ri == 2) return 11;

    AssertFatal(1 == 0, "illegal combination p %d, ri %d, no_pmi %d\n",
                cc->p_eNB,
                ri,
                no_pmi);
  } else if (cqi_ReportPeriodic->choice.setup.cqi_FormatIndicatorPeriodic.present == LTE_CQI_ReportPeriodic__setup__cqi_FormatIndicatorPeriodic_PR_subbandCQI) {
    if (no_pmi == 1 || ri == 1) return (4 + Ltab[cc->mib->message.dl_Bandwidth]);

    return (7 + Ltab[cc->mib->message.dl_Bandwidth]);
  }

  AssertFatal(1 == 0, "Shouldn't get here : cqi_ReportPeriodic->present %d\n",
              cqi_ReportPeriodic->choice.setup.cqi_FormatIndicatorPeriodic.present);
  return 0;
}

//------------------------------------------------------------------------------
void
fill_nfapi_dl_dci_1A(nfapi_dl_config_request_pdu_t *dl_config_pdu,
                     uint8_t                       aggregation_level,
                     uint16_t                      rnti,
                     uint8_t                       rnti_type,
                     uint8_t                       harq_process,
                     uint8_t                       tpc,
                     uint16_t                      resource_block_coding,
                     uint8_t                       mcs,
                     uint8_t                       ndi,
                     uint8_t                       rv,
                     uint8_t                       vrb_flag)
//------------------------------------------------------------------------------
{
  memset((void *) dl_config_pdu, 0, sizeof(nfapi_dl_config_request_pdu_t));
  dl_config_pdu->pdu_type                                                          = NFAPI_DL_CONFIG_DCI_DL_PDU_TYPE;
  dl_config_pdu->pdu_size                                                          = (uint8_t) (2 + sizeof(nfapi_dl_config_dci_dl_pdu));
  dl_config_pdu->dci_dl_pdu.dci_dl_pdu_rel8.tl.tag                                 = NFAPI_DL_CONFIG_REQUEST_DCI_DL_PDU_REL8_TAG;
  dl_config_pdu->dci_dl_pdu.dci_dl_pdu_rel8.dci_format                             = NFAPI_DL_DCI_FORMAT_1A;
  dl_config_pdu->dci_dl_pdu.dci_dl_pdu_rel8.aggregation_level                      = aggregation_level;
  dl_config_pdu->dci_dl_pdu.dci_dl_pdu_rel8.rnti                                   = rnti;
  dl_config_pdu->dci_dl_pdu.dci_dl_pdu_rel8.rnti_type                              = rnti_type;
  dl_config_pdu->dci_dl_pdu.dci_dl_pdu_rel8.transmission_power                     = 6000;  // equal to RS power
  dl_config_pdu->dci_dl_pdu.dci_dl_pdu_rel8.harq_process                           = harq_process;
  dl_config_pdu->dci_dl_pdu.dci_dl_pdu_rel8.tpc                                    = tpc;
  dl_config_pdu->dci_dl_pdu.dci_dl_pdu_rel8.resource_block_coding                  = resource_block_coding;
  dl_config_pdu->dci_dl_pdu.dci_dl_pdu_rel8.mcs_1                                  = mcs;
  dl_config_pdu->dci_dl_pdu.dci_dl_pdu_rel8.new_data_indicator_1                   = ndi;
  dl_config_pdu->dci_dl_pdu.dci_dl_pdu_rel8.redundancy_version_1                   = rv;
  dl_config_pdu->dci_dl_pdu.dci_dl_pdu_rel8.virtual_resource_block_assignment_flag = vrb_flag;
  return;
}

//------------------------------------------------------------------------------
void
program_dlsch_acknak(module_id_t module_idP,
                     int CC_idP,
                     int UE_idP,
                     frame_t frameP,
                     sub_frame_t subframeP,
                     uint8_t cce_idx)
//------------------------------------------------------------------------------
{
  eNB_MAC_INST                           *eNB                         = RC.mac[module_idP];
  COMMON_channels_t                      *cc                          = eNB->common_channels;
  UE_info_t                              *UE_info                     = &eNB->UE_info;
  rnti_t                                 rnti                         = UE_RNTI(module_idP, UE_idP);
  nfapi_ul_config_request_body_t         *ul_req;
  nfapi_ul_config_request_pdu_t          *ul_config_pdu;
  int                                    use_simultaneous_pucch_pusch = 0;
  nfapi_ul_config_ulsch_harq_information *ulsch_harq_information      = NULL;
  nfapi_ul_config_harq_information       *harq_information            = NULL;
  struct LTE_PhysicalConfigDedicated__ext2 *ext2 = UE_info->UE_template[CC_idP][UE_idP].physicalConfigDedicated->ext2;

  if (ext2 &&
      ext2->pucch_ConfigDedicated_v1020 &&
      ext2->pucch_ConfigDedicated_v1020->simultaneousPUCCH_PUSCH_r10 &&
      *ext2->pucch_ConfigDedicated_v1020->simultaneousPUCCH_PUSCH_r10 == LTE_PUCCH_ConfigDedicated_v1020__simultaneousPUCCH_PUSCH_r10_true)
    use_simultaneous_pucch_pusch = 1;

  // pucch1 and pusch feedback is similar, namely in n+k subframes from now
  // This is used in the following "if/else" condition to check if there isn't or is already an UL grant in n+k
  int16_t ul_absSF = get_pucch1_absSF(&cc[CC_idP],
                                      subframeP + (10 * frameP));

  if ((ul_config_pdu = has_ul_grant(module_idP,
                                    CC_idP,
                                    ul_absSF,
                                    rnti)) == NULL) {
    // no UL grant so
    // Program ACK/NAK alone Format 1a/b or 3
    ul_req = &eNB->UL_req_tmp[CC_idP][ul_absSF % 10].ul_config_request_body;
    ul_config_pdu = &ul_req->ul_config_pdu_list[ul_req->number_of_pdus];
    // Do PUCCH
    fill_nfapi_uci_acknak(module_idP,
                          CC_idP,
                          rnti,
                          subframeP + (10 * frameP),
                          cce_idx);
  } else {
    /* there is already an existing UL grant so update it if needed
     * on top of some other UL resource (PUSCH,combined SR/CQI/HARQ on PUCCH, etc)
     */
    switch (ul_config_pdu->pdu_type) {
      /* [ulsch] to [ulsch + harq] or [ulsch + harq on pucch] */
      case NFAPI_UL_CONFIG_ULSCH_PDU_TYPE:
        if (use_simultaneous_pucch_pusch == 1) {
          // Convert it to an NFAPI_UL_CONFIG_ULSCH_UCI_HARQ_PDU_TYPE
          harq_information = &ul_config_pdu->ulsch_uci_harq_pdu.harq_information;
          ul_config_pdu->pdu_type = NFAPI_UL_CONFIG_ULSCH_UCI_HARQ_PDU_TYPE;
          LOG_D(MAC, "Frame %d, Subframe %d: Switched UCI HARQ to ULSCH UCI HARQ\n",
                frameP,
                subframeP);
        } else {
          // Convert it to an NFAPI_UL_CONFIG_ULSCH_HARQ_PDU_TYPE
          ulsch_harq_information = &ul_config_pdu->ulsch_harq_pdu.harq_information;
          ul_config_pdu->pdu_type = NFAPI_UL_CONFIG_ULSCH_HARQ_PDU_TYPE;
          ul_config_pdu->ulsch_harq_pdu.initial_transmission_parameters.initial_transmission_parameters_rel8.tl.tag
            = NFAPI_UL_CONFIG_REQUEST_INITIAL_TRANSMISSION_PARAMETERS_REL8_TAG;
          ul_config_pdu->ulsch_harq_pdu.initial_transmission_parameters.initial_transmission_parameters_rel8.n_srs_initial = 0; // last symbol not punctured
          ul_config_pdu->ulsch_harq_pdu.initial_transmission_parameters.initial_transmission_parameters_rel8.initial_number_of_resource_blocks
            = ul_config_pdu->ulsch_harq_pdu.ulsch_pdu.ulsch_pdu_rel8.number_of_resource_blocks;  // we don't change the number of resource blocks across retransmissions yet
          LOG_D(MAC,"Frame %d, Subframe %d: Switched UCI HARQ to ULSCH HARQ\n",
                frameP,
                subframeP);
        }

        break;

      case NFAPI_UL_CONFIG_ULSCH_HARQ_PDU_TYPE:
        AssertFatal(use_simultaneous_pucch_pusch == 0, "Cannot be NFAPI_UL_CONFIG_ULSCH_HARQ_PDU_TYPE, simultaneous_pucch_pusch is active");
        break;

      case NFAPI_UL_CONFIG_ULSCH_UCI_HARQ_PDU_TYPE:
        AssertFatal(use_simultaneous_pucch_pusch == 1, "Cannot be NFAPI_UL_CONFIG_ULSCH_UCI_PDU_TYPE, simultaneous_pucch_pusch is inactive\n");
        break;

      /* [ulsch + cqi] to [ulsch + cqi + harq] */

      case NFAPI_UL_CONFIG_ULSCH_CQI_RI_PDU_TYPE:
        // Convert it to an NFAPI_UL_CONFIG_ULSCH_CQI_HARQ_RI_PDU_TYPE
        ulsch_harq_information = &ul_config_pdu->ulsch_cqi_harq_ri_pdu.harq_information;
        ul_config_pdu->pdu_type = NFAPI_UL_CONFIG_ULSCH_CQI_HARQ_RI_PDU_TYPE;
        /* TODO: check this - when converting from nfapi_ul_config_ulsch_cqi_ri_pdu to
         * nfapi_ul_config_ulsch_cqi_harq_ri_pdu, shouldn't we copy initial_transmission_parameters
         * from the one to the other?
         * Those two types are not compatible. 'initial_transmission_parameters' is not at the
         * place in both.
         */
        ul_config_pdu->ulsch_cqi_harq_ri_pdu.initial_transmission_parameters.initial_transmission_parameters_rel8.tl.tag
          = NFAPI_UL_CONFIG_REQUEST_INITIAL_TRANSMISSION_PARAMETERS_REL8_TAG;
        ul_config_pdu->ulsch_cqi_harq_ri_pdu.initial_transmission_parameters.initial_transmission_parameters_rel8.n_srs_initial = 0;  // last symbol not punctured
        ul_config_pdu->ulsch_cqi_harq_ri_pdu.initial_transmission_parameters.initial_transmission_parameters_rel8.initial_number_of_resource_blocks
          = ul_config_pdu->ulsch_harq_pdu.ulsch_pdu.ulsch_pdu_rel8.number_of_resource_blocks; // we don't change the number of resource blocks across retransmissions yet
        break;

      case NFAPI_UL_CONFIG_ULSCH_CQI_HARQ_RI_PDU_TYPE:
        AssertFatal(use_simultaneous_pucch_pusch == 0, "Cannot be NFAPI_UL_CONFIG_ULSCH_CQI_HARQ_RI_PDU_TYPE, simultaneous_pucch_pusch is active\n");
        break;

      /* [ulsch + cqi on pucch] to [ulsch + cqi on pucch + harq on pucch] */

      case NFAPI_UL_CONFIG_ULSCH_UCI_CSI_PDU_TYPE:
        // convert it to an NFAPI_UL_CONFIG_ULSCH_CSI_UCI_HARQ_PDU_TYPE
        harq_information = &ul_config_pdu->ulsch_csi_uci_harq_pdu.harq_information;
        ul_config_pdu->pdu_type = NFAPI_UL_CONFIG_ULSCH_CSI_UCI_HARQ_PDU_TYPE;
        break;

      case NFAPI_UL_CONFIG_ULSCH_CSI_UCI_HARQ_PDU_TYPE:
        AssertFatal(use_simultaneous_pucch_pusch == 1, "Cannot be NFAPI_UL_CONFIG_ULSCH_CSI_UCI_HARQ_PDU_TYPE, simultaneous_pucch_pusch is inactive\n");
        break;

      /* [sr] to [sr + harq] */

      case NFAPI_UL_CONFIG_UCI_SR_PDU_TYPE:
        // convert to NFAPI_UL_CONFIG_UCI_SR_HARQ_PDU_TYPE
        ul_config_pdu->pdu_type = NFAPI_UL_CONFIG_UCI_SR_HARQ_PDU_TYPE;
        harq_information = &ul_config_pdu->uci_sr_harq_pdu.harq_information;
        break;

      case NFAPI_UL_CONFIG_UCI_SR_HARQ_PDU_TYPE:
        /* nothing to do */
        break;

      /* [cqi] to [cqi + harq] */
      case NFAPI_UL_CONFIG_UCI_CQI_PDU_TYPE:
        // convert to NFAPI_UL_CONFIG_UCI_CQI_HARQ_PDU_TYPE
        ul_config_pdu->pdu_type = NFAPI_UL_CONFIG_UCI_CQI_HARQ_PDU_TYPE;
        harq_information = &ul_config_pdu->uci_cqi_harq_pdu.harq_information;
        break;

      case NFAPI_UL_CONFIG_UCI_CQI_HARQ_PDU_TYPE:
        /* nothing to do */
        break;

      /* [cqi + sr] to [cqr + sr + harq] */
      case NFAPI_UL_CONFIG_UCI_CQI_SR_PDU_TYPE:
        // convert to NFAPI_UL_CONFIG_UCI_CQI_SR_HARQ_PDU_TYPE
        ul_config_pdu->pdu_type = NFAPI_UL_CONFIG_UCI_CQI_SR_HARQ_PDU_TYPE;
        harq_information = &ul_config_pdu->uci_cqi_sr_harq_pdu.harq_information;
        break;

      case NFAPI_UL_CONFIG_UCI_CQI_SR_HARQ_PDU_TYPE:
        /* nothing to do */
        break;
    }
  }

  if (ulsch_harq_information) {
    fill_nfapi_ulsch_harq_information(module_idP,
                                      CC_idP,
                                      rnti,
                                      ulsch_harq_information,
                                      subframeP);
  }

  if (harq_information) {
    fill_nfapi_harq_information(module_idP,
                                CC_idP,
                                rnti,
                                harq_information,
                                cce_idx);
  }

  return;
}

//------------------------------------------------------------------------------
uint8_t
get_V_UL_DAI(module_id_t module_idP,
             int CC_idP,
             uint16_t rntiP,
             sub_frame_t subframeP)
//------------------------------------------------------------------------------
{
  nfapi_hi_dci0_request_body_t *HI_DCI0_req = &RC.mac[module_idP]->HI_DCI0_req[CC_idP][subframeP].hi_dci0_request_body;
  nfapi_hi_dci0_request_pdu_t *hi_dci0_pdu  = &HI_DCI0_req->hi_dci0_pdu_list[0];

  for (int i = 0; i < HI_DCI0_req->number_of_dci; i++) {
    if (hi_dci0_pdu[i].pdu_type == NFAPI_HI_DCI0_DCI_PDU_TYPE &&
        hi_dci0_pdu[i].dci_pdu.dci_pdu_rel8.rnti == rntiP) {
      return hi_dci0_pdu[i].dci_pdu.dci_pdu_rel8.dl_assignment_index;
    }
  }

  return 4;     // this is rule from Section 7.3 in 36.213
}

//------------------------------------------------------------------------------
void
fill_nfapi_ulsch_harq_information(module_id_t                            module_idP,
                                  int                                    CC_idP,
                                  uint16_t                               rntiP,
                                  nfapi_ul_config_ulsch_harq_information *harq_information,
                                  sub_frame_t                            subframeP)
//------------------------------------------------------------------------------
{
  eNB_MAC_INST *eNB     = RC.mac[module_idP];
  COMMON_channels_t *cc = &eNB->common_channels[CC_idP];
  UE_info_t *UE_info    = &eNB->UE_info;
  int UE_id = find_UE_id(module_idP, rntiP);
  nfapi_ul_config_ulsch_harq_information_rel10_t *harq_information_rel10 = &harq_information->harq_information_rel10;
  AssertFatal(UE_id >= 0, "UE_id cannot be found, impossible\n");
  AssertFatal(UE_info != NULL, "UE_info is null\n");
  LTE_PhysicalConfigDedicated_t *physicalConfigDedicated = UE_info->UE_template[CC_idP][UE_id].physicalConfigDedicated;
  AssertFatal(physicalConfigDedicated != NULL, "physicalConfigDedicated for rnti %x is null\n",
              rntiP);
  struct LTE_PUSCH_ConfigDedicated *puschConfigDedicated = physicalConfigDedicated->pusch_ConfigDedicated;
  AssertFatal(puschConfigDedicated != NULL, "physicalConfigDedicated->puschConfigDedicated for rnti %x is null\n",
              rntiP);
  harq_information_rel10->delta_offset_harq = puschConfigDedicated->betaOffset_ACK_Index;
  harq_information_rel10->tl.tag = NFAPI_UL_CONFIG_REQUEST_ULSCH_HARQ_INFORMATION_REL10_TAG;
  struct LTE_PUCCH_ConfigDedicated *pucch_ConfigDedicated = physicalConfigDedicated->pucch_ConfigDedicated;
  AssertFatal(pucch_ConfigDedicated != NULL, "pucch_ConfigDedicated is null!\n");

  if (pucch_ConfigDedicated->tdd_AckNackFeedbackMode != NULL &&
      *pucch_ConfigDedicated->tdd_AckNackFeedbackMode == LTE_PUCCH_ConfigDedicated__tdd_AckNackFeedbackMode_multiplexing)
    harq_information_rel10->ack_nack_mode = 1; // multiplexing
  else
    harq_information_rel10->ack_nack_mode = 0; // bundling

  switch (get_tmode(module_idP, CC_idP, UE_id)) {
    case 1:
    case 2:
    case 5:
    case 6:
    case 7:
      if (cc->tdd_Config == NULL) // FDD
        harq_information_rel10->harq_size = 1;
      else {
        if (harq_information_rel10->ack_nack_mode == 1)
          harq_information_rel10->harq_size = get_V_UL_DAI(module_idP,
                                              CC_idP,
                                              rntiP,
                                              subframeP);
        else
          harq_information_rel10->harq_size = 1;
      }

      break;

    default:      // for any other TM we need 2 bits harq
      if (cc->tdd_Config == NULL) {
        harq_information_rel10->harq_size = 2;
      } else {
        if (harq_information_rel10->ack_nack_mode == 1)
          harq_information_rel10->harq_size = get_V_UL_DAI(module_idP,
                                              CC_idP,
                                              rntiP,
                                              subframeP);
        else
          harq_information_rel10->harq_size = 2;
      }

      break;
  }       // get Tmode

  return;
}

//------------------------------------------------------------------------------
const uint8_t Np[6][4] = {{0, 1, 3, 5}, {0, 3, 8, 13}, {0, 5, 13, 22}, {0, 11, 27, 44}, {0, 16, 41, 66}, {0, 22, 55, 88}};
//------------------------------------------------------------------------------

// This is part of the PUCCH allocation procedure (see Section 10.1 36.213)
//------------------------------------------------------------------------------
uint16_t
getNp(int dl_Bandwidth,
      uint8_t nCCE,
      uint8_t plus1)
//------------------------------------------------------------------------------
{
  AssertFatal(dl_Bandwidth < 6, "dl_Bandwidth %d>5\n", dl_Bandwidth);

  if (nCCE >= Np[dl_Bandwidth][2]) {
    return(Np[dl_Bandwidth][2+plus1]);
  }

  if (nCCE >= Np[dl_Bandwidth][1]) {
    return(Np[dl_Bandwidth][1+plus1]);
  }

  return(Np[dl_Bandwidth][0+plus1]);
}

//------------------------------------------------------------------------------
void
fill_nfapi_harq_information(module_id_t                      module_idP,
                            int                              CC_idP,
                            uint16_t                         rntiP,
                            nfapi_ul_config_harq_information *harq_information,
                            uint8_t                          cce_idxP)
//------------------------------------------------------------------------------
{
  eNB_MAC_INST *eNB     = RC.mac[module_idP];
  COMMON_channels_t *cc = &eNB->common_channels[CC_idP];
  UE_info_t *UE_info    = &eNB->UE_info;
  int UE_id = find_UE_id(module_idP,
                         rntiP);
  AssertFatal(UE_id >= 0, "UE_id cannot be found, impossible\n");
  AssertFatal(UE_info != NULL, "UE_info is null\n");
  harq_information->harq_information_rel11.tl.tag        = NFAPI_UL_CONFIG_REQUEST_HARQ_INFORMATION_REL11_TAG;
  harq_information->harq_information_rel11.num_ant_ports = 1;
  LTE_PhysicalConfigDedicated_t *physicalConfigDedicated = UE_info->UE_template[CC_idP][UE_id].physicalConfigDedicated;
  struct LTE_PUCCH_ConfigDedicated *pucch_ConfigDedicated = NULL;

  if (physicalConfigDedicated != NULL) pucch_ConfigDedicated = physicalConfigDedicated->pucch_ConfigDedicated;

  switch (get_tmode(module_idP,
                    CC_idP,
                    UE_id)) {
    case 1:
    case 2:
    case 5:
    case 6:
    case 7:
      if (cc->tdd_Config != NULL) {
        //      AssertFatal(UE_info->UE_template[CC_idP][UE_id].physicalConfigDedicated->pucch_ConfigDedicated != NULL,
        //      "pucch_ConfigDedicated is null for TDD!\n");
        if (physicalConfigDedicated != NULL && pucch_ConfigDedicated != NULL &&
            pucch_ConfigDedicated->tdd_AckNackFeedbackMode != NULL &&
            *pucch_ConfigDedicated->tdd_AckNackFeedbackMode == LTE_PUCCH_ConfigDedicated__tdd_AckNackFeedbackMode_multiplexing) {
          harq_information->harq_information_rel10_tdd.harq_size             = 2;        // 2-bit ACK/NAK
          harq_information->harq_information_rel10_tdd.ack_nack_mode         = 1;        // multiplexing
        } else {
          harq_information->harq_information_rel10_tdd.harq_size             = 1;        // 1-bit ACK/NAK
          harq_information->harq_information_rel10_tdd.ack_nack_mode         = 0;        // bundling
        }

        harq_information->harq_information_rel10_tdd.tl.tag                    = NFAPI_UL_CONFIG_REQUEST_HARQ_INFORMATION_REL10_TDD_TAG;
        harq_information->harq_information_rel10_tdd.n_pucch_1_0
          = getNp(cc->mib->message.dl_Bandwidth, cce_idxP, 0) + cc->radioResourceConfigCommon->pucch_ConfigCommon.n1PUCCH_AN + cce_idxP;
        harq_information->harq_information_rel10_tdd.number_of_pucch_resources = 1;
      } else {
        harq_information->harq_information_rel9_fdd.tl.tag                     = NFAPI_UL_CONFIG_REQUEST_HARQ_INFORMATION_REL9_FDD_TAG;
        harq_information->harq_information_rel9_fdd.number_of_pucch_resources  = 1;
        harq_information->harq_information_rel9_fdd.harq_size                  = 1; // 1-bit ACK/NAK
        harq_information->harq_information_rel9_fdd.n_pucch_1_0                = cc->radioResourceConfigCommon->pucch_ConfigCommon.n1PUCCH_AN + cce_idxP;
      }

      break;

    default:      // for any other TM we need 2 bits harq
      if (cc->tdd_Config != NULL) {
        AssertFatal(pucch_ConfigDedicated != NULL, "pucch_ConfigDedicated is null for TDD!\n");

        if (pucch_ConfigDedicated->tdd_AckNackFeedbackMode != NULL &&
            *pucch_ConfigDedicated->tdd_AckNackFeedbackMode == LTE_PUCCH_ConfigDedicated__tdd_AckNackFeedbackMode_multiplexing) {
          harq_information->harq_information_rel10_tdd.ack_nack_mode            = 1;  // multiplexing
        } else {
          harq_information->harq_information_rel10_tdd.ack_nack_mode            = 0;  // bundling
        }

        harq_information->harq_information_rel10_tdd.tl.tag                     = NFAPI_UL_CONFIG_REQUEST_HARQ_INFORMATION_REL10_TDD_TAG;
        harq_information->harq_information_rel10_tdd.harq_size                  = 2;
        harq_information->harq_information_rel10_tdd.n_pucch_1_0                = cc->radioResourceConfigCommon->pucch_ConfigCommon.n1PUCCH_AN + cce_idxP;
        harq_information->harq_information_rel10_tdd.number_of_pucch_resources  = 1;
      } else {
        harq_information->harq_information_rel9_fdd.tl.tag                      = NFAPI_UL_CONFIG_REQUEST_HARQ_INFORMATION_REL9_FDD_TAG;
        harq_information->harq_information_rel9_fdd.number_of_pucch_resources   = 1;
        harq_information->harq_information_rel9_fdd.ack_nack_mode               = 0;  // 1a/b
        harq_information->harq_information_rel9_fdd.harq_size                   = 2;
        harq_information->harq_information_rel9_fdd.n_pucch_1_0                 = cc->radioResourceConfigCommon->pucch_ConfigCommon.n1PUCCH_AN + cce_idxP;
      }

      break;
  }       // get Tmode

  return;
}

//------------------------------------------------------------------------------
uint16_t
fill_nfapi_uci_acknak(module_id_t module_idP,
                      int         CC_idP,
                      uint16_t    rntiP,
                      uint16_t    absSFP,
                      uint8_t     cce_idxP)
//------------------------------------------------------------------------------
{
  eNB_MAC_INST                   *eNB           = RC.mac[module_idP];
  COMMON_channels_t              *cc            = &eNB->common_channels[CC_idP];
  int                            ackNAK_absSF   = get_pucch1_absSF(cc, absSFP);
  nfapi_ul_config_request_t      *ul_req        = &eNB->UL_req_tmp[CC_idP][ackNAK_absSF % 10];
  nfapi_ul_config_request_body_t *ul_req_body   = &ul_req->ul_config_request_body;
  nfapi_ul_config_request_pdu_t  *ul_config_pdu = &ul_req_body->ul_config_pdu_list[ul_req_body->number_of_pdus];
  memset((void *) ul_config_pdu, 0, sizeof(nfapi_ul_config_request_pdu_t));
  ul_config_pdu->pdu_type                                               = NFAPI_UL_CONFIG_UCI_HARQ_PDU_TYPE;
  ul_config_pdu->pdu_size                                               = (uint8_t) (2 + sizeof(nfapi_ul_config_uci_harq_pdu));
  ul_config_pdu->uci_harq_pdu.ue_information.ue_information_rel8.tl.tag = NFAPI_UL_CONFIG_REQUEST_UE_INFORMATION_REL8_TAG;
  ul_config_pdu->uci_harq_pdu.ue_information.ue_information_rel8.handle = 0;  // don't know how to use this
  ul_config_pdu->uci_harq_pdu.ue_information.ue_information_rel8.rnti   = rntiP;
  fill_nfapi_harq_information(module_idP,
                              CC_idP,
                              rntiP,
                              &ul_config_pdu->uci_harq_pdu.harq_information,
                              cce_idxP);
  LOG_D(MAC, "Filled in UCI HARQ request for rnti %x SF %d.%d acknakSF %d.%d, cce_idxP %d-> n1_pucch %d\n",
        rntiP,
        absSFP / 10,
        absSFP % 10,
        ackNAK_absSF / 10,
        ackNAK_absSF % 10,
        cce_idxP,
        ul_config_pdu->uci_harq_pdu.harq_information.harq_information_rel9_fdd.n_pucch_1_0);
  ul_req_body->number_of_pdus++;
  ul_req_body->tl.tag       = NFAPI_UL_CONFIG_REQUEST_BODY_TAG;
  ul_req->header.message_id = NFAPI_UL_CONFIG_REQUEST;
  ul_req->sfn_sf            = (ackNAK_absSF/10) << 4 | ackNAK_absSF%10;
  return (((ackNAK_absSF / 10) << 4) + (ackNAK_absSF % 10));
}

//------------------------------------------------------------------------------

void
fill_nfapi_mch_config(nfapi_dl_config_request_body_t *dl_req,
                  uint16_t length,
                  uint16_t pdu_index,
                  uint16_t rnti,
                  uint8_t resource_allocation_type,
                  uint16_t resource_block_coding,
                  uint8_t modulation,
                  uint16_t transmission_power,
                  uint8_t mbsfn_area_id){
  nfapi_dl_config_request_pdu_t *dl_config_pdu =
    &dl_req->dl_config_pdu_list[dl_req->number_pdu];
  memset((void *) dl_config_pdu, 0,
         sizeof(nfapi_dl_config_request_pdu_t));
  dl_config_pdu->pdu_type                                                    = NFAPI_DL_CONFIG_MCH_PDU_TYPE;
  dl_config_pdu->pdu_size                                                    = (uint8_t) (2 + sizeof(nfapi_dl_config_mch_pdu));
  dl_config_pdu->mch_pdu.mch_pdu_rel8.tl.tag                                 = NFAPI_DL_CONFIG_REQUEST_MCH_PDU_REL8_TAG;
  dl_config_pdu->mch_pdu.mch_pdu_rel8.length                                 = length;
  dl_config_pdu->mch_pdu.mch_pdu_rel8.pdu_index                              = pdu_index;
  dl_config_pdu->mch_pdu.mch_pdu_rel8.rnti                                   = rnti;
  dl_config_pdu->mch_pdu.mch_pdu_rel8.resource_allocation_type               = resource_allocation_type;
  dl_config_pdu->mch_pdu.mch_pdu_rel8.resource_block_coding                  = resource_block_coding;
  dl_config_pdu->mch_pdu.mch_pdu_rel8.modulation                             = modulation;
  dl_config_pdu->mch_pdu.mch_pdu_rel8.transmission_power                     = transmission_power;
  dl_config_pdu->mch_pdu.mch_pdu_rel8.mbsfn_area_id                          = mbsfn_area_id;
  dl_req->number_pdu++;
}

//------------------------------------------------------------------------------


//------------------------------------------------------------------------------
void
fill_nfapi_dlsch_config(nfapi_dl_config_request_pdu_t *dl_config_pdu,
                        uint16_t length,
                        int16_t pdu_index,
                        uint16_t rnti,
                        uint8_t resource_allocation_type,
                        uint8_t
                        virtual_resource_block_assignment_flag,
                        uint32_t resource_block_coding,
                        uint8_t modulation,
                        uint8_t redundancy_version,
                        uint8_t transport_blocks,
                        uint8_t transport_block_to_codeword_swap_flag,
                        uint8_t transmission_scheme,
                        uint8_t number_of_layers,
                        uint8_t number_of_subbands,
                        // uint8_t codebook_index,
                        uint8_t ue_category_capacity,
                        uint8_t pa,
                        uint8_t delta_power_offset_index,
                        uint8_t ngap,
                        uint8_t nprb,
                        uint8_t transmission_mode,
                        uint8_t num_bf_prb_per_subband,
                        uint8_t num_bf_vector)
//------------------------------------------------------------------------------
{
  memset((void *) dl_config_pdu, 0, sizeof(nfapi_dl_config_request_pdu_t));
  dl_config_pdu->pdu_type                                                        = NFAPI_DL_CONFIG_DLSCH_PDU_TYPE;
  dl_config_pdu->pdu_size                                                        = (uint8_t) (2 + sizeof(nfapi_dl_config_dlsch_pdu));
  dl_config_pdu->dlsch_pdu.dlsch_pdu_rel8.tl.tag                                 = NFAPI_DL_CONFIG_REQUEST_DLSCH_PDU_REL8_TAG;
  dl_config_pdu->dlsch_pdu.dlsch_pdu_rel8.length                                 = length;
  dl_config_pdu->dlsch_pdu.dlsch_pdu_rel8.pdu_index                              = pdu_index;
  dl_config_pdu->dlsch_pdu.dlsch_pdu_rel8.rnti                                   = rnti;
  dl_config_pdu->dlsch_pdu.dlsch_pdu_rel8.resource_allocation_type               = resource_allocation_type;
  dl_config_pdu->dlsch_pdu.dlsch_pdu_rel8.virtual_resource_block_assignment_flag = virtual_resource_block_assignment_flag;
  dl_config_pdu->dlsch_pdu.dlsch_pdu_rel8.resource_block_coding                  = resource_block_coding;
  dl_config_pdu->dlsch_pdu.dlsch_pdu_rel8.modulation                             = modulation;
  dl_config_pdu->dlsch_pdu.dlsch_pdu_rel8.redundancy_version                     = redundancy_version;
  dl_config_pdu->dlsch_pdu.dlsch_pdu_rel8.transport_blocks                       = transport_blocks;
  dl_config_pdu->dlsch_pdu.dlsch_pdu_rel8.transport_block_to_codeword_swap_flag  = transport_block_to_codeword_swap_flag;
  dl_config_pdu->dlsch_pdu.dlsch_pdu_rel8.transmission_scheme                    = transmission_scheme;
  dl_config_pdu->dlsch_pdu.dlsch_pdu_rel8.number_of_layers                       = number_of_layers;
  dl_config_pdu->dlsch_pdu.dlsch_pdu_rel8.number_of_subbands                     = number_of_subbands;
  // dl_config_pdu->dlsch_pdu.dlsch_pdu_rel8.codebook_index                         = codebook_index;
  dl_config_pdu->dlsch_pdu.dlsch_pdu_rel8.ue_category_capacity                   = ue_category_capacity;
  dl_config_pdu->dlsch_pdu.dlsch_pdu_rel8.pa                                     = pa;
  dl_config_pdu->dlsch_pdu.dlsch_pdu_rel8.delta_power_offset_index               = delta_power_offset_index;
  dl_config_pdu->dlsch_pdu.dlsch_pdu_rel8.ngap                                   = ngap;
  dl_config_pdu->dlsch_pdu.dlsch_pdu_rel8.nprb                                   = nprb;
  dl_config_pdu->dlsch_pdu.dlsch_pdu_rel8.transmission_mode                      = transmission_mode;
  dl_config_pdu->dlsch_pdu.dlsch_pdu_rel8.num_bf_prb_per_subband                 = num_bf_prb_per_subband;
  dl_config_pdu->dlsch_pdu.dlsch_pdu_rel8.num_bf_vector                          = num_bf_vector;
  return;
}

//------------------------------------------------------------------------------
uint16_t
fill_nfapi_tx_req(nfapi_tx_request_body_t *tx_req_body,
                  uint16_t                absSF,
                  uint16_t                pdu_length,
                  int16_t                 pdu_index,
                  uint8_t                 *pdu)
//------------------------------------------------------------------------------
{
  nfapi_tx_request_pdu_t *TX_req = &tx_req_body->tx_pdu_list[tx_req_body->number_of_pdus];
  LOG_D(MAC, "Filling TX_req %d for pdu length %d\n",
        tx_req_body->number_of_pdus,
        pdu_length);
  TX_req->pdu_length                 = pdu_length;
  TX_req->pdu_index                  = pdu_index;
  TX_req->num_segments               = 1;
  TX_req->segments[0].segment_length = pdu_length;
  TX_req->segments[0].segment_data   = pdu;
  tx_req_body->tl.tag                = NFAPI_TX_REQUEST_BODY_TAG;
  tx_req_body->number_of_pdus++;
  return (((absSF / 10) << 4) + (absSF % 10));
}

//------------------------------------------------------------------------------
void
fill_nfapi_ulsch_config_request_rel8(nfapi_ul_config_request_pdu_t *ul_config_pdu,
                                     uint8_t                        cqi_req,
                                     COMMON_channels_t              *cc,
                                     struct LTE_PhysicalConfigDedicated *physicalConfigDedicated,
                                     uint8_t                        tmode,
                                     uint32_t                       handle,
                                     uint16_t                       rnti,
                                     uint8_t                        resource_block_start,
                                     uint8_t                        number_of_resource_blocks,
                                     uint8_t                        mcs,
                                     uint8_t                        cyclic_shift_2_for_drms,
                                     uint8_t                        frequency_hopping_enabled_flag,
                                     uint8_t                        frequency_hopping_bits,
                                     uint8_t                        new_data_indication,
                                     uint8_t                        redundancy_version,
                                     uint8_t                        harq_process_number,
                                     uint8_t                        ul_tx_mode,
                                     uint8_t                        current_tx_nb,
                                     uint8_t                        n_srs,
                                     uint16_t                       size)
//------------------------------------------------------------------------------
{
  uint8_t ri_size = 0;
  memset((void *) ul_config_pdu, 0, sizeof(nfapi_ul_config_request_pdu_t));
  ul_config_pdu->pdu_type                                                    = NFAPI_UL_CONFIG_ULSCH_PDU_TYPE;
  ul_config_pdu->pdu_size                                                    = (uint8_t) (2 + sizeof(nfapi_ul_config_ulsch_pdu));
  ul_config_pdu->ulsch_pdu.ulsch_pdu_rel8.tl.tag                             = NFAPI_UL_CONFIG_REQUEST_ULSCH_PDU_REL8_TAG;
  ul_config_pdu->ulsch_pdu.ulsch_pdu_rel8.handle                             = handle;
  ul_config_pdu->ulsch_pdu.ulsch_pdu_rel8.rnti                               = rnti;
  ul_config_pdu->ulsch_pdu.ulsch_pdu_rel8.resource_block_start               = resource_block_start;
  ul_config_pdu->ulsch_pdu.ulsch_pdu_rel8.number_of_resource_blocks          = number_of_resource_blocks;

  if (mcs < 11)      ul_config_pdu->ulsch_pdu.ulsch_pdu_rel8.modulation_type = 2;
  else if (mcs < 21) ul_config_pdu->ulsch_pdu.ulsch_pdu_rel8.modulation_type = 4;
  else if(mcs < 29)  ul_config_pdu->ulsch_pdu.ulsch_pdu_rel8.modulation_type = 6;
  else               ul_config_pdu->ulsch_pdu.ulsch_pdu_rel8.modulation_type = 0;

  ul_config_pdu->ulsch_pdu.ulsch_pdu_rel8.cyclic_shift_2_for_drms            = cyclic_shift_2_for_drms;
  ul_config_pdu->ulsch_pdu.ulsch_pdu_rel8.frequency_hopping_enabled_flag     = frequency_hopping_enabled_flag;
  ul_config_pdu->ulsch_pdu.ulsch_pdu_rel8.frequency_hopping_bits             = frequency_hopping_bits;
  ul_config_pdu->ulsch_pdu.ulsch_pdu_rel8.new_data_indication                = new_data_indication;
  ul_config_pdu->ulsch_pdu.ulsch_pdu_rel8.redundancy_version                 = redundancy_version;
  ul_config_pdu->ulsch_pdu.ulsch_pdu_rel8.harq_process_number                = harq_process_number;
  ul_config_pdu->ulsch_pdu.ulsch_pdu_rel8.ul_tx_mode                         = ul_tx_mode;
  ul_config_pdu->ulsch_pdu.ulsch_pdu_rel8.current_tx_nb                      = current_tx_nb;
  ul_config_pdu->ulsch_pdu.ulsch_pdu_rel8.n_srs                              = n_srs;
  ul_config_pdu->ulsch_pdu.ulsch_pdu_rel8.size                               = size;

  if (cqi_req == 1) {
    // Add CQI portion
    ul_config_pdu->pdu_type = NFAPI_UL_CONFIG_ULSCH_CQI_RI_PDU_TYPE;
    ul_config_pdu->pdu_size = (uint8_t) (2 + sizeof(nfapi_ul_config_ulsch_cqi_ri_pdu));
    ul_config_pdu->ulsch_cqi_ri_pdu.cqi_ri_information.cqi_ri_information_rel9.tl.tag = NFAPI_UL_CONFIG_REQUEST_CQI_RI_INFORMATION_REL9_TAG;
    ul_config_pdu->ulsch_cqi_ri_pdu.cqi_ri_information.cqi_ri_information_rel9.report_type = 1;
    ul_config_pdu->ulsch_cqi_ri_pdu.cqi_ri_information.cqi_ri_information_rel9.aperiodic_cqi_pmi_ri_report.number_of_cc = 1;
    LOG_D(MAC, "report_type %d\n",
          ul_config_pdu->ulsch_cqi_ri_pdu.cqi_ri_information.cqi_ri_information_rel9.report_type);

    if (cc->p_eNB <= 2 && (tmode == 3 || tmode == 4 || tmode == 8 || tmode == 9 || tmode == 10)) ri_size = 1;
    else if (cc->p_eNB <= 2) ri_size = 0;
    else if (cc->p_eNB == 4) ri_size = 2;

    ul_config_pdu->ulsch_cqi_ri_pdu.cqi_ri_information.cqi_ri_information_rel9.aperiodic_cqi_pmi_ri_report.cc[0].ri_size = ri_size;
    AssertFatal(physicalConfigDedicated->cqi_ReportConfig != NULL,"physicalConfigDedicated->cqi_ReportConfig is null!\n");
    AssertFatal(physicalConfigDedicated->cqi_ReportConfig->cqi_ReportModeAperiodic != NULL,"physicalConfigDedicated->cqi_ReportModeAperiodic is null!\n");
    AssertFatal(physicalConfigDedicated->pusch_ConfigDedicated != NULL,"physicalConfigDedicated->puschConfigDedicated is null!\n");
    nfapi_ul_config_cqi_ri_information_rel9_t *ri_information = &ul_config_pdu->ulsch_cqi_ri_pdu.cqi_ri_information.cqi_ri_information_rel9;
    int max_ri = (1 << ri_information->aperiodic_cqi_pmi_ri_report.cc[0].ri_size);

    for (int ri = 0; ri < max_ri; ri++) {
      ri_information->aperiodic_cqi_pmi_ri_report.cc[0].dl_cqi_pmi_size[ri]
        = get_dl_cqi_pmi_size_pusch(cc,
                                    tmode,
                                    1 + ri,
                                    physicalConfigDedicated->cqi_ReportConfig->cqi_ReportModeAperiodic);
    }

    ri_information->delta_offset_cqi = physicalConfigDedicated->pusch_ConfigDedicated->betaOffset_CQI_Index;
    ri_information->delta_offset_ri = physicalConfigDedicated->pusch_ConfigDedicated->betaOffset_RI_Index;
  }

  return;
}

//------------------------------------------------------------------------------
void
fill_nfapi_ulsch_config_request_emtc(nfapi_ul_config_request_pdu_t *ul_config_pdu,
                                     uint8_t ue_type,
                                     uint16_t total_number_of_repetitions,
                                     uint16_t repetition_number,
                                     uint16_t initial_transmission_sf_io)
//------------------------------------------------------------------------------
{
  // Re13 fields
  ul_config_pdu->ulsch_pdu.ulsch_pdu_rel13.tl.tag                      = NFAPI_UL_CONFIG_REQUEST_ULSCH_PDU_REL13_TAG;
  ul_config_pdu->ulsch_pdu.ulsch_pdu_rel13.ue_type                     = ue_type;
  ul_config_pdu->ulsch_pdu.ulsch_pdu_rel13.total_number_of_repetitions = total_number_of_repetitions;
  ul_config_pdu->ulsch_pdu.ulsch_pdu_rel13.repetition_number           = repetition_number;
  ul_config_pdu->ulsch_pdu.ulsch_pdu_rel13.initial_transmission_sf_io  = initial_transmission_sf_io;
  return;
}

//------------------------------------------------------------------------------
int
get_numnarrowbands(long dl_Bandwidth)
//------------------------------------------------------------------------------
{
  int nb_tab[6] = { 1, 2, 4, 8, 12, 16 };
  AssertFatal(dl_Bandwidth >= 0 && dl_Bandwidth < 6, "dl_Bandwidth %ld not in [0..5]\n", dl_Bandwidth);
  return (nb_tab[dl_Bandwidth]);
}

//------------------------------------------------------------------------------
int
get_numnarrowbandbits(long dl_Bandwidth)
//------------------------------------------------------------------------------
{
  int nbbits_tab[6] = { 0, 1, 2, 3, 4, 4 };
  AssertFatal(dl_Bandwidth >= 0 && dl_Bandwidth < 6, "dl_Bandwidth %ld not in [0..5]\n", dl_Bandwidth);
  return (nbbits_tab[dl_Bandwidth]);
}

//This implements the frame/subframe condition for first subframe of MPDCCH transmission (Section 9.1.5 36.213, Rel 13/14)
//------------------------------------------------------------------------------
static const int startSF_fdd_RA_times2[8] = {2, 3, 4, 5, 8, 10, 16, 20};
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
static const int startSF_tdd_RA[7] = {1, 2, 4, 5, 8, 10, 20};
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
int
mpdcch_sf_condition(eNB_MAC_INST *eNB,
                    int CC_id,
                    frame_t frameP,
                    sub_frame_t subframeP,
                    int rmax,
                    MPDCCH_TYPES_t mpdcch_type,
                    int UE_id)
//------------------------------------------------------------------------------
{
  struct LTE_PRACH_ConfigSIB_v1310 *ext4_prach = eNB->common_channels[CC_id].radioResourceConfigCommon_BR-> ext4->prach_ConfigCommon_v1310;
  int T;
  LTE_EPDCCH_SetConfig_r11_t *epdcch_setconfig_r11;

  switch (mpdcch_type) {
    case TYPE0:
      AssertFatal(1 == 0, "MPDCCH Type 0 not handled yet\n");
      break;

    case TYPE1:
      AssertFatal(1 == 0, "MPDCCH Type 1 not handled yet\n");
      break;

    case TYPE1A:
      AssertFatal(1 == 0, "MPDCCH Type 1A not handled yet\n");
      break;

    case TYPE2:   // RAR
      AssertFatal(ext4_prach->mpdcch_startSF_CSS_RA_r13 != NULL, "mpdcch_startSF_CSS_RA_r13 is null\n");
      AssertFatal(rmax > 0, "rmax is 0!\b");

      if (eNB->common_channels[CC_id].tdd_Config == NULL) //FDD
        T = (rmax * startSF_fdd_RA_times2[ext4_prach->mpdcch_startSF_CSS_RA_r13->choice.fdd_r13]) >> 1;
      else      //TDD
        T = rmax * startSF_tdd_RA[ext4_prach->mpdcch_startSF_CSS_RA_r13->choice.tdd_r13];

      break;

    case TYPE2A:
      AssertFatal(1 == 0, "MPDCCH Type 2A not handled yet\n");
      break;

    case TYPEUESPEC:
      epdcch_setconfig_r11 = eNB->UE_info.UE_template[CC_id][UE_id].physicalConfigDedicated->ext4->epdcch_Config_r11->config_r11.choice.setup.setConfigToAddModList_r11->list.array[0];
      AssertFatal(epdcch_setconfig_r11 != NULL, " epdcch_setconfig_r11 is null for UE specific \n");
      AssertFatal(epdcch_setconfig_r11->ext2 != NULL, " ext2 doesn't exist in epdcch config ' \n");

      if (eNB->common_channels[CC_id].tdd_Config == NULL) //FDD
        T = (rmax * startSF_fdd_RA_times2[epdcch_setconfig_r11->ext2->mpdcch_config_r13->choice.setup.mpdcch_StartSF_UESS_r13.choice.fdd_r13]) >> 1;
      else      //TDD
        T = rmax * startSF_tdd_RA[epdcch_setconfig_r11->ext2->mpdcch_config_r13->choice.setup.mpdcch_StartSF_UESS_r13.choice.tdd_r13];

      break;

    default:
      return 0;
  }

  AssertFatal(T > 0, "T is 0!\n");

  if (((10 * frameP) + subframeP) % T == 0) return 1;

  return 0;
}

//------------------------------------------------------------------------------
int
narrowband_to_first_rb(COMMON_channels_t *cc,
                       int nb_index)
//------------------------------------------------------------------------------
{
  switch (cc->mib->message.dl_Bandwidth) {
    case 0:     // 6 PRBs, N_NB=1, i_0=0
      return 0;

    case 3:     // 50 PRBs, N_NB=8, i_0=1
      return (1 + (6 * nb_index));

    case 5:     // 100 PRBs, N_NB=16, i_0=2
      return (2 + (6 * nb_index));

    case 1:     // 15 PRBs  N_NB=2, i_0=1
      if (nb_index > 0)
        return 1;

      return 0;

    case 2:     // 25 PRBs, N_NB=4, i_0=0
      if (nb_index > 1)
        return (1 + (6 * nb_index));

      return ((6 * nb_index));

    case 4:     // 75 PRBs, N_NB=12, i_0=1
      if (nb_index > 5)
        return (2 + (6 * nb_index));

      return (1 + (6 * nb_index));

    default:
      AssertFatal(1 == 0, "Impossible dl_Bandwidth %d\n",
                  (int) cc->mib->message.dl_Bandwidth);
      break;
  }

  return 0;
}
void
init_ue_sched_info(void)
//------------------------------------------------------------------------------
{
  module_id_t i, j, k;

  for (i = 0; i < NUMBER_OF_eNB_MAX; i++) {
    for (k = 0; k < MAX_NUM_CCs; k++) {
      for (j = 0; j < MAX_MOBILES_PER_ENB; j++) {
        eNB_dlsch_info[i][k][j].status = S_DL_NONE;
      }
    }
  }

  return;
}

//------------------------------------------------------------------------------
int
find_UE_id(module_id_t mod_idP,
           rnti_t rntiP)
//------------------------------------------------------------------------------
{
  UE_info_t *UE_info = &RC.mac[mod_idP]->UE_info;
  if(!UE_info)
    return -1;

  for (int UE_id = 0; UE_id < MAX_MOBILES_PER_ENB; UE_id++) {
    if (UE_info->active[UE_id] == true) {
      int CC_id = UE_PCCID(mod_idP, UE_id);
      if (CC_id>=0 && CC_id<NFAPI_CC_MAX && UE_info->UE_template[CC_id][UE_id].rnti == rntiP) {
        return UE_id;
      }
    }
  }

  return -1;
}

//------------------------------------------------------------------------------
int
find_RA_id(module_id_t mod_idP,
           int CC_idP,
           rnti_t rntiP)
//------------------------------------------------------------------------------
{
  int RA_id;
  AssertFatal(RC.mac[mod_idP], "RC.mac[%d] is null\n", mod_idP);
  RA_t *ra = (RA_t *) &RC.mac[mod_idP]->common_channels[CC_idP].ra[0];

  for (RA_id = 0; RA_id < NB_RA_PROC_MAX; RA_id++) {
    LOG_D(MAC, "Checking RA_id %d for %x : state %s\n", RA_id, rntiP, era_text[ra[RA_id].eRA_state]);

    if (ra[RA_id].eRA_state != IDLE && ra[RA_id].rnti == rntiP)
      return RA_id;
  }

  return -1;
}

//------------------------------------------------------------------------------
int
UE_num_active_CC(UE_info_t *listP,
                 int ue_idP)
//------------------------------------------------------------------------------
{
  return (listP->numactiveCCs[ue_idP]);
}

//------------------------------------------------------------------------------
int
UE_PCCID(module_id_t mod_idP,
         int ue_idP)
//------------------------------------------------------------------------------
{
  return (RC.mac[mod_idP]->UE_info.pCC_id[ue_idP]);
}

//------------------------------------------------------------------------------
rnti_t
UE_RNTI(module_id_t mod_idP,
        int ue_idP)
//------------------------------------------------------------------------------
{
  if (!RC.mac || !RC.mac[mod_idP]) return 0;

  rnti_t rnti = RC.mac[mod_idP]->UE_info.UE_template[UE_PCCID(mod_idP,
                ue_idP)][ue_idP].rnti;

  if (rnti > 0) {
    return (rnti);
  }

  //LOG_D(MAC, "[eNB %d] Couldn't find RNTI for UE %d\n", mod_idP, ue_idP);
  return (NOT_A_RNTI);
}

//------------------------------------------------------------------------------
bool
is_UE_active(module_id_t mod_idP,
             int ue_idP)
//------------------------------------------------------------------------------
{
  return (RC.mac[mod_idP]->UE_info.active[ue_idP]);
}

//------------------------------------------------------------------------------
unsigned char
get_aggregation(uint8_t bw_index,
                uint8_t cqi,
                uint8_t dci_fmt)
//------------------------------------------------------------------------------
{
  unsigned char aggregation = 3;

  switch (dci_fmt) {
    case format0:
      aggregation = cqi2fmt0_agg[bw_index][cqi];
      break;

    case format1:
    case format1A:
    case format1B:
    case format1D:
      aggregation = cqi2fmt1x_agg[bw_index][cqi];
      break;

    case format2:
    case format2A:
    case format2B:
    case format2C:
    case format2D:
      aggregation = cqi2fmt2x_agg[bw_index][cqi];
      break;

    case format1C:
    case format1E_2A_M10PRB:
    case format3:
    case format3A:
    case format4:
    default:
      LOG_W(MAC, "unsupported DCI format %d\n",
            dci_fmt);
      break;
  }

  LOG_D(MAC, "Aggregation level %d (cqi %d, bw_index %d, format %d)\n",
        1 << aggregation,
        cqi,
        bw_index,
        dci_fmt);
  return 1 << aggregation;
}

//------------------------------------------------------------------------------
/*
 * Dump the UE_list into LOG_T(MAC)
 */
void
dump_ue_list(UE_list_t *listP) {
  for (int j = listP->head; j >= 0; j = listP->next[j])
    LOG_T(MAC, "DL list node %d => %d\n", j, listP->next[j]);
}

//------------------------------------------------------------------------------
/*
 * Add a UE to UE_list listP
 */
inline void add_ue_list(UE_list_t *listP, int UE_id) {
  int *cur = &listP->head;
  while (*cur >= 0)
    cur = &listP->next[*cur];
  *cur = UE_id;
  LOG_D(MAC, "added UE %d in UE list\n", UE_id);
}

//------------------------------------------------------------------------------
/*
 * Remove a UE from the UE_list listP
 */
inline int remove_ue_list(UE_list_t *listP, int UE_id) {
  int *cur = &listP->head;
  while (*cur != -1 && *cur != UE_id)
    cur = &listP->next[*cur];
  if (*cur == -1)
    return 0;
  int *next = &listP->next[*cur];
  *cur = listP->next[*cur];
  *next = -1;
  return 1;
}

//------------------------------------------------------------------------------
/*
 * Initialize the UE_list listP
 */
inline void init_ue_list(UE_list_t *listP) {
  listP->head = -1;
  for (int i = 0; i < MAX_MOBILES_PER_ENB; ++i)
    listP->next[i] = -1;
}

//------------------------------------------------------------------------------
int
add_new_ue(module_id_t mod_idP,
           int cc_idP,
           rnti_t rntiP,
           int harq_pidP,
           uint8_t rach_resource_type
          )
//------------------------------------------------------------------------------
{
  eNB_MAC_INST *eNB     = RC.mac[mod_idP];
  int UE_id;
  int i, j;
  UE_info_t *UE_info = &RC.mac[mod_idP]->UE_info;
  LOG_D(MAC, "[eNB %d, CC_id %d] Adding UE with rnti %x (prev. num_UEs %d)\n",
        mod_idP,
        cc_idP,
        rntiP,
        UE_info->num_UEs);

  for (i = 0; i < MAX_MOBILES_PER_ENB; i++) {
    if (UE_info->active[i] == true)
      continue;

    UE_id = i;
    memset(&UE_info->UE_template[cc_idP][UE_id], 0, sizeof(UE_TEMPLATE));
    UE_info->UE_template[cc_idP][UE_id].rnti = rntiP;
    UE_info->UE_template[cc_idP][UE_id].configured = false;
    UE_info->numactiveCCs[UE_id] = 1;
    UE_info->numactiveULCCs[UE_id] = 1;
    UE_info->pCC_id[UE_id] = cc_idP;
    UE_info->ordered_CCids[0][UE_id] = cc_idP;
    UE_info->ordered_ULCCids[0][UE_id] = cc_idP;
    UE_info->num_UEs++;
    UE_info->active[UE_id] = true;
    add_ue_list(&UE_info->list, UE_id);
    dump_ue_list(&UE_info->list);
    pp_impl_param_t* dl = &RC.mac[mod_idP]->pre_processor_dl;
    if (dl->slices) // inform slice implementation about new UE
      dl->add_UE(dl->slices, UE_id);
    pp_impl_param_t* ul = &RC.mac[mod_idP]->pre_processor_ul;
    if (ul->slices) // inform slice implementation about new UE
      ul->add_UE(ul->slices, UE_id);
    if (IS_SOFTMODEM_IQPLAYER)// not specific to record/playback ?
      UE_info->UE_template[cc_idP][UE_id].pre_assigned_mcs_ul = 0;
    UE_info->UE_template[cc_idP][UE_id].rach_resource_type = rach_resource_type;
    memset((void *) &UE_info->UE_sched_ctrl[UE_id],
           0,
           sizeof(UE_sched_ctrl_t));
    memset((void *) &UE_info->eNB_UE_stats[cc_idP][UE_id],
           0,
           sizeof(eNB_UE_STATS));
    UE_info->UE_sched_ctrl[UE_id].ue_reestablishment_reject_timer = 0;
    UE_info->UE_sched_ctrl[UE_id].ta_update_f = 31.0;
    UE_info->UE_sched_ctrl[UE_id].ta_update = 31;
    UE_info->UE_sched_ctrl[UE_id].pusch_snr[cc_idP] = 0;
    UE_info->UE_sched_ctrl[UE_id].pusch_cqi_f[cc_idP]     = (eNB->puSch10xSnr+640)/5;
    UE_info->UE_sched_ctrl[UE_id].pusch_cqi[cc_idP]     = (eNB->puSch10xSnr+640)/5;
    UE_info->UE_sched_ctrl[UE_id].pusch_snr_avg[cc_idP] = eNB->puSch10xSnr/10;
    UE_info->UE_sched_ctrl[UE_id].pusch_rx_num[cc_idP] = 0;
    UE_info->UE_sched_ctrl[UE_id].pusch_rx_num_old[cc_idP] = 0;
    UE_info->UE_sched_ctrl[UE_id].pusch_rx_error_num[cc_idP] = 0;
    UE_info->UE_sched_ctrl[UE_id].pusch_rx_error_num_old[cc_idP] = 0;
    UE_info->UE_sched_ctrl[UE_id].pusch_bler[cc_idP] = 0;
    UE_info->UE_sched_ctrl[UE_id].ret_cnt[cc_idP] = 0;
    UE_info->UE_sched_ctrl[UE_id].first_cnt[cc_idP] = 0;

    for (j = 0; j < 8; j++) {
      UE_info->UE_template[cc_idP][UE_id].oldNDI[j] = 0;
      UE_info->UE_template[cc_idP][UE_id].oldNDI_UL[j] = 0;
      UE_info->UE_sched_ctrl[UE_id].round[cc_idP][j] = 8;
      UE_info->UE_sched_ctrl[UE_id].round_UL[cc_idP][j] = 0;
    }
    eNB_dlsch_info[mod_idP][cc_idP][UE_id].status = S_DL_NONE;
    LOG_D(MAC, "[eNB %d] Add UE_id %d on Primary CC_id %d: rnti %x\n",
          mod_idP,
          UE_id,
          cc_idP,
          rntiP);
    return (UE_id);
  }

  LOG_E(MAC, "error in add_new_ue(), could not find space in UE_list, Dumping UE list\n");
  dump_ue_list(&UE_info->list);
  return -1;
}

//------------------------------------------------------------------------------
/*
 * Remove MAC context of UE
 */
int
rrc_mac_remove_ue(module_id_t mod_idP,
                  rnti_t rntiP)
//------------------------------------------------------------------------------
{
  UE_info_t *UE_info = &RC.mac[mod_idP]->UE_info;
  int UE_id = find_UE_id(mod_idP, rntiP);
  eNB_UE_STATS *ue_stats = NULL;
  int pCC_id = -1;

  if (UE_id == -1) {
    LOG_W(MAC,"rrc_mac_remove_ue: UE %x not found\n",
          rntiP);
    return 0;
  }

  pCC_id = UE_PCCID(mod_idP, UE_id);
  LOG_I(MAC,"Removing UE %d from Primary CC_id %d (rnti %x)\n",
        UE_id,
        pCC_id,
        rntiP);
  UE_info->active[UE_id] = false;
  UE_info->num_UEs--;

  remove_ue_list(&UE_info->list, UE_id);
  pp_impl_param_t* dl = &RC.mac[mod_idP]->pre_processor_dl;
  if (dl->slices) // inform slice implementation about new UE
    dl->remove_UE(dl->slices, UE_id);
  pp_impl_param_t* ul = &RC.mac[mod_idP]->pre_processor_ul;
  if (ul->slices) // inform slice implementation about new UE
    ul->remove_UE(ul->slices, UE_id);

  /* Clear all remaining pending transmissions */
  memset(&UE_info->UE_template[pCC_id][UE_id], 0, sizeof(UE_TEMPLATE));
  ue_stats = &UE_info->eNB_UE_stats[pCC_id][UE_id];
  ue_stats->total_rbs_used = 0;
  ue_stats->total_rbs_used_retx = 0;

  for (int j = 0; j < NB_RB_MAX; j++ ) {
    ue_stats->num_pdu_tx[j] = 0;
    ue_stats->num_bytes_tx[j] = 0;
  }

  ue_stats->num_retransmission = 0;
  ue_stats->total_sdu_bytes = 0;
  ue_stats->total_pdu_bytes = 0;
  ue_stats->total_num_pdus = 0;
  ue_stats->total_rbs_used_rx = 0;

  for (int j = 0; j < NB_RB_MAX; j++ ) {
    ue_stats->num_pdu_rx[j] = 0;
    ue_stats->num_bytes_rx[j] = 0;
  }

  ue_stats->num_errors_rx = 0;
  ue_stats->total_pdu_bytes_rx = 0;
  ue_stats->total_num_pdus_rx = 0;
  ue_stats->total_num_errors_rx = 0;
  eNB_dlsch_info[mod_idP][pCC_id][UE_id].status = S_DL_NONE;
  // check if this has an RA process active
  if (find_RA_id(mod_idP,
                 pCC_id,
                 rntiP) != -1) {
    cancel_ra_proc(mod_idP,
                   pCC_id,
                   0,
                   rntiP);
  }

  if(rrc_release_info.num_UEs > 0) {
    while(pthread_mutex_trylock(&rrc_release_freelist)) {
      /* spin... */
    }

    uint16_t release_total = 0;

    for (uint16_t release_num = 0; release_num < NUMBER_OF_UE_MAX; release_num++) {
      if (rrc_release_info.RRC_release_ctrl[release_num].flag > 0) {
        release_total++;
      } else {
        continue;
      }

      if (rrc_release_info.RRC_release_ctrl[release_num].rnti == rntiP) {
        rrc_release_info.RRC_release_ctrl[release_num].flag = 0;
        rrc_release_info.num_UEs--;
        release_total--;
      }

      if (release_total >= rrc_release_info.num_UEs) {
        break;
      }
    }

    pthread_mutex_unlock(&rrc_release_freelist);
  }

  pthread_mutex_unlock(&rrc_release_freelist);
  return 0;
}


// This has to be updated to include BSR information
//------------------------------------------------------------------------------
uint8_t
UE_is_to_be_scheduled(module_id_t module_idP,
                      int CC_id,
                      uint8_t UE_id)
//------------------------------------------------------------------------------
{
  UE_TEMPLATE *UE_template = &RC.mac[module_idP]->UE_info.UE_template[CC_id][UE_id];
  UE_sched_ctrl_t *UE_sched_ctl = &RC.mac[module_idP]->UE_info.UE_sched_ctrl[UE_id];
  int rrc_status;

  // do not schedule UE if UL is not working
  if (UE_sched_ctl->ul_failure_timer > 0 || UE_sched_ctl->ul_out_of_sync > 0)
    return 0;

  rnti_t ue_rnti = UE_RNTI(module_idP, UE_id);
  LOG_D(MAC, "[eNB %d][PUSCH] Checking UL requirements UE %d/%x\n",
        module_idP,
        UE_id,
        ue_rnti);

  rrc_status = mac_eNB_get_rrc_status(module_idP, ue_rnti);

  if (UE_template->scheduled_ul_bytes < UE_template->estimated_ul_buffer ||
      UE_template->ul_SR > 0 || // uplink scheduling request
      (UE_sched_ctl->ul_inactivity_timer > 19 && UE_sched_ctl->ul_scheduled == 0) ||  // every 2 frames when RRC_CONNECTED
      (UE_sched_ctl->ul_inactivity_timer > 10 &&
       UE_sched_ctl->ul_scheduled == 0 && rrc_status < RRC_CONNECTED) || // every Frame when not RRC_CONNECTED
      (UE_sched_ctl->cqi_req_timer > 300 && rrc_status >= RRC_CONNECTED)) { // cqi req timer expired long ago (do not put too low value)
    LOG_D(MAC, "[eNB %d][PUSCH] UE %d/%x should be scheduled (BSR0 estimated size %d, SR %d)\n",
          module_idP,
          UE_id,
          ue_rnti,
          UE_template->ul_buffer_info[LCGID0],
          UE_template->ul_SR);
    return 1;
  }

  return 0;
}

//------------------------------------------------------------------------------
uint8_t
get_tmode(module_id_t module_idP,
          int CC_idP,
          int UE_idP)
//------------------------------------------------------------------------------
{
  eNB_MAC_INST *eNB = RC.mac[module_idP];
  COMMON_channels_t *cc = &eNB->common_channels[CC_idP];
  struct LTE_PhysicalConfigDedicated *physicalConfigDedicated = eNB->UE_info.UE_template[CC_idP][UE_idP].physicalConfigDedicated;

  if (physicalConfigDedicated == NULL) {  // RRCConnectionSetup not received by UE yet
    AssertFatal(cc->p_eNB <= 2, "p_eNB is %d, should be <2\n",
                cc->p_eNB);
    return (cc->p_eNB);
  }

  AssertFatal(physicalConfigDedicated->antennaInfo != NULL,
              "antennaInfo (mod_id %d) is null for CCId %d, UEid %d, physicalConfigDedicated %p\n",
              module_idP,
              CC_idP,
              UE_idP,
              physicalConfigDedicated);
  AssertFatal(physicalConfigDedicated->antennaInfo->present != LTE_PhysicalConfigDedicated__antennaInfo_PR_NOTHING,
              "antennaInfo (mod_id %d, CC_id %d) is set to NOTHING\n",
              module_idP,
              CC_idP);

  if (physicalConfigDedicated->antennaInfo->present == LTE_PhysicalConfigDedicated__antennaInfo_PR_explicitValue) {
    return (1 + physicalConfigDedicated->antennaInfo->choice.explicitValue.transmissionMode);
  }

  if (physicalConfigDedicated->antennaInfo->present == LTE_PhysicalConfigDedicated__antennaInfo_PR_defaultValue) {
    AssertFatal(cc->p_eNB <= 2, "p_eNB is %d, should be <2\n",
                cc->p_eNB);
    return (cc->p_eNB);
  }

  AssertFatal(false, "Shouldn't be here, antenna info: %p, %d cc:%d, ue:%d active: %d; \n", physicalConfigDedicated->antennaInfo, physicalConfigDedicated->antennaInfo->present,CC_idP, UE_idP, is_UE_active( module_idP,UE_idP ) );
  return 0;
}

//------------------------------------------------------------------------------
int8_t
get_ULharq(module_id_t module_idP,
           int CC_idP,
           uint16_t frameP,
           uint8_t subframeP)
//------------------------------------------------------------------------------
{
  int8_t ret = -1;
  eNB_MAC_INST *eNB = RC.mac[module_idP];
  COMMON_channels_t *cc = &eNB->common_channels[CC_idP];

  if (cc->tdd_Config == NULL) { // FDD
    ret = (((frameP << 1) + subframeP) & 7);
  } else {
    switch (cc->tdd_Config->subframeAssignment) {
      case 1:
        switch (subframeP) {
          case 2:
          case 3:
            ret = (subframeP - 2);
            break;

          case 7:
          case 8:
            ret = (subframeP - 5);
            break;

          default:
            AssertFatal(1 == 0, "subframe2_harq_pid, Illegal subframe %d for TDD mode %d\n",
                        subframeP,
                        (int) cc->tdd_Config->subframeAssignment);
            break;
        }

        break;

      case 2:
        AssertFatal((subframeP == 2) || (subframeP == 7), "subframe2_harq_pid, Illegal subframe %d for TDD mode %d\n",
                    subframeP,
                    (int) cc->tdd_Config->subframeAssignment);
        ret = (subframeP / 7);
        break;

      case 3:
        AssertFatal((subframeP > 1) && (subframeP < 5), "subframe2_harq_pid, Illegal subframe %d for TDD mode %d\n",
                    subframeP,
                    (int) cc->tdd_Config->subframeAssignment);
        ret = (subframeP - 2);
        break;

      case 4:
        AssertFatal((subframeP > 1) && (subframeP < 4), "subframe2_harq_pid, Illegal subframe %d for TDD mode %d\n",
                    subframeP,
                    (int) cc->tdd_Config->subframeAssignment);
        ret = (subframeP - 2);
        break;

      case 5:
        AssertFatal(subframeP == 2, "subframe2_harq_pid, Illegal subframe %d for TDD mode %d\n",
                    subframeP,
                    (int) cc->tdd_Config->subframeAssignment);
        ret = (subframeP - 2);
        break;

      default:
        AssertFatal(1 == 0, "subframe2_harq_pid, Unsupported TDD mode %d\n",
                    (int) cc->tdd_Config->subframeAssignment);
        break;
    }
  }

  AssertFatal(ret != -1, "invalid harq_pid(%d) at SFN/SF = %d/%d\n", (int8_t) ret,
              frameP,
              subframeP);
  return ret;
}

//------------------------------------------------------------------------------
uint16_t
getRIV(uint16_t N_RB_DL,
       uint16_t RBstart,
       uint16_t Lcrbs)
//------------------------------------------------------------------------------
{
  uint16_t RIV;

  if (Lcrbs <= (1 + (N_RB_DL >> 1)))
    RIV = (N_RB_DL * (Lcrbs - 1)) + RBstart;
  else
    RIV = (N_RB_DL * (N_RB_DL + 1 - Lcrbs)) + (N_RB_DL - 1 - RBstart);

  return RIV;
}

//------------------------------------------------------------------------------
uint32_t
allocate_prbs(int UE_id,
              unsigned char nb_rb,
              int N_RB_DL,
              uint32_t *rballoc)
//------------------------------------------------------------------------------
{
  int i;
  uint32_t rballoc_dci = 0;
  unsigned char nb_rb_alloc = 0;

  for (i = 0; i < (N_RB_DL - 2); i += 2) {
    if (((*rballoc >> i) & 3) == 0) {
      *rballoc |= (3 << i);
      rballoc_dci |= (1 << ((12 - i) >> 1));
      nb_rb_alloc += 2;
    }

    if (nb_rb_alloc == nb_rb) {
      return (rballoc_dci);
    }
  }

  if ((N_RB_DL & 1) == 1) {
    if ((*rballoc >> (N_RB_DL - 1) & 1) == 0) {
      *rballoc |= (1 << (N_RB_DL - 1));
      rballoc_dci |= 1;
    }
  }

  return (rballoc_dci);
}

//------------------------------------------------------------------------------
int
get_bw_index(module_id_t module_id,
             uint8_t CC_id)
//------------------------------------------------------------------------------
{
  int bw_index = 0;
  int N_RB_DL = to_prb(RC.mac[module_id]->common_channels[CC_id].mib->message.dl_Bandwidth);

  switch (N_RB_DL) {
    case 6:     // 1.4 MHz
      bw_index = 0;
      break;

    case 25:      // 5HMz
      bw_index = 1;
      break;

    case 50:      // 10HMz
      bw_index = 2;
      break;

    case 100:     // 20HMz
      bw_index = 3;
      break;

    default:
      bw_index = 1;
      LOG_W(MAC, "[eNB %d] N_RB_DL %d unknown for CC_id %d, setting bw_index to 1\n",
            module_id,
            N_RB_DL,
            CC_id);
      break;
  }

  return bw_index;
}

//------------------------------------------------------------------------------
int
get_min_rb_unit(module_id_t module_id,
                uint8_t CC_id)
//------------------------------------------------------------------------------
{
  int min_rb_unit = 0;
  int N_RB_DL = to_prb(RC.mac[module_id]->common_channels[CC_id].mib->message.dl_Bandwidth);

  switch (N_RB_DL) {
    case 6:       // 1.4MHz
      min_rb_unit = 1;
      break;

    case 15:      // 3MHz
    case 25:      // 5MHz
      min_rb_unit = 2;
      break;

    case 50:      // 10MHz
      min_rb_unit = 3;
      break;

    case 75:      // 15MHz
    case 100:     // 20MHz
      min_rb_unit = 4;
      break;

    default:
      min_rb_unit = 2;
      LOG_W(MAC, "[eNB %d] N_DL_RB %d unknown for CC_id %d, setting min_rb_unit to 2\n",
            module_id,
            N_RB_DL,
            CC_id);
      break;
  }

  return min_rb_unit;
}

//------------------------------------------------------------------------------
uint32_t
allocate_prbs_sub(int nb_rb,
                  int N_RB_DL,
                  int N_RBG,
                  const uint8_t *rballoc)
//------------------------------------------------------------------------------
{
  int check = 0;    //check1=0,check2=0;
  uint32_t rballoc_dci = 0;
  //uint8_t number_of_subbands=13;
  LOG_T(MAC, "*****Check1RBALLOC****: %d%d%d%d (nb_rb %d,N_RBG %d)\n",
        rballoc[3],
        rballoc[2],
        rballoc[1],
        rballoc[0],
        nb_rb,
        N_RBG);

  while (nb_rb > 0 && check < N_RBG) {
    //printf("rballoc[%d] %d\n",check,rballoc[check]);
    if (rballoc[check] == 1) {
      rballoc_dci |= (1 << ((N_RBG - 1) - check));

      switch (N_RB_DL) {
        case 6:
          nb_rb--;
          break;

        case 25:
          if (check == N_RBG - 1) {
            nb_rb--;
          } else {
            nb_rb -= 2;
          }

          break;

        case 50:
          if (check == N_RBG - 1) {
            nb_rb -= 2;
          } else {
            nb_rb -= 3;
          }

          break;

        case 100:
          nb_rb -= 4;
          break;
      }
    }

    //      printf("rb_alloc %x\n",rballoc_dci);
    check++;
    //    check1 = check1+2;
  }

  // rballoc_dci = (rballoc_dci)&(0x1fff);
  LOG_T(MAC, "*********RBALLOC : %x\n",
        rballoc_dci);
  // exit(-1);
  return rballoc_dci;
}

//------------------------------------------------------------------------------
int
get_subbandsize(uint8_t dl_Bandwidth)
//------------------------------------------------------------------------------
{
  uint8_t ss[6] = { 6, 4, 4, 6, 8, 8 };
  AssertFatal(dl_Bandwidth < 6, "dl_Bandwidth %d is out of bounds\n",
              dl_Bandwidth);
  return (ss[dl_Bandwidth]);
}

//------------------------------------------------------------------------------
int
get_nb_subband(int N_RB_DL)
//------------------------------------------------------------------------------
{
  int nb_sb = 0;

  switch (N_RB_DL) {
    case 6:
      nb_sb = 0;
      break;

    case 15:
      nb_sb = 4;    // sb_size =4
      break;

    case 25:
      nb_sb = 7;    // sb_size =4, 1 sb with 1PRB, 6 with 2 RBG, each has 2 PRBs
      break;

    case 50:      // sb_size =6
      nb_sb = 9;
      break;

    case 75:      // sb_size =8
      nb_sb = 10;
      break;

    case 100:     // sb_size =8 , 1 sb with 1 RBG + 12 sb with 2RBG, each RBG has 4 PRBs
      nb_sb = 13;
      break;

    default:
      nb_sb = 0;
      break;
  }

  return nb_sb;
}

//------------------------------------------------------------------------------
void
init_CCE_table(int *CCE_table)
//------------------------------------------------------------------------------
{
  memset(CCE_table, 0, 800 * sizeof(int));
}

//------------------------------------------------------------------------------
int
get_nCCE_offset(int *CCE_table,
                const unsigned char L,
                const int nCCE,
                const int common_dci,
                const unsigned short rnti,
                const unsigned char subframe)
//------------------------------------------------------------------------------
{
  int search_space_free, m, nb_candidates = 0, l, i;
  unsigned int Yk;

  /*
    printf("CCE Allocation: ");
    for (i=0;i<nCCE;i++)
    printf("%d.",CCE_table[i]);
    printf("\n");
  */
  if (common_dci == 1) {
    // check CCE(0 ... L-1)
    nb_candidates = (L == 4) ? 4 : 2;
    nb_candidates = min(nb_candidates, nCCE / L);

    //    printf("Common DCI nb_candidates %d, L %d\n",nb_candidates,L);

    for (m = nb_candidates - 1; m >= 0; m--) {
      search_space_free = 1;

      for (l = 0; l < L; l++) {
        //        printf("CCE_table[%d] %d\n",(m*L)+l,CCE_table[(m*L)+l]);
        if (CCE_table[(m * L) + l] == 1) {
          search_space_free = 0;
          break;
        }
      }

      if (search_space_free == 1) {
        //        printf("returning %d\n",m*L);
        for (l = 0; l < L; l++) {
          CCE_table[(m * L) + l] = 1;
        }

        return (m * L);
      }
    }

    return -1;
  }

  // Find first available in ue specific search space
  // according to procedure in Section 9.1.1 of 36.213 (v. 8.6)
  // compute Yk
  Yk = (unsigned int) rnti;

  for (i = 0; i <= subframe; i++) {
    Yk = (Yk * 39827) % 65537;
  }

  Yk = Yk % (nCCE / L);

  switch (L) {
    case 1:
    case 2:
      nb_candidates = 6;
      break;

    case 4:
    case 8:
      nb_candidates = 2;
      break;

    default:
      DevParam(L,
               nCCE,
               rnti);
      break;
  }

  LOG_D(MAC, "rnti %x, Yk = %d, nCCE %d (nCCE/L %d),nb_cand %d\n",
        rnti,
        Yk,
        nCCE,
        nCCE / L,
        nb_candidates);

  for (m = 0; m < nb_candidates; m++) {
    search_space_free = 1;

    for (l = 0; l < L; l++) {
      int cce = (((Yk + m) % (nCCE / L)) * L) + l;

      if (cce >= nCCE || CCE_table[cce] == 1) {
        search_space_free = 0;
        break;
      }
    }

    if (search_space_free == 1) {
      for (l = 0; l < L; l++) {
        CCE_table[(((Yk + m) % (nCCE / L)) * L) + l] = 1;
      }

      return (((Yk + m) % (nCCE / L)) * L);
    }
  }

  return -1;
}

//------------------------------------------------------------------------------
void
dump_CCE_table(int *CCE_table,
               const int nCCE,
               const unsigned short rnti,
               const int subframe,
               int L)
//------------------------------------------------------------------------------
{
  int nb_candidates = 0, i;
  unsigned int Yk;
  printf("CCE 0: ");

  for (i = 0; i < nCCE; i++) {
    printf("%1d.", CCE_table[i]);

    if ((i & 7) == 7)
      printf("\n CCE %d: ",
             i);
  }

  Yk = (unsigned int) rnti;

  for (i = 0; i <= subframe; i++) {
    Yk = (Yk * 39827) % 65537;
  }

  Yk = Yk % (nCCE / L);

  switch (L) {
    case 1:
    case 2:
      nb_candidates = 6;
      break;

    case 4:
    case 8:
      nb_candidates = 2;
      break;

    default:
      DevParam(L, nCCE, rnti);
      break;
  }

  LOG_I(PHY, "rnti %x, Yk*L = %u, nCCE %d (nCCE/L %d),nb_cand*L %d\n",
        rnti,
        Yk * L,
        nCCE,
        nCCE / L,
        nb_candidates * L);
}

//------------------------------------------------------------------------------
uint16_t
getnquad(COMMON_channels_t *cc,
         uint8_t num_pdcch_symbols,
         uint8_t mi)
//------------------------------------------------------------------------------
{
  uint16_t Nreg = 0;
  AssertFatal(cc != NULL, "cc is null\n");
  AssertFatal(cc->mib != NULL, "cc->mib is null\n");
  int N_RB_DL = to_prb(cc->mib->message.dl_Bandwidth);
  int phich_resource = get_phich_resource_times6(cc);
  uint8_t Ngroup_PHICH = (phich_resource * N_RB_DL) / 48;

  if (((phich_resource * N_RB_DL) % 48) > 0) {
    Ngroup_PHICH++;
  }

  if (cc->Ncp == 1) {
    Ngroup_PHICH <<= 1;
  }

  Ngroup_PHICH *= mi;

  if (num_pdcch_symbols > 0 && num_pdcch_symbols < 4)
    switch (N_RB_DL) {
      case 6:
        Nreg = 12 + (num_pdcch_symbols - 1) * 18;
        break;

      case 25:
        Nreg = 50 + (num_pdcch_symbols - 1) * 75;
        break;

      case 50:
        Nreg = 100 + (num_pdcch_symbols - 1) * 150;
        break;

      case 100:
        Nreg = 200 + (num_pdcch_symbols - 1) * 300;
        break;

      default:
        return 0;
    }

  //   printf("Nreg %d (%d)\n",Nreg,Nreg - 4 - (3*Ngroup_PHICH));
  return (Nreg - 4 - (3 * Ngroup_PHICH));
}

//------------------------------------------------------------------------------
uint16_t
getnCCE(COMMON_channels_t *cc,
        uint8_t num_pdcch_symbols,
        uint8_t mi)
//------------------------------------------------------------------------------
{
  AssertFatal(cc != NULL, "cc is null\n");
  return (getnquad(cc,
                   num_pdcch_symbols,
                   mi) / 9);
}

//------------------------------------------------------------------------------
uint8_t
getmi(COMMON_channels_t *cc,
      int subframe)
//------------------------------------------------------------------------------
{
  AssertFatal(cc != NULL, "cc is null\n");

  // for FDD
  if (cc->tdd_Config == NULL) // FDD
    return 1;

  // for TDD
  switch (cc->tdd_Config->subframeAssignment) {
    case 0:
      if (subframe == 0 || subframe == 5) {
        return 2;
      }

      return 1;

    case 1:
      if (subframe == 0 || subframe == 5) {
        return 0;
      }

      return 1;

    case 2:
      if (subframe == 3 || subframe == 8) {
        return 0;
      }

      return 1;

    case 3:
      if (subframe == 0 || subframe == 8 || subframe == 9) {
        return 1;
      }

      return 0;

    case 4:
      if (subframe == 8 || subframe == 9) {
        return 1;
      }

      return 0;

    case 5:
      if (subframe == 8) {
        return 1;
      }

      return 0;

    case 6:
      return 1;

    default:
      break;
  }

  return 0;
}

//------------------------------------------------------------------------------
uint16_t
get_nCCE_max(COMMON_channels_t *cc,
             int num_pdcch_symbols,
             int subframe)
//------------------------------------------------------------------------------
{
  AssertFatal(cc != NULL, "cc is null\n");
  return (getnCCE(cc,
                  num_pdcch_symbols,
                  getmi(cc,
                        subframe)));
}

// Allocate the CCEs
//------------------------------------------------------------------------------
int
allocate_CCEs(int module_idP,
              int CC_idP,
              frame_t frameP,
              sub_frame_t subframeP,
              int test_onlyP)
//------------------------------------------------------------------------------
{
  eNB_MAC_INST *eNB = RC.mac[module_idP];
  int *CCE_table = eNB->CCE_table[CC_idP];
  nfapi_dl_config_request_body_t *DL_req = &eNB->DL_req[CC_idP].dl_config_request_body;
  nfapi_hi_dci0_request_body_t *HI_DCI0_req = &eNB->HI_DCI0_req[CC_idP][subframeP].hi_dci0_request_body;
  nfapi_dl_config_request_pdu_t *dl_config_pdu = &DL_req->dl_config_pdu_list[0];
  nfapi_hi_dci0_request_pdu_t *hi_dci0_pdu = &HI_DCI0_req->hi_dci0_pdu_list[0];
  COMMON_channels_t *cc = &eNB->common_channels[CC_idP];
  int nCCE_max = get_nCCE_max(cc, 1, subframeP);
  int fCCE;
  int i, j, idci;
  int nCCE = 0;
  int max_symbol;
  int ackNAK_absSF = get_pucch1_absSF(cc, (frameP*10+subframeP));
  nfapi_dl_config_request_pdu_t *dl_config_pduLoop;
  nfapi_hi_dci0_request_pdu_t *hi_dci0_pduLoop;

  if (cc->tdd_Config!=NULL && is_S_sf(cc,subframeP) > 0)
    max_symbol = 2;
  else
    max_symbol = 3;

  nfapi_ul_config_request_body_t *ul_req = &eNB->UL_req_tmp[CC_idP][ackNAK_absSF % 10].ul_config_request_body;
  LOG_D(MAC, "Allocate CCEs subframe %d, test %d : (DL PDU %d, DL DCI %d, UL %d)\n",
        subframeP,
        test_onlyP,
        DL_req->number_pdu,
        DL_req->number_dci,
        HI_DCI0_req->number_of_dci);
  DL_req->number_pdcch_ofdm_symbols = 1;
try_again:
  init_CCE_table(CCE_table);
  nCCE = 0;

  for (i = 0, idci = 0; i < DL_req->number_pdu; i++) {
    dl_config_pduLoop = &dl_config_pdu[i];

    // allocate DL common DCIs first
    if (dl_config_pduLoop->pdu_type == NFAPI_DL_CONFIG_DCI_DL_PDU_TYPE &&
        dl_config_pduLoop->dci_dl_pdu.dci_dl_pdu_rel8.rnti_type == 2) {
      LOG_D(MAC, "Trying to allocate COMMON DCI %d/%d (%d,%d) : rnti %x, aggreg %d nCCE %d / %d (num_pdcch_symbols %d)\n",
            idci,
            DL_req->number_dci + HI_DCI0_req->number_of_dci,
            DL_req->number_dci,
            HI_DCI0_req->number_of_dci,
            dl_config_pduLoop->dci_dl_pdu.dci_dl_pdu_rel8.rnti,
            dl_config_pduLoop->dci_dl_pdu.dci_dl_pdu_rel8.aggregation_level,
            nCCE,
            nCCE_max,
            DL_req->number_pdcch_ofdm_symbols);

      if (nCCE + (dl_config_pduLoop->dci_dl_pdu.dci_dl_pdu_rel8.aggregation_level) > nCCE_max) {
        if (DL_req->number_pdcch_ofdm_symbols == max_symbol)
          return -1;

        LOG_D(MAC, "Can't fit DCI allocations with %d PDCCH symbols, increasing by 1\n",
              DL_req->number_pdcch_ofdm_symbols);
        DL_req->number_pdcch_ofdm_symbols++;
        nCCE_max = get_nCCE_max(cc, DL_req->number_pdcch_ofdm_symbols, subframeP);
        goto try_again;
      }

      // number of CCEs left can potentially hold this allocation
      fCCE = get_nCCE_offset(CCE_table,
                             dl_config_pduLoop->dci_dl_pdu.dci_dl_pdu_rel8.aggregation_level,
                             nCCE_max,
                             1,
                             dl_config_pduLoop->dci_dl_pdu.dci_dl_pdu_rel8.rnti,
                             subframeP);

      if (fCCE == -1) {
        if (DL_req->number_pdcch_ofdm_symbols == max_symbol) {
          LOG_D(MAC, "subframe %d: Dropping Allocation for RNTI %x\n",
                subframeP,
                dl_config_pduLoop->dci_dl_pdu.dci_dl_pdu_rel8.rnti);

          for (j = 0; j <= i; j++) {
            dl_config_pduLoop = &dl_config_pdu[j];

            if (dl_config_pduLoop->pdu_type == NFAPI_DL_CONFIG_DCI_DL_PDU_TYPE)
              LOG_D(MAC, "DCI %d/%d (%d,%d) : rnti %x dci format %d, aggreg %d nCCE %d / %d (num_pdcch_symbols %d)\n",
                    j,
                    DL_req->number_dci + HI_DCI0_req->number_of_dci,
                    DL_req->number_dci,
                    HI_DCI0_req->number_of_dci,
                    dl_config_pduLoop->dci_dl_pdu.dci_dl_pdu_rel8.rnti,
                    dl_config_pduLoop->dci_dl_pdu.dci_dl_pdu_rel8.dci_format,
                    dl_config_pduLoop->dci_dl_pdu.dci_dl_pdu_rel8.aggregation_level,
                    nCCE, nCCE_max, DL_req->number_pdcch_ofdm_symbols);
          }

          //dump_CCE_table(CCE_table,nCCE_max,subframeP,dci_alloc->rnti,1<<dci_alloc->L);
          return -1;
        }

        LOG_D(MAC, "Can't fit DCI allocations with %d PDCCH symbols (rnti condition), increasing by 1\n",
              DL_req->number_pdcch_ofdm_symbols);
        DL_req->number_pdcch_ofdm_symbols++;
        nCCE_max = get_nCCE_max(cc, DL_req->number_pdcch_ofdm_symbols, subframeP);
        goto try_again;
      }     // fCCE==-1

      // the allocation is feasible, rnti rule passes
      nCCE += dl_config_pduLoop->dci_dl_pdu.dci_dl_pdu_rel8.aggregation_level;
      LOG_D(MAC, "Allocating at nCCE %d\n", fCCE);

      if ((test_onlyP%2) == 0) {
        dl_config_pduLoop->dci_dl_pdu.dci_dl_pdu_rel8.cce_idx = fCCE;
        LOG_D(MAC, "Allocate COMMON DCI CCEs subframe %d, test %d => L %d fCCE %d\n",
              subframeP,
              test_onlyP,
              dl_config_pduLoop->dci_dl_pdu.dci_dl_pdu_rel8.aggregation_level,
              fCCE);
      }

      idci++;
    }
  }       // for i = 0 ... num_DL_DCIs

  // now try to allocate UL DCIs
  for (i = 0; i < HI_DCI0_req->number_of_dci + HI_DCI0_req->number_of_hi; i++) {
    hi_dci0_pduLoop = &hi_dci0_pdu[i];

    // allocate UL DCIs
    if (hi_dci0_pdu[i].pdu_type == NFAPI_HI_DCI0_DCI_PDU_TYPE) {
      LOG_D(MAC, "Trying to allocate format 0 DCI %d/%d (%d,%d) : rnti %x, aggreg %d nCCE %d / %d (num_pdcch_symbols %d)\n",
            idci,
            DL_req->number_dci + HI_DCI0_req->number_of_dci,
            DL_req->number_dci,
            HI_DCI0_req->number_of_dci,
            hi_dci0_pduLoop->dci_pdu.dci_pdu_rel8.rnti,
            hi_dci0_pduLoop->dci_pdu.dci_pdu_rel8.aggregation_level,
            nCCE, nCCE_max,
            DL_req->number_pdcch_ofdm_symbols);

      if (nCCE + hi_dci0_pduLoop->dci_pdu.dci_pdu_rel8.aggregation_level > nCCE_max) {
        if (DL_req->number_pdcch_ofdm_symbols == max_symbol)
          return -1;

        LOG_D(MAC, "Can't fit DCI allocations with %d PDCCH symbols, increasing by 1\n",
              DL_req->number_pdcch_ofdm_symbols);
        DL_req->number_pdcch_ofdm_symbols++;
        nCCE_max = get_nCCE_max(cc, DL_req->number_pdcch_ofdm_symbols, subframeP);
        goto try_again;
      }

      // number of CCEs left can potentially hold this allocation
      fCCE = get_nCCE_offset(CCE_table,
                             hi_dci0_pduLoop->dci_pdu.dci_pdu_rel8.aggregation_level,
                             nCCE_max,
                             0,
                             hi_dci0_pduLoop->dci_pdu.dci_pdu_rel8.rnti,
                             subframeP);

      if (fCCE == -1) {
        if (DL_req->number_pdcch_ofdm_symbols == max_symbol) {
          LOG_D(MAC, "subframe %d: Dropping Allocation for RNTI %x\n",
                subframeP,
                hi_dci0_pduLoop->dci_pdu.dci_pdu_rel8.rnti);

          for (j = 0; j <= i; j++) {
            hi_dci0_pduLoop = &hi_dci0_pdu[j];

            if (hi_dci0_pdu[j].pdu_type == NFAPI_HI_DCI0_DCI_PDU_TYPE)
              LOG_D(MAC, "DCI %d/%d (%d,%d) : rnti %x dci format %d, aggreg %d nCCE %d / %d (num_pdcch_symbols %d)\n",
                    j,
                    DL_req->number_dci + HI_DCI0_req->number_of_dci,
                    DL_req->number_dci,
                    HI_DCI0_req->number_of_dci,
                    hi_dci0_pduLoop->dci_pdu.dci_pdu_rel8.rnti,
                    hi_dci0_pduLoop->dci_pdu.dci_pdu_rel8.dci_format,
                    hi_dci0_pduLoop->dci_pdu.dci_pdu_rel8.aggregation_level,
                    nCCE, nCCE_max, DL_req->number_pdcch_ofdm_symbols);
          }

          //dump_CCE_table(CCE_table,nCCE_max,subframeP,dci_alloc->rnti,1<<dci_alloc->L);
          return -1;
        }

        LOG_D(MAC, "Can't fit DCI allocations with %d PDCCH symbols (rnti condition), increasing by 1\n",
              DL_req->number_pdcch_ofdm_symbols);
        DL_req->number_pdcch_ofdm_symbols++;
        nCCE_max = get_nCCE_max(cc, DL_req->number_pdcch_ofdm_symbols, subframeP);
        goto try_again;
      }     // fCCE==-1

      // the allocation is feasible, rnti rule passes
      nCCE += hi_dci0_pduLoop->dci_pdu.dci_pdu_rel8.aggregation_level;
      LOG_D(MAC, "Allocating at nCCE %d\n",
            fCCE);

      if ((test_onlyP%2) == 0) {
        hi_dci0_pduLoop->dci_pdu.dci_pdu_rel8.cce_index = fCCE;
        LOG_D(MAC, "Allocate CCEs subframe %d, test %d\n",
              subframeP,
              test_onlyP);
      }

      idci++;
    }
  }       // for i = 0 ... num_UL_DCIs

  for (i = 0; i < DL_req->number_pdu; i++) {
    dl_config_pduLoop = &dl_config_pdu[i];

    // allocate DL UE specific DCIs
    if ((dl_config_pdu[i].pdu_type == NFAPI_DL_CONFIG_DCI_DL_PDU_TYPE)
        && (dl_config_pduLoop->dci_dl_pdu.dci_dl_pdu_rel8.rnti_type == 1)) {
      LOG_D(MAC, "Trying to allocate DL UE-SPECIFIC DCI %d/%d (%d,%d) : rnti %x, aggreg %d nCCE %d / %d (num_pdcch_symbols %d)\n",
            idci,
            DL_req->number_dci + HI_DCI0_req->number_of_dci,
            DL_req->number_dci, HI_DCI0_req->number_of_dci,
            dl_config_pduLoop->dci_dl_pdu.dci_dl_pdu_rel8.rnti,
            dl_config_pduLoop->dci_dl_pdu.dci_dl_pdu_rel8.aggregation_level,
            nCCE,
            nCCE_max,
            DL_req->number_pdcch_ofdm_symbols);

      if (nCCE + (dl_config_pduLoop->dci_dl_pdu.dci_dl_pdu_rel8.aggregation_level) > nCCE_max) {
        if (DL_req->number_pdcch_ofdm_symbols == max_symbol)
          return -1;

        LOG_D(MAC, "Can't fit DCI allocations with %d PDCCH symbols, increasing by 1\n",
              DL_req->number_pdcch_ofdm_symbols);
        DL_req->number_pdcch_ofdm_symbols++;
        nCCE_max = get_nCCE_max(cc, DL_req->number_pdcch_ofdm_symbols, subframeP);
        goto try_again;
      }

      // number of CCEs left can potentially hold this allocation
      fCCE = get_nCCE_offset(CCE_table,
                             dl_config_pduLoop->dci_dl_pdu.dci_dl_pdu_rel8.aggregation_level,
                             nCCE_max,
                             0,
                             dl_config_pduLoop->dci_dl_pdu.dci_dl_pdu_rel8.rnti,
                             subframeP);

      if (fCCE == -1) {
        if (DL_req->number_pdcch_ofdm_symbols == max_symbol) {
          LOG_D(MAC, "subframe %d: Dropping Allocation for RNTI %x\n",
                subframeP,
                dl_config_pduLoop->dci_dl_pdu.dci_dl_pdu_rel8.rnti);

          for (j = 0; j <= i; j++) {
            dl_config_pduLoop = &dl_config_pdu[j];

            if (dl_config_pduLoop->pdu_type == NFAPI_DL_CONFIG_DCI_DL_PDU_TYPE)
              LOG_D(MAC, "DCI %d/%d (%d,%d) : rnti %x dci format %d, aggreg %d nCCE %d / %d (num_pdcch_symbols %d)\n",
                    j,
                    DL_req->number_dci + HI_DCI0_req->number_of_dci,
                    DL_req->number_dci,
                    HI_DCI0_req->number_of_dci,
                    dl_config_pduLoop->dci_dl_pdu.dci_dl_pdu_rel8.rnti,
                    dl_config_pduLoop->dci_dl_pdu.dci_dl_pdu_rel8.dci_format,
                    dl_config_pduLoop->dci_dl_pdu.dci_dl_pdu_rel8.aggregation_level,
                    nCCE,
                    nCCE_max,
                    DL_req->number_pdcch_ofdm_symbols);
          }

          //dump_CCE_table(CCE_table,nCCE_max,subframeP,dci_alloc->rnti,1<<dci_alloc->L);
          return -1;
        }

        LOG_D(MAC, "Can't fit DCI allocations with %d PDCCH symbols (rnti condition), increasing by 1\n",
              DL_req->number_pdcch_ofdm_symbols);
        DL_req->number_pdcch_ofdm_symbols++;
        nCCE_max = get_nCCE_max(cc, DL_req->number_pdcch_ofdm_symbols, subframeP);
        goto try_again;
      }     // fCCE==-1

      // the allocation is feasible, rnti rule passes
      nCCE += dl_config_pduLoop->dci_dl_pdu.dci_dl_pdu_rel8.aggregation_level;
      LOG_D(MAC, "Allocating at nCCE %d\n",
            fCCE);

      if ((test_onlyP%2) == 0) {
        dl_config_pduLoop->dci_dl_pdu.dci_dl_pdu_rel8.cce_idx = fCCE;
        LOG_D(MAC, "Allocate CCEs subframe %d, test %d\n",
              subframeP,
              test_onlyP);
      }

      if ((test_onlyP/2) == 1) {
        for(int ack_int = 0; ack_int < ul_req->number_of_pdus; ack_int++) {
          if(((ul_req->ul_config_pdu_list[ack_int].pdu_type == NFAPI_UL_CONFIG_UCI_HARQ_PDU_TYPE) ||
              (ul_req->ul_config_pdu_list[ack_int].pdu_type == NFAPI_UL_CONFIG_UCI_SR_HARQ_PDU_TYPE)) &&
              (ul_req->ul_config_pdu_list[ack_int].uci_harq_pdu.ue_information.ue_information_rel8.rnti == dl_config_pdu[i].dci_dl_pdu.dci_dl_pdu_rel8.rnti)) {
            if (cc->tdd_Config==NULL)
              ul_req->ul_config_pdu_list[ack_int].uci_harq_pdu.harq_information.harq_information_rel9_fdd.n_pucch_1_0 =
                cc->radioResourceConfigCommon->pucch_ConfigCommon.n1PUCCH_AN + fCCE;
            else
              ul_req->ul_config_pdu_list[ack_int].uci_harq_pdu.harq_information.harq_information_rel10_tdd.n_pucch_1_0 =
                cc->radioResourceConfigCommon->pucch_ConfigCommon.n1PUCCH_AN + fCCE + getNp(cc->mib->message.dl_Bandwidth,fCCE,0) ;
          }
        }
      }

      idci++;
    }
  }       // for i = 0 ... num_DL_DCIs

  return 0;
}

//------------------------------------------------------------------------------
nfapi_ul_config_request_pdu_t *
has_ul_grant(module_id_t module_idP,
             int CC_idP,
             uint16_t absSFP,
             uint16_t rnti)
//------------------------------------------------------------------------------
{
  nfapi_ul_config_request_body_t *ul_req = &RC.mac[module_idP]->UL_req_tmp[CC_idP][absSFP % 10].ul_config_request_body;
  nfapi_ul_config_request_pdu_t *ul_config_pdu = &ul_req->ul_config_pdu_list[0];
  uint8_t pdu_type;
  LOG_D(MAC, "Checking for rnti %x UL grant in subframeP %d (num pdu %d)\n",
        rnti,
        absSFP % 10,
        ul_req->number_of_pdus);

  for (int i = 0; i < ul_req->number_of_pdus; i++, ul_config_pdu++) {
    pdu_type = ul_config_pdu->pdu_type;
    LOG_D(MAC, "PDU %d : type %d,rnti %x\n",
          i,
          pdu_type,
          rnti);

    if (pdu_type == NFAPI_UL_CONFIG_ULSCH_PDU_TYPE && ul_config_pdu->ulsch_pdu.ulsch_pdu_rel8.rnti == rnti)
      return ul_config_pdu;

    if (pdu_type == NFAPI_UL_CONFIG_ULSCH_CQI_RI_PDU_TYPE && ul_config_pdu->ulsch_cqi_ri_pdu.ulsch_pdu.ulsch_pdu_rel8.rnti == rnti)
      return ul_config_pdu;

    if (pdu_type == NFAPI_UL_CONFIG_ULSCH_HARQ_PDU_TYPE && ul_config_pdu->ulsch_harq_pdu.ulsch_pdu.ulsch_pdu_rel8.rnti == rnti)
      return ul_config_pdu;

    if (pdu_type == NFAPI_UL_CONFIG_ULSCH_CQI_HARQ_RI_PDU_TYPE && ul_config_pdu->ulsch_cqi_harq_ri_pdu.ulsch_pdu.ulsch_pdu_rel8.rnti == rnti)
      return ul_config_pdu;

    if (pdu_type == NFAPI_UL_CONFIG_UCI_CQI_PDU_TYPE && ul_config_pdu->uci_cqi_pdu.ue_information.ue_information_rel8.rnti == rnti)
      return ul_config_pdu;

    if (pdu_type == NFAPI_UL_CONFIG_UCI_SR_PDU_TYPE && ul_config_pdu->uci_sr_pdu.ue_information.ue_information_rel8.rnti == rnti)
      return ul_config_pdu;

    if (pdu_type == NFAPI_UL_CONFIG_UCI_HARQ_PDU_TYPE && ul_config_pdu->uci_harq_pdu.ue_information.ue_information_rel8.rnti == rnti)
      return ul_config_pdu;

    if (pdu_type == NFAPI_UL_CONFIG_UCI_SR_HARQ_PDU_TYPE && ul_config_pdu->uci_sr_harq_pdu.ue_information.ue_information_rel8.rnti == rnti)
      return ul_config_pdu;

    if (pdu_type == NFAPI_UL_CONFIG_UCI_CQI_HARQ_PDU_TYPE && ul_config_pdu->uci_cqi_harq_pdu.ue_information.ue_information_rel8.rnti == rnti)
      return ul_config_pdu;

    if (pdu_type == NFAPI_UL_CONFIG_UCI_CQI_SR_PDU_TYPE && ul_config_pdu->uci_cqi_sr_pdu.ue_information.ue_information_rel8.rnti == rnti)
      return ul_config_pdu;

    if (pdu_type == NFAPI_UL_CONFIG_UCI_CQI_SR_HARQ_PDU_TYPE && ul_config_pdu->uci_cqi_sr_harq_pdu.ue_information.ue_information_rel8.rnti == rnti)
      return ul_config_pdu;

    if (pdu_type == NFAPI_UL_CONFIG_ULSCH_UCI_CSI_PDU_TYPE && ul_config_pdu->ulsch_uci_csi_pdu.ulsch_pdu.ulsch_pdu_rel8.rnti == rnti)
      return ul_config_pdu;

    if (pdu_type == NFAPI_UL_CONFIG_ULSCH_UCI_HARQ_PDU_TYPE && ul_config_pdu->ulsch_uci_harq_pdu.ulsch_pdu.ulsch_pdu_rel8.rnti == rnti)
      return ul_config_pdu;

    if (pdu_type == NFAPI_UL_CONFIG_ULSCH_CSI_UCI_HARQ_PDU_TYPE && ul_config_pdu->ulsch_csi_uci_harq_pdu.ulsch_pdu.ulsch_pdu_rel8.rnti == rnti)
      return ul_config_pdu;
  }

  return (NULL);    // no ul grant at all for this UE
}

//------------------------------------------------------------------------------
bool
CCE_allocation_infeasible(int module_idP,
                          int CC_idP,
                          int format_flag,
                          int subframe,
                          int aggregation,
                          int rnti)
//------------------------------------------------------------------------------
{
  nfapi_dl_config_request_body_t *DL_req       = &RC.mac[module_idP]->DL_req[CC_idP].dl_config_request_body;
  nfapi_dl_config_request_pdu_t *dl_config_pdu = &DL_req->dl_config_pdu_list[DL_req->number_pdu];
  nfapi_hi_dci0_request_body_t *HI_DCI0_req    = &RC.mac[module_idP]->HI_DCI0_req[CC_idP][subframe].hi_dci0_request_body;
  nfapi_hi_dci0_request_pdu_t *hi_dci0_pdu     = &HI_DCI0_req->hi_dci0_pdu_list[HI_DCI0_req->number_of_dci + HI_DCI0_req->number_of_hi];
  bool res = true;

  if (format_flag != 2) { // DL DCI
    if (DL_req->number_pdu == MAX_NUM_DL_PDU) {
      LOG_W(MAC, "Subframe %d: FAPI DL structure is full, skip scheduling UE %x\n", subframe, rnti);
    } else {
      dl_config_pdu->dci_dl_pdu.dci_dl_pdu_rel8.tl.tag            = NFAPI_DL_CONFIG_REQUEST_DCI_DL_PDU_REL8_TAG;
      dl_config_pdu->pdu_type                                     = NFAPI_DL_CONFIG_DCI_DL_PDU_TYPE;
      dl_config_pdu->dci_dl_pdu.dci_dl_pdu_rel8.rnti              = rnti;
      dl_config_pdu->dci_dl_pdu.dci_dl_pdu_rel8.rnti_type         = (format_flag == 0) ? 2 : 1;
      dl_config_pdu->dci_dl_pdu.dci_dl_pdu_rel8.aggregation_level = aggregation;
      DL_req->number_pdu++;
      LOG_D(MAC, "Subframe %d: Checking CCE feasibility format %d : (%x,%d) \n",
            subframe, format_flag, rnti, aggregation);

      if (allocate_CCEs(module_idP, CC_idP, 0, subframe, 0) != -1)
        res = false;

      DL_req->number_pdu--;
    }
  } else { // ue-specific UL DCI
    if (HI_DCI0_req->number_of_dci + HI_DCI0_req->number_of_hi == MAX_NUM_HI_DCI0_PDU) {
      LOG_W(MAC, "Subframe %d: FAPI UL structure is full, skip scheduling UE %x\n", subframe, rnti);
    } else {
      hi_dci0_pdu->pdu_type                               = NFAPI_HI_DCI0_DCI_PDU_TYPE;
      hi_dci0_pdu->dci_pdu.dci_pdu_rel8.tl.tag            = NFAPI_HI_DCI0_REQUEST_DCI_PDU_REL8_TAG;
      hi_dci0_pdu->dci_pdu.dci_pdu_rel8.rnti              = rnti;
      hi_dci0_pdu->dci_pdu.dci_pdu_rel8.aggregation_level = aggregation;
      HI_DCI0_req->number_of_dci++;

      if (allocate_CCEs(module_idP, CC_idP, 0, subframe, 0) != -1)
        res = false;

      HI_DCI0_req->number_of_dci--;
    }
  }

  return res;
}

int CCE_try_allocate_dlsch(int module_id,
                           int CC_id,
                           int subframe,
                           int UE_id,
                           uint8_t dl_cqi) {
  const rnti_t rnti = RC.mac[module_id]->UE_info.UE_template[CC_id][UE_id].rnti;
  nfapi_dl_config_request_body_t *DL_req       = &RC.mac[module_id]->DL_req[CC_id].dl_config_request_body;

  if (DL_req->number_pdu >= MAX_NUM_DL_PDU) {
    LOG_W(MAC, "Subframe %d: FAPI DL structure is full, skip scheduling UE %x\n", subframe, rnti);
    return -1;
  }

  int aggregation = 2;
  const uint8_t tm = get_tmode(module_id, CC_id, UE_id);
  switch (tm) {
    case 1:
    case 2:
    case 7:
      aggregation = get_aggregation(get_bw_index(module_id, CC_id),
                                    dl_cqi,
                                    format1);
      break;

    case 3:
      aggregation = get_aggregation(get_bw_index(module_id, CC_id),
                                    dl_cqi,
                                    format2A);
      break;

    default:
      AssertFatal(0, "Unsupported transmission mode %d\n", tm);
      break;
  }

  nfapi_dl_config_request_pdu_t *dl_config_pdu = &DL_req->dl_config_pdu_list[DL_req->number_pdu];
  memset(dl_config_pdu, 0, sizeof(nfapi_dl_config_request_pdu_t));
  dl_config_pdu->dci_dl_pdu.dci_dl_pdu_rel8.tl.tag            = NFAPI_DL_CONFIG_REQUEST_DCI_DL_PDU_REL8_TAG;
  dl_config_pdu->pdu_type                                     = NFAPI_DL_CONFIG_DCI_DL_PDU_TYPE;
  dl_config_pdu->dci_dl_pdu.dci_dl_pdu_rel8.rnti              = rnti;
  dl_config_pdu->dci_dl_pdu.dci_dl_pdu_rel8.rnti_type         = 1;
  dl_config_pdu->dci_dl_pdu.dci_dl_pdu_rel8.aggregation_level = aggregation;
  DL_req->number_pdu++;
  LOG_D(MAC, "Subframe %d: Checking CCE feasibility format 1: (%x,%d) \n",
        subframe, rnti, aggregation);

  if (allocate_CCEs(module_id, CC_id, 0, subframe, 0) < 0) {
    DL_req->number_pdu--;
    return -1;
  }

  DL_req->number_dci++;
  nfapi_dl_config_request_pdu_t *dl_dlsch_pdu = &DL_req->dl_config_pdu_list[DL_req->number_pdu];
  dl_dlsch_pdu->pdu_type = NFAPI_DL_CONFIG_DLSCH_PDU_TYPE;
  dl_dlsch_pdu->dlsch_pdu.dlsch_pdu_rel8.tl.tag = NFAPI_DL_CONFIG_REQUEST_DLSCH_PDU_REL8_TAG;
  dl_dlsch_pdu->dlsch_pdu.dlsch_pdu_rel8.rnti = rnti;
  DL_req->number_pdu++;

  return DL_req->number_pdu - 2;
}

int CCE_try_allocate_ulsch(int module_id,
                           int CC_id,
                           int subframe,
                           int UE_id,
                           uint8_t dl_cqi) {
  const rnti_t rnti = RC.mac[module_id]->UE_info.UE_template[CC_id][UE_id].rnti;
  nfapi_hi_dci0_request_body_t *HI_DCI0_req = &RC.mac[module_id]->HI_DCI0_req[CC_id][subframe].hi_dci0_request_body;

  if (HI_DCI0_req->number_of_dci + HI_DCI0_req->number_of_hi >= MAX_NUM_HI_DCI0_PDU) {
    LOG_W(MAC, "Subframe %d: FAPI UL structure is full, skip scheduling UE %x\n", subframe, rnti);
    return -1;
  }

  int aggregation = 2;
  const uint8_t tm = get_tmode(module_id, CC_id, UE_id);
  switch (tm) {
    case 1:
    case 2:
    case 7:
      aggregation = get_aggregation(get_bw_index(module_id, CC_id),
                                    dl_cqi,
                                    format1);
      break;

    case 3:
      aggregation = get_aggregation(get_bw_index(module_id, CC_id),
                                    dl_cqi,
                                    format2A);
      break;

    default:
      AssertFatal(0, "Unsupported transmission mode %d\n", tm);
      break;
  }

  const int idx = HI_DCI0_req->number_of_dci + HI_DCI0_req->number_of_hi;
  nfapi_hi_dci0_request_pdu_t *hi_dci0_pdu = &HI_DCI0_req->hi_dci0_pdu_list[idx];
  memset(hi_dci0_pdu, 0, sizeof(nfapi_hi_dci0_request_pdu_t));
  hi_dci0_pdu->pdu_type = NFAPI_HI_DCI0_DCI_PDU_TYPE;
  hi_dci0_pdu->dci_pdu.dci_pdu_rel8.tl.tag = NFAPI_HI_DCI0_REQUEST_DCI_PDU_REL8_TAG;
  hi_dci0_pdu->dci_pdu.dci_pdu_rel8.rnti = rnti;
  hi_dci0_pdu->dci_pdu.dci_pdu_rel8.aggregation_level = aggregation;
  HI_DCI0_req->number_of_dci++;
  LOG_D(MAC, "%s() sf %d: Checking CCE feasibility format 2: RNTI %04x, agg %d\n",
        __func__, subframe, rnti, aggregation);

  if (allocate_CCEs(module_id, CC_id, 0, subframe, 0) < 0) {
    HI_DCI0_req->number_of_dci--;
    return -1;
  }

  return idx;
}

//------------------------------------------------------------------------------
void
get_retransmission_timing(LTE_TDD_Config_t *tdd_Config,
                          frame_t *frameP,
                          sub_frame_t *subframeP)
//------------------------------------------------------------------------------
{
  if (tdd_Config == NULL) {
    if (*subframeP > 1) {
      *frameP = (*frameP + 1) % 1024;
    }

    *subframeP = (*subframeP + 8) % 10;
  } else {
    switch (tdd_Config->subframeAssignment) { //TODO fill in other TDD configs
      default:
        printf("%s:%d: TODO\n",
               __FILE__,
               __LINE__);
        abort();
        break;

      case 1:
        if (*subframeP == 0 || *subframeP == 5) {
          *subframeP  += 19;
          *frameP = (*frameP + (*subframeP / 10)) % 1024;
          *subframeP %= 10;
        } else if (*subframeP == 4 || *subframeP == 9) {
          *subframeP  += 16;
          *frameP = (*frameP + (*subframeP / 10)) % 1024;
          *subframeP %= 10;
        } else {
          AssertFatal(2 == 1, "Illegal dl subframe %d for tdd config %ld\n",
                      *subframeP,
                      tdd_Config->subframeAssignment);
        }

        break;
    }
  }

  return;
}

//------------------------------------------------------------------------------
uint8_t
get_dl_subframe_count(int tdd_config_sfa,
                      sub_frame_t subframeP)
//------------------------------------------------------------------------------
{
  uint8_t tdd1[10] = {1, -1, -1, -1, 2, 3, -1, -1, -1, 4}; // special subframes 1,6 are excluded

  switch (tdd_config_sfa) {// TODO fill in other tdd configs
    case 1:
      return tdd1[subframeP];
  }

  return -1;
}

//------------------------------------------------------------------------------
uint8_t
frame_subframe2_dl_harq_pid(LTE_TDD_Config_t *tdd_Config,
                            int abs_frameP,
                            sub_frame_t subframeP)
//------------------------------------------------------------------------------
{
  int harq_pid;
  uint8_t count;

  if (tdd_Config) {
    switch(tdd_Config->subframeAssignment) { //TODO fill in other tdd config
      case 1:
        count = get_dl_subframe_count(tdd_Config->subframeAssignment,
                                      subframeP);
        harq_pid = (((frame_cnt * 1024 + abs_frameP) * 4) - 1 + count) % 7;//4 dl subframe in a frame

        if (harq_pid < 0) {
          harq_pid += 7;
        }

        LOG_D(MAC,"[frame_subframe2_dl_harq_pid] (%d,%d) calculate harq_pid ((( %d * 1024 + %d) *4) - 1 + %d) = %d \n",
              (abs_frameP + 1024) % 1024,
              subframeP,
              frame_cnt,
              abs_frameP,
              count,
              harq_pid);
        return harq_pid;
    }
  } else {
    return ((abs_frameP * 10) + subframeP) & 7;
  }

  return -1;
}

//------------------------------------------------------------------------------
unsigned char
ul_ACK_subframe2M(LTE_TDD_Config_t *tdd_Config,
                  unsigned char subframe)
//------------------------------------------------------------------------------
{
  if (tdd_Config == NULL) {
    return 1;
  }

  switch (tdd_Config->subframeAssignment) {
    case 1:
      return 1; // don't ACK special subframe for now

    /*
    if (subframe == 2) {  // ACK subframes 5 and 6
      return(2);
    } else if (subframe == 3) { // ACK subframe 9
      return(1);  // To be updated
    } else if (subframe == 7) { // ACK subframes 0 and 1
      return(2);  // To be updated
    } else if (subframe == 8) { // ACK subframe 4
      return(1);  // To be updated
    } else {
      AssertFatal(1==0,"illegal subframe %d for tdd_config %ld\n",
                  subframe,tdd_Config->subframeAssignment);
    }
    break;
    */
    case 3:
      if (subframe == 2) {  // ACK subframes 5 and 6
        return 2; // should be 3
      }

      if (subframe == 3) { // ACK subframes 7 and 8
        return 2;  // To be updated
      }

      if (subframe == 4) { // ACK subframes 9 and 0
        return 2;
      }

      AssertFatal(1==0,"illegal subframe %d for tdd_config %ld\n",
                  subframe,
                  tdd_Config->subframeAssignment);
      break;

    case 4:
      if (subframe == 2) {  // ACK subframes 0,4 and 5
        return 3; // should be 4
      }

      if (subframe == 3) { // ACK subframes 6,7,8 and 9
        return 4;
      }

      AssertFatal(1==0,"illegal subframe %d for tdd_config %ld\n",
                  subframe,
                  tdd_Config->subframeAssignment);
      break;

    case 5:
      if (subframe == 2) {  // ACK subframes 0,3,4,5,6,7,8 and 9
        return 8; // should be 3
      }

      AssertFatal(1==0,"illegal subframe %d for tdd_config %ld\n",
                  subframe,
                  tdd_Config->subframeAssignment);
      break;
  }

  return 0;
}

//------------------------------------------------------------------------------
unsigned char
ul_ACK_subframe2dl_subframe(LTE_TDD_Config_t *tdd_Config,
                            unsigned char subframe,
                            unsigned char ACK_index)
//------------------------------------------------------------------------------
{
  if (tdd_Config == NULL) {
    return ((subframe < 4) ? subframe + 6 : subframe - 4);
  }

  switch (tdd_Config->subframeAssignment) {
    case 3:
      if (subframe == 2) {  // ACK subframes 5 and 6
        if (ACK_index == 2) return 1;

        return (5 + ACK_index);
      }

      if (subframe == 3) { // ACK subframes 7 and 8
        return (7 + ACK_index);  // To be updated
      }

      if (subframe == 4) { // ACK subframes 9 and 0
        return ((9 + ACK_index) % 10);
      }

      AssertFatal(1==0, "illegal subframe %d for tdd_config->subframeAssignment %ld\n",
                  subframe,
                  tdd_Config->subframeAssignment);
      break;

    case 4:
      if (subframe == 2) {  // ACK subframes 0, 4 and 5
        //if (ACK_index==2)
        //  return(1); TBC
        if (ACK_index == 2) return 0;

        return (4 + ACK_index);
      }

      if (subframe == 3) { // ACK subframes 6, 7 8 and 9
        return (6 + ACK_index);  // To be updated
      }

      AssertFatal(1 == 0, "illegal subframe %d for tdd_config %ld\n",
                  subframe,
                  tdd_Config->subframeAssignment);
      break;

    case 1:
      if (subframe == 2) {  // ACK subframes 5 and 6
        return (5 + ACK_index);
      }

      if (subframe == 3) { // ACK subframe 9
        return 9;  // To be updated
      }

      if (subframe == 7) { // ACK subframes 0 and 1
        return ACK_index;  // To be updated
      }

      if (subframe == 8) { // ACK subframe 4
        return 4;  // To be updated
      }

      AssertFatal(1 == 0, "illegal subframe %d for tdd_config %ld\n",
                  subframe,
                  tdd_Config->subframeAssignment);
      break;
  }

  return 0;
}

//------------------------------------------------------------------------------
void
extract_harq(module_id_t mod_idP,
             int CC_idP,
             int UE_id,
             frame_t frameP,
             sub_frame_t subframeP,
             void *harq_indication,
             int format)
//------------------------------------------------------------------------------
{
  eNB_MAC_INST *eNB = RC.mac[mod_idP];
  UE_info_t *UE_info = &eNB->UE_info;
  UE_sched_ctrl_t *sched_ctl = &UE_info->UE_sched_ctrl[UE_id];
  rnti_t rnti = UE_RNTI(mod_idP, UE_id);
  COMMON_channels_t *cc = &eNB->common_channels[CC_idP];
  nfapi_harq_indication_fdd_rel13_t *harq_indication_fdd;
  nfapi_harq_indication_tdd_rel13_t *harq_indication_tdd;
  uint16_t num_ack_nak;
  int numCC = UE_info->numactiveCCs[UE_id];
  int pCCid = UE_info->pCC_id[UE_id];
  int spatial_bundling = 0;
  int tmode[5];
  int i, j, m;
  uint8_t *pdu;
  sub_frame_t subframe_tx;
  int frame_tx;
  uint8_t harq_pid;
  LTE_PhysicalConfigDedicated_t *physicalConfigDedicated = UE_info->UE_template[pCCid][UE_id].physicalConfigDedicated;

  if (physicalConfigDedicated != NULL && physicalConfigDedicated->pucch_ConfigDedicated != NULL &&
      physicalConfigDedicated->ext7 != NULL && physicalConfigDedicated->ext7->pucch_ConfigDedicated_r13 != NULL &&
      ((physicalConfigDedicated->ext7->pucch_ConfigDedicated_r13->spatialBundlingPUCCH_r13 && format == 0) ||
       (physicalConfigDedicated->ext7->pucch_ConfigDedicated_r13->spatialBundlingPUSCH_r13 && format == 1))) {
    spatial_bundling = 1;
  }

  for (i = 0; i < numCC; i++) {
    tmode[i] = get_tmode(mod_idP,
                         i,
                         UE_id);
  }

  if (cc->tdd_Config) {
    harq_indication_tdd = (nfapi_harq_indication_tdd_rel13_t *) harq_indication;
    //    pdu = &harq_indication_tdd->harq_tb_n[0];
    num_ack_nak = harq_indication_tdd->number_of_ack_nack;

    switch (harq_indication_tdd->mode) {
      case 0:   // Format 1a/b bundling
        AssertFatal(numCC == 1, "numCC %d > 1, should not be using Format1a/b\n",
                    numCC);
        int M = ul_ACK_subframe2M(cc->tdd_Config,
                                  subframeP);

        for (m=0; m<M; m++) {
          subframe_tx = ul_ACK_subframe2dl_subframe(cc->tdd_Config,
                        subframeP,
                        m);

          if (frameP==1023&&subframeP>5) frame_tx=-1;
          else frame_tx = subframeP < 4 ? frameP -1 : frameP;

          harq_pid = frame_subframe2_dl_harq_pid(cc->tdd_Config,
                                                 frame_tx,
                                                 subframe_tx);
          RA_t *ra = &eNB->common_channels[CC_idP].ra[0];

          if(num_ack_nak == 1) {
            if (harq_indication_tdd->harq_data[0].bundling.value_0 == 1) { //ack
              sched_ctl->round[CC_idP][harq_pid] = 8; // release HARQ process
              sched_ctl->tbcnt[CC_idP][harq_pid] = 0;
              LOG_D(MAC, "frame %d subframe %d Acking (%d,%d) harq_pid %d round %d\n",
                    frameP,
                    subframeP,
                    frame_tx,
                    subframe_tx,
                    harq_pid,
                    sched_ctl->round[CC_idP][harq_pid]);
            } else { //nack
              if (sched_ctl->round[CC_idP][harq_pid] < 8) sched_ctl->round[CC_idP][harq_pid]++;

              if (sched_ctl->round[CC_idP][harq_pid] == 4) {
                sched_ctl->round[CC_idP][harq_pid] = 8;     // release HARQ process
                sched_ctl->tbcnt[CC_idP][harq_pid] = 0;
              }

              LOG_D(MAC,"frame %d subframe %d Nacking (%d,%d) harq_pid %d round %d\n",
                    frameP,
                    subframeP,
                    frame_tx,
                    subframe_tx,
                    harq_pid,
                    sched_ctl->round[CC_idP][harq_pid]);

              if (sched_ctl->round[CC_idP][harq_pid] == 8) {
                for (uint8_t ra_i = 0; ra_i < NB_RA_PROC_MAX; ra_i++) {
                  if (ra[ra_i].rnti == rnti && ra[ra_i].eRA_state == WAITMSG4ACK) {
                    //Msg NACK num to MAC ,remove UE
                    // add UE info to freeList
                    LOG_I(RRC, "put UE %x into freeList\n",
                          rnti);
                    put_UE_in_freelist(mod_idP,
                                       rnti,
                                       1);
                  }
                }
              }
            }
          }

          for (uint8_t ra_i = 0; ra_i < NB_RA_PROC_MAX; ra_i++) {
            if (ra[ra_i].rnti == rnti && ra[ra_i].eRA_state == MSGCRNTI_ACK && ra[ra_i].crnti_harq_pid == harq_pid) {
              LOG_D(MAC,"CRNTI Reconfiguration: ACK %d rnti %x round %d frame %d subframe %d \n",
                    harq_indication_tdd->harq_data[0].bundling.value_0,
                    rnti,
                    sched_ctl->round[CC_idP][harq_pid],
                    frameP,
                    subframeP);

              if (num_ack_nak == 1 && harq_indication_tdd->harq_data[0].bundling.value_0 == 1) {
                cancel_ra_proc(mod_idP,
                               CC_idP,
                               frameP,
                               ra[ra_i].rnti);
              } else {
                if(sched_ctl->round[CC_idP][harq_pid] == 7) {
                  cancel_ra_proc(mod_idP,
                                 CC_idP,
                                 frameP,
                                 ra[ra_i].rnti);
                }
              }

              break;
            }
          }
        }

        break;

      case 1:   // Channel Selection
      case 2:   // Format 3
      case 3:   // Format 4
      case 4:   // Format 5
        break;
    }
  } else {
    harq_indication_fdd = (nfapi_harq_indication_fdd_rel13_t *) harq_indication;
    num_ack_nak         = harq_indication_fdd->number_of_ack_nack;
    pdu                 = &harq_indication_fdd->harq_tb_n[0];
    harq_pid = ((10 * frameP) + subframeP + 10236) & 7;
    LOG_D(MAC, "frame %d subframe %d harq_pid %d mode %d tmode[0] %d num_ack_nak %d round %d\n",
          frameP,
          subframeP,
          harq_pid,
          harq_indication_fdd->mode,
          tmode[0],
          num_ack_nak,
          sched_ctl->round[CC_idP][harq_pid]);

    // use 1 HARQ proces of BL/CE UE for now
    if (UE_info->UE_template[pCCid][UE_id].rach_resource_type > 0) harq_pid = 0;

    switch (harq_indication_fdd->mode) {
      case 0:   // Format 1a/b (10.1.2.1)
        AssertFatal(numCC == 1, "numCC %d > 1, should not be using Format1a/b\n",
                    numCC);

        if (tmode[0] == 1 || tmode[0] == 2 || tmode[0] == 5 || tmode[0] == 6 || tmode[0] == 7) {  // NOTE: have to handle the case of TM9-10 with 1 antenna port
          // single ACK/NAK bit
          AssertFatal(num_ack_nak == 1, "num_ack_nak %d > 1 for 1 CC and single-layer transmission frame:%d subframe:%d\n",
                      num_ack_nak,
                      frameP,
                      subframeP);

          // In case of nFAPI, sometimes timing of eNB and UE become different.
          // So if nfapi_mode == 2(VNF), this function don't check assertion to avoid process exit.
          if (NFAPI_MODE != NFAPI_MODE_VNF) {
            AssertFatal(sched_ctl->round[CC_idP][harq_pid] < 8, "Got ACK/NAK for inactive harq_pid %d for UE %d/%x\n",
                        harq_pid,
                        UE_id,
                        rnti);
          } else {
            if (sched_ctl->round[CC_idP][harq_pid] == 8) {
              LOG_E(MAC,"Got ACK/NAK for inactive harq_pid %d for UE %d/%x\n",
                    harq_pid,
                    UE_id,
                    rnti);
              return;
            }
          }

          AssertFatal(pdu[0] == 1 || pdu[0] == 2 || pdu[0] == 4, "Received ACK/NAK %d which is not 1 or 2 for harq_pid %d from UE %d/%x\n",
                      pdu[0],
                      harq_pid,
                      UE_id,
                      rnti);
          LOG_D(MAC, "Received %d for harq_pid %d\n",
                pdu[0],
                harq_pid);
          RA_t *ra = &eNB->common_channels[CC_idP].ra[0];

          for (uint8_t ra_i = 0; ra_i < NB_RA_PROC_MAX; ra_i++) {
            if (ra[ra_i].rnti == rnti && ra[ra_i].eRA_state == MSGCRNTI_ACK && ra[ra_i].crnti_harq_pid == harq_pid) {
              LOG_D(MAC,"CRNTI Reconfiguration: ACK %d rnti %x round %d frame %d subframe %d \n",
                    pdu[0],
                    rnti,
                    sched_ctl->round[CC_idP][harq_pid],
                    frameP,
                    subframeP);

              if (pdu[0] == 1) {
                cancel_ra_proc(mod_idP,
                               CC_idP,
                               frameP,
                               ra[ra_i].rnti);
              } else {
                if (sched_ctl->round[CC_idP][harq_pid] == 7) {
                  cancel_ra_proc(mod_idP,
                                 CC_idP,
                                 frameP,
                                 ra[ra_i].rnti);
                }
              }

              break;
            }
          }

          LOG_D(MAC, "In extract_harq(): pdu[0] = %d for harq_pid = %d\n", pdu[0], harq_pid);

          if (pdu[0] == 1) {  // ACK
            sched_ctl->round[CC_idP][harq_pid] = 8; // release HARQ process
            sched_ctl->tbcnt[CC_idP][harq_pid] = 0;
            /* CDRX: PUCCH gives an ACK, so reset corresponding HARQ RTT */
            sched_ctl->harq_rtt_timer[CC_idP][harq_pid] = 0;
          } else if (pdu[0] == 2 || pdu[0] == 4) {  // NAK (treat DTX as NAK)
            sched_ctl->round[CC_idP][harq_pid]++; // increment round

            if (sched_ctl->round[CC_idP][harq_pid] == 4) {
              sched_ctl->round[CC_idP][harq_pid] = 8; // release HARQ process
              sched_ctl->tbcnt[CC_idP][harq_pid] = 0;
              /* CDRX: PUCCH gives an NACK and max number of repetitions reached so reset corresponding HARQ RTT */
              sched_ctl->harq_rtt_timer[CC_idP][harq_pid] = 0;
            }

            if (sched_ctl->round[CC_idP][harq_pid] == 8) {
              for (uint8_t ra_i = 0; ra_i < NB_RA_PROC_MAX; ra_i++) {
                if ((ra[ra_i].rnti == rnti) && (ra[ra_i].eRA_state == WAITMSG4ACK)) {
                  // Msg NACK num to MAC ,remove UE
                  // add UE info to freeList
                  LOG_I(RRC, "put UE %x into freeList\n",
                        rnti);
                  put_UE_in_freelist(mod_idP,
                                     rnti,
                                     1);
                }
              }
            }
          }
        } else {
          // one or two ACK/NAK bits
          AssertFatal(num_ack_nak <= 2, "num_ack_nak %d > 2 for 1 CC and TM3/4/8/9/10\n",
                      num_ack_nak);

          if (num_ack_nak == 2 && sched_ctl->round[CC_idP][harq_pid] < 8 && sched_ctl->tbcnt[CC_idP][harq_pid] == 1 && pdu[0] == 1 && pdu[1] == 1) {
            sched_ctl->round[CC_idP][harq_pid] = 8;
            sched_ctl->tbcnt[CC_idP][harq_pid] = 0;
            /* CDRX: PUCCH gives an ACK, so reset corresponding HARQ RTT */
            sched_ctl->harq_rtt_timer[CC_idP][harq_pid] = 0;
          }

          if ((num_ack_nak == 2)
              && (sched_ctl->round[CC_idP][harq_pid] < 8)
              && (sched_ctl->tbcnt[CC_idP][harq_pid] == 1)
              && (pdu[0] == 2) && (pdu[1] == 2)) {
            sched_ctl->round[CC_idP][harq_pid]++;

            if (sched_ctl->round[CC_idP][harq_pid] == 4) {
              sched_ctl->round[CC_idP][harq_pid] = 8;     // release HARQ process
              sched_ctl->tbcnt[CC_idP][harq_pid] = 0;
              /* CDRX: PUCCH gives an NACK and max number of repetitions reached so reset corresponding HARQ RTT */
              sched_ctl->harq_rtt_timer[CC_idP][harq_pid] = 0;
            }
          } else if (((num_ack_nak == 2)
                      && (sched_ctl->round[CC_idP][harq_pid] < 8)
                      && (sched_ctl->tbcnt[0][harq_pid] == 2)
                      && (pdu[0] == 1) && (pdu[1] == 2))
                     || ((num_ack_nak == 2)
                         && (sched_ctl->round[CC_idP][harq_pid] < 8)
                         && (sched_ctl->tbcnt[CC_idP][harq_pid] == 2)
                         && (pdu[0] == 2) && (pdu[1] == 1))) {
            sched_ctl->round[CC_idP][harq_pid]++;
            sched_ctl->tbcnt[CC_idP][harq_pid] = 1;

            if (sched_ctl->round[CC_idP][harq_pid] == 4) {
              sched_ctl->round[CC_idP][harq_pid] = 8;     // release HARQ process
              sched_ctl->tbcnt[CC_idP][harq_pid] = 0;  /* TODO: do we have to set it to 0? */
              /* CDRX: PUCCH gives an NACK and max number of repetitions reached so reset corresponding HARQ RTT */
              sched_ctl->harq_rtt_timer[CC_idP][harq_pid] = 0;
            }
          } else if ((num_ack_nak == 2)
                     && (sched_ctl->round[CC_idP][harq_pid] < 8)
                     && (sched_ctl->tbcnt[CC_idP][harq_pid] == 2)
                     && (pdu[0] == 2) && (pdu[1] == 2)) {
            sched_ctl->round[CC_idP][harq_pid]++;

            if (sched_ctl->round[CC_idP][harq_pid] == 4) {
              sched_ctl->round[CC_idP][harq_pid] = 8;     // release HARQ process
              sched_ctl->tbcnt[CC_idP][harq_pid] = 0;
              /* CDRX: PUCCH gives an NACK and max number of repetitions reached so reset corresponding HARQ RTT */
              sched_ctl->harq_rtt_timer[CC_idP][harq_pid] = 0;
            }
          } else
            AssertFatal(1 == 0, "Illegal ACK/NAK/round combination (%d,%d,%d,%d,%d) for harq_pid %d, UE %d/%x\n",
                        num_ack_nak,
                        sched_ctl->round[CC_idP][harq_pid],
                        sched_ctl->round[CC_idP][harq_pid],
                        pdu[0],
                        pdu[1],
                        harq_pid,
                        UE_id,
                        rnti);
        }

        break;

      case 1:   // FDD Channel Selection (10.1.2.2.1), must be received for 2 serving cells
        AssertFatal(numCC == 2, "Should not receive harq indication with channel selection with %d active CCs\n",
                    numCC);

        if ((num_ack_nak == 2)
            && (sched_ctl->round[pCCid][harq_pid] < 8)
            && (sched_ctl->round[1 - pCCid][harq_pid] < 8)
            && (sched_ctl->tbcnt[pCCid][harq_pid] == 1)
            && (sched_ctl->tbcnt[1 - pCCid][harq_pid] == 1)) {
          AssertFatal(pdu[0] <= 3, "pdu[0] %d is not ACK/NAK/DTX\n",
                      pdu[0]);
          AssertFatal(pdu[1] <= 3, "pdu[1] %d is not ACK/NAK/DTX\n",
                      pdu[1]);

          if (pdu[0] == 1)
            sched_ctl->round[pCCid][harq_pid] = 8;
          else {
            sched_ctl->round[pCCid][harq_pid]++;

            if (sched_ctl->round[pCCid][harq_pid] == 4)
              sched_ctl->round[pCCid][harq_pid] = 8;
          }

          if (pdu[1] == 1)
            sched_ctl->round[1 - pCCid][harq_pid] = 8;
          else {
            sched_ctl->round[1 - pCCid][harq_pid]++;

            if (sched_ctl->round[1 - pCCid][harq_pid] == 4)
              sched_ctl->round[1 - pCCid][harq_pid] = 8;
          }
        }     // A=2
        else if ((num_ack_nak == 3)
                 && (sched_ctl->round[pCCid][harq_pid] < 8)
                 && (sched_ctl->tbcnt[pCCid][harq_pid] == 2)
                 && (sched_ctl->round[1 - pCCid][harq_pid] < 8)
                 && (sched_ctl->tbcnt[1 - pCCid][harq_pid] == 1)) {
          AssertFatal(pdu[0] <= 3, "pdu[0] %d is not ACK/NAK/DTX\n",
                      pdu[0]);
          AssertFatal(pdu[1] <= 3, "pdu[1] %d is not ACK/NAK/DTX\n",
                      pdu[1]);
          AssertFatal(pdu[2] <= 3, "pdu[2] %d is not ACK/NAK/DTX\n",
                      pdu[2]);
          AssertFatal(sched_ctl->tbcnt[pCCid][harq_pid] == 2, "sched_ctl->tbcnt[%d][%d] != 2 for UE %d/%x\n",
                      pCCid,
                      harq_pid,
                      UE_id,
                      rnti);
          AssertFatal(sched_ctl->tbcnt[1 - pCCid][harq_pid] == 1, "sched_ctl->tbcnt[%d][%d] != 1 for UE %d/%x\n",
                      1 - pCCid,
                      harq_pid,
                      UE_id,
                      rnti);

          if (pdu[0] == 1 && pdu[1] == 1) { // both ACK
            sched_ctl->round[pCCid][harq_pid] = 8;
            sched_ctl->tbcnt[pCCid][harq_pid] = 0;
          } else if ((pdu[0] == 2 && pdu[1] == 1) || (pdu[0] == 1 && pdu[1] == 2)) {
            sched_ctl->round[pCCid][harq_pid]++;
            sched_ctl->tbcnt[pCCid][harq_pid] = 1;

            if (sched_ctl->round[pCCid][harq_pid] == 4) {
              sched_ctl->round[pCCid][harq_pid] = 8;
              sched_ctl->tbcnt[pCCid][harq_pid] = 0; /* TODO: do we have to set it to 0? */
            }
          } else {
            sched_ctl->round[pCCid][harq_pid]++;

            if (sched_ctl->round[pCCid][harq_pid] == 4) {
              sched_ctl->round[pCCid][harq_pid] = 8;
              sched_ctl->tbcnt[pCCid][harq_pid] = 0;
            }
          }

          if (pdu[2] == 1) sched_ctl->round[1 - pCCid][harq_pid] = 8;
          else {
            sched_ctl->round[1 - pCCid][harq_pid]++;

            if (sched_ctl->round[1 - pCCid][harq_pid] == 4) {
              sched_ctl->round[1 - pCCid][harq_pid] = 8;
            }
          }
        }     // A=3 primary cell has 2 TBs
        else if ((num_ack_nak == 3)
                 && (sched_ctl->round[1 - pCCid][harq_pid] < 8)
                 && (sched_ctl->round[pCCid][harq_pid] < 8)
                 && (sched_ctl->tbcnt[1 - pCCid][harq_pid] == 2)
                 && (sched_ctl->tbcnt[pCCid][harq_pid] == 1)) {
          AssertFatal(pdu[0] <= 3, "pdu[0] %d is not ACK/NAK/DTX\n",
                      pdu[0]);
          AssertFatal(pdu[1] <= 3, "pdu[1] %d is not ACK/NAK/DTX\n",
                      pdu[1]);
          AssertFatal(pdu[2] <= 3, "pdu[2] %d is not ACK/NAK/DTX\n",
                      pdu[2]);
          AssertFatal(sched_ctl->tbcnt[1 - pCCid][harq_pid] == 2, "sched_ctl->tbcnt[%d][%d] != 2 for UE %d/%x\n",
                      1 - pCCid,
                      harq_pid,
                      UE_id,
                      rnti);
          AssertFatal(sched_ctl->tbcnt[pCCid][harq_pid] == 1, "sched_ctl->tbcnt[%d][%d] != 1 for UE %d/%x\n",
                      pCCid,
                      harq_pid,
                      UE_id,
                      rnti);

          if (pdu[0] == 1 && pdu[1] == 1) { // both ACK
            sched_ctl->round[1 - pCCid][harq_pid] = 8;
            sched_ctl->tbcnt[1 - pCCid][harq_pid] = 0;
          } else if ((pdu[0] >= 2 && pdu[1] == 1) || (pdu[0] == 1 && pdu[1] >= 2)) { // one ACK
            sched_ctl->round[1 - pCCid][harq_pid]++;
            sched_ctl->tbcnt[1 - pCCid][harq_pid] = 1;

            if (sched_ctl->round[1 - pCCid][harq_pid] == 4) {
              sched_ctl->round[1 - pCCid][harq_pid] = 8;
              sched_ctl->tbcnt[1 - pCCid][harq_pid] = 0;
            }
          } else {    // both NAK/DTX
            sched_ctl->round[1 - pCCid][harq_pid]++;

            if (sched_ctl->round[1 - pCCid][harq_pid] == 4) {
              sched_ctl->round[1 - pCCid][harq_pid] = 8;
              sched_ctl->tbcnt[1 - pCCid][harq_pid] = 0;
            }
          }

          if (pdu[2] == 1) sched_ctl->round[pCCid][harq_pid] = 8;
          else {
            sched_ctl->round[pCCid][harq_pid]++;

            if (sched_ctl->round[pCCid][harq_pid] == 4) {
              sched_ctl->round[pCCid][harq_pid] = 8;
            }
          }
        }     // A=3 secondary cell has 2 TBs

#if MAX_NUM_CCs>1
        else if ((num_ack_nak == 4)
                 && (sched_ctl->round[0][harq_pid] < 8)
                 && (sched_ctl->round[1][harq_pid] < 8)
                 && (sched_ctl->tbcnt[1 - pCCid][harq_pid] == 2)
                 && (sched_ctl->tbcnt[pCCid][harq_pid] == 2)) {
          AssertFatal(pdu[0] <= 3, "pdu[0] %d is not ACK/NAK/DTX\n",
                      pdu[0]);
          AssertFatal(pdu[1] <= 3, "pdu[1] %d is not ACK/NAK/DTX\n",
                      pdu[1]);
          AssertFatal(pdu[2] <= 3, "pdu[2] %d is not ACK/NAK/DTX\n",
                      pdu[2]);
          AssertFatal(pdu[3] <= 3, "pdu[3] %d is not ACK/NAK/DTX\n",
                      pdu[3]);
          AssertFatal(sched_ctl->tbcnt[0][harq_pid] == 2, "sched_ctl->tbcnt[0][%d] != 2 for UE %d/%x\n",
                      harq_pid,
                      UE_id,
                      rnti);
          AssertFatal(sched_ctl->tbcnt[1][harq_pid] == 2, "sched_ctl->tbcnt[1][%d] != 2 for UE %d/%x\n",
                      harq_pid,
                      UE_id,
                      rnti);

          if (pdu[0] == 1 && pdu[1] == 1) { // both ACK
            sched_ctl->round[0][harq_pid] = 8;
            sched_ctl->tbcnt[0][harq_pid] = 0;
          } else if ((pdu[0] >= 2 && pdu[1] == 1) || (pdu[0] == 1 && pdu[1] >= 2)) { // one ACK
            sched_ctl->round[0][harq_pid]++;
            sched_ctl->tbcnt[0][harq_pid] = 1;

            if (sched_ctl->round[0][harq_pid] == 4) {
              sched_ctl->round[0][harq_pid] = 8;
              sched_ctl->tbcnt[0][harq_pid] = 0;
            }
          } else {    // both NAK/DTX
            sched_ctl->round[0][harq_pid]++;

            if (sched_ctl->round[0][harq_pid] == 4) {
              sched_ctl->round[0][harq_pid] = 8;
              sched_ctl->tbcnt[0][harq_pid] = 0;
            }
          }

          if (pdu[2] == 1 && pdu[3] == 1) { // both ACK
            sched_ctl->round[1][harq_pid] = 8;
            sched_ctl->tbcnt[1][harq_pid] = 0;
          } else if ((pdu[2] >= 2 && pdu[3] == 1) || (pdu[2] == 1 && pdu[3] >= 2)) { // one ACK
            sched_ctl->round[1][harq_pid]++;
            sched_ctl->tbcnt[1][harq_pid] = 1;

            if (sched_ctl->round[1][harq_pid] == 4) {
              sched_ctl->round[1][harq_pid] = 8;
              sched_ctl->tbcnt[1][harq_pid] = 0;
            }
          } else {    // both NAK/DTX
            sched_ctl->round[1][harq_pid]++;

            if (sched_ctl->round[1][harq_pid] == 4) {
              sched_ctl->round[1][harq_pid] = 8;
              sched_ctl->tbcnt[1][harq_pid] = 0;
            }
          }
        }     // A=4 both serving cells have 2 TBs

#endif
        break;

      case 2:   // Format 3
        AssertFatal(numCC > 2, "Should not receive harq indication with FDD format 3 with %d < 3 active CCs\n",
                    numCC);

        for (i = 0, j = 0; i < numCC; i++) {
          if (sched_ctl->round[i][harq_pid] < 8) {
            if (tmode[i] == 1 || tmode[i] == 2 || tmode[0] == 5 || tmode[0] == 6 || tmode[0] == 7) {
              if (pdu[j] == 1) {
                sched_ctl->round[i][harq_pid] = 8;
                sched_ctl->tbcnt[i][harq_pid] = 0;
              } else if (pdu[j] == 2) {
                sched_ctl->round[i][harq_pid]++;

                if (sched_ctl->round[i][harq_pid] == 4) {
                  sched_ctl->round[i][harq_pid] = 8;
                  sched_ctl->tbcnt[i][harq_pid] = 0;
                }
              } else
                AssertFatal(1 == 0, "Illegal harq_ack value for CC %d harq_pid %d (%d) UE %d/%x\n",
                            i,
                            harq_pid,
                            pdu[j],
                            UE_id,
                            rnti);

              j++;
            } else if (spatial_bundling == 0) {
              if (sched_ctl->tbcnt[i][harq_pid] == 2 && pdu[j] == 1 && pdu[j + 1] == 1) {
                sched_ctl->round[i][harq_pid] = 8;
                sched_ctl->tbcnt[i][harq_pid] = 0;
              } else if (sched_ctl->tbcnt[i][harq_pid] == 2 && pdu[j] == 1 && pdu[j + 1] == 2) {
                sched_ctl->round[i][harq_pid]++;
                sched_ctl->tbcnt[i][harq_pid] = 1;

                if (sched_ctl->round[i][harq_pid] == 4) {
                  sched_ctl->round[i][harq_pid] = 8;
                  sched_ctl->tbcnt[i][harq_pid] = 0;
                }
              } else if (sched_ctl->tbcnt[i][harq_pid] == 2 && pdu[j] == 2 && pdu[j + 1] == 1) {
                sched_ctl->round[i][harq_pid]++;
                sched_ctl->tbcnt[i][harq_pid] = 1;

                if (sched_ctl->round[i][harq_pid] == 4) {
                  sched_ctl->round[i][harq_pid] = 8;
                  sched_ctl->tbcnt[i][harq_pid] = 0;
                }
              } else if (sched_ctl->tbcnt[i][harq_pid] == 2 && pdu[j] == 2 && pdu[j + 1] == 2) {
                sched_ctl->round[i][harq_pid]++;

                if (sched_ctl->round[i][harq_pid] == 4) {
                  sched_ctl->round[i][harq_pid] = 8;
                  sched_ctl->tbcnt[i][harq_pid] = 0;
                }
              } else
                AssertFatal(1 == 0, "Illegal combination for CC %d harq_pid %d (%d,%d,%d) UE %d/%x\n",
                            i,
                            harq_pid,
                            sched_ctl->tbcnt[i][harq_pid],
                            pdu[j],
                            pdu[j + 1],
                            UE_id,
                            rnti);

              j += 2;
            } else if (spatial_bundling == 1) {
              if (pdu[j] == 1) {
                sched_ctl->round[i][harq_pid] = 8;
                sched_ctl->tbcnt[i][harq_pid] = 0;
              } else if (pdu[j] == 2) {
                sched_ctl->round[i][harq_pid]++;

                if (sched_ctl->round[i][harq_pid] == 4) {
                  sched_ctl->round[i][harq_pid] = 8;
                  sched_ctl->tbcnt[i][harq_pid] = 0;
                }
              } else {
                AssertFatal(1 == 0, "Illegal hack_nak value %d for CC %d harq_pid %d UE %d/%x\n",
                            pdu[j],
                            i,
                            harq_pid,
                            UE_id,
                            rnti);
              }

              j++;
            } else {
              AssertFatal(1 == 0, "Illegal value for spatial_bundling %d\n",
                          spatial_bundling);
            }
          }
        }

        break;

      case 3:   // Format 4
        AssertFatal(1 == 0, "Should not receive harq indication with Format 4\n");
        break;

      case 4:   // Format 5
        AssertFatal(1 == 0, "Should not receive harq indication with Format 5\n");
        break;
    }
  }

  return;
}

//------------------------------------------------------------------------------
void
extract_pucch_csi(module_id_t mod_idP,
                  int CC_idP,
                  int UE_id,
                  frame_t frameP,
                  sub_frame_t subframeP,
                  uint8_t *pdu,
                  uint8_t length)
//------------------------------------------------------------------------------
{
  UE_info_t *UE_info = &RC.mac[mod_idP]->UE_info;
  UE_sched_ctrl_t *sched_ctl = &UE_info->UE_sched_ctrl[UE_id];
  COMMON_channels_t *cc = &RC.mac[mod_idP]->common_channels[CC_idP];
  int no_pmi;
  uint8_t Ltab[6] = { 0, 2, 4, 4, 4, 4 };
  uint8_t Jtab[6] = { 0, 2, 2, 3, 4, 4 };
  int feedback_cnt;
  AssertFatal(UE_info->UE_template[CC_idP][UE_id].physicalConfigDedicated != NULL, "physicalConfigDedicated is null for UE %d\n",
              UE_id);
  AssertFatal(UE_info->UE_template[CC_idP][UE_id].physicalConfigDedicated->cqi_ReportConfig != NULL, "cqi_ReportConfig is null for UE %d\n",
              UE_id);
  struct LTE_CQI_ReportPeriodic *cqi_ReportPeriodic = UE_info->UE_template[CC_idP][UE_id].physicalConfigDedicated->cqi_ReportConfig->cqi_ReportPeriodic;
  AssertFatal(cqi_ReportPeriodic != NULL, "cqi_ReportPeriodic is null for UE %d\n",
              UE_id);
  // determine feedback mode
  AssertFatal(cqi_ReportPeriodic->present != LTE_CQI_ReportPeriodic_PR_NOTHING, "cqi_ReportPeriodic->present == LTE_CQI_ReportPeriodic_PR_NOTHING!\n");
  AssertFatal(cqi_ReportPeriodic->choice.setup.cqi_FormatIndicatorPeriodic.present != LTE_CQI_ReportPeriodic__setup__cqi_FormatIndicatorPeriodic_PR_NOTHING,
              "cqi_ReportPeriodic->cqi_FormatIndicatorPeriodic.choice.setup.present == LTE_CQI_ReportPeriodic__setup__cqi_FormatIndicatorPeriodic_PR_NOTHING!\n");
  uint16_t Npd = 0;
  uint16_t N_OFFSET_CQI = 0;
  int H, K, bandwidth_part, L, Lmask;
  int ri = sched_ctl->periodic_ri_received[CC_idP];
  get_csi_params(cc,
                 cqi_ReportPeriodic,
                 &Npd,
                 &N_OFFSET_CQI,
                 &H);
  K = (H - 1) / Jtab[cc->mib->message.dl_Bandwidth];
  L = Ltab[cc->mib->message.dl_Bandwidth];
  Lmask = L - 1;
  feedback_cnt = (((frameP * 10) + subframeP) / Npd) % H;

  if (feedback_cnt > 0) bandwidth_part = (feedback_cnt - 1) % K;
  else bandwidth_part = 0;

  switch (get_tmode(mod_idP,
                    CC_idP,
                    UE_id)) {
    case 1:
    case 2:
    case 3:
    case 7:
      no_pmi = 1;
      break;

    case 4:
    case 5:
    case 6:
      no_pmi = 0;
      break;

    default:
      // note: need to check TM8-10 without PMI/RI or with 1 antenna port (see Section 5.2.3.3.1 from 36.213)
      no_pmi = 0;
      break;
  }

  if (cqi_ReportPeriodic->choice.setup.cqi_FormatIndicatorPeriodic.present == LTE_CQI_ReportPeriodic__setup__cqi_FormatIndicatorPeriodic_PR_widebandCQI ||
      feedback_cnt == 0) {
    // Note: This implements only Tables: 5.3.3.1-1,5.3.3.1-1A and 5.3.3.1-2 from 36.213 (1,2,4 antenna ports Wideband CQI/PMI)
    if (no_pmi == 1) {  // get spatial_diffcqi if needed
      sched_ctl->periodic_wideband_cqi[CC_idP] = pdu[0] & 0xF;
      sched_ctl->periodic_wideband_spatial_diffcqi[CC_idP] = (pdu[0] >> 4) & 7;
    } else if (cc->p_eNB == 2 && ri == 1) {
      // p=2 Rank 1 wideband CQI/PMI 6 bits
      sched_ctl->periodic_wideband_cqi[CC_idP] = pdu[0] & 0xF;
      sched_ctl->periodic_wideband_pmi[CC_idP] = (pdu[0] >> 4) & 3;
    } else if (cc->p_eNB == 2 && ri > 1) {
      // p=2 Rank 2 wideband CQI/PMI 8 bits
      sched_ctl->periodic_wideband_cqi[CC_idP] = pdu[0] & 0xF;
      sched_ctl->periodic_wideband_spatial_diffcqi[CC_idP] = (pdu[0] >> 4) & 7;
      sched_ctl->periodic_wideband_pmi[CC_idP] = (pdu[0] >> 7) & 1;
    } else if (cc->p_eNB == 4 && ri == 1) {
      // p=4 Rank 1 wideband CQI/PMI 8 bits
      sched_ctl->periodic_wideband_cqi[CC_idP] = pdu[0] & 0xF;
      sched_ctl->periodic_wideband_pmi[CC_idP] = (pdu[0] >> 4) & 0x0F;
    } else if (cc->p_eNB == 4 && ri > 1) {
      // p=4 Rank 2 wideband CQI/PMI 11 bits
      sched_ctl->periodic_wideband_cqi[CC_idP] = pdu[0] & 0xF;
      sched_ctl->periodic_wideband_spatial_diffcqi[CC_idP] = (pdu[0] >> 4) & 7;
      sched_ctl->periodic_wideband_pmi[CC_idP] = (pdu[0] >> 7) & 0xF;
    } else
      AssertFatal(1 == 0, "illegal combination p %d, ri %d, no_pmi %d\n",
                  cc->p_eNB,
                  ri,
                  no_pmi);
  } else if (cqi_ReportPeriodic->choice.setup.cqi_FormatIndicatorPeriodic.present == LTE_CQI_ReportPeriodic__setup__cqi_FormatIndicatorPeriodic_PR_subbandCQI) {
    // This is Table 5.2.3.3.2-2 for 36.213
    if (ri == 1) {
      //4+Ltab[cc->mib->message.dl_Bandwidth] bits
      sched_ctl->periodic_subband_cqi[CC_idP][(bandwidth_part * L) +((pdu[0] >> 4) & Lmask)] = pdu[0] & 0xF;
    } else if (ri > 1) {
      //7+Ltab[cc->mib->message.dl_Bandwidth] bits;
      sched_ctl->periodic_subband_spatial_diffcqi[CC_idP][(bandwidth_part * L) + ((pdu[0] >> 7) & Lmask)] = (pdu[0] >> 4) & 7;
      sched_ctl->periodic_subband_cqi[CC_idP][(bandwidth_part * L) + ((pdu[0] >> 7) & Lmask)] = pdu[0] & 0xF;
    }
  }

  return;
}

//------------------------------------------------------------------------------
void
extract_pusch_csi(module_id_t mod_idP,
                  int CC_idP,
                  int UE_id,
                  frame_t frameP,
                  sub_frame_t subframeP,
                  uint8_t *pdu,
                  uint8_t length)
//------------------------------------------------------------------------------
{
  UE_info_t *UE_info = &RC.mac[mod_idP]->UE_info;
  COMMON_channels_t *cc = &RC.mac[mod_idP]->common_channels[CC_idP];
  UE_sched_ctrl_t *sched_ctl = &UE_info->UE_sched_ctrl[UE_id];
  int Ntab[6] = { 0, 4, 7, 9, 10, 13 };
  int Ntab_uesel[6] = { 0, 8, 13, 17, 19, 25 };
  int Ltab_uesel[6] = { 0, 6, 9, 13, 15, 18 };
  int Mtab_uesel[6] = { 0, 1, 3, 5, 6, 6 };
  int v[6];
  int i;
  uint64_t p = *(uint64_t *) pdu;
  int curbyte, curbit;
  AssertFatal(UE_info->UE_template[CC_idP][UE_id].physicalConfigDedicated != NULL, "physicalConfigDedicated is null for UE %d\n",
              UE_id);
  AssertFatal(UE_info->UE_template[CC_idP][UE_id].physicalConfigDedicated->cqi_ReportConfig != NULL, "cqi_ReportConfig is null for UE %d\n",
              UE_id);
  LTE_CQI_ReportModeAperiodic_t *cqi_ReportModeAperiodic
    = UE_info->UE_template[CC_idP][UE_id].physicalConfigDedicated->cqi_ReportConfig->cqi_ReportModeAperiodic;
  AssertFatal(cqi_ReportModeAperiodic  != NULL, "cqi_ReportModeAperiodic is null for UE %d\n",
              UE_id);
  int N = Ntab[cc->mib->message.dl_Bandwidth];
  int tmode = get_tmode(mod_idP, CC_idP, UE_id);
  int ri = sched_ctl->aperiodic_ri_received[CC_idP];
  int r, diffcqi0 = 0, diffcqi1 = 0, pmi_uesel = 0;
  int bw = cc->mib->message.dl_Bandwidth;
  int m;

  switch (*cqi_ReportModeAperiodic) {
    case LTE_CQI_ReportModeAperiodic_rm12:
      AssertFatal(0 == 1, "to be fixed, don't use p but pdu directly\n");
      // wideband multiple PMI (TM4/6), Table 5.2.2.6.1-1 (for TM4/6)
      AssertFatal(tmode == 4 || tmode == 6 || tmode == 8 || tmode == 9 || tmode == 10, "Illegal transmission mode %d for CQI_ReportModeAperiodic_rm12\n",
                  tmode);

      if (tmode <= 6) { //Table 5.2.2.6.1-1 36.213
        if (ri == 1 && cc->p_eNB == 2) {
          sched_ctl->aperiodic_wideband_cqi0[CC_idP] = (uint8_t) (p & 0x0F);
          p >>= 4;

          for (i = 0; i < N; i++) {
            sched_ctl->aperiodic_subband_pmi[CC_idP][i] = (uint8_t) (p & 0x03);
            p >>= 2;
          }
        }

        if (ri == 2 && cc->p_eNB == 2) {
          sched_ctl->aperiodic_wideband_cqi0[CC_idP] = (uint8_t) (p & 0x0F);
          p >>= 4;
          sched_ctl->aperiodic_wideband_cqi1[CC_idP] = (uint8_t) (p & 0x0F);
          p >>= 4;

          for (i = 0; i < N; i++) {
            sched_ctl->aperiodic_subband_pmi[CC_idP][i] = (uint8_t) (p & 0x01);
            p >>= 1;
          }
        }

        if (ri == 1 && cc->p_eNB == 4) {
          sched_ctl->aperiodic_wideband_cqi0[CC_idP] = (uint8_t) (p & 0x0F);
          p >>= 4;

          for (i = 0; i < N; i++) {
            sched_ctl->aperiodic_subband_pmi[CC_idP][i] = (uint8_t) (p & 0x03);
            p >>= 4;
          }
        }

        if (ri == 2 && cc->p_eNB == 4) {
          sched_ctl->aperiodic_wideband_cqi0[CC_idP] = (uint8_t) (p & 0x0F);
          p >>= 4;
          sched_ctl->aperiodic_wideband_cqi1[CC_idP] = (uint8_t) (p & 0x0F);
          p >>= 4;

          for (i = 0; i < N; i++) {
            sched_ctl->aperiodic_subband_pmi[CC_idP][i] = (uint8_t) (p & 0x01);
            p >>= 4;
          }
        }
      }     // if (tmode <= 6) { //Table 5.2.2.6.1-1 36.213
      else {
        AssertFatal(1 == 0, "support for TM 8-10 to be done\n");
      }

      break;

    case LTE_CQI_ReportModeAperiodic_rm20:
      AssertFatal(0 == 1, "to be fixed, don't use p but pdu directly\n");
      // UE-selected subband CQI no PMI (TM1/2/3/7) , Table 5.2.2.6.3-1 from 36.213
      AssertFatal(tmode == 1 || tmode == 2 || tmode == 3 || tmode == 7, "Illegal transmission mode %d for CQI_ReportModeAperiodic_rm20\n",
                  tmode);
      sched_ctl->aperiodic_wideband_cqi0[CC_idP] = (uint8_t) (p & 0x0F);
      p >>= 4;
      diffcqi0 = (uint8_t) (p & 0x03);
      p >>= 2;
      r = (uint8_t) (p & ((1 >> Ltab_uesel[bw]) - 1));
      reverse_index(Ntab_uesel[bw],
                    Mtab_uesel[bw],
                    r,
                    v);

      for (m = 0; m < Mtab_uesel[bw]; m++) {
        sched_ctl->aperiodic_subband_diffcqi0[CC_idP][v[m]] = diffcqi0;
      }

      break;

    case LTE_CQI_ReportModeAperiodic_rm22:
      AssertFatal(0 == 1, "to be fixed, don't use p but pdu directly\n");
      // UE-selected subband CQI multiple PMI (TM4/6) Table 5.2.2.6.3-2 from 36.213
      AssertFatal(tmode == 4 || tmode == 6 || tmode == 8 || tmode == 9 || tmode == 10, "Illegal transmission mode %d for CQI_ReportModeAperiodic_rm22\n",
                  tmode);
      sched_ctl->aperiodic_wideband_cqi0[CC_idP] = (uint8_t) (p & 0x0F);
      p >>= 4;
      diffcqi0 = (uint8_t) (p & 0x03);
      p >>= 2;

      if (ri > 1) {
        sched_ctl->aperiodic_wideband_cqi1[CC_idP] =
          (uint8_t) (p & 0x0F);
        p >>= 4;
        diffcqi1 = (uint8_t) (p & 0x03);
        p >>= 2;
      }

      r = (uint8_t) (p & ((1 >> Ltab_uesel[bw]) - 1));
      p >>= Ltab_uesel[bw];
      reverse_index(Ntab_uesel[bw],
                    Mtab_uesel[bw],
                    r,
                    v);

      if (ri == 1 && cc->p_eNB == 2) {
        pmi_uesel = p & 0x3;
        p >>= 2;
        sched_ctl->aperiodic_wideband_pmi[CC_idP] = p & 0x3;
      } else if (ri == 2 && cc->p_eNB == 2) {
        pmi_uesel = p & 0x1;
        p >>= 1;
        sched_ctl->aperiodic_wideband_pmi[CC_idP] = p & 0x1;
      } else if (cc->p_eNB == 4) {
        pmi_uesel = p & 0x0F;
        p >>= 4;
        sched_ctl->aperiodic_wideband_pmi[CC_idP] = p & 0x0F;
      }

      for (m = 0; m < Mtab_uesel[bw]; m++) {
        sched_ctl->aperiodic_subband_diffcqi0[CC_idP][v[m]] = diffcqi0;

        if (ri > 1) sched_ctl->aperiodic_subband_diffcqi1[CC_idP][v[m]] = diffcqi1;

        sched_ctl->aperiodic_subband_pmi[CC_idP][v[m]] = pmi_uesel;
      }

      break;

    case LTE_CQI_ReportModeAperiodic_rm30:
      //subband CQI no PMI (TM1/2/3/7)
      AssertFatal(tmode == 1 || tmode == 2 || tmode == 3 || tmode == 7, "Illegal transmission mode %d for CQI_ReportModeAperiodic_rm30\n",
                  tmode);
      sched_ctl->aperiodic_wideband_cqi0[CC_idP] = pdu[0] >> 4;
      curbyte = 0;
      curbit = 3;

      for (i = 0; i < N; i++) {
        sched_ctl->aperiodic_subband_diffcqi0[CC_idP][i] =
          (pdu[curbyte] >> (curbit - 1)) & 0x03;
        curbit -= 2;

        if (curbit < 0) {
          curbit = 7;
          curbyte++;
        }
      }

      sched_ctl->dl_cqi[CC_idP] = sched_ctl->aperiodic_wideband_cqi0[CC_idP];
      break;

    case LTE_CQI_ReportModeAperiodic_rm31:
      AssertFatal(0 == 1, "to be fixed, don't use p but pdu directly\n");
      //subband CQI single PMI (TM4/5/6)
      AssertFatal(tmode == 4 || tmode == 5 || tmode == 6 || tmode == 8 || tmode == 9 || tmode == 10,
                  "Illegal transmission mode %d for CQI_ReportModeAperiodic_rm31\n",
                  tmode);

      if (ri == 1 && cc->p_eNB == 2) {
        sched_ctl->aperiodic_wideband_cqi0[CC_idP] = (uint8_t) (p & 0x0F);
        p >>= 4;

        for (i = 0; i < N; i++) {
          sched_ctl->aperiodic_subband_diffcqi0[CC_idP][i] = (uint8_t) (p & 0x03);
          p >>= 2;
        }

        sched_ctl->aperiodic_wideband_pmi[CC_idP] = p & 0x03;
      }

      if (ri == 2 && cc->p_eNB == 2) {
        sched_ctl->aperiodic_wideband_cqi0[CC_idP] = (uint8_t) (p & 0x0F);
        p >>= 4;

        for (i = 0; i < N; i++) {
          sched_ctl->aperiodic_subband_pmi[CC_idP][i] = (uint8_t) (p & 0x01);
          p >>= 1;
        }

        sched_ctl->aperiodic_wideband_cqi1[CC_idP] = (uint8_t) (p & 0x0F);
        p >>= 4;

        for (i = 0; i < N; i++) {
          sched_ctl->aperiodic_subband_pmi[CC_idP][i] = (uint8_t) (p & 0x01);
          p >>= 1;
        }

        sched_ctl->aperiodic_wideband_pmi[CC_idP] = p & 0x01;
      }

      if (ri == 1 && cc->p_eNB == 4) {
        sched_ctl->aperiodic_wideband_cqi0[CC_idP] = (uint8_t) (p & 0x0F);
        p >>= 4;

        for (i = 0; i < N; i++) {
          sched_ctl->aperiodic_subband_diffcqi0[CC_idP][i] = (uint8_t) (p & 0x03);
          p >>= 2;
        }

        sched_ctl->aperiodic_wideband_pmi[CC_idP] = p & 0x0F;
      }

      if (ri > 1 && cc->p_eNB == 4) { // Note : 64 bits for 20 MHz
        sched_ctl->aperiodic_wideband_cqi0[CC_idP] = (uint8_t) (p & 0x0F);
        p >>= 4;

        for (i = 0; i < N; i++) {
          sched_ctl->aperiodic_subband_pmi[CC_idP][i] = (uint8_t) (p & 0x01);
          p >>= 1;
        }

        sched_ctl->aperiodic_wideband_cqi1[CC_idP] = (uint8_t) (p & 0x0F);
        p >>= 4;

        for (i = 0; i < N; i++) {
          sched_ctl->aperiodic_subband_pmi[CC_idP][i] = (uint8_t) (p & 0x01);
          p >>= 2;
        }

        sched_ctl->aperiodic_wideband_pmi[CC_idP] = p & 0x0F;
      }

      break;

    case LTE_CQI_ReportModeAperiodic_rm32_v1250:
      AssertFatal(tmode == 4 || tmode == 5 || tmode == 6 || tmode == 8 || tmode == 9 || tmode == 10,
                  "Illegal transmission mode %d for CQI_ReportModeAperiodic_rm32\n",
                  tmode);
      AssertFatal(1 == 0, "CQI_ReportModeAperiodic_rm32 to be done\n");
      break;

    case LTE_CQI_ReportModeAperiodic_rm10_v1310:
      AssertFatal(tmode == 1 || tmode == 2 || tmode == 3 || tmode == 7, "Illegal transmission mode %d for CQI_ReportModeAperiodic_rm10\n",
                  tmode);
      AssertFatal(1 == 0, "CQI_ReportModeAperiodic_rm10 to be done\n");
      break;

    case LTE_CQI_ReportModeAperiodic_rm11_v1310:
      AssertFatal(tmode == 4 || tmode == 5 || tmode == 6 || tmode == 8 || tmode == 9 || tmode == 10,
                  "Illegal transmission mode %d for CQI_ReportModeAperiodic_rm11\n",
                  tmode);
      AssertFatal(1 == 0, "CQI_ReportModeAperiodic_rm11 to be done\n");
      break;
  }

  return;
}

//------------------------------------------------------------------------------
void
cqi_indication(module_id_t mod_idP,
               int CC_idP,
               frame_t frameP,
               sub_frame_t subframeP,
               rnti_t rntiP,
               nfapi_cqi_indication_rel9_t *rel9,
               uint8_t *pdu,
               nfapi_ul_cqi_information_t *ul_cqi_information)
//------------------------------------------------------------------------------
{
  int UE_id = find_UE_id(mod_idP, rntiP);
  UE_info_t *UE_info = &RC.mac[mod_idP]->UE_info;
  uint64_t pdu_val = *(uint64_t *) pdu;

  if (UE_id == -1) {
    LOG_W(MAC, "cqi_indication: UE %x not found\n", rntiP);
    return;
  }

  UE_sched_ctrl_t *sched_ctl = &UE_info->UE_sched_ctrl[UE_id];

  if (UE_id >= 0) {
    LOG_D(MAC,"%s() UE_id:%d channel:%d cqi:%d\n",
          __FUNCTION__,
          UE_id,
          ul_cqi_information->channel,
          ul_cqi_information->ul_cqi);

    if (ul_cqi_information->channel == 0) { // PUCCH
      // extract pucch csi information before changing RI information
      extract_pucch_csi(mod_idP,
                        CC_idP,
                        UE_id,
                        frameP,
                        subframeP,
                        pdu, rel9->length);
      memcpy((void *) sched_ctl->periodic_ri_received,
             (void *) rel9->ri,
             rel9->number_of_cc_reported);
      // SNR for PUCCH2
      sched_ctl->pucch2_snr[CC_idP] = ul_cqi_information->ul_cqi;
    } else {    //PUSCH
      memcpy((void *) sched_ctl->aperiodic_ri_received,
             (void *) rel9->ri,
             rel9->number_of_cc_reported);
      extract_pusch_csi(mod_idP,
                        CC_idP,
                        UE_id,
                        frameP,
                        subframeP,
                        pdu,
                        rel9->length);
      LOG_D(MAC,"Frame %d Subframe %d update CQI:%d pdu 0x%016"PRIx64"\n",
            frameP,
            subframeP,
            sched_ctl->dl_cqi[CC_idP],pdu_val);
      sched_ctl->cqi_req_flag &= (~(1 << subframeP));
      sched_ctl->cqi_received = 1;
    }

    // timing advance
    sched_ctl->timing_advance = rel9->timing_advance;
    sched_ctl->timing_advance_r9 = rel9->timing_advance_r9;
  }

  return;
}

//------------------------------------------------------------------------------
void
SR_indication(module_id_t mod_idP,
              int cc_idP,
              frame_t frameP,
              sub_frame_t subframeP,
              rnti_t rntiP,
              uint8_t ul_cqi)
//------------------------------------------------------------------------------
{
  T(T_ENB_MAC_SCHEDULING_REQUEST,
    T_INT(mod_idP),
    T_INT(cc_idP),
    T_INT(frameP),
    T_INT(subframeP),
    T_INT(rntiP));
  int UE_id = find_UE_id(mod_idP, rntiP);
  UE_info_t *UE_info = &RC.mac[mod_idP]->UE_info;
  UE_sched_ctrl_t *UE_scheduling_ctrl = NULL;

  if (UE_id != -1) {
    UE_scheduling_ctrl = &(UE_info->UE_sched_ctrl[UE_id]);

    if ((UE_scheduling_ctrl->cdrx_configured == true) &&
        (UE_scheduling_ctrl->dci0_ongoing_timer > 0)  &&
        (UE_scheduling_ctrl->dci0_ongoing_timer < 8)) {
      LOG_D(MAC, "[eNB %d][SR %x] Frame %d subframeP %d Signaling SR for UE %d on CC_id %d.  \
                  The SR is not set do to ongoing DCI0 with CDRX activated\n",
            mod_idP,
            rntiP,
            frameP,
            subframeP,
            UE_id,
            cc_idP);
    } else {
      if (mac_eNB_get_rrc_status(mod_idP, UE_RNTI(mod_idP, UE_id)) <  RRC_CONNECTED) {
        LOG_D(MAC, "[eNB %d][SR %x] Frame %d subframeP %d Signaling SR for UE %d on CC_id %d\n",
              mod_idP,
              rntiP,
              frameP,
              subframeP,
              UE_id,
              cc_idP);
      }

      UE_info->UE_template[cc_idP][UE_id].ul_SR = 1;
      UE_info->UE_template[cc_idP][UE_id].ul_active = true;
      VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME(VCD_SIGNAL_DUMPER_FUNCTIONS_SR_INDICATION, 1);
      VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME(VCD_SIGNAL_DUMPER_FUNCTIONS_SR_INDICATION, 0);
    }
  } else {
    LOG_D(MAC, "[eNB %d][SR %x] Frame %d subframeP %d Signaling SR for UE %d (unknown UE_id) on CC_id %d\n",
          mod_idP,
          rntiP,
          frameP,
          subframeP,
          UE_id,
          cc_idP);
  }

  return;
}

//------------------------------------------------------------------------------
void
UL_failure_indication(module_id_t mod_idP,
                      int cc_idP,
                      frame_t frameP,
                      rnti_t rntiP,
                      sub_frame_t subframeP)
//------------------------------------------------------------------------------
{
  int UE_id = find_UE_id(mod_idP, rntiP);
  UE_info_t *UE_info = &RC.mac[mod_idP]->UE_info;

  if (UE_id != -1) {
    LOG_D(MAC, "[eNB %d][UE %d/%x] Frame %d subframeP %d Signaling UL Failure for UE %d on CC_id %d (timer %d)\n",
          mod_idP,
          UE_id,
          rntiP,
          frameP,
          subframeP,
          UE_id,
          cc_idP,
          UE_info->UE_sched_ctrl[UE_id].ul_failure_timer);

    if (UE_info->UE_sched_ctrl[UE_id].ul_failure_timer == 0) UE_info->UE_sched_ctrl[UE_id].ul_failure_timer = 1;
  } else {
    //     AssertFatal(0, "find_UE_id(%u,rnti %d) not found", enb_mod_idP, rntiP);
    //    AssertError(0, 0, "Frame %d: find_UE_id(%u,rnti %d) not found\n", frameP, enb_mod_idP, rntiP);
    LOG_W(MAC, "[eNB %d][SR %x] Frame %d subframeP %d Signaling UL Failure for UE %d (unknown UEid) on CC_id %d\n",
          mod_idP,
          rntiP,
          frameP,
          subframeP,
          UE_id,
          cc_idP);
  }
}

//------------------------------------------------------------------------------
static int
nack_or_dtx_reported(COMMON_channels_t *cc,
                     nfapi_harq_indication_pdu_t *harq_pdu)
//------------------------------------------------------------------------------
{
  int i;

  if (cc->tdd_Config) {
    nfapi_harq_indication_tdd_rel13_t *hi = &harq_pdu->harq_indication_tdd_rel13;

    for (i = 0; i < hi->number_of_ack_nack; i++) {
      if (hi->harq_data[i].bundling.value_0 != 1) //only bundling is used for tdd for now
        return 1;
    }

    return 0;
  }

  nfapi_harq_indication_fdd_rel13_t *hi = &harq_pdu->harq_indication_fdd_rel13;

  for (i = 0; i < hi->number_of_ack_nack; i++) {
    if (hi->harq_tb_n[i] != 1)
      return 1;
  }

  return 0;
}

//------------------------------------------------------------------------------
void
harq_indication(module_id_t mod_idP,
                int CC_idP,
                frame_t frameP,
                sub_frame_t subframeP,
                nfapi_harq_indication_pdu_t *harq_pdu)
//------------------------------------------------------------------------------
{
  rnti_t rnti = harq_pdu->rx_ue_information.rnti;
  uint8_t ul_cqi = harq_pdu->ul_cqi_information.ul_cqi;
  uint8_t channel = harq_pdu->ul_cqi_information.channel;
  int UE_id = find_UE_id(mod_idP, rnti);

  if (UE_id == -1) {
    LOG_W(MAC, "harq_indication: UE %x not found\n",
          rnti);
    return;
  }

  UE_info_t *UE_info = &RC.mac[mod_idP]->UE_info;
  UE_sched_ctrl_t *sched_ctl = &UE_info->UE_sched_ctrl[UE_id];
  COMMON_channels_t *cc = &RC.mac[mod_idP]->common_channels[CC_idP];
  // extract HARQ Information

  if (cc->tdd_Config) {
    extract_harq(mod_idP,
                 CC_idP,
                 UE_id,
                 frameP,
                 subframeP,
                 (void *) &harq_pdu->harq_indication_tdd_rel13,
                 channel);
  } else {
    extract_harq(mod_idP,
                 CC_idP,
                 UE_id,
                 frameP,
                 subframeP,
                 (void *) &harq_pdu->harq_indication_fdd_rel13,
                 channel);
  }

  /* don't care about cqi reporting if NACK/DTX is there */
  if (channel == 0 && !nack_or_dtx_reported(cc,
      harq_pdu)) {
    sched_ctl->pucch1_snr[CC_idP] = (5 * ul_cqi - 640) / 10;
    sched_ctl->pucch1_cqi_update[CC_idP] = 1;
  }

  return;
}
