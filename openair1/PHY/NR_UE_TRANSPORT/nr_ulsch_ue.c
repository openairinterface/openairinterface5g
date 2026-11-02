/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief Top-level routines for transmission of the PUSCH TS 38.211 v 15.4.0
 */
#include "utils.h"
#include <stdint.h>
#include "PHY/NR_REFSIG/dmrs_nr.h"
#include "PHY/NR_REFSIG/ptrs_nr.h"
#include "PHY/NR_REFSIG/nr_refsig.h"
#include "PHY/NR_UE_TRANSPORT/nr_transport_ue.h"
#include "PHY/NR_UE_TRANSPORT/nr_transport_proto_ue.h"
#include "PHY/MODULATION/nr_modulation.h"
#include "PHY/MODULATION/phy_ofdm_mod.h"
#include "common/utils/assertions.h"
#include "common/utils/nr/nr_common.h"
#include "PHY/nr_phy_common/inc/nr_phy_common.h"
#include "PHY/NR_TRANSPORT/nr_transport_common_proto.h"
#include "PHY/NR_TRANSPORT/nr_sch_dmrs.h"
#include "PHY/defs_nr_common.h"
#include "PHY/TOOLS/tools_defs.h"
#include "executables/nr-softmodem.h"
#include "executables/softmodem-common.h"
#include "T_messages_creator.h"
#include "PHY/NR_REFSIG/ul_ref_seq_nr.h"
#include <openair2/UTIL/OPT/opt.h>
#include "PHY/NR_UE_TRANSPORT/pucch_nr.h"
#include <math.h>

#define MAX_RE_PER_SYMBOL_IN_ALLOC (275 * 12)
#define MAX_NLQM (4 * 8)
#define MAX_UCI_CODED_BITS 1024

//#define DEBUG_PUSCH_MAPPING
//#define DEBUG_MAC_PDU
//#define DEBUG_DFT_IDFT

typedef enum {
  BIT_TYPE_ULSCH = 0, // Default: UL-SCH data
  BIT_TYPE_ACK = 1, // HARQ-ACK bit
  BIT_TYPE_ACK_RESERVED = 2, // Reserved for HARQ-ACK data (punctured)
  BIT_TYPE_ACK_PLACEHOLDER = 3, // Reserved for HARQ-ACK placeholders (not scrambled)
  BIT_TYPE_CSI1 = 4, // CSI Part 1 bit
  BIT_TYPE_CSI2 = 5, // CSI Part 2 bit
  BIT_TYPE_ACK_RESERVED_CSI2,
  BIT_TYPE_ACK_PLACEHOLDER_CSI2
} uci_on_pusch_bit_type_t;

static void nr_pusch_codeword_scrambling(uint8_t *in,
                                         uint32_t size,
                                         uint32_t Nid,
                                         uint32_t A,
                                         uint32_t n_RNTI,
                                         const uci_on_pusch_bit_type_t *template,
                                         uint32_t *out)
{
  // no UCI on PUSCH -> optimized scrambling
  if (template == NULL) {
    nr_codeword_scrambling(in, size, 0, Nid, n_RNTI, out);
    return;
  }
  uint32_t *seq = gold_cache((n_RNTI << 15) + Nid, (size + 31) / 32);
  uint32_t num_words = (size + 31) / 32;
  memset(out, 0, num_words * sizeof(uint32_t));
  for (uint32_t i = 0; i < size; i++) {
    uint32_t word_idx = i / 32;
    uint32_t bit_idx  = i % 32;
    uint32_t bit = (in[i / 8] >> (i % 8)) & 1;
    if (template[i] != BIT_TYPE_ACK_PLACEHOLDER && template[i] != BIT_TYPE_ACK_PLACEHOLDER_CSI2)
      bit ^= (seq[word_idx] >> bit_idx) & 1;
    else if (A == 1 && (template[i - 1] == BIT_TYPE_ACK_RESERVED || template[i - 1] == BIT_TYPE_ACK_RESERVED_CSI2)) {
      uint32_t last_word_idx = (i - 1) / 32;
      uint32_t last_bit_idx  = (i - 1) % 32;
      bit ^= (seq[last_word_idx] >> last_bit_idx) & 1;
    }
    out[word_idx] |= (bit << bit_idx);
  }
}

/*
The function pointers are set once before calling the mapping funcion for
all symbols based on different parameters. Then the mapping is done for
each symbol by calling the function pointers.
*/
typedef void (*map_dmrs_func_t)(const unsigned int, const c16_t *, c16_t *);
typedef void (*map_data_dmrs_func_t)(const unsigned int, const c16_t *, c16_t *);

/*
The following set of functions map dmrs and/or data REs in one RB based on
configuration of DMRS type, number of CDM groups with no data and delta.
For all other combinations of the parameters not present below is not
applicable.
*/

/*
DMRS mapping in a RB for Type 1.
Mapping as in TS 38.211 6.4.1.1.3 k = 4n + 2k^prime + delta
*/
static void map_dmrs_type1_cdm1_rb(const unsigned int delta, const c16_t *dmrs, c16_t *out)
{
  *(out + delta) = *dmrs++;
  *(out + delta + 2) = *dmrs++;
  *(out + delta + 4) = *dmrs++;
  *(out + delta + 6) = *dmrs++;
  *(out + delta + 8) = *dmrs++;
  *(out + delta + 10) = *dmrs++;
}

/*
Data in DMRS symbol for Type 1, NumCDMGroupNoData = 1 and delta 0 (antenna port 0 and 1).
There is no data in DMRS symbol for other scenarios in type 1.
*/
static void map_data_dmrs_type1_cdm1_rb(const unsigned int num_cdm_no_data, const c16_t *data, c16_t *out)
{
  UNUSED(num_cdm_no_data);
  *(out + 1) = *data++;
  *(out + 3) = *data++;
  *(out + 5) = *data++;
  *(out + 7) = *data++;
  *(out + 9) = *data++;
  *(out + 11) = *data++;
}

#define NR_DMRS_TYPE2_CDM_GRP_SIZE 2
#define NR_DMRS_TYPE2_NUM_CDM_GRP 3

/*
Map DMRS for type 2
Mapping as in TS 38.211 6.4.1.1.3 k = 6n + k^prime + delta
*/
static void map_dmrs_type2_rb(const unsigned int delta, const c16_t *dmrs, c16_t *out)
{
  memcpy(out + delta, dmrs, sizeof(c16_t) * NR_DMRS_TYPE2_CDM_GRP_SIZE);
  out += (NR_DMRS_TYPE2_CDM_GRP_SIZE * NR_DMRS_TYPE2_NUM_CDM_GRP);
  dmrs += NR_DMRS_TYPE2_CDM_GRP_SIZE;
  memcpy(out + delta, dmrs, sizeof(c16_t) * NR_DMRS_TYPE2_CDM_GRP_SIZE);
}

/*
Map data for type 2 DMRS
*/
static void map_data_dmrs_type2_rb(const unsigned int num_cdm_no_data, const c16_t *data, c16_t *out)
{
  unsigned int offset = num_cdm_no_data * NR_DMRS_TYPE2_CDM_GRP_SIZE;
  const unsigned int size = (NR_DMRS_TYPE2_NUM_CDM_GRP - num_cdm_no_data) * NR_DMRS_TYPE2_CDM_GRP_SIZE;
  memcpy(out + offset, data, sizeof(c16_t) * size);
  offset += NR_DMRS_TYPE2_CDM_GRP_SIZE * NR_DMRS_TYPE2_NUM_CDM_GRP;
  data += size;
  memcpy(out + offset, data, sizeof(c16_t) * size);
}

/*
Map data and PTRS in RB
*/
static void map_data_ptrs(const unsigned int ptrsIdx, const c16_t *data, const c16_t *ptrs, c16_t *out)
{
  memcpy(out, data, sizeof(c16_t) * ptrsIdx);
  data += ptrsIdx;
  *(out + ptrsIdx) = *ptrs;
  memcpy(out + ptrsIdx + 1, data, sizeof(c16_t) * (NR_NB_SC_PER_RB - ptrsIdx - 1));
}

/*
Map data only in RB
*/
static void map_data_rb(const c16_t *data, c16_t *out)
{
  memcpy(out, data, sizeof(c16_t) * NR_NB_SC_PER_RB);
}

/*
Holds params needed for PUSCH resoruce mapping
*/
typedef struct {
  rnti_t rnti;
  unsigned int K_ptrs;
  unsigned int k_RE_ref;
  unsigned int fft_size;
  unsigned int num_rb_max;
  unsigned int symbols_per_slot;
  unsigned int slot;
  unsigned int dmrs_scrambling_id;
  unsigned int scid;
  unsigned int dmrs_port;
  int *Wt;
  int *Wf;
  unsigned int dmrs_symb_pos;
  unsigned int ptrs_symb_pos;
  unsigned int pdu_bit_map;
  transformPrecoder_t transform_precoding;
  unsigned int bwp_start;
  unsigned int start_rb;
  unsigned int nb_rb;
  unsigned int start_symbol;
  unsigned int num_symbols;
  pusch_dmrs_type_t dmrs_type;
  unsigned int delta;
  unsigned int num_cdm_no_data;
} nr_phy_pxsch_params_t;

/*
Map all REs in one OFDM symbol.
txdataF is FFT shifted, i.e. the first negative frequency of the carrier is at
index 0 and the REs of the whole carrier are contiguous, so the allocation is
always a contiguous range of REs.
*/
static void map_current_symbol(const nr_phy_pxsch_params_t p,
                               const bool dmrs_symbol,
                               const bool ptrs_symbol,
                               const c16_t *dmrs_seq,
                               const c16_t *ptrs_seq,
                               const c16_t **data,
                               c16_t *out,
                               map_dmrs_func_t map_dmrs_ptr,
                               map_data_dmrs_func_t map_data_dmrs_ptr)
{
  const unsigned int abs_start_rb = p.bwp_start + p.start_rb;
  const unsigned int start_sc = abs_start_rb * NR_NB_SC_PER_RB;
  const unsigned int n_cdm = p.num_cdm_no_data;
  const c16_t *data_tmp = *data;
  /* If current symbol is DMRS symbol */
  if (dmrs_symbol) {
    const unsigned int dmrs_per_rb = (p.dmrs_type == pusch_dmrs_type1) ? 6 : 4;
    const unsigned int data_per_rb = NR_NB_SC_PER_RB - dmrs_per_rb;

    const c16_t *p_mod_dmrs = dmrs_seq + abs_start_rb * dmrs_per_rb;
    c16_t *out_tmp = out + start_sc;
    for (unsigned int rb = 0; rb < p.nb_rb; rb++) {
      map_dmrs_ptr(p.delta, p_mod_dmrs, out_tmp);
      p_mod_dmrs += dmrs_per_rb;
      out_tmp += NR_NB_SC_PER_RB;
    }

    /* if there is data in current DMRS symbol, we map it here. */
    if (map_data_dmrs_ptr) {
      c16_t *out_tmp = out + start_sc;
      for (unsigned int rb = 0; rb < p.nb_rb; rb++) {
        map_data_dmrs_ptr(n_cdm, data_tmp, out_tmp);
        data_tmp += data_per_rb;
        out_tmp += NR_NB_SC_PER_RB;
      }
    }
  /* If current symbol is a PTRS symbol */
  } else if (ptrs_symbol) {
    const unsigned int first_ptrs_re = get_first_ptrs_re(p.rnti, p.K_ptrs, p.nb_rb, p.k_RE_ref);
    const unsigned int ptrs_idx_re = first_ptrs_re % NR_NB_SC_PER_RB; // PTRS RE index within RB
    const unsigned int non_ptrs_rb = first_ptrs_re / NR_NB_SC_PER_RB; // number of RBs before the first PTRS RB
    int ptrs_idx_rb = -non_ptrs_rb; // RB count to check for PTRS RB
    c16_t *out_tmp = out + start_sc;
    const c16_t *p_mod_ptrs = ptrs_seq;
    /* map data to RBs before the first PTRS RB or if current RB has no PTRS */
    for (unsigned int rb = 0; rb < p.nb_rb; rb++) {
      if (rb < non_ptrs_rb || ptrs_idx_rb % p.K_ptrs) {
        map_data_rb(data_tmp, out_tmp);
        data_tmp += NR_NB_SC_PER_RB;
      } else {
        map_data_ptrs(ptrs_idx_re, data_tmp, p_mod_ptrs, out_tmp);
        p_mod_ptrs++; // increament once as only one PTRS RE per RB
        data_tmp += (NR_NB_SC_PER_RB - 1);
      }
      out_tmp += NR_NB_SC_PER_RB;
      ptrs_idx_rb++;
    }
  } else {
    /* only data in this symbol */
    const unsigned int nb_re = p.nb_rb * NR_NB_SC_PER_RB;
    memcpy(out + start_sc, data_tmp, nb_re * sizeof(c16_t));
    data_tmp += nb_re;
  }
  *data = data_tmp;
}

/*
TS 38.211 table 6.4.1.1.3-1 and 2
*/
static void dmrs_amp_mult(const int Wt,
                          const int Wf[2],
                          const c16_t *mod_dmrs,
                          c16_t *mod_dmrs_out,
                          const uint32_t n_dmrs,
                          const pusch_dmrs_type_t dmrs_type,
                          const unsigned int num_cdm_groups_no_data)
{
  float beta_dmrs_pusch = get_beta_dmrs(num_cdm_groups_no_data, dmrs_type == pusch_dmrs_type2);
  /* short array that hold amplitude for k_prime = 0 and k_prime = 1 */
  int32_t alpha_dmrs[2] __attribute((aligned(16)));
  for (int_fast8_t i = 0; i < sizeofArray(alpha_dmrs); i++) {
    const int32_t a = Wf[i] * Wt * AMP;
    alpha_dmrs[i] = a * beta_dmrs_pusch;
  }

  /* multiply amplitude with complex DMRS vector */
  for (int_fast16_t i = 0; i < n_dmrs; i++) {
    mod_dmrs_out[i] = c16mulRealShift(mod_dmrs[i], alpha_dmrs[i % 2], 15);
  }
}

/*
Map ULSCH data and DMRS in all of the scheduled symbols and PRBs
*/
static void map_symbols(const nr_phy_pxsch_params_t p,
                        const unsigned int slot,
                        const c16_t *dmrs_seq,
                        const c16_t *data,
                        c16_t *out)
{
  // asign the function pointers
  map_dmrs_func_t map_dmrs_ptr = NULL;
  map_data_dmrs_func_t map_data_dmrs_ptr = NULL;
  if (p.dmrs_type == pusch_dmrs_type1) {
    map_dmrs_ptr = map_dmrs_type1_cdm1_rb;
    map_data_dmrs_ptr = (p.num_cdm_no_data == 1) ? map_data_dmrs_type1_cdm1_rb : NULL;
  } else {
    map_dmrs_ptr = map_dmrs_type2_rb;
    map_data_dmrs_ptr = (p.num_cdm_no_data < 3) ? map_data_dmrs_type2_rb : NULL;
  }
  // for all symbols
  const unsigned int n_dmrs = (p.bwp_start + p.start_rb + p.nb_rb) * ((p.dmrs_type == pusch_dmrs_type1) ? 6 : 4);
  const c16_t *cur_data = data;
  uint8_t dmrs_symb_idx = 0;
  for (int l = p.start_symbol; l < p.start_symbol + p.num_symbols; l++) {
    const bool dmrs_symbol = IS_BIT_SET(p.dmrs_symb_pos, l);
    const bool ptrs_symbol = IS_BIT_SET(p.ptrs_symb_pos, l);
    c16_t mod_dmrs_amp[ALNARS_16_4(n_dmrs)] __attribute((aligned(16)));
    c16_t mod_ptrs_amp[ALNARS_16_4(p.nb_rb)] __attribute((aligned(16)));
    const uint32_t *gold = NULL;
    if (dmrs_symbol || ptrs_symbol) {
      gold = nr_gold_pusch(p.num_rb_max, p.symbols_per_slot, p.dmrs_scrambling_id, p.scid, slot, l);
    }
    if (dmrs_symbol) {
      c16_t mod_dmrs[ALNARS_16_4(n_dmrs)] __attribute((aligned(16)));
      if (p.transform_precoding == transformPrecoder_disabled) {
        nr_modulation(gold, n_dmrs * 2, DMRS_MOD_ORDER, (int16_t *)mod_dmrs);
        dmrs_amp_mult(p.Wt[dmrs_symb_idx % 2], p.Wf, mod_dmrs, mod_dmrs_amp, n_dmrs, p.dmrs_type, p.num_cdm_no_data);
      } else {
        dmrs_amp_mult(p.Wt[dmrs_symb_idx % 2], p.Wf, dmrs_seq, mod_dmrs_amp, n_dmrs, p.dmrs_type, p.num_cdm_no_data);
      }
      dmrs_symb_idx++;
    } else if ((p.pdu_bit_map & PUSCH_PDU_BITMAP_PUSCH_PTRS) && ptrs_symbol) {
      AssertFatal(p.transform_precoding == transformPrecoder_disabled, "PTRS NOT SUPPORTED IF TRANSFORM PRECODING IS ENABLED\n");
      c16_t mod_ptrs[ALNARS_16_4(p.nb_rb)] __attribute((aligned(16)));
      nr_modulation(gold, p.nb_rb, DMRS_MOD_ORDER, (int16_t *)mod_ptrs);
      const unsigned int beta_ptrs = 1; // temp value until power control is implemented
      mult_complex_vector_real_scalar(mod_ptrs, beta_ptrs * AMP, mod_ptrs_amp, p.nb_rb);
    }
    map_current_symbol(p,
                       dmrs_symbol,
                       ptrs_symbol,
                       mod_dmrs_amp,
                       mod_ptrs_amp,
                       &cur_data, // increments every symbol
                       out + l * p.fft_size,
                       map_dmrs_ptr,
                       map_data_dmrs_ptr);
  }
}

static uint32_t get_d_factor_re(const uint32_t a, const uint32_t b)
{
  uint32_t d_factor_re;
  if (a >= b) {
    d_factor_re = 1;
  } else {
    d_factor_re = floor((double)b / a);
    if (d_factor_re == 0) {
      d_factor_re = 1;
    }
  }
  return d_factor_re;
}

/*
 * This function calculates the rate matching information for UCI multiplexing with PUSCH
 */
static rate_match_info_uci_t calc_rate_match_info_uci(const nfapi_nr_ue_pusch_pdu_t *pusch_pdu,
                                                      const nr_ptrs_info_t *ptrs_info,
                                                      const NR_UL_UE_HARQ_t *harq_process_ul_ue,
                                                      unsigned int *G)
{
  // get beta offset
  uint8_t beta_offset_index = pusch_pdu->pusch_uci.beta_offset_harq_ack;
  double beta = get_beta_offset_harq_ack(beta_offset_index);

  // get alpha scaling value
  uint8_t alpha_scaling = pusch_pdu->pusch_uci.alpha_scaling;
  double alpha = get_alpha_scaling_value(alpha_scaling);

  // Calculate sumKr (total bits in all code blocks)
  uint32_t sumKr = harq_process_ul_ue->K * harq_process_ul_ue->C;

  int s1 = 0;
  int s2 = 0;
  get_s1_s2(&s1,
            &s2,
            pusch_pdu->rb_size,
            pusch_pdu->nr_of_symbols,
            pusch_pdu->start_symbol_index,
            pusch_pdu->ul_dmrs_symb_pos,
            ptrs_info->ptrs_symbols,
            ptrs_info->n_ptrs);


  rate_match_info_uci_t rminfo = {0};
  // if the number of HARQ-ACK information bits to be transmitted on PUSCH is 0, 1 or 2 bits
  // the number of reserved resource elements for potential HARQ-ACK transmission is calculated using oack = 2
  // according to TS 38.212 section 6.2.7, step 1
  rminfo.O_ack = (pusch_pdu->pusch_uci.harq_ack_bit_length <= 2) ? 2 : pusch_pdu->pusch_uci.harq_ack_bit_length;
  const int nlqm = pusch_pdu->nrOfLayers * pusch_pdu->qam_mod_order; // product of number of layers and modulation order

  // get the number of coded HARQ-ACK symbols and bits, TS 38.212 section 6.3.2.4.1.1 (considering reservetion)
  rminfo.Q_dash_ACK = get_Qd(rminfo.O_ack, beta, alpha, sumKr, s1, s2, 0);
  rminfo.E_uci_ACK = rminfo.Q_dash_ACK * nlqm;
  // actual number of coded HARQ-ACK bits to place
  if (pusch_pdu->pusch_uci.harq_ack_bit_length <= 2) {
    uint16_t Q_dash_ACK_actual = get_Qd(pusch_pdu->pusch_uci.harq_ack_bit_length, beta, alpha, sumKr, s1, s2, 0);
    rminfo.E_uci_ACK_actual = Q_dash_ACK_actual * nlqm;
  }

  // get beta offset for csi
  const double beta_csi1 = get_beta_offset_csi(pusch_pdu->pusch_uci.beta_offset_csi1);

  // get the number of coded CSI part 1 symbols and bits, TS 38.212 section 6.3.2.4.1.2
  const uint16_t ocsi1 = pusch_pdu->pusch_uci.csi_payload.p1_bits;
  rminfo.Q_dash_CSI1 = get_Qd(ocsi1, beta_csi1, alpha, sumKr, s1, s1, rminfo.Q_dash_ACK);
  rminfo.E_uci_CSI1 = rminfo.Q_dash_CSI1 * nlqm;

  // get the number of coded CSI part 2 symbols and bits, TS 38.212 section 6.3.2.4.1.3
  const double beta_csi2 = get_beta_offset_csi(pusch_pdu->pusch_uci.beta_offset_csi2);
  const uint16_t ocsi2 = pusch_pdu->pusch_uci.csi_payload.p2_bits;
  int sub = rminfo.Q_dash_CSI1 + (rminfo.O_ack > 2 ? rminfo.Q_dash_ACK : 0);
  rminfo.Q_dash_CSI2 = get_Qd(ocsi2, beta_csi2, alpha, sumKr, s1, s1, sub);
  rminfo.E_uci_CSI2 = rminfo.Q_dash_CSI2 * nlqm;

  rminfo.G_ulsch = *G - (rminfo.E_uci_CSI1 + rminfo.E_uci_CSI2);
  if (rminfo.O_ack > 2) {
    rminfo.G_ulsch -= rminfo.E_uci_ACK;
  }

  *G = rminfo.G_ulsch;
  LOG_D(PHY, "[UCI_RATE_MATCH] sumKr=%u, s1=%u, s2=%u, Final G_ulsch (output G): %u\n", sumKr, s1, s2, *G);
  LOG_D(PHY,
        "[UCI_RATE_MATCH] rate matching info returned: E_uci_ACK=%u, E_uci_CSI1=%u, E_uci_CSI2=%u, G_ulsch=%u\n",
        rminfo.E_uci_ACK,
        rminfo.E_uci_CSI1,
        rminfo.E_uci_CSI2,
        rminfo.G_ulsch);

  return rminfo;
}

static int initialize_mapping_resources(const nfapi_nr_ue_pusch_pdu_t *pusch_pdu,
                                        const nr_ptrs_info_t *ptrs_info,
                                        uint32_t *m_ulsch_initial,
                                        uint32_t *m_uci_current)
{
  if (!m_ulsch_initial || !m_uci_current)
    return -1;
  const uint8_t n_pusch_sym_all = pusch_pdu->nr_of_symbols;
  const uint16_t ul_dmrs_symb_pos = pusch_pdu->ul_dmrs_symb_pos;
  const uint8_t dmrs_type = pusch_pdu->dmrs_config_type;
  const uint8_t cdm_grps_no_data = pusch_pdu->num_dmrs_cdm_grps_no_data;
  const uint32_t res_per_symbol_non_dmrs = pusch_pdu->rb_size * NR_NB_SC_PER_RB;
  const uint32_t data_re_on_dmrs_sym_per_prb = NR_NB_SC_PER_RB - get_num_dmrs_re_per_rb(dmrs_type, cdm_grps_no_data);

  // Initialize resources per symbol for ULSCH and UCI
  for (uint8_t i = 0; i < n_pusch_sym_all; i++) {
    uint8_t absolute_symbol_idx = pusch_pdu->start_symbol_index + i;
    bool is_ptrs = (ptrs_info->ptrs_symbols >> absolute_symbol_idx) & 0x01;
    int ptrs_overhead = is_ptrs ? ptrs_info->n_ptrs : 0;
    if ((ul_dmrs_symb_pos >> absolute_symbol_idx) & 0x01) {
      // Calculate available data REs on DMRS symbols based on DMRS configuration
      m_ulsch_initial[i] = pusch_pdu->rb_size * data_re_on_dmrs_sym_per_prb - ptrs_overhead;
      m_uci_current[i] = 0; // UCI is not mapped on DMRS symbols
    } else { // Not a DMRS symbol
      m_ulsch_initial[i] = res_per_symbol_non_dmrs - ptrs_overhead;
      m_uci_current[i] = m_ulsch_initial[i];
    }
  }
  return 0;
}

static inline bool skip_mapping_current_uci(const uci_on_pusch_bit_type_t template, const uci_on_pusch_bit_type_t uci_type_to_map)
{
  bool ret = false;
  switch (uci_type_to_map) {
    case BIT_TYPE_ACK_RESERVED:
    case BIT_TYPE_ACK:
      // ACK bits get the highest priority. Don't skip
      ret = false;
      break;

    case BIT_TYPE_CSI1:
      // Skip if already occupied by ACK and reserved ACK. Template is initialized with ULSCH
      ret = (template != BIT_TYPE_ULSCH);
      break;

    case BIT_TYPE_CSI2:
      // Skip if already occupied by ACK but not reserved ACK
      ret = (template != BIT_TYPE_ACK_RESERVED && template != BIT_TYPE_ULSCH);
      break;

    default:
      AssertFatal(0, "Mapping called with incorrect UCI type\n");
  }
  return ret;
}

struct map_uci_common_arg {
  uci_on_pusch_bit_type_t *template;
  uci_on_pusch_bit_type_t uci_type_to_map;
  uint32_t n_symbols;
  uint32_t nlqm;
  uint16_t G_uci;
  uint8_t l1_c;
  uint32_t *m_uci_current;
  uint32_t *m_ulsch_initial;
  uint32_t (*resv_ack_pos_symb)[MAX_UCI_CODED_BITS];
  uint32_t *resv_ack_count_symb;
};

static void map_uci_common(struct map_uci_common_arg p)
{
  uint32_t symbol_start_bit_idx[NR_SYMBOLS_PER_SLOT] = {0};
  for (uint8_t s = 1; s < p.n_symbols; s++) {
    symbol_start_bit_idx[s] = symbol_start_bit_idx[s - 1] + (p.m_ulsch_initial[s - 1] * p.nlqm);
  }

  if (p.uci_type_to_map == BIT_TYPE_ACK_RESERVED)
    memset(p.resv_ack_count_symb, 0, sizeof(*p.resv_ack_count_symb) * NR_SYMBOLS_PER_SLOT);

  uint32_t total_placed = 0;
  for (uint8_t sym = p.l1_c; sym < p.n_symbols && total_placed < p.G_uci; sym++) {
    const uint32_t re_on_sym_total = p.m_ulsch_initial[sym];
    uint32_t uci_re_on_sym = p.m_uci_current[sym];
    if (p.uci_type_to_map == BIT_TYPE_CSI1 && p.resv_ack_count_symb) // need to remove reserved res
      uci_re_on_sym -= p.resv_ack_count_symb[sym] / p.nlqm;
    if (uci_re_on_sym <= 0) {
      continue;
    }

    const uint32_t remaining_to_place = p.G_uci - total_placed;
    const uint32_t num_re_to_select = ceil((double)remaining_to_place / p.nlqm);
    uint32_t d_factor_re = get_d_factor_re(num_re_to_select, uci_re_on_sym);
    uint32_t re_offset = 0;
    uint32_t *cur_sym_resv_ack_pos = p.resv_ack_pos_symb[sym];
    while (re_offset < re_on_sym_total && total_placed < p.G_uci) {
      uci_on_pusch_bit_type_t cur_template = p.template[symbol_start_bit_idx[sym] + (re_offset * p.nlqm)];
      if (skip_mapping_current_uci(cur_template, p.uci_type_to_map)) {
        re_offset++;
        continue; // if RE already allocated to UCI or reserved for ACK
      }
      for (uint32_t bit_in_re = 0; bit_in_re < p.nlqm; bit_in_re++) {
        if (total_placed >= p.G_uci) {
          break;
        }
        uint32_t bit_offset_in_sym = (re_offset * p.nlqm) + bit_in_re;
        uint32_t cw_idx = symbol_start_bit_idx[sym] + bit_offset_in_sym;
        p.template[cw_idx] = p.uci_type_to_map;
        if (p.uci_type_to_map == BIT_TYPE_ACK_RESERVED) {
          cur_sym_resv_ack_pos[p.resv_ack_count_symb[sym]++] = cw_idx;
        }
        total_placed++;
      }
      if (p.uci_type_to_map != BIT_TYPE_ACK_RESERVED)
        p.m_uci_current[sym]--;
      if (p.uci_type_to_map == BIT_TYPE_CSI1 || p.uci_type_to_map == BIT_TYPE_CSI2) {
        uint32_t prev_re_offset = re_offset;
        re_offset += d_factor_re;
        for (uint32_t re = prev_re_offset + 1; re <= re_offset && re < re_on_sym_total; re++) {
          uci_on_pusch_bit_type_t t = p.template[symbol_start_bit_idx[sym] + (re * p.nlqm)];
          if (skip_mapping_current_uci(t, p.uci_type_to_map))
            re_offset++;
        }
      } else {
        re_offset += d_factor_re;
      }
    }
  }
}

/*
 * Maps HARQ-ACK bits when O_ACK <= 2 (overlapped ACK/ULSCH case).
 *
 * The template already has BIT_TYPE_ACK_RESERVED positions marked by map_uci_common,
 * some of which may have been overwritten by CSI2 mapping.
 *
 * This function:
 * 1. Resets all non-CSI2 reserved positions back to BIT_TYPE_ULSCH
 * 2. Selects a subset of reserved positions for actual ACK placement,
 *    marking them as BIT_TYPE_ACK_RESERVED (real ACK bit) or
 *    BIT_TYPE_ACK_PLACEHOLDER (resolved x/y bit, not scrambled), based
 *    on their position within the Qm-bit modulation group:
 *      A=1: pos 0 is real ACK, pos 1+ are placeholders (y at pos 1, x at pos 2+)
 *      A=2: pos 0,1 are real ACK, pos 2+ are placeholders (x only)
 *
 */
static void map_overlapped_ack(uci_on_pusch_bit_type_t *template,
                               uint16_t G_ack,
                               uint8_t l1_c,
                               const nfapi_nr_ue_pusch_pdu_t *pusch_pdu,
                               uint32_t positions_by_sym[][MAX_UCI_CODED_BITS],
                               const uint32_t *count_by_sym)
{
  const int placeholder_start = (pusch_pdu->pusch_uci.harq_ack_bit_length == 1) ? 1 : 2;
  const int Qm = pusch_pdu->qam_mod_order;
  const int nlqm = Qm * pusch_pdu->nrOfLayers;
  uint32_t ack_bits_marked = 0;
  for (uint8_t sym_iter = l1_c; sym_iter < pusch_pdu->nr_of_symbols; sym_iter++) {
    const uint32_t num_reserved_bits_on_sym = count_by_sym[sym_iter];
    if (num_reserved_bits_on_sym == 0)
      continue;
    const uint32_t *reserved_indices_on_this_sym = positions_by_sym[sym_iter];
    // pass 1: reset all non-CSI2 reserved positions to ULSCH
    for (uint32_t i = 0; i < num_reserved_bits_on_sym; i++) {
      uint32_t pos = reserved_indices_on_this_sym[i];
      if (template[pos] != BIT_TYPE_CSI2)
        template[pos] = BIT_TYPE_ULSCH;
    }
    // pass 2: mark selected positions as ACK_RESERVED or PLACEHOLDER
    const int32_t num_ack_remaining = G_ack - ack_bits_marked;
    if (num_ack_remaining <= 0)
      continue;
    AssertFatal(num_reserved_bits_on_sym % nlqm == 0,
                "reserved bits on symbol (%u) not a multiple of nlqm (%d)\n",
                num_reserved_bits_on_sym,
                nlqm);
    const uint32_t num_reserved_re = num_reserved_bits_on_sym / nlqm;
    const uint32_t num_ack_re_remaining = num_ack_remaining / nlqm;
    const uint32_t d_factor_re = get_d_factor_re(num_ack_re_remaining, num_reserved_re);
    for (uint32_t re = 0; re < num_reserved_re && ack_bits_marked < G_ack; re += d_factor_re) {
      for (int b = 0; b < nlqm; b++) {
        uint32_t pos = reserved_indices_on_this_sym[re * nlqm + b];
        int bit_in_group = pos % Qm;
        if (template[pos] == BIT_TYPE_ULSCH) // puncturing ULSCH
          template[pos] = (bit_in_group >= placeholder_start) ? BIT_TYPE_ACK_PLACEHOLDER : BIT_TYPE_ACK_RESERVED;
        else // puncturing CSIp2
          template[pos] = (bit_in_group >= placeholder_start) ? BIT_TYPE_ACK_PLACEHOLDER_CSI2 : BIT_TYPE_ACK_RESERVED_CSI2;
        ack_bits_marked++;
      }
    }
  }
}


/*
 * Applies the template to build the final codeword
 */
#define WRITE_BIT(cw, i, bit) do { if (bit) (cw)[(i) / 8] |= (1 << ((i) % 8)); } while(0)
#define READ_PACKED(arr, idx) (((arr)[(idx) / 64] >> ((idx) % 64)) & 1ULL)

static void apply_template_to_codeword(uint8_t *codeword,
                                       const uci_on_pusch_bit_type_t *template,
                                       rate_match_info_uci_t *rm_info,
                                       uint32_t codeword_len,
                                       const uint8_t *ulsch_bits,
                                       const uint64_t *cack,
                                       const uint64_t *csi1,
                                       const uint64_t *csi2,
                                       uint32_t G_ulsch)
{
  uint32_t ulsch_idx = 0;
  uint32_t ack_idx = 0;
  uint32_t csi1_idx = 0;
  uint32_t csi2_idx = 0;
  memset(codeword, 0, (codeword_len + 7) / 8);

  for (uint32_t i = 0; i < codeword_len; i++) {
    switch (template[i]) {
      case BIT_TYPE_ACK:
        if (rm_info->E_uci_ACK > 0 && ack_idx < rm_info->E_uci_ACK) {
          WRITE_BIT(codeword, i, READ_PACKED(cack, ack_idx));
          ack_idx++;
        }
        break;
      case BIT_TYPE_ACK_RESERVED:
      case BIT_TYPE_ACK_PLACEHOLDER:
        if (rm_info->E_uci_ACK > 0 && ack_idx < rm_info->E_uci_ACK) {
          WRITE_BIT(codeword, i, READ_PACKED(cack, ack_idx));
          ack_idx++;
          if (G_ulsch > 0 && ulsch_idx < G_ulsch)
            ulsch_idx++;
        }
        break;
      case BIT_TYPE_ACK_RESERVED_CSI2:
      case BIT_TYPE_ACK_PLACEHOLDER_CSI2:
        if (rm_info->E_uci_ACK > 0 && ack_idx < rm_info->E_uci_ACK) {
          WRITE_BIT(codeword, i, READ_PACKED(cack, ack_idx));
          ack_idx++;
        }
        // advance csi2_idx for punctured CSI2 bits
        if (rm_info->E_uci_CSI2 > 0 && csi2_idx < rm_info->E_uci_CSI2)
          csi2_idx++;
        break;
      case BIT_TYPE_CSI1:
        if (rm_info->E_uci_CSI1 > 0 && csi1_idx < rm_info->E_uci_CSI1) {
          WRITE_BIT(codeword, i, READ_PACKED(csi1, csi1_idx));
          csi1_idx++;
        }
        break;
      case BIT_TYPE_CSI2:
        if (rm_info->E_uci_CSI2 > 0 && csi2_idx < rm_info->E_uci_CSI2) {
          WRITE_BIT(codeword, i, READ_PACKED(csi2, csi2_idx));
          csi2_idx++;
        }
        break;
      case BIT_TYPE_ULSCH:
      default:
        if (G_ulsch > 0 && ulsch_idx < G_ulsch) {
          WRITE_BIT(codeword, i, (ulsch_bits[ulsch_idx / 8] >> (ulsch_idx % 8)) & 1);
          ulsch_idx++;
        }
        break;
    }
  }
}

/*
 * This function implements the UCI multiplexing on PUSCH according to TS 38.212 section 6.2.7.
 */
static uci_on_pusch_bit_type_t *nr_data_control_mapping(const nfapi_nr_ue_pusch_pdu_t *pusch_pdu,
                                                        const nr_ptrs_info_t *ptrs_info,
                                                        uci_on_pusch_bit_type_t *template,
                                                        unsigned int G_ulsch,
                                                        rate_match_info_uci_t *rm_info,
                                                        uint8_t *codeword,
                                                        uint32_t codeword_len,
                                                        const uint8_t *ulsch_bits,
                                                        const uint64_t *cack,
                                                        const uint64_t *csi1,
                                                        const uint64_t *csi2)
{
  if (!codeword || codeword_len == 0 || !template)
    return NULL;

  const uint8_t n_symbols = pusch_pdu->nr_of_symbols;
  if (n_symbols == 0 || n_symbols > NR_SYMBOLS_PER_SLOT)
    return NULL;

  uint32_t m_ulsch_initial[NR_SYMBOLS_PER_SLOT] = {0};
  uint32_t m_uci_current[NR_SYMBOLS_PER_SLOT] = {0}; // This holds RE counts, not bit counts

  if (initialize_mapping_resources(pusch_pdu, ptrs_info, m_ulsch_initial, m_uci_current) != 0) {
    LOG_E(PHY, "Failed to initialize mapping resources\n");
    return NULL;
  }

  int first_non_dmrs_sym = 0;
  int first_symb_after_dmrs = 0;
  get_dmrs_uci_symbol_info(pusch_pdu->start_symbol_index,
                           pusch_pdu->nr_of_symbols,
                           pusch_pdu->ul_dmrs_symb_pos,
                           &first_non_dmrs_sym,
                           &first_symb_after_dmrs);

  memset(template, 0, codeword_len * sizeof(uci_on_pusch_bit_type_t));

  uint32_t positions_by_sym[NR_SYMBOLS_PER_SLOT][MAX_UCI_CODED_BITS] = {0};
  uint32_t count_by_sym[NR_SYMBOLS_PER_SLOT] = {0};

  struct map_uci_common_arg map_arg = {.template = template,
                                       .n_symbols = pusch_pdu->nr_of_symbols,
                                       .nlqm = pusch_pdu->qam_mod_order * pusch_pdu->nrOfLayers,
                                       .l1_c = first_symb_after_dmrs,
                                       .m_uci_current = m_uci_current,
                                       .m_ulsch_initial = m_ulsch_initial};

  int G_ack = rm_info->E_uci_ACK;
  if (rm_info->O_ack == 2) {
    map_arg.uci_type_to_map = BIT_TYPE_ACK_RESERVED;
    map_arg.G_uci = G_ack;
    map_arg.resv_ack_pos_symb = positions_by_sym;
    map_arg.resv_ack_count_symb = count_by_sym;
    map_uci_common(map_arg);
  } else if (G_ack > 0) {
    map_arg.uci_type_to_map = BIT_TYPE_ACK;
    map_arg.G_uci = G_ack;
    map_uci_common(map_arg);
  }

  // CSI part 1
  map_arg.uci_type_to_map = BIT_TYPE_CSI1;
  map_arg.G_uci = rm_info->E_uci_CSI1;
  map_arg.resv_ack_pos_symb = positions_by_sym;
  map_arg.resv_ack_count_symb = count_by_sym;
  map_arg.l1_c = first_non_dmrs_sym;
  map_uci_common(map_arg);
  // CSI part 2
  map_arg.uci_type_to_map = BIT_TYPE_CSI2;
  map_arg.G_uci = rm_info->E_uci_CSI2;
  map_uci_common(map_arg);

  if (rm_info->O_ack == 2) {
    map_overlapped_ack(template, rm_info->E_uci_ACK_actual, first_symb_after_dmrs, pusch_pdu, positions_by_sym, count_by_sym);
  }

  apply_template_to_codeword(codeword, template, rm_info, codeword_len, ulsch_bits, cack, csi1, csi2, G_ulsch);

  return template;
}

void nr_ue_ulsch_procedures(PHY_VARS_NR_UE *UE,
                            const uint32_t frame,
                            const uint8_t slot,
                            nr_phy_data_tx_t *phy_data,
                            c16_t **txdataF,
                            bool was_symbol_used[NR_SYMBOLS_PER_SLOT])
{

  int harq_pid = phy_data->ulsch.pusch_pdu.pusch_data.harq_process_id;

  if (phy_data->ulsch.status != NR_ACTIVE)
    return;

  start_meas_nr_ue_phy(UE, PUSCH_PROC_STATS);

  uint8_t ULSCH_ids[1];
  unsigned int G[1];
  uint8_t pusch_id = 0;
  ULSCH_ids[pusch_id] = 0;
  LOG_D(PHY, "nr_ue_ulsch_procedures_slot hard_id %d %d.%d prepare for coding\n", harq_pid, frame, slot);

  NR_UE_ULSCH_t *ulsch_ue = &phy_data->ulsch;
  NR_UE_PUCCH *pucch_ue = &phy_data->pucch_vars;
  NR_UL_UE_HARQ_t *harq_process_ul_ue = &UE->ul_harq_processes[harq_pid];
  const nfapi_nr_ue_pusch_pdu_t *pusch_pdu = &ulsch_ue->pusch_pdu;
  const fapi_nr_ul_config_pucch_pdu *pucch_pdu = &pucch_ue->pucch_pdu[0];
  uci_on_pusch_bit_type_t *uci_mapping_template = NULL;

  uint16_t number_dmrs_symbols = 0;

  uint16_t nb_rb = pusch_pdu->rb_size;
  uint8_t number_of_symbols = pusch_pdu->nr_of_symbols;
  uint8_t dmrs_type = pusch_pdu->dmrs_config_type;
  uint8_t cdm_grps_no_data = pusch_pdu->num_dmrs_cdm_grps_no_data;
  uint8_t nb_dmrs_re_per_rb = ((dmrs_type == pusch_dmrs_type1) ? 6 : 4) * cdm_grps_no_data;
  int start_symbol = pusch_pdu->start_symbol_index;
  uint16_t ul_dmrs_symb_pos = pusch_pdu->ul_dmrs_symb_pos;
  uint8_t mod_order = pusch_pdu->qam_mod_order;
  uint8_t Nl = pusch_pdu->nrOfLayers;
  uint32_t tb_size = pusch_pdu->pusch_data.tb_size;
  uint16_t rnti = pusch_pdu->rnti;

  for (int i = start_symbol; i < start_symbol + number_of_symbols; i++) {
    was_symbol_used[i] = true;
    if ((ul_dmrs_symb_pos >> i) & 0x01)
      number_dmrs_symbols += 1;
  }

  ///////////////////////PTRS parameters' initialization///////////////////

  unsigned int K_ptrs = 0, k_RE_ref = 0;
  uint32_t unav_res = 0;
  nr_ptrs_info_t ptrs_info = {0};
  if (pusch_pdu->pdu_bit_map & PUSCH_PDU_BITMAP_PUSCH_PTRS) {
    K_ptrs = pusch_pdu->pusch_ptrs.ptrs_freq_density;
    k_RE_ref = pusch_pdu->pusch_ptrs.ptrs_ports_list[0].ptrs_re_offset;
    uint8_t L_ptrs = 1 << pusch_pdu->pusch_ptrs.ptrs_time_density;
    ptrs_info.ptrs_symbols = get_ptrs_symb_idx(number_of_symbols, start_symbol, L_ptrs, ul_dmrs_symb_pos);
    ptrs_info.n_ptrs = (nb_rb + K_ptrs - 1) / K_ptrs;
    int ptrsSymbPerSlot = get_ptrs_symbols_in_slot(ptrs_info.ptrs_symbols, start_symbol, number_of_symbols);
    unav_res = ptrs_info.n_ptrs * ptrsSymbPerSlot;
  }

  G[pusch_id] = nr_get_G(nb_rb, number_of_symbols, nb_dmrs_re_per_rb, number_dmrs_symbols, unav_res, mod_order, Nl);

  // Capture the initial total PUSCH bits. This is the total_codeword_length for mapping.
  unsigned int G_initial_total_pusch_bits = G[pusch_id];

  uci_on_pusch_bit_type_t template_buffer[G_initial_total_pusch_bits];

  ws_trace_t tmp = {.nr = true,
                    .direction = DIRECTION_UPLINK,
                    .type = UE->frame_parms.frame_type == FDD ? FDD_RADIO : TDD_RADIO,
                    .pdu_buffer = harq_process_ul_ue->payload_AB,
                    .pdu_buffer_size = tb_size,
                    .ueid = 0,
                    .rntiType = WS_C_RNTI,
                    .rnti = rnti,
                    .sysFrame = frame,
                    .subframe = slot,
                    .harq_pid = harq_pid,
                    .oob_event = 0,
                    .oob_event_value = 0};
  trace_pdu(&tmp);

#if T_TRACER
    {
      // capture Tx Payload via T-Tracer
      log_ul_payload_tx_bits(frame, slot, &UE->frame_parms, pusch_pdu,
                             get_num_dmrs(pusch_pdu->ul_dmrs_symb_pos),
                             get_dmrs_port(0, pusch_pdu->dmrs_ports),
                             (const uint8_t *)harq_process_ul_ue->payload_AB,
                             pusch_pdu->pusch_data.tb_size);
    }
#endif

  /////////////////////////ULSCH coding/////////////////////////

  rate_match_info_uci_t rm_info = {0};
  if (nr_ulsch_pre_encoding(harq_process_ul_ue, pusch_pdu->pusch_data.tb_size, pusch_pdu->nrOfLayers, pusch_pdu->ldpcBaseGraph)
      != 0) {
    LOG_E(PHY, "Error pre-encoding\n");
    return;
  }

  bool uci_present = (pusch_pdu->pusch_uci.harq_ack_bit_length != 0) || (pusch_pdu->pusch_uci.csi_payload.p1_bits != 0);
  if (uci_present) {
    rm_info = calc_rate_match_info_uci(pusch_pdu, &ptrs_info, harq_process_ul_ue, &G[pusch_id]);
    if (rm_info.E_uci_ACK > MAX_UCI_CODED_BITS
        || rm_info.E_uci_CSI1 > MAX_UCI_CODED_BITS
        || rm_info.E_uci_CSI2 > MAX_UCI_CODED_BITS) {
      LOG_E(PHY,
            "UCI on PUSCH encoded lengths ACK=%u CSI1=%u CSI2=%u exceed the %u-bit buffer capacity\n",
            rm_info.E_uci_ACK,
            rm_info.E_uci_CSI1,
            rm_info.E_uci_CSI2,
            MAX_UCI_CODED_BITS);
      stop_meas_nr_ue_phy(UE, PUSCH_PROC_STATS);
      return;
    }
  }

  if (nr_ulsch_encoding(UE, ulsch_ue, frame, slot, G, 1, ULSCH_ids) == -1) {
    stop_meas_nr_ue_phy(UE, PUSCH_PROC_STATS);
    return;
  }

  LOG_D(PHY, "nr_ue_ulsch_procedures_slot hard_id %d %d.%d\n", harq_pid, frame, slot);

  NR_DL_FRAME_PARMS *frame_parms = &UE->frame_parms;

  int N_PRB_oh = 0; // higher layer (RRC) parameter xOverhead in PUSCH-ServingCellConfig

  // b is the block of bits transmitted on the physical channel after payload coding
  uint64_t b_ack[MAX_UCI_CODED_BITS / 64] = {0};

  if (pusch_pdu->pusch_uci.harq_ack_bit_length != 0) {
    if (pucch_pdu == NULL) {
      LOG_E(PHY, "nr_ue_ulsch_procedures: pucch_pdu is NULL but HARQ-ACK is present. Cannot proceed with UCI encoding.\n");
      stop_meas_nr_ue_phy(UE, PUSCH_PROC_STATS);
      return;
    }

    nr_uci_encoding(pusch_pdu->pusch_uci.harq_payload,
                    pusch_pdu->pusch_uci.harq_ack_bit_length,
                    rm_info.E_uci_ACK,
                    rm_info.E_uci_ACK,
                    mod_order,
                    &b_ack[0],
                    true);

    LOG_D(PHY,
          "[UCI_ON_PUSCH] G_ulsch=%u (updated G[pusch_id]), G_ack=%u (M_bit), total_len=%u "
          "(G_initial_total_pusch_bits).\n",
          G[pusch_id],
          rm_info.E_uci_ACK,
          G_initial_total_pusch_bits);
  }

  uint64_t b_csi1[MAX_UCI_CODED_BITS / 64] = {0};
  uint64_t b_csi2[MAX_UCI_CODED_BITS / 64] = {0};
  if (pusch_pdu->pusch_uci.csi_payload.p1_bits != 0) {
    nr_uci_encoding(pusch_pdu->pusch_uci.csi_payload.part1_payload,
                    pusch_pdu->pusch_uci.csi_payload.p1_bits,
                    rm_info.E_uci_CSI1,
                    rm_info.E_uci_CSI1,
                    mod_order,
                    &b_csi1[0],
                    true);

    // Process CSI Part 2 if any
    if (pusch_pdu->pusch_uci.csi_payload.p2_bits > 0)
      nr_uci_encoding(pusch_pdu->pusch_uci.csi_payload.part2_payload,
                      pusch_pdu->pusch_uci.csi_payload.p2_bits,
                      rm_info.E_uci_CSI2,
                      rm_info.E_uci_CSI2,
                      mod_order,
                      &b_csi2[0],
                      true);
  }


  uint8_t pusch_codeword[(G_initial_total_pusch_bits + 31) / 32 * 4];
  if (uci_present) {
    start_meas_nr_ue_phy(UE, UCI_ON_PUSCH_MAPPING);
    nr_data_control_mapping(pusch_pdu,
                            &ptrs_info,
                            template_buffer,
                            G[pusch_id],
                            &rm_info,
                            pusch_codeword,
                            G_initial_total_pusch_bits,
                            harq_process_ul_ue->f,
                            b_ack,
                            b_csi1,
                            b_csi2);
    stop_meas_nr_ue_phy(UE, UCI_ON_PUSCH_MAPPING);
    uci_mapping_template = template_buffer;
  } else
    memcpy(pusch_codeword, harq_process_ul_ue->f, (G_initial_total_pusch_bits + 7) / 8);

  uint16_t start_rb = pusch_pdu->rb_start;
  const int start_sc = (start_rb + pusch_pdu->bwp_start) * NR_NB_SC_PER_RB;
  ulsch_ue->Nid_cell = frame_parms->Nid_cell;

  LOG_D(PHY,
        "ulsch TX %x : start_rb %d nb_rb %d mod_order %d Nl %d Tpmi %d bwp_start %d start_sc %d start_symbol %d num_symbols %d "
        "cdmgrpsnodata %d "
        "num_dmrs %d dmrs_re_per_rb %d\n",
        rnti,
        start_rb,
        nb_rb,
        mod_order,
        Nl,
        pusch_pdu->Tpmi,
        pusch_pdu->bwp_start,
        start_sc,
        start_symbol,
        number_of_symbols,
        cdm_grps_no_data,
        number_dmrs_symbols,
        nb_dmrs_re_per_rb);
  // TbD num_of_mod_symbols is set but never used
  const uint32_t N_RE_prime = NR_NB_SC_PER_RB * number_of_symbols - nb_dmrs_re_per_rb * number_dmrs_symbols - N_PRB_oh;
  harq_process_ul_ue->num_of_mod_symbols = N_RE_prime * nb_rb;

  /////////////////////////ULSCH scrambling/////////////////////////

  uint32_t available_bits;

  if (uci_present) {
    // UCI on PUSCH is present, so available bits are the total codeword length
    available_bits = G_initial_total_pusch_bits;
  } else {
    // No UCI on PUSCH, so available bits are the initial G value
    available_bits = G[pusch_id];
  }

  // +1 because size can be not modulo 4 for the uint32_t array
  uint32_t scrambled_output_len_u32 = (available_bits + 31) / 32; // Round up to nearest uint32_t count
  uint32_t scrambled_output[scrambled_output_len_u32];
  memset(scrambled_output, 0, sizeof(scrambled_output));

  nr_pusch_codeword_scrambling(pusch_codeword,
                               available_bits,
                               pusch_pdu->data_scrambling_id,
                               pusch_pdu->pusch_uci.harq_ack_bit_length,
                               rnti,
                               uci_mapping_template,
                               scrambled_output);
  if (UE->phy_sim_test_buf) {
    memcpy(UE->phy_sim_test_buf, scrambled_output, (available_bits + 7) / 8);
  }
#if T_TRACER
    {
      // capture scrambled Tx bits via T-Tracer
      log_ul_scrambled_tx_bits(frame, slot, frame_parms, pusch_pdu,
                               number_dmrs_symbols,
                               get_dmrs_port(0, pusch_pdu->dmrs_ports),
                               (const uint8_t *)scrambled_output,
                               available_bits);
    }
#endif
  /////////////////////////ULSCH modulation/////////////////////////

  int max_num_re = Nl * number_of_symbols * nb_rb * NR_NB_SC_PER_RB;
  c16_t d_mod[max_num_re] __attribute__((aligned(16)));

  nr_modulation(scrambled_output, // assume one codeword for the moment
                available_bits,
                mod_order,
                (int16_t *)d_mod);

  /////////////////////////ULSCH layer mapping/////////////////////////

  const int sz = available_bits / mod_order / Nl;
  c16_t ulsch_mod[Nl][sz];

  nr_ue_layer_mapping(d_mod, Nl, sz, ulsch_mod);

  //////////////////////// ULSCH transform precoding ////////////////////////

  uint8_t u = 0, v = 0;
  c16_t *dmrs_seq = NULL;
  /// Transform-coded "y"-sequences (for definition see 38-211 V15.3.0 2018-09, subsection 6.3.1.4)
  c16_t ulsch_mod_tp[max_num_re] __attribute__((aligned(16)));
  memset(ulsch_mod_tp, 0, sizeof(ulsch_mod_tp));

  if (pusch_pdu->transform_precoding == transformPrecoder_enabled) {
    uint32_t nb_re_pusch = nb_rb * NR_NB_SC_PER_RB;
    uint32_t y_offset = 0;
    uint16_t num_dmrs_res_per_symbol = nb_rb * (NR_NB_SC_PER_RB / 2);

    // Calculate index to dmrs seq array based on number of DMRS Subcarriers on this symbol
    int index = get_index_for_dmrs_lowpapr_seq(num_dmrs_res_per_symbol);
    u = pusch_pdu->dfts_ofdm.low_papr_group_number;
    v = pusch_pdu->dfts_ofdm.low_papr_sequence_number;
    dmrs_seq = dmrs_lowpaprtype1_ul_ref_sig[u][v][index];

    AssertFatal(index >= 0,
                "Num RBs not configured according to 3GPP 38.211 section 6.3.1.4. For PUSCH with transform precoding, num RBs "
                "cannot be multiple "
                "of any other primenumber other than 2,3,5\n");
    AssertFatal(dmrs_seq != NULL, "DMRS low PAPR seq not found, check if DMRS sequences are generated");

    LOG_D(PHY, "Transform Precoding params. u: %d, v: %d, index for dmrsseq: %d\n", u, v, index);

    for (int l = start_symbol; l < start_symbol + number_of_symbols; l++) {
      if ((ul_dmrs_symb_pos >> l) & 0x01)
        /* In the symbol with DMRS no data would be transmitted CDM groups is 2*/
        continue;

      nr_dft(&ulsch_mod_tp[y_offset], &ulsch_mod[0][y_offset], nb_re_pusch);

      y_offset = y_offset + nb_re_pusch;

      LOG_D(PHY, "Transform precoding being done on data- symbol: %d, nb_re_pusch: %d, y_offset: %d\n", l, nb_re_pusch, y_offset);

#ifdef DEBUG_PUSCH_MAPPING
      printf("NR_ULSCH_UE: y_offset %u\t nb_re_pusch %u \t Symbol %d \t nb_rb %d \n", y_offset, nb_re_pusch, l, nb_rb);
#endif
    }

#ifdef DEBUG_DFT_IDFT
    int32_t debug_symbols[MAX_NUM_NR_RE] __attribute__((aligned(16)));
    int offset = 0;
    printf("NR_ULSCH_UE: available_bits: %u, mod_order: %d", available_bits, mod_order);

    for (int ll = 0; ll < (available_bits / mod_order); ll++) {
      debug_symbols[ll] = ulsch_ue->ulsch_mod_tp[ll];
    }

    printf("NR_ULSCH_UE: numSym: %d, num_dmrs_sym: %d", number_of_symbols, number_dmrs_symbols);
    for (int ll = 0; ll < (number_of_symbols - number_dmrs_symbols); ll++) {
      nr_idft(&debug_symbols[offset], nb_re_pusch);
      offset = offset + nb_re_pusch;
    }
    LOG_M("preDFT_all_symbols.m", "UE_preDFT", ulsch_mod[0], number_of_symbols * nb_re_pusch, 1, 1);
    LOG_M("postDFT_all_symbols.m", "UE_postDFT", ulsch_mod_tp, number_of_symbols * nb_re_pusch, 1, 1);
    LOG_M("DEBUG_IDFT_SYMBOLS.m", "UE_Debug_IDFT", debug_symbols, number_of_symbols * nb_re_pusch, 1, 1);
    LOG_M("UE_DMRS_SEQ.m", "UE_DMRS_SEQ", dmrs_seq, nb_re_pusch, 1, 1);
#endif
  }

  /////////////////////////ULSCH RE mapping/////////////////////////

  const int slot_sz = frame_parms->ofdm_symbol_size * frame_parms->symbols_per_slot;
  c16_t tx_precoding[Nl][slot_sz];
  memset(tx_precoding, 0, sizeof(tx_precoding));

  for (int nl = 0; nl < Nl; nl++) {
#ifdef DEBUG_PUSCH_MAPPING
    printf("NR_ULSCH_UE: Value of CELL ID %d /t, u %d \n", frame_parms->Nid_cell, u);
#endif

    const uint8_t dmrs_port = get_dmrs_port(nl, pusch_pdu->dmrs_ports);
    const uint8_t delta = get_delta(dmrs_port, dmrs_type);
    int Wt[2];
    int Wf[2];
    get_Wt(Wt, dmrs_port, dmrs_type);
    get_Wf(Wf, dmrs_port, dmrs_type);

    c16_t *data = (pusch_pdu->transform_precoding == transformPrecoder_enabled) ? ulsch_mod_tp : ulsch_mod[nl];

    nr_phy_pxsch_params_t params = {.rnti = rnti,
                                    .K_ptrs = K_ptrs,
                                    .k_RE_ref = k_RE_ref,
                                    .fft_size = frame_parms->ofdm_symbol_size,
                                    .num_rb_max = frame_parms->N_RB_UL,
                                    .symbols_per_slot = frame_parms->symbols_per_slot,
                                    .dmrs_scrambling_id = pusch_pdu->ul_dmrs_scrambling_id,
                                    .scid = pusch_pdu->scid,
                                    .dmrs_port = dmrs_port,
                                    .Wt = Wt,
                                    .Wf = Wf,
                                    .dmrs_symb_pos = ul_dmrs_symb_pos,
                                    .ptrs_symb_pos = ptrs_info.ptrs_symbols,
                                    .pdu_bit_map = pusch_pdu->pdu_bit_map,
                                    .transform_precoding = pusch_pdu->transform_precoding,
                                    .bwp_start = pusch_pdu->bwp_start,
                                    .start_rb = start_rb,
                                    .nb_rb = nb_rb,
                                    .start_symbol = start_symbol,
                                    .num_symbols = number_of_symbols,
                                    .dmrs_type = dmrs_type,
                                    .delta = delta,
                                    .num_cdm_no_data = cdm_grps_no_data};

    map_symbols(params, slot, dmrs_seq, data, tx_precoding[nl]);

  } // for (nl=0; nl < Nl; nl++)

  /////////////////////////ULSCH precoding/////////////////////////

  /// Layer Precoding and Antenna port mapping
  // ulsch_mod 0-3 are mapped on antenna ports
  // The precoding info is supported by nfapi such as num_prgs, prg_size, prgs_list and pm_idx
  // The same precoding matrix is applied on prg_size RBs, Thus
  //        pmi = prgs_list[rbidx/prg_size].pm_idx, rbidx =0,...,rbSize-1

  // The Precoding matrix:
  for (int ap = 0; ap < frame_parms->nb_antennas_tx; ap++) {
    for (int l = start_symbol; l < start_symbol + number_of_symbols; l++) {
      int k = start_sc;

      for (int rb = 0; rb < nb_rb; rb++) {
        // get pmi info
        uint8_t pmi = pusch_pdu->Tpmi;

        if (pmi == 0) { // unitary Precoding
          const int re_offset = l * frame_parms->ofdm_symbol_size + k;
          if (ap < pusch_pdu->nrOfLayers)
            memcpy(&txdataF[ap][re_offset], &tx_precoding[ap][re_offset], NR_NB_SC_PER_RB * sizeof(c16_t));
          else
            memset(&txdataF[ap][re_offset], 0, NR_NB_SC_PER_RB * sizeof(c16_t));
          k += NR_NB_SC_PER_RB;
        } else {
          // get the precoding matrix weights:
          const char *W_prec;
          switch (frame_parms->nb_antennas_tx) {
            case 1: // 1 antenna port
              W_prec = nr_W_1l_2p[pmi][ap];
              break;
            case 2: // 2 antenna ports
              if (pusch_pdu->nrOfLayers == 1) // 1 layer
                W_prec = nr_W_1l_2p[pmi][ap];
              else // 2 layers
                W_prec = nr_W_2l_2p[pmi][ap];
              break;
            case 4: // 4 antenna ports
              if (pusch_pdu->nrOfLayers == 1) // 1 layer
                W_prec = nr_W_1l_4p[pmi][ap];
              else if (pusch_pdu->nrOfLayers == 2) // 2 layers
                W_prec = nr_W_2l_4p[pmi][ap];
              else if (pusch_pdu->nrOfLayers == 3) // 3 layers
                W_prec = nr_W_3l_4p[pmi][ap];
              else // 4 layers
                W_prec = nr_W_4l_4p[pmi][ap];
              break;
            default:
              LOG_D(PHY, "Precoding 1,2, or 4 antenna ports are currently supported\n");
              W_prec = nr_W_1l_2p[pmi][ap];
              break;
          }

          for (int i = 0; i < NR_NB_SC_PER_RB; i++) {
            int32_t re_offset = l * frame_parms->ofdm_symbol_size + k;
            txdataF[ap][re_offset] = nr_layer_precoder(slot_sz, tx_precoding, W_prec, pusch_pdu->nrOfLayers, re_offset);
            k++;
          }
        }
      } // RB loop
    } // symbol loop
  } // port loop

  stop_meas_nr_ue_phy(UE, PUSCH_PROC_STATS);
}

void nr_tx_rotation_and_ofdm_mod(const uint8_t slot,
                                 const NR_DL_FRAME_PARMS *frame_parms,
                                 const uint8_t n_antenna_ports,
                                 c16_t **txdataF,
                                 c16_t **txdata,
                                 uint32_t linktype,
                                 bool was_symbol_used[NR_SYMBOLS_PER_SLOT],
                                 bool no_phase_pre_comp)
{
  const int N_RB = (linktype == link_type_sl) ? frame_parms->N_RB_SL : frame_parms->N_RB_UL;

  for (int i = 0; i < frame_parms->symbols_per_slot; i++) {
    if (was_symbol_used[i] == false)
      continue;
    for (int ap = 0; ap < n_antenna_ports; ap++) {
      // Phase compensation
      if (!no_phase_pre_comp)
        apply_nr_rotation_TX(frame_parms, txdataF[ap], frame_parms->symbol_rotation[linktype], slot, N_RB, i, 1);
      // Inverse FFT shift: L1 fills txdataF FFT shifted, the IDFT needs the native FFT layout
      fftshift_inverse_inplace(txdataF[ap] + i * frame_parms->ofdm_symbol_size,
                               N_RB * NR_NB_SC_PER_RB,
                               frame_parms->ofdm_symbol_size);
    }
  }

  for (int ap = 0; ap < n_antenna_ports; ap++) {
    if (frame_parms->Ncp == 1) { // extended cyclic prefix
      for (int i = 0; i < frame_parms->symbols_per_slot; i++) {
        if (was_symbol_used[i] == false) {
          memset(&txdata[ap][(frame_parms->ofdm_symbol_size + frame_parms->nb_prefix_samples) * i],
                 0,
                 (frame_parms->nb_prefix_samples + frame_parms->ofdm_symbol_size) * sizeof(int32_t));
          continue;
        }
        PHY_ofdm_mod((int *)&txdataF[ap][frame_parms->ofdm_symbol_size * i],
                     (int *)&txdata[ap][frame_parms->ofdm_symbol_size * i],
                     frame_parms->ofdm_symbol_size,
                     1,
                     frame_parms->nb_prefix_samples,
                     CYCLIC_PREFIX);
      }
    } else { // normal cyclic prefix
      nr_normal_prefix_mod(txdataF[ap], txdata[ap], frame_parms->symbols_per_slot, frame_parms, slot, was_symbol_used);
    }
  }
}
