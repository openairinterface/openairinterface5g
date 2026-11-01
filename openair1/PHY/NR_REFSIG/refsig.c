/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include "nr_refsig.h"
#include "openair1/PHY/gold.h"
#include "ds/hashtable.h"
#include "log.h"

#define GOLD_HT_SIZE 1024
static const int grain = 64 / sizeof(uint32_t); // align to 64 bytes for AVX-512

typedef struct {
  int length;
  uint32_t *seq; // 64-byte aligned sequence data
} gold_entry_t;

static void gold_entry_free(void *ptr)
{
  gold_entry_t *e = (gold_entry_t *)ptr;
  free(e->seq);
  free(e);
}

static uint32_t *gold_generate(uint32_t key, int length)
{
  uint32_t *seq;
  AssertFatal(length > 0, "gold_cache: invalid length=%d\n", length);
  int ret = posix_memalign((void **)&seq, 64, length * sizeof(uint32_t));
  AssertFatal(ret == 0, "gold_generate: out of memory\n");
  unsigned int x1 = 0, x2 = key;
  seq[0] = gold_generic(&x1, &x2, 1);
  for (int n = 1; n < length; n++)
    seq[n] = gold_generic(&x1, &x2, 0);
  return seq;
}

static pthread_key_t gold_table_key;
static pthread_once_t gold_key_once = PTHREAD_ONCE_INIT;

static void delete_table(void *ptr)
{
  hash_table_t *ht = (hash_table_t *)ptr;
  hashtable_destroy(ht, gold_entry_free);
  free(ptr);
}

static void make_table_key(void)
{
  (void)pthread_key_create(&gold_table_key, delete_table);
}

uint32_t *gold_cache(uint32_t key, int length)
{
  (void)pthread_once(&gold_key_once, make_table_key);
  hash_table_t *ht;
  if ((ht = pthread_getspecific(gold_table_key)) == NULL) {
    ht = malloc_or_fail(sizeof(*ht));
    *ht = hashtable_create(GOLD_HT_SIZE, NULL);
    (void)pthread_setspecific(gold_table_key, ht);
  }

  length = ((length + grain - 1) / grain) * grain;

  gold_entry_t *entry = NULL;
  if (hashtable_get(ht, key, (void **)&entry) == HASH_TABLE_OK) {
    if (entry->length >= length)
      return entry->seq;
    // same key but need longer sequence: replace
    hashtable_remove(ht, key, gold_entry_free);
  }

  entry = malloc(sizeof(*entry));
  AssertFatal(entry, "gold_cache: out of memory\n");
  entry->length = length;
  entry->seq = gold_generate(key, length);
  hashtable_insert(ht, key, entry, gold_entry_free);
  LOG_D(PHY, "gold_cache: new entry key=%u len=%d\n", key, length);
  return entry->seq;
}

uint32_t *nr_gold_pbch(int Lmax, int Nid, int n_hf, int l)
{
  int i_ssb = l & (Lmax - 1);
  int i_ssb2 = i_ssb + (n_hf << 2);
  uint32_t x2 = (1 << 11) * (i_ssb2 + 1) * ((Nid >> 2) + 1) + (1 << 6) * (i_ssb2 + 1) + (Nid & 3);
  return gold_cache(x2, NR_PBCH_DMRS_LENGTH_DWORD);
}

uint32_t *nr_gold_pdcch(int N_RB_DL, int symbols_per_slot, unsigned short nid, int ns, int l)
{
  int pdcch_dmrs_init_length = (((N_RB_DL << 1) * 3) >> 5) + 1;
  uint64_t x2tmp0 = (((uint64_t)symbols_per_slot * ns + l + 1) * ((nid << 1) + 1));
  x2tmp0 <<= 17;
  x2tmp0 += (nid << 1);
  uint32_t x2 = x2tmp0 % (1U << 31); // cinit
  LOG_D(PHY, "PDCCH DMRS slot %d, symb %d, Nid %d, x2 %x\n", ns, l, nid, x2);
  return gold_cache(x2, pdcch_dmrs_init_length);
}

uint32_t *nr_gold_pdsch(int N_RB_DL, int symbols_per_slot, int nid, int nscid, int slot, int symbol)
{
  int pdsch_dmrs_init_length = ((N_RB_DL * 24) >> 5) + 1;
  uint64_t x2tmp0 = (((uint64_t)symbols_per_slot * slot + symbol + 1) * (((uint64_t)nid << 1) + 1)) << 17;
  uint32_t x2 = (x2tmp0 + (nid << 1) + nscid) % (1U << 31); // cinit
  LOG_D(PHY, "UE DMRS slot %d, symb %d, nscid %d, x2 %x\n", slot, symbol, nscid, x2);
  return gold_cache(x2, pdsch_dmrs_init_length);
}

uint32_t *nr_gold_pusch(int N_RB_UL, int symbols_per_slot, int Nid, int nscid, int slot, int symbol)
{
  return nr_gold_pdsch(N_RB_UL, symbols_per_slot, Nid, nscid, slot, symbol);
}

uint32_t *nr_gold_csi_rs(int N_RB_DL, int symbols_per_slot, int slot, int symb, uint32_t Nid)
{
  int csi_dmrs_init_length = ((N_RB_DL << 4) >> 5) + 1;
  uint64_t temp_x2 = (1ULL << 10) * ((uint64_t)symbols_per_slot * slot + symb + 1) * ((Nid << 1) + 1) + Nid;
  uint32_t x2 = temp_x2 % (1U << 31);
  return gold_cache(x2, csi_dmrs_init_length);
}

uint32_t *nr_gold_prs(int Nid, int slotNum, int symNum)
{
  LOG_D(PHY, "Initialised NR-PRS sequence for PCI %d\n", Nid);
  // initial x2 for prs as ts138.211
  uint32_t pow22 = 1 << 22;
  uint32_t pow10 = 1 << 10;
  uint32_t c_init1 = pow22 * ceil(Nid / 1024);
  uint32_t c_init2 = pow10 * (slotNum + symNum + 1) * (2 * (Nid % 1024) + 1);
  uint32_t c_init3 = Nid % 1024;
  uint32_t x2 = c_init1 + c_init2 + c_init3;
  return gold_cache(x2, NR_MAX_PRS_INIT_LENGTH_DWORD);
}
