/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*!
 * \brief common utility functions for NR (gNB and UE)
 */

#include <stdint.h>
#include "assertions.h"
#include "common/utils/assertions.h"
#include "common/utils/LOG/log.h"
#include "nr_common.h"
#include <limits.h>
#include <math.h>

#define C_SRS_NUMBER (64)
#define B_SRS_NUMBER (4)

/* TS 38.211 Table 6.4.1.4.3-1: SRS bandwidth configuration */
static const unsigned short srs_bandwidth_config[C_SRS_NUMBER][B_SRS_NUMBER][2] = {
    /*           B_SRS = 0    B_SRS = 1   B_SRS = 2    B_SRS = 3     */
    /* C SRS   m_srs0  N_0  m_srs1 N_1  m_srs2   N_2 m_srs3   N_3    */
    /* 0  */ {{4, 1}, {4, 1}, {4, 1}, {4, 1}},
    /* 1  */ {{8, 1}, {4, 2}, {4, 1}, {4, 1}},
    /* 2  */ {{12, 1}, {4, 3}, {4, 1}, {4, 1}},
    /* 3  */ {{16, 1}, {4, 4}, {4, 1}, {4, 1}},
    /* 4  */ {{16, 1}, {8, 2}, {4, 2}, {4, 1}},
    /* 5  */ {{20, 1}, {4, 5}, {4, 1}, {4, 1}},
    /* 6  */ {{24, 1}, {4, 6}, {4, 1}, {4, 1}},
    /* 7  */ {{24, 1}, {12, 2}, {4, 3}, {4, 1}},
    /* 8  */ {{28, 1}, {4, 7}, {4, 1}, {4, 1}},
    /* 9  */ {{32, 1}, {16, 2}, {8, 2}, {4, 2}},
    /* 10 */ {{36, 1}, {12, 3}, {4, 3}, {4, 1}},
    /* 11 */ {{40, 1}, {20, 2}, {4, 5}, {4, 1}},
    /* 12 */ {{48, 1}, {16, 3}, {8, 2}, {4, 2}},
    /* 13 */ {{48, 1}, {24, 2}, {12, 2}, {4, 3}},
    /* 14 */ {{52, 1}, {4, 13}, {4, 1}, {4, 1}},
    /* 15 */ {{56, 1}, {28, 2}, {4, 7}, {4, 1}},
    /* 16 */ {{60, 1}, {20, 3}, {4, 5}, {4, 1}},
    /* 17 */ {{64, 1}, {32, 2}, {16, 2}, {4, 4}},
    /* 18 */ {{72, 1}, {24, 3}, {12, 2}, {4, 3}},
    /* 19 */ {{72, 1}, {36, 2}, {12, 3}, {4, 3}},
    /* 20 */ {{76, 1}, {4, 19}, {4, 1}, {4, 1}},
    /* 21 */ {{80, 1}, {40, 2}, {20, 2}, {4, 5}},
    /* 22 */ {{88, 1}, {44, 2}, {4, 11}, {4, 1}},
    /* 23 */ {{96, 1}, {32, 3}, {16, 2}, {4, 4}},
    /* 24 */ {{96, 1}, {48, 2}, {24, 2}, {4, 6}},
    /* 25 */ {{104, 1}, {52, 2}, {4, 13}, {4, 1}},
    /* 26 */ {{112, 1}, {56, 2}, {28, 2}, {4, 7}},
    /* 27 */ {{120, 1}, {60, 2}, {20, 3}, {4, 5}},
    /* 28 */ {{120, 1}, {40, 3}, {8, 5}, {4, 2}},
    /* 29 */ {{120, 1}, {24, 5}, {12, 2}, {4, 3}},
    /* 30 */ {{128, 1}, {64, 2}, {32, 2}, {4, 8}},
    /* 31 */ {{128, 1}, {64, 2}, {16, 4}, {4, 4}},
    /* 32 */ {{128, 1}, {16, 8}, {8, 2}, {4, 2}},
    /* 33 */ {{132, 1}, {44, 3}, {4, 11}, {4, 1}},
    /* 34 */ {{136, 1}, {68, 2}, {4, 17}, {4, 1}},
    /* 35 */ {{144, 1}, {72, 2}, {36, 2}, {4, 9}},
    /* 36 */ {{144, 1}, {48, 3}, {24, 2}, {12, 2}},
    /* 37 */ {{144, 1}, {48, 3}, {16, 3}, {4, 4}},
    /* 38 */ {{144, 1}, {16, 9}, {8, 2}, {4, 2}},
    /* 39 */ {{152, 1}, {76, 2}, {4, 19}, {4, 1}},
    /* 40 */ {{160, 1}, {80, 2}, {40, 2}, {4, 10}},
    /* 41 */ {{160, 1}, {80, 2}, {20, 4}, {4, 5}},
    /* 42 */ {{160, 1}, {32, 5}, {16, 2}, {4, 4}},
    /* 43 */ {{168, 1}, {84, 2}, {28, 3}, {4, 7}},
    /* 44 */ {{176, 1}, {88, 2}, {44, 2}, {4, 11}},
    /* 45 */ {{184, 1}, {92, 2}, {4, 23}, {4, 1}},
    /* 46 */ {{192, 1}, {96, 2}, {48, 2}, {4, 12}},
    /* 47 */ {{192, 1}, {96, 2}, {24, 4}, {4, 6}},
    /* 48 */ {{192, 1}, {64, 3}, {16, 4}, {4, 4}},
    /* 49 */ {{192, 1}, {24, 8}, {8, 3}, {4, 2}},
    /* 50 */ {{208, 1}, {104, 2}, {52, 2}, {4, 13}},
    /* 51 */ {{216, 1}, {108, 2}, {36, 3}, {4, 9}},
    /* 52 */ {{224, 1}, {112, 2}, {56, 2}, {4, 14}},
    /* 53 */ {{240, 1}, {120, 2}, {60, 2}, {4, 15}},
    /* 54 */ {{240, 1}, {80, 3}, {20, 4}, {4, 5}},
    /* 55 */ {{240, 1}, {48, 5}, {16, 3}, {8, 2}},
    /* 56 */ {{240, 1}, {24, 10}, {12, 2}, {4, 3}},
    /* 57 */ {{256, 1}, {128, 2}, {64, 2}, {4, 16}},
    /* 58 */ {{256, 1}, {128, 2}, {32, 4}, {4, 8}},
    /* 59 */ {{256, 1}, {16, 16}, {8, 2}, {4, 2}},
    /* 60 */ {{264, 1}, {132, 2}, {44, 3}, {4, 11}},
    /* 61 */ {{272, 1}, {136, 2}, {68, 2}, {4, 17}},
    /* 62 */ {{272, 1}, {68, 4}, {4, 17}, {4, 1}},
    /* 63 */ {{272, 1}, {16, 17}, {8, 2}, {4, 2}},
};

/** @brief 3GPP TS 38.133 Table 10.1.6.1-1 mapping from dBm to RSRP index */
uint8_t get_rsrp_index(int rsrp_dBm)
{
  int index = rsrp_dBm + 157;
  if (rsrp_dBm > -44)
    index = 113;
  if (rsrp_dBm < -140)
    index = 16;
  return (uint8_t)index;
}

#define NUM_BW_ENTRIES 15

static const int tables_5_3_2[5][NUM_BW_ENTRIES] = {
    {25, 52, 79, 106, 133, 160, 188, 216, 242, 270, -1, -1, -1, -1, -1}, // 15 FR1
    {11, 24, 38, 51, 65, 78, 92, 106, 119, 133, 162, 189, 217, 245, 273}, // 30 FR1
    {-1, 11, 18, 24, 31, 38, 44, 51, 58, 65, 79, 93, 107, 121, 135}, // 60 FR1
    {66, 132, 264, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}, // 60 FR2
    {32, 66, 132, 264, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1} // 120FR2
};

int get_supported_band_index(int scs, frequency_range_t freq_range, int n_rbs)
{
  int scs_index = scs + freq_range;
  for (int i = 0; i < NUM_BW_ENTRIES; i++) {
    if(n_rbs == tables_5_3_2[scs_index][i])
      return i;
  }
  return (-1); // not found
}

int get_smallest_supported_bandwidth_index(int scs, frequency_range_t frequency_range, int n_rbs)
{
  int scs_index = scs + frequency_range;
  for (int i = 0; i < NUM_BW_ENTRIES; i++) {
    if (n_rbs <= tables_5_3_2[scs_index][i])
      return i;
  }
  return -1; // not found
}

// Table 5.4.3.3-1 38-101
uint8_t set_ssb_case(int scs, int nr_band)
{
  uint8_t ssb_case = 0;
  switch (scs) {
    case 0:
      ssb_case = 0; // case A
      break;
    case 1:
      if (nr_band == 5 || nr_band == 24 || nr_band == 66 || nr_band == 255)
        ssb_case = 1; // case B
      else
        ssb_case = 2; // case C
      break;
    case 3:
      ssb_case = 3; // case D
      break;
    case 4:
      ssb_case = 4; // case E
      break;
    default:
      AssertFatal(false, "Invalid sub-carrier spacing for SSB\n");
  }
  return ssb_case;
}

// Table 5.2-1 NR operating bands in FR1 & FR2 (3GPP TS 38.101) (Rel.18)
// Table 5.4.2.3-1 Applicable NR-ARFCN per operating band in FR1 & FR2 (3GPP TS 38.101)
// Notes:
// - Frequencies are expressed in KHz
// - col: NR_band ul_min   ul_max    dl_min    dl_max  ul_stepsize dl_stepsize  N_OFFs_UL N_OFFs_DL deltaf_raster
const nr_bandentry_t nr_bandtable[] = {{1,    1920000,  1980000,  2110000,  2170000, 20, 20,  384000,  422000, 100},
                                       {2,    1850000,  1910000,  1930000,  1990000, 20, 20,  370000,  386000, 100},
                                       {3,    1710000,  1785000,  1805000,  1880000, 20, 20,  342000,  361000, 100},
                                       {5,     824000,   849000,   869000,   894000, 20, 20,  164800,  173800, 100},
                                       {7,    2500000,  2570000,  2620000,  2690000, 20, 20,  500000,  524000, 100},
                                       {8,     880000,   915000,   925000,   960000, 20, 20,  176000,  185000, 100},
                                       {12,    699000,   716000,   729000,   746000, 20, 20,  139800,  145800, 100},
                                       {13,    777000,   787000,   746000,   756000, 20, 20,  155400,  149200, 100},
                                       {14,    788000,   798000,   758000,   768000, 20, 20,  157600,  151600, 100},
                                       {18,    815000,   830000,   860000,   875000, 20, 20,  163000,  172000, 100},
                                       {20,    832000,   862000,   791000,   821000, 20, 20,  166400,  158200, 100},
                                       {24,   1626500,  1660500,  1525000,  1559000, 20, 20,  325300,  305000, 100},
                                       {25,   1850000,  1915000,  1930000,  1995000, 20, 20,  370000,  386000, 100},
                                       {26,    814000,   849000,   859000,   894000, 20, 20,  162800,  171800, 100},
                                       {28,    703000,   748000,   758000,   803000, 20, 20,  140600,  151600, 100},
                                       {29,       000,      000,   717000,   728000,  0, 20,       0,  143400, 100},
                                       {30,   2305000,  2315000,  2350000,  2360000, 20, 20,  461000,  470000, 100},
                                       {34,   2010000,  2025000,  2010000,  2025000, 20, 20,  402000,  402000, 100},
                                       {38,   2570000,  2620000,  2570000,  2620000, 20, 20,  514000,  514000, 100},
                                       {39,   1880000,  1920000,  1880000,  1920000, 20, 20,  376000,  376000, 100},
                                       {40,   2300000,  2400000,  2300000,  2400000, 20, 20,  460000,  460000, 100},
                                       {41,   2496000,  2690000,  2496000,  2690000,  3,  3,  499200,  499200,  15},
                                       {41,   2496000,  2690000,  2496000,  2690000,  6,  6,  499200,  499200,  30},
                                       {46,   5150000,  5925000,  5150000,  5925000,  1,  1,  743334,  743334,  15},
                                       {47,   5855000,  5925000,  5855000,  5925000,  1,  1,  790334,  790334,  15},
                                       {48,   3550000,  3700000,  3550000,  3700000,  1,  1,  636667,  636667,  15},
                                       {48,   3550000,  3700000,  3550000,  3700000,  2,  2,  636668,  636668,  30},
                                       {50,   1432000,  1517000,  1432000,  1517000, 20, 20,  286400,  286400, 100},
                                       {51,   1427000,  1432000,  1427000,  1432000, 20, 20,  285400,  285400, 100},
                                       {53,   2483500,  2495000,  2483500,  2495000, 20, 20,  496700,  496700, 100},
                                       {65,   1920000,  2010000,  2110000,  2200000, 20, 20,  384000,  422000, 100},
                                       {66,   1710000,  1780000,  2110000,  2200000, 20, 20,  342000,  422000, 100},
                                       {67,       000,      000,   738000,   758000,  0, 20,       0,  147600, 100},
                                       {70,   1695000,  1710000,  1995000,  2020000, 20, 20,  339000,  399000, 100},
                                       {71,    663000,   698000,   617000,   652000, 20, 20,  132600,  123400, 100},
                                       {74,   1427000,  1470000,  1475000,  1518000, 20, 20,  285400,  295000, 100},
                                       {75,       000,      000,  1432000,  1517000,  0, 20,       0,  286400, 100},
                                       {76,       000,      000,  1427000,  1432000,  0, 20,       0,  285400, 100},
                                       {77,   3300000,  4200000,  3300000,  4200000,  1,  1,  620000,  620000,  15},
                                       {77,   3300000,  4200000,  3300000,  4200000,  2,  2,  620000,  620000,  30},
                                       {78,   3300000,  3800000,  3300000,  3800000,  1,  1,  620000,  620000,  15},
                                       {78,   3300000,  3800000,  3300000,  3800000,  2,  2,  620000,  620000,  30},
                                       {79,   4400000,  5000000,  4400000,  5000000,  1,  1,  693334,  693334,  15},
                                       {79,   4400000,  5000000,  4400000,  5000000,  2,  2,  693334,  693334,  30},
                                       {80,   1710000,  1785000,      000,      000, 20,  0,  342000,       0, 100},
                                       {81,    880000,   915000,      000,      000, 20,  0,  176000,       0, 100},
                                       {82,    832000,   862000,      000,      000, 20,  0,  166400,       0, 100},
                                       {83,    703000,   748000,      000,      000, 20,  0,  140600,       0, 100},
                                       {84,   1920000,  1980000,      000,      000, 20,  0,  384000,       0, 100},
                                       {85,    698000,   716000,   728000,   746000, 20, 20,  139600,  145600, 100},
                                       {86,   1710000,  1780000,      000,      000, 20,  0,  342000,       0, 100},
                                       {89,    824000,   849000,      000,      000, 20,  0,  342000,       0, 100},
                                       {90,   2496000,  2690000,  2496000,  2690000,  3,  3,  499200,  499200,  15},
                                       {90,   2496000,  2690000,  2496000,  2690000,  6,  6,  499200,  499200,  30},
                                       {90,   2496000,  2690000,  2496000,  2690000, 20, 20,  499200,  499200, 100},
                                       {91,    832000,   862000,  1427000,  1432000, 20, 20,  166400,  285400, 100},
                                       {92,    832000,   862000,  1432000,  1517000, 20, 20,  166400,  286400, 100},
                                       {93,    880000,   915000,  1427000,  1432000, 20, 20,  176000,  285400, 100},
                                       {94,    880000,   915000,  1432000,  1517000, 20, 20,  176000,  286400, 100},
                                       {95,   2010000,  2025000,      000,      000, 20, 20,  402000,  402000, 100},
                                       {96,   5925000,  7125000,  5925000,  7125000,  1,  1,  795000,  795000,  15},
                                       {100,   874400,   880000,   919400,   925000, 20, 20,  174880,  174880, 100},
                                       {101,  1900000,  1910000,  1900000,  1910000, 20, 20,  380000,  380000, 100},
                                       {102,  5925000,  6425000,  5925000,  6425000,  1,  1,  795000,  795000,  15},
                                       {104,  6425000,  7125000,  6425000,  7125000,  1,  1,  828334,  828334,  15},
                                       {104,  6425000,  7125000,  6425000,  7125000,  2,  2,  828334,  828334,  30},
                                       {254,  1610000,  1626500,  2483500,  2500000, 20, 20,  322000,  496700, 100},
                                       {254,  1610000,  1626500,  2483500,  2500000,  2,  2,  322000,  496700,  10},
                                       {255,  1626500,  1660500,  1525000,  1559000, 20, 20,  325300,  305000, 100},
                                       {255,  1626500,  1660500,  1525000,  1559000,  2,  2,  325300,  305000,  10},
                                       {256,  1980000,  2010000,  2170000,  2200000, 20, 20,  396000,  434000, 100},
                                       {256,  1980000,  2010000,  2170000,  2200000,  2,  2,  396000,  434000,  10},
                                       {257, 26500020, 29500000, 26500020, 29500000,  1,  1, 2054166, 2054166,  60},
                                       {257, 26500080, 29500000, 26500080, 29500000,  2,  2, 2054167, 2054167, 120},
                                       {258, 24250080, 27500000, 24250080, 27500000,  1,  1, 2016667, 2016667,  60},
                                       {258, 24250080, 27500000, 24250080, 27500000,  2,  2, 2016667, 2016667, 120},
                                       {260, 37000020, 40000000, 37000020, 40000000,  1,  1, 2229166, 2229166,  60},
                                       {260, 37000080, 40000000, 37000080, 40000000,  2,  2, 2229167, 2229167, 120},
                                       {261, 27500040, 28350000, 27500040, 28350000,  1,  1, 2070833, 2070833,  60},
                                       {261, 27500040, 28350000, 27500040, 28350000,  2,  2, 2070833, 2070833, 120},
                                       {510, 27500000, 28350000, 17300000, 20200000,  1,  4, 2070833, 1553336,  60},
                                       {510, 27500000, 28350000, 17300000, 20200000,  2,  8, 2070833, 1553336, 120},
                                       {511, 28350000, 30000000, 17300000, 20200000,  1,  4, 2084999, 1553336,  60},
                                       {511, 28350000, 30000000, 17300000, 20200000,  2,  8, 2084999, 1553336, 120},
                                       {512, 27500000, 30000000, 17300000, 20200000,  1,  4, 2070833, 1553336,  60},
                                       {512, 27500000, 30000000, 17300000, 20200000,  2,  8, 2070833, 1553336, 120}};

// synchronization raster per band tables (Rel.17)
// (38.101-1 Table 5.4.3.3-1 and 38.101-2 Table 5.4.3.3-1)
// band nb, sub-carrier spacing index, Range of gscn (First, Step size, Last)
// clang-format off
const sync_raster_t sync_raster[] = {
  {1, 0, 5279, 1, 5419},
  {2, 0, 4829, 1, 4969},
  {3, 0, 4517, 1, 4693},
  {5, 0, 2177, 1, 2230},
  {5, 1, 2183, 1, 2224},
  {7, 0, 6554, 1, 6718},
  {8, 0, 2318, 1, 2395},
  {12, 0, 1828, 1, 1858},
  {13, 0, 1871, 1, 1885},
  {14, 0, 1901, 1, 1915},
  {18, 0, 2156, 1, 2182},
  {20, 0, 1982, 1, 2047},
  {24, 0, 3818, 1, 3892},
  {24, 1, 3824, 1, 3886},
  {25, 0, 4829, 1, 4981},
  {26, 0, 2153, 1, 2230},
  {28, 0, 1901, 1, 2002},
  {29, 0, 1798, 1, 1813},
  {30, 0, 5879, 1, 5893},
  {34, 0, 5030, 1, 5056},
  {34, 1, 5036, 1, 5050},
  {38, 0, 6431, 1, 6544},
  {38, 1, 6437, 1, 6538},
  {39, 0, 4706, 1, 4795},
  {39, 1, 4712, 1, 4789},
  {40, 1, 5762, 1, 5989},
  {41, 0, 6246, 3, 6717},
  {41, 1, 6252, 3, 6714},
  {46, 1, 8993, 1, 9530},
  {48, 1, 7884, 1, 7982},
  {50, 1, 3590, 1, 3781},
  {51, 0, 3572, 1, 3574},
  {53, 0, 6215, 1, 6232},
  {53, 1, 6221, 1, 6226},
  {65, 0, 5279, 1, 5494},
  {66, 0, 5279, 1, 5494},
  {66, 1, 5285, 1, 5488},
  {67, 0, 1850, 1, 1888},
  {70, 0, 4993, 1, 5044},
  {71, 0, 1547, 1, 1624},
  {74, 0, 3692, 1, 3790},
  {75, 0, 3584, 1, 3787},
  {76, 0, 3572, 1, 3574},
  {77, 1, 7711, 1, 8329},
  {78, 1, 7711, 1, 8051},
  {79, 1, 8480, 16, 8880},
  {85, 0, 1826, 1, 1858},
  {90, 1, 6252, 1, 6714},
  {91, 0, 3572, 1, 3574},
  {92, 0, 3584, 1, 3787},
  {93, 0, 3572, 1, 3574},
  {94, 0, 3584, 1, 3787},
  {96, 1, 9531, 1, 10363},
  {100, 0, 2303, 1, 2307},
  {101, 0, 4754, 1, 4768},
  {101, 1, 4760, 1, 4764},
  {102, 1, 9531, 1, 9877},
  {104, 1, 9882, 7, 10358},
  {254, 0, 6215, 1, 6244},
  {254, 1, 6218, 1, 6241},
  {255, 0, 3818, 1, 3892},
  {255, 1, 3824, 1, 3886},
  {256, 0, 5429, 1, 5494},
  {257, 3, 22388, 1, 22558},
  {257, 4, 22390, 2, 22556},
  {258, 3, 22257, 1, 22443},
  {258, 4, 22258, 2, 22442},
  {260, 3, 22995, 1, 23166},
  {260, 4, 22996, 2, 23164},
  {261, 3, 22446, 1, 22492},
  {261, 4, 22446, 2, 22490},
  {510, 3, 17448, 12, 19428},
  {510, 4, 17472, 24, 19416},
  {511, 3, 17448, 12, 19428},
  {511, 4, 17472, 24, 19416},
  {512, 3, 17448, 12, 19428},
  {512, 4, 17472, 24, 19416},
/***************************/
};
// clang-format on

// Section 5.4.3 of 38.101-1 and -2
void check_ssb_raster(uint64_t freq, int band, int scs)
{
  int start_gscn = 0, step_gscn = 0, end_gscn = 0;
  for (int i = 0; i < sizeof(sync_raster) / sizeof(sync_raster_t); i++) {
    if (sync_raster[i].band == band && sync_raster[i].scs_index == scs) {
      start_gscn = sync_raster[i].first_gscn;
      step_gscn = sync_raster[i].step_gscn;
      end_gscn = sync_raster[i].last_gscn;
      break;
    }
  }
  AssertFatal(start_gscn != 0, "Couldn't find band %d with SCS %d\n", band, scs);
  int gscn;
  if (freq < 3000000000) {
    int N = 0;
    int M = 0;
    for (int k = 0; k < 3; k++) {
      M = (k << 1) + 1;
      if ((freq - M * 50000) % 1200000 == 0) {
        N = (freq - M * 50000) / 1200000;
        break;
      }
    }
    AssertFatal(N != 0, "SSB frequency %lu Hz not on the synchronization raster (N * 1200kHz + M * 50 kHz)\n", freq);
    gscn = (3 * N) + (M - 3) / 2;
  } else if (freq < 24250000000) {
    AssertFatal((freq - 3000000000) % 1440000 == 0,
                "SSB frequency %lu Hz not on the synchronization raster (3000 MHz + N * 1.44 MHz)\n",
                freq);
    gscn = ((freq - 3000000000) / 1440000) + 7499;
  } else {
    AssertFatal((freq - 24250080000) % 17280000 == 0,
                "SSB frequency %lu Hz not on the synchronization raster (24250.08 MHz + N * 17.28 MHz)\n",
                freq);
    gscn = ((freq - 24250080000) / 17280000) + 22256;
  }
  AssertFatal(gscn >= start_gscn && gscn <= end_gscn,
              "GSCN %d corresponding to SSB frequency %lu does not belong to GSCN range for band %d\n",
              gscn,
              freq,
              band);
  int rel_gscn = gscn - start_gscn;
  AssertFatal(rel_gscn % step_gscn == 0,
              "GSCN %d corresponding to SSB frequency %lu not in accordance with GSCN step for band %d\n",
              gscn,
              freq,
              band);
}

int get_supported_bw_mhz(frequency_range_t frequency_range, int bw_index)
{
  if (frequency_range == FR1) {
    int bandwidth_index_to_mhz[] = {5, 10, 15, 20, 25, 30, 35, 40, 45, 50, 60, 70, 80, 90, 100};
    AssertFatal(bw_index >= 0 && bw_index <= sizeofArray(bandwidth_index_to_mhz), "Bandwidth index %d is invalid\n", bw_index);
    return bandwidth_index_to_mhz[bw_index];
  } else {
    int bandwidth_index_to_mhz[] = {50, 100, 200, 400};
    AssertFatal(bw_index >= 0 && bw_index <= sizeofArray(bandwidth_index_to_mhz),
                "Bandwidth index %d is invalid\n",
                bw_index);
    return bandwidth_index_to_mhz[bw_index];
  }
}

bool compare_relative_ul_channel_bw(int nr_band, int scs, int channel_bandwidth, frame_type_t frame_type)
{
  // 38.101-1 section 6.2.2
  // Relative channel bandwidth <= 4% for TDD bands and <= 3% for FDD bands
  int index = get_nr_table_idx(nr_band, scs);
  float limit = frame_type == TDD ? 0.04 : 0.03;
  float rel_bw = (float) (2 * channel_bandwidth * 1000) / (float) (nr_bandtable[index].ul_max - nr_bandtable[index].ul_min);
  return rel_bw > limit;
}

int NRRIV2BW(int locationAndBandwidth,int N_RB) {
  int tmp = locationAndBandwidth/N_RB;
  int tmp2 = locationAndBandwidth%N_RB;
  if (tmp <= ((N_RB>>1)+1) && (tmp+tmp2)<N_RB) return(tmp+1);
  else                      return(N_RB+1-tmp);

}

int NRRIV2PRBOFFSET(int locationAndBandwidth,int N_RB) {
  int tmp = locationAndBandwidth/N_RB;
  int tmp2 = locationAndBandwidth%N_RB;
  if (tmp <= ((N_RB>>1)+1) && (tmp+tmp2)<N_RB) return(tmp2);
  else                      return(N_RB-1-tmp2);
}

/* TS 38.214 ch. 6.1.2.2.2 - Resource allocation type 1 for DL and UL */
int PRBalloc_to_locationandbandwidth0(int NPRB, int RBstart, int BWPsize)
{
  AssertFatal(NPRB>0 && (NPRB + RBstart <= BWPsize),
              "Illegal NPRB/RBstart Configuration (%d,%d) for BWPsize %d\n",
              NPRB, RBstart, BWPsize);

  if (NPRB <= 1 + (BWPsize >> 1))
    return (BWPsize * (NPRB - 1) + RBstart);
  else
    return (BWPsize * (BWPsize + 1 - NPRB) + (BWPsize - 1 - RBstart));
}

int PRBalloc_to_locationandbandwidth(int NPRB,int RBstart) {
  return(PRBalloc_to_locationandbandwidth0(NPRB,RBstart,275));
}

int cce_to_reg_interleaving(const int R, int k, int n_shift, const int C, int L, const int N_regs) {

  int f;  // interleaving function
  if(R==0)
    f = k;
  else {
    int c = k/R;
    int r = k % R;
    f = (r * C + c + n_shift) % (N_regs / L);
  }
  return f;
}

void get_coreset_rballoc(const uint8_t *FreqDomainResource, int *n_rb, int *rb_offset)
{
  uint8_t count=0, start=0, start_set=0;

  uint64_t bitmap = (((uint64_t)FreqDomainResource[0])<<37)|
    (((uint64_t)FreqDomainResource[1])<<29)|
    (((uint64_t)FreqDomainResource[2])<<21)|
    (((uint64_t)FreqDomainResource[3])<<13)|
    (((uint64_t)FreqDomainResource[4])<<5)|
    (((uint64_t)FreqDomainResource[5])>>3);

  for (int i=0; i<45; i++)
    if ((bitmap>>(44-i))&1) {
      count++;
      if (!start_set) {
        start = i;
        start_set = 1;
      }
    }
  *rb_offset = 6*start;
  *n_rb = 6*count;
}

// According to 38.211 7.3.2.2
int get_coreset_num_cces(const uint8_t *FreqDomainResource, int duration)
{
  int num_rbs;
  int rb_offset;
  get_coreset_rballoc(FreqDomainResource, &num_rbs, &rb_offset);
  int total_resource_element_groups = num_rbs * duration;
  int reg_per_cce = 6;
  int total_cces = total_resource_element_groups / reg_per_cce;
  return total_cces;
}

int get_nb_periods_per_frame(uint8_t tdd_period)
{

  int nb_periods_per_frame;
  switch(tdd_period) {
    case 0:
      nb_periods_per_frame = 20; // 10ms/0p5ms
      break;

    case 1:
      nb_periods_per_frame = 16; // 10ms/0p625ms
      break;

    case 2:
      nb_periods_per_frame = 10; // 10ms/1ms
      break;

    case 3:
      nb_periods_per_frame = 8; // 10ms/1p25ms
      break;

    case 4:
      nb_periods_per_frame = 5; // 10ms/2ms
      break;

    case 5:
      nb_periods_per_frame = 4; // 10ms/2p5ms
      break;

    case 6:
      nb_periods_per_frame = 2; // 10ms/5ms
      break;

    case 7:
      nb_periods_per_frame = 1; // 10ms/10ms
      break;

    default:
      AssertFatal(1==0,"Undefined tdd period %d\n", tdd_period);
  }
  return nb_periods_per_frame;
}

uint32_t to_nrarfcn(uint64_t CarrierFreq)
{
  uint64_t CarrierFreq_by_1k = CarrierFreq / 1000;
  // FR2
  int deltaFglobal = 60;
  uint32_t N_REF_Offs = 2016667;
  uint64_t F_REF_Offs_khz = 24250080;
  // FR1
  if (CarrierFreq < 3e9) {
    deltaFglobal = 5;
    N_REF_Offs = 0;
    F_REF_Offs_khz = 0;
  } else if (CarrierFreq < 24.25e9) {
    deltaFglobal = 15;
    N_REF_Offs = 600000;
    F_REF_Offs_khz = 3000000;
  }

  // This is equation before Table 5.4.2.1-1 in 38101-1-f30
  uint32_t nrarfcn =  (((CarrierFreq_by_1k - F_REF_Offs_khz) / deltaFglobal) + N_REF_Offs);
  return nrarfcn;
}

// This function computes the RF reference frequency from the NR-ARFCN according to 5.4.2.1 of 3GPP TS 38.104
// this function applies to both DL and UL
uint64_t from_nrarfcn(int nr_bandP, uint8_t scs_index, uint32_t nrarfcn)
{
  int deltaFglobal = 5;
  uint32_t N_REF_Offs = 0;
  uint64_t F_REF_Offs_khz = 0;
  int i = get_nr_table_idx(nr_bandP, scs_index);

  if (nrarfcn > 599999 && nrarfcn < 2016667) {
    deltaFglobal = 15;
    N_REF_Offs = 600000;
    F_REF_Offs_khz = 3000000;
  }
  if (nrarfcn > 2016666 && nrarfcn < 3279166) {
    deltaFglobal = 60;
    N_REF_Offs = 2016667;
    F_REF_Offs_khz = 24250080;
  }

  // First check if the ARFCN is on the RASTER
  uint32_t stepsize = nr_bandtable[i].dl_stepsize;
  uint32_t N_OFFs = nr_bandtable[i].N_OFFs_DL;

  if (nrarfcn >= nr_bandtable[i].N_OFFs_UL) {
    if (nr_bandtable[i].N_OFFs_UL > N_OFFs || nrarfcn < N_OFFs) {
      N_OFFs = nr_bandtable[i].N_OFFs_UL;
      stepsize = nr_bandtable[i].ul_stepsize;
    }
  }

  LOG_D(NR_MAC, "N_OFFs %u, deltaFglobal %d KHz, stepsize:%d\n", N_OFFs, deltaFglobal, stepsize);

  AssertFatal(nrarfcn >= N_OFFs,"nrarfcn %u < N_OFFs[%d] %u\n", nrarfcn, nr_bandtable[i].band, N_OFFs);

  if ((nrarfcn - N_OFFs) % stepsize != 0)
    LOG_E(NR_MAC, "nrarfcn %u is not on the channel raster for step size %u. N_OFFS:%d\n",
                  nrarfcn, stepsize, N_OFFs);

  uint64_t frequency = 1000 * (F_REF_Offs_khz + (nrarfcn - N_REF_Offs) * deltaFglobal);
  LOG_I(NR_MAC, "Computing frequency (nrarfcn %llu => %llu KHz, NR band %d\n",
        (unsigned long long)nrarfcn,
        (unsigned long long)frequency/1000,
        nr_bandP);

  return frequency;
}

/**
 * @brief Get the slot index within the period
 */
int get_slot_idx_in_period(const int slot, const frame_structure_t *fs)
{
  return slot % fs->numb_slots_period;
}

int get_dmrs_port(int nl, uint16_t dmrs_ports)
{
  if (dmrs_ports == 0)
    return 0; // dci 1_0
  int p = -1;
  int found = -1;
  for (int i = 0; i < 12; i++) { // loop over dmrs ports
    if((dmrs_ports >> i) & 0x01) { // check if current bit is 1
      found++;
      if (found == nl) { // found antenna port number corresponding to current layer
        p = i;
        break;
      }
    }
  }
  AssertFatal(p > -1, "No dmrs port corresponding to layer %d found\n", nl);
  return p;
}

frame_type_t get_frame_type(uint16_t current_band, uint8_t scs_index)
{
  int32_t delta_duplex = get_delta_duplex(current_band, scs_index);
  frame_type_t current_type = delta_duplex == 0 ? TDD : FDD;
  LOG_D(NR_MAC, "NR band %d, duplex mode %s, duplex spacing = %d KHz\n", current_band, duplex_mode_txt[current_type], delta_duplex);
  return current_type;
}

// Computes the duplex spacing (either positive or negative) in KHz
int32_t get_delta_duplex(int nr_bandP, uint8_t scs_index)
{
  int nr_table_idx = get_nr_table_idx(nr_bandP, scs_index);

  int32_t delta_duplex = (nr_bandtable[nr_table_idx].ul_min - nr_bandtable[nr_table_idx].dl_min);

  LOG_D(NR_MAC, "NR band duplex spacing is %d KHz (nr_bandtable[%d].band = %d)\n", delta_duplex, nr_table_idx, nr_bandtable[nr_table_idx].band);

  return delta_duplex;
}

// Returns the corresponding row index of the NR table
int get_nr_table_idx(int nr_bandP, uint8_t scs_index)
{
  int scs_khz = 15 << scs_index;
  int supplementary_bands[] = {29, 75, 76, 80, 81, 82, 83, 84, 86, 89, 95};
  for(int j = 0; j < sizeofArray(supplementary_bands); j++) {
    AssertFatal(nr_bandP != supplementary_bands[j],
                "Band %d is a supplementary band (%d). This is not supported yet.\n",
                nr_bandP,
                supplementary_bands[j]);
  }
  int i;
  for (i = 0; i < sizeofArray(nr_bandtable); i++) {
    if (nr_bandtable[i].band == nr_bandP && nr_bandtable[i].deltaf_raster == scs_khz)
      break;
  }

  if (i == sizeofArray(nr_bandtable)) {
    LOG_D(PHY, "Not found same deltaf_raster == scs_khz, use only band and last deltaf_raster \n");
    for(i = sizeofArray(nr_bandtable) - 1; i >= 0; i--)
       if (nr_bandtable[i].band == nr_bandP)
         break;
  }

  AssertFatal(i >= 0 && i < sizeofArray(nr_bandtable), "band is not existing: %d\n", nr_bandP);
  LOG_D(PHY,
        "NR band table index %d (Band %d, dl_min %lu, ul_min %lu)\n",
         i,
         nr_bandtable[i].band,
         nr_bandtable[i].dl_min,
         nr_bandtable[i].ul_min);

  return i;
}

int get_subband_size(int NPRB,int size) {
  // implements table  5.2.1.4-2 from 36.214
  //
  //Bandwidth part (PRBs)	Subband size (PRBs)
  // < 24	                   N/A
  //24 – 72	                   4, 8
  //73 – 144	                   8, 16
  //145 – 275	                  16, 32

  if (NPRB<24) return(1);
  if (NPRB<72) return (size==0 ? 4 : 8);
  if (NPRB<144) return (size==0 ? 8 : 16);
  if (NPRB<275) return (size==0 ? 16 : 32);
  AssertFatal(1==0,"Shouldn't get here, NPRB %d\n",NPRB);
 
}

void warn_higher_threequarter_fs(const int n_rb, const int mu)
{
  LOG_W(NR_PHY,
        "3/4 sampling is not possible for current PRB size: %d and numerology: %d.\n "
        "So 6/4 sampling is chosen to support x3xx type USRPs.\n "
        "Note that this sampling rate increases fronthaul traffic, FFT buffer size and processing time by a factor of two compared "
        "to 3/4 sampling rate.\n "
        "Some PRACH configuration might not be supported with 6/4 FFT size.\n "
        "Consider reducing the PRB size that would fit within the FFT size of 3/4 sampling\n",
        n_rb,
        mu);
}

void get_samplerate_and_bw(int mu,
                           int n_rb,
                           int8_t threequarter_fs,
                           double *sample_rate,
                           double *tx_bw,
                           double *rx_bw) {

  if (mu == 0) {
    switch(n_rb) {
    case 270:
      if (threequarter_fs) {
        warn_higher_threequarter_fs(n_rb, mu);
        *sample_rate=92.16e6;
        *tx_bw = 50e6;
        *rx_bw = 50e6;
      } else {
        *sample_rate=61.44e6;
        *tx_bw = 50e6;
        *rx_bw = 50e6;
      }
      break;
    case 242: // 45Mhz
    case 188: // 35Mhz
      if (threequarter_fs) {
        *sample_rate = 46.08e6;
      } else {
        *sample_rate = 61.44e6;
      }
      *tx_bw = (n_rb == 242) ? 45e6 : 35e6;
      *rx_bw = (n_rb == 242) ? 45e6 : 35e6;
      break;
    case 216:
      if (threequarter_fs) {
        *sample_rate=46.08e6;
        *tx_bw = 40e6;
        *rx_bw = 40e6;
      }
      else {
        *sample_rate=61.44e6;
        *tx_bw = 40e6;
        *rx_bw = 40e6;
      }
      break;
    case 160: //30 MHz
    case 133: //25 MHz
      if (threequarter_fs) {
        AssertFatal(1==0,"N_RB %d cannot use 3/4 sampling\n",n_rb);
      }
      else {
        *sample_rate=30.72e6;
        *tx_bw = 20e6;
        *rx_bw = 20e6;
      }
      break;
    case 106:
      if (threequarter_fs) {
        *sample_rate=23.04e6;
        *tx_bw = 20e6;
        *rx_bw = 20e6;
      }
      else {
        *sample_rate=30.72e6;
        *tx_bw = 20e6;
        *rx_bw = 20e6;
      }
      break;
    case 52:
      if (threequarter_fs) {
        *sample_rate=11.52e6;
        *tx_bw = 10e6;
        *rx_bw = 10e6;
      }
      else {
        *sample_rate=15.36e6;
        *tx_bw = 10e6;
        *rx_bw = 10e6;
      }
      break;
    case 25:
      if (threequarter_fs) {
        *sample_rate=5.76e6;
        *tx_bw = 5e6;
        *rx_bw = 5e6;
      }
      else {
        *sample_rate=7.68e6;
        *tx_bw = 5e6;
        *rx_bw = 5e6;
      }
      break;
    default:
      AssertFatal(0==1,"N_RB %d not yet supported for numerology %d\n",n_rb,mu);
    }
  } else if (mu == 1) {
    switch(n_rb) {

    case 273:
      if (threequarter_fs) {
        warn_higher_threequarter_fs(n_rb, mu);
        *sample_rate=184.32e6;
        *tx_bw = 100e6;
        *rx_bw = 100e6;
      } else {
        *sample_rate=122.88e6;
        *tx_bw = 100e6;
        *rx_bw = 100e6;
      }
      break;
    case 217:
      if (threequarter_fs) {
        *sample_rate=92.16e6;
        *tx_bw = 80e6;
        *rx_bw = 80e6;
      } else {
        *sample_rate=122.88e6;
        *tx_bw = 80e6;
        *rx_bw = 80e6;
      }
      break;
    case 189:
      if (threequarter_fs) {
        *sample_rate = 92.16e6;
      } else {
        *sample_rate = 122.88e6;
      }
      *tx_bw = 70e6;
      *rx_bw = 70e6;
      break;
    case 162 :
      if (threequarter_fs) {
        AssertFatal(1==0,"N_RB %d cannot use 3/4 sampling\n",n_rb);
      }
      else {
        *sample_rate=61.44e6;
        *tx_bw = 60e6;
        *rx_bw = 60e6;
      }

      break;

    case 133 :
      if (threequarter_fs) {
	AssertFatal(1==0,"N_RB %d cannot use 3/4 sampling\n",n_rb);
      }
      else {
        *sample_rate=61.44e6;
        *tx_bw = 50e6;
        *rx_bw = 50e6;
      }

      break;

    case 119: // 45Mhz
    case 92: // 35Mhz
      if (threequarter_fs) {
        *sample_rate = 46.08e6;
      } else {
        *sample_rate = 61.44e6;
      }
      *tx_bw = (n_rb == 119) ? 45e6 : 35e6;
      *rx_bw = (n_rb == 119) ? 45e6 : 35e6;
      break;

    case 106:
      if (threequarter_fs) {
        *sample_rate=46.08e6;
        *tx_bw = 40e6;
        *rx_bw = 40e6;
      }
      else {
        *sample_rate=61.44e6;
        *tx_bw = 40e6;
        *rx_bw = 40e6;
      }
     break;
    case 51:
      if (threequarter_fs) {
        *sample_rate=23.04e6;
        *tx_bw = 20e6;
        *rx_bw = 20e6;
      }
      else {
        *sample_rate=30.72e6;
        *tx_bw = 20e6;
        *rx_bw = 20e6;
      }
      break;
    case 24:
      if (threequarter_fs) {
        *sample_rate=11.52e6;
        *tx_bw = 10e6;
        *rx_bw = 10e6;
      }
      else {
        *sample_rate=15.36e6;
        *tx_bw = 10e6;
        *rx_bw = 10e6;
      }
      break;
    default:
      AssertFatal(0==1,"N_RB %d not yet supported for numerology %d\n",n_rb,mu);
    }
  } else if (mu == 3) {
    switch(n_rb) {
      case 132:
      case 128:
        if (threequarter_fs) {
          *sample_rate=184.32e6;
          *tx_bw = 200e6;
          *rx_bw = 200e6;
        } else {
          *sample_rate = 245.76e6;
          *tx_bw = 200e6;
          *rx_bw = 200e6;
        }
        break;

      case 66:
        if (threequarter_fs) {
          *sample_rate=92.16e6;
          *tx_bw = 100e6;
          *rx_bw = 100e6;
        } else {
          *sample_rate = 122.88e6;
          *tx_bw = 100e6;
          *rx_bw = 100e6;
        }
        break;

      case 32:
        if (threequarter_fs) {
          *sample_rate=92.16e6;
          *tx_bw = 50e6;
          *rx_bw = 50e6;
        } else {
          *sample_rate=61.44e6;
          *tx_bw = 50e6;
          *rx_bw = 50e6;
        }
        break;

      default:
        AssertFatal(0==1,"N_RB %d not yet supported for numerology %d\n",n_rb,mu);
    }
  } else {
    AssertFatal(0 == 1,"Numerology %d not supported for the moment\n",mu);
  }
}

// from start symbol index and nb or symbols to symbol occupation bitmap in a slot
uint16_t SL_to_bitmap(int startSymbolIndex, int nrOfSymbols) {
 return ((1<<nrOfSymbols)-1)<<startSymbolIndex;
}

int get_SLIV(uint8_t S, uint8_t L) {
  return ( (uint16_t)(((L-1)<=7)? (14*(L-1)+S) : (14*(15-L)+(13-S))) );
}

void SLIV2SL(int SLIV,int *S,int *L) {

  int SLIVdiv14 = SLIV/14;
  int SLIVmod14 = SLIV%14;
  // Either SLIV = 14*(L-1) + S, or SLIV = 14*(14-L+1) + (14-1-S). Condition is 0 <= L <= 14-S
  if ((SLIVdiv14 + 1) >= 0 && (SLIVdiv14 <= 13-SLIVmod14)) {
    *L=SLIVdiv14+1;
    *S=SLIVmod14;
  } else  {
    *L=15-SLIVdiv14;
    *S=13-SLIVmod14;
  }
}

int get_ssb_subcarrier_offset(uint32_t absoluteFrequencySSB, uint32_t absoluteFrequencyPointA, int scs)
{
  // for FR1 k_SSB expressed in terms of 15kHz SCS
  // for FR2 k_SSB expressed in terms of the subcarrier spacing provided by the higher-layer parameter subCarrierSpacingCommon
  // absoluteFrequencySSB and absoluteFrequencyPointA are ARFCN
  // NR-ARFCN delta frequency is 5kHz if f < 3 GHz, 15kHz for other FR1 freq and 60kHz for FR2
  const uint32_t absolute_diff = absoluteFrequencySSB - absoluteFrequencyPointA;
  int scaling = 1;
  if (absoluteFrequencyPointA < 600000) // correspond to 3GHz
    scaling = 3;
  if (scs > 2) // FR2
    scaling <<= (scs - 2);
  // k_SSB grid, see TS 38.211 7.4.3.1
  AssertFatal(absolute_diff % scaling == 0,
              "Invalid absoluteFrequencySSB %u with absoluteFrequencyPointA %u: the SSB-to-pointA "
              "frequency offset is not on the k_SSB grid (ARFCN difference %u is not a multiple of %d). "
              "No valid k_SSB exists; the SSB cannot be transmitted at the configured raster position. "
              "Choose an SSB ARFCN whose distance to point A is a multiple of 15 kHz (FR1)\n",
              absoluteFrequencySSB, absoluteFrequencyPointA, absolute_diff, scaling);
  int sco_limit = scs == 1 ? 24 : 12;
  int subcarrier_offset = (absolute_diff / scaling) % sco_limit;
  // 30kHz is the only case where k_SSB is expressed in terms of a different SCS (15kHz)
  // the assertion is to avoid having an offset of half a subcarrier
  if (scs == 1)
    AssertFatal(subcarrier_offset % 2 == 0, "ssb offset %d invalid for scs %d\n", subcarrier_offset, scs);
  return subcarrier_offset;
}

uint32_t get_ssb_offset_to_pointA(uint32_t absoluteFrequencySSB,
                                  uint32_t absoluteFrequencyPointA,
                                  int ssbSubcarrierSpacing,
                                  int frequency_range)
{
  // offset to pointA is expressed in terms of 15kHz SCS for FR1 and 60kHz for FR2
  // only difference wrt NR-ARFCN is delta frequency 5kHz if f < 3 GHz for ARFCN
  uint32_t absolute_diff = (absoluteFrequencySSB - absoluteFrequencyPointA);
  const int scaling_5khz = absoluteFrequencyPointA < 600000 ? 3 : 1;
  // k_SSB grid, see TS 38.211 7.4.3.1
  AssertFatal(absolute_diff % scaling_5khz == 0,
              "Invalid absoluteFrequencySSB %u with absoluteFrequencyPointA %u: the SSB-to-pointA "
              "frequency offset is not on the k_SSB grid (ARFCN difference %u is not a multiple of %d)\n",
              absoluteFrequencySSB, absoluteFrequencyPointA, absolute_diff, scaling_5khz);
  const int scaling = (absoluteFrequencyPointA >= 2016667) ? 1 << (ssbSubcarrierSpacing - 2) : 1 << ssbSubcarrierSpacing;
  const int scaled_abs_diff = absolute_diff / (scaling_5khz * scaling);
  // absoluteFrequencySSB is the central frequency of SSB which is made by 20RBs in total
  const int ssb_offset_scaling = (frequency_range == FR2) ? 1 << (ssbSubcarrierSpacing - 2) : 1 << ssbSubcarrierSpacing;
  const int ssb_offset_point_a = ((scaled_abs_diff / 12) - 10) * ssb_offset_scaling;
  // Offset to point A needs to be divisible by scaling
  AssertFatal(ssb_offset_point_a % ssb_offset_scaling == 0, "PRB offset %d not valid for scs %d\n", ssb_offset_point_a, ssbSubcarrierSpacing);
  AssertFatal(ssb_offset_point_a >= 0, "ssb offset is negative %d for scs %d\n", ssb_offset_point_a, ssbSubcarrierSpacing);
  return ssb_offset_point_a;
}

static double get_start_freq(const double fc, const int nbRB, const int mu)
{
  const int scs = MU_SCS(mu) * 1000;
  return fc - ((double)nbRB / 2 * NR_NB_SC_PER_RB * scs);
}

static double get_stop_freq(const double fc, const int nbRB, const int mu)
{
  int scs = MU_SCS(mu) * 1000;
  return fc + ((double)nbRB / 2 * NR_NB_SC_PER_RB * scs);
}

static void compute_M_and_N(const int gscn, int *rM, int *rN)
{
  if (gscn > 1 && gscn < 7499) {
    for (int M = 1; M < 6; M += 2) {
      /* GSCN = 3N + (M-3) / 2
         N(int) = 2 * GSCN + 3 - M
      */
      if (((2 * gscn + 3 - M) % 6) == 0) {
        *rM = M;
        *rN = (2 * gscn + 3 - M) / 6;
        break;
      }
    }
  } else if (gscn > 7498 && gscn < 22256) {
    *rN = gscn - 7499;
  } else if (gscn > 22255 && gscn < 26638) {
    *rN = gscn - 22256;
  } else {
    LOG_E(NR_PHY, "Invalid GSCN\n");
    abort();
  }
}

// Section 5.4.3 of 38.101-1 and -2
static double get_ssref_from_gscn(const int gscn)
{
  int M, N = -1;
  compute_M_and_N(gscn, &M, &N);
  if (gscn > 1 && gscn < 7499) { // Sub 3GHz
    AssertFatal(N > 0 && N < 2500, "Invalid N\n");
    AssertFatal(M > 0 && M < 6 && (M & 0x1), "Invalid M\n");
    return (N * 1200e3 + M * 50e3);
  } else if (gscn > 7498 && gscn < 22256) {
    AssertFatal(N > -1 && N < 14757, "Invalid N\n");
    return (3000e6 + N * 1.44e6);
  } else if (gscn > 22255 && gscn < 26638) {
    AssertFatal(N > -1 && N < 4382, "Invalid N\n");
    return (24250.08e6 + N * 17.28e6);
  } else {
    LOG_E(NR_PHY, "Invalid GSCN\n");
    abort();
  }
}

static void find_gscn_to_scan(const double startFreq,
                              const double stopFreq,
                              const sync_raster_t gscn,
                              int *scanGscnStart,
                              int *scanGscnStop)
{
  const double scs = MU_SCS(gscn.scs_index) * 1e3;
  const double ssbBW = 20 * NR_NB_SC_PER_RB * scs;

  for (int g = gscn.first_gscn; g < gscn.last_gscn; g += gscn.step_gscn) {
    const double centerSSBFreq = get_ssref_from_gscn(g);
    const double startSSBFreq = centerSSBFreq - ssbBW / 2;
    if (startSSBFreq < startFreq)
      continue;

    *scanGscnStart = g;
    break;
  }
  *scanGscnStop = *scanGscnStart;

  for (int g = gscn.last_gscn; g > gscn.first_gscn; g -= gscn.step_gscn) {
    const double centerSSBFreq = get_ssref_from_gscn(g);
    const double stopSSBFreq = centerSSBFreq + ssbBW / 2 - 1;
    if (stopSSBFreq > stopFreq)
      continue;

    *scanGscnStop = g;
    break;
  }
}

static int get_ssb_first_sc(const double pointA, const double ssbCenter, const int mu)
{
  const double scs = MU_SCS(mu) * 1e3;
  const int ssbRBs = 20;
  return (int)((ssbCenter - pointA) / scs - (ssbRBs / 2 * NR_NB_SC_PER_RB));
}

/* Returns array of first SCS offset in the scanning window */
int get_scan_ssb_first_sc(const double fc, const int nbRB, const int nrBand, const int mu, nr_gscn_info_t ssbInfo[MAX_GSCN_BAND])
{
  const double startFreq = get_start_freq(fc, nbRB, mu);
  const double stopFreq = get_stop_freq(fc, nbRB, mu);

  int scanGscnStart = 0;
  int scanGscnStop = 0;
  const sync_raster_t *tmpRaster = sync_raster;
  const sync_raster_t * end=sync_raster + sizeofArray(sync_raster);
  while (tmpRaster < end && (tmpRaster->band != nrBand || tmpRaster->scs_index != mu))
    tmpRaster++;
  if (tmpRaster >= end) {
    LOG_E(PHY, "raster not found nrband=%d, mu=%d\n", nrBand, mu);
    return 0;
  }

  find_gscn_to_scan(startFreq, stopFreq, *tmpRaster, &scanGscnStart, &scanGscnStop);

  const double scs = MU_SCS(mu) * 1e3;
  const double pointA = fc - ((double)nbRB / 2 * scs * NR_NB_SC_PER_RB);
  int numGscn = 0;
  for (int g = scanGscnStart; (g <= scanGscnStop) && (numGscn < MAX_GSCN_BAND); g += tmpRaster->step_gscn) {
    ssbInfo[numGscn].ssRef = get_ssref_from_gscn(g);
    ssbInfo[numGscn].ssbFirstSC = get_ssb_first_sc(pointA, ssbInfo[numGscn].ssRef, mu);
    ssbInfo[numGscn].gscn = g;
    numGscn++;
  }

  return numGscn;
}

// Table 38.211 6.3.3.1-1
static uint8_t long_prach_dur[4] = {1, 3, 4, 1}; // 0.9, 2.28, 3.35, 0.9 ms

uint8_t get_long_prach_dur(unsigned int format, unsigned int mu)
{
  AssertFatal(format < 4, "Invalid long PRACH format %d\n", format);
  const int num_slots_subframe = (1 << mu);
  const int prach_dur_subframes = long_prach_dur[format];
  return (prach_dur_subframes * num_slots_subframe);
}

// Table 38.211 6.3.3.2-1
uint8_t get_PRACH_k_bar(unsigned int delta_f_RA_PRACH, unsigned int delta_f_PUSCH)
{
  uint8_t k_bar = 0;
  if (delta_f_RA_PRACH > 3) { // Rel 15 max PRACH SCS is 120 kHz, 4 and 5 are 1.25 and 5 kHz
    // long formats
    DevAssert(delta_f_PUSCH < 3);
    DevAssert(delta_f_RA_PRACH < 6);
    const uint8_t k_bar_table[3][2] = {{7, 12},
                                       {1, 10},
                                       {133, 7}};

    k_bar = k_bar_table[delta_f_PUSCH][delta_f_RA_PRACH - 4];
  } else {
    if (delta_f_RA_PRACH == 3 && delta_f_PUSCH == 4) // \delta f_RA == 120 kHz AND \delta f == 480 kHz
      k_bar = 1;
    else if (delta_f_RA_PRACH == 3 && delta_f_PUSCH == 5) // \delta f_RA == 120 kHz AND \delta f == 960 kHz
      k_bar = 23;
    else
      k_bar = 2;
  }
  return k_bar;
}

// K according to 38.211 5.3.2
unsigned int get_prach_K(int prach_sequence_length, int prach_fmt_id, int pusch_mu, int prach_mu)
{
  unsigned int K = 1;
  if (prach_sequence_length == 0) {
    if (prach_fmt_id == 3)
      K = (15 << pusch_mu) / 5;
    else
      K = (15 << pusch_mu) / 1.25;
  } else if (prach_sequence_length == 1) {
    K = (15 << pusch_mu) / (15 << prach_mu);
  } else {
    AssertFatal(0, "Invalid PRACH sequence length %d\n", prach_sequence_length);
  }
  return K;
}

int get_delay_idx(int delay, int max_delay_comp)
{
  int delay_idx = max_delay_comp + delay;
  // If the measured delay is less than -MAX_DELAY_COMP, a -MAX_DELAY_COMP delay is compensated.
  delay_idx = max(delay_idx, 0);
  // If the measured delay is greater than +MAX_DELAY_COMP, a +MAX_DELAY_COMP delay is compensated.
  delay_idx = min(delay_idx, max_delay_comp << 1);
  return delay_idx;
}

int set_default_nta_offset(frequency_range_t freq_range, uint32_t samples_per_subframe)
{
  // ta_offset_samples : ta_offset = samples_per_subframe : (Δf_max x N_f / 1000)
  // As described in Section 4.3.1 in 38.211

  // TODO There is no way for the UE to know about LTE-NR coexistence case
  //      as mentioned in Table 7.1.2-2 of 38.133
  //      LTE-NR coexistence means the presence of an active LTE service in the same band as NR in current deployment
  //      We assume no coexistence

  uint64_t numer = (freq_range == FR1 ? 25600 : 13792) * (uint64_t)samples_per_subframe;
  return numer / (4096 * 480);
}

void nr_timer_start(NR_timer_t *timer)
{
  timer->active = true;
  timer->suspended = false;
  timer->counter = 0;
}

void nr_timer_stop(NR_timer_t *timer)
{
  timer->active = false;
  timer->suspended = false;
  timer->counter = 0;
}

void nr_timer_suspension(NR_timer_t *timer)
{
  timer->suspended = !timer->suspended;
}

bool nr_timer_is_active(const NR_timer_t *timer)
{
  return timer->active;
}

bool nr_timer_tick(NR_timer_t *timer)
{
  bool expired = false;
  if (timer->active) {
    timer->counter += timer->step;
    if (timer->target == UINT_MAX || timer->suspended) // infinite target, never expires
      return false;
    expired = nr_timer_expired(timer);
    if (expired)
      timer->active = false;
  }
  return expired;
}

bool nr_timer_expired(const NR_timer_t *timer)
{
  if (timer->target == UINT_MAX || timer->suspended) // infinite target, never expires
    return false;
  return timer->counter >= timer->target;
}

uint32_t nr_timer_elapsed_time(const NR_timer_t *timer)
{
  return timer->counter;
}

uint32_t nr_timer_remaining_time(const NR_timer_t *timer)
{
  return timer->target - timer->counter;
}

void nr_timer_setup(NR_timer_t *timer, const uint32_t target, const uint32_t step)
{
  timer->target = target;
  timer->step = step;
  nr_timer_stop(timer);
}

unsigned short get_m_srs(int c_srs, int b_srs) {
  return srs_bandwidth_config[c_srs][b_srs][0];
}

unsigned short get_N_b_srs(int c_srs, int b_srs) {
  return srs_bandwidth_config[c_srs][b_srs][1];
}

// TODO: Implement to b_SRS = 1 and b_SRS = 2
long rrc_get_max_nr_csrs(const int max_rbs, const long b_SRS)
{
  if(b_SRS>0) {
    LOG_E(NR_RRC,"rrc_get_max_nr_csrs(): Not implemented yet for b_SRS>0\n");
    return 0; // This c_srs is always valid
  }

  const uint16_t m_SRS[64] = { 4, 8, 12, 16, 16, 20, 24, 24, 28, 32, 36, 40, 48, 48, 52, 56, 60, 64, 72, 72, 76, 80, 88,
                               96, 96, 104, 112, 120, 120, 120, 128, 128, 128, 132, 136, 144, 144, 144, 144, 152, 160,
                               160, 160, 168, 176, 184, 192, 192, 192, 192, 208, 216, 224, 240, 240, 240, 240, 256, 256,
                               256, 264, 272, 272, 272 };

  long c_srs = 0;
  uint16_t m = 4;
  for(int c = 1; c<64; c++) {
    if(m_SRS[c]>m && m_SRS[c]<max_rbs) {
      c_srs = c;
      m = m_SRS[c];
    }
  }

  return c_srs;
}

frequency_range_t get_freq_range_from_freq(uint64_t freq)
{
  // 3GPP TS 38.101-1,38.101-5 Version 19.0.0 Table 5.1-1: Definition of frequency ranges
  if (freq >= 410000000 && freq <= 7125000000)
    return FR1;
  // 3GPP TS 38.101-1 Version 19.0.0 Table 5.1-1: Definition of frequency ranges
  // FR2 is from 24250 MHz to 71000 MHz
  // 3GPP TS 38.101-5 Version 19.0.0 Table 5.1-1: Definition of frequency ranges
  // FR2 for NTN is from 17300 Mhz to 30000 MHz
  if (freq >= 17300000000 && freq <= 71000000000)
    return FR2;

  AssertFatal(false, "Undefined Frequency Range for frequency %ld Hz\n", freq);
}

frequency_range_t get_freq_range_from_arfcn(uint32_t arfcn)
{
  // 3GPP TS 38.101-1 Version 19.0.0 Table 5.1-1: Definition of frequency ranges
  if (arfcn >= 82000 && arfcn <= 875000)
    return FR1;
  // 3GPP TS 38.101-1 Version 19.0.0 Table 5.1-1: Definition of frequency ranges
  // FR2 is from 24250 MHz to 71000 MHz (from ARFCN 2016667)
  // 3GPP TS 38.101-5 Version 19.0.0 Table 5.1-1: Definition of frequency ranges
  // FR2 for NTN is from 17300 Mhz to 30000 MHz, from ARFCN 1553336 (Table 5.4.2.3-3)
  if (arfcn >= 1553336 && arfcn <= 2795832)
    return FR2;

  AssertFatal(false, "Undefined Frequency Range for ARFCN %d\n", arfcn);
}

frequency_range_t get_freq_range_from_band(uint16_t band)
{
  return band <= 256 ? FR1 : FR2;
}

float get_beta_dmrs(int num_cdm_groups_no_data, bool is_type2)
{
  float beta_dmrs_pusch = 1.0;
  if (num_cdm_groups_no_data == 2) {
    beta_dmrs_pusch = powf(10.0, 3.0 / 20.0);
  } else if (num_cdm_groups_no_data == 3) {
    if (is_type2)
      beta_dmrs_pusch = powf(10.0, 4.77 / 20.0);
  }
  return beta_dmrs_pusch;
}

/** @brief Construct full 5G-S-TMSI from 5G-S-TMSI components
 * @param amf_set_id AMF Set ID (10 bits)
 * @param amf_pointer AMF Pointer (6 bits)
 * @param m_tmsi 5G-TMSI (32 bits)
 * @return Full 5G-S-TMSI (48 bits)
 * @note The 5G-S-TMSI is constructed as a 48-bit value:
 *       - Bits 38-47: AMF Set ID (10 bits)
 *       - Bits 32-37: AMF Pointer (6 bits)
 *       - Bits 0-31:  5G-TMSI (32 bits)
 * @ref 3GPP TS 23.003 */
uint64_t nr_construct_5g_s_tmsi(uint16_t amf_set_id, uint8_t amf_pointer, uint32_t m_tmsi)
{
  // Construct full 5G-S-TMSI: <AMF Set ID (10 bits)><AMF Pointer (6 bits)><5G-TMSI (32 bits)>
  return ((uint64_t)amf_set_id << 38) | ((uint64_t)amf_pointer << 32) | m_tmsi;
}

/** @brief Extract 5G-S-TMSI-Part1 from full 5G-S-TMSI
 * @param fiveg_s_tmsi Full 5G-S-TMSI (48 bits)
 * @return 5G-S-TMSI-Part1 (rightmost 39 bits)
 * @note 5G-S-TMSI-Part1 is the rightmost 39 bits of the full 5G-S-TMSI:
 *       - Bits 32-37: AMF Pointer (6 bits)
 *       - Bits 0-31:  5G-TMSI (32 bits)
 * @ref 3GPP TS 23.003 */
uint64_t nr_extract_5g_s_tmsi_part1(const uint64_t fiveg_s_tmsi)
{
  return fiveg_s_tmsi & ((1ULL << 39) - 1);
}

/** @brief Construct 5G-S-TMSI-Part1 from 5G-S-TMSI components
 * @param amf_set_id AMF Set ID (10 bits)
 * @param amf_pointer AMF Pointer (6 bits)
 * @param m_tmsi 5G-TMSI (32 bits)
 * @return 5G-S-TMSI-Part1 (rightmost 39 bits of the full 5G-S-TMSI)
 * @note 5G-S-TMSI-Part1 is the rightmost 39 bits of the full 5G-S-TMSI:
 *       - Bits 32-37: AMF Pointer (6 bits)
 *       - Bits 0-31:  5G-TMSI (32 bits)
 * @ref 3GPP TS 23.003 */
uint64_t nr_construct_5g_s_tmsi_part1(uint16_t amf_set_id, uint8_t amf_pointer, uint32_t m_tmsi)
{
  // Construct full 5G-S-TMSI and extract Part1: rightmost 39 bits
  uint64_t full_s_tmsi = nr_construct_5g_s_tmsi(amf_set_id, amf_pointer, m_tmsi);
  return nr_extract_5g_s_tmsi_part1(full_s_tmsi);
}

/** @brief Extract 5G-S-TMSI-Part2 from full 5G-S-TMSI
 * @param fiveg_s_tmsi Full 5G-S-TMSI (48 bits)
 * @return 5G-S-TMSI-Part2 (leftmost 9 bits)
 * @note 5G-S-TMSI-Part2 is the leftmost 9 bits of the full 5G-S-TMSI:
 *       - Bits 39-47: AMF Set ID (9 bits)
 * @ref 3GPP TS 23.003 */
uint16_t nr_extract_5g_s_tmsi_part2(const uint64_t fiveg_s_tmsi)
{
  return (fiveg_s_tmsi >> 39) & ((1ULL << 9) - 1);
}

/** @brief Build full 5G-S-TMSI from Part1 and Part2
 * @param part1 5G-S-TMSI-Part1 (rightmost 39 bits)
 * @param part2 5G-S-TMSI-Part2 (leftmost 9 bits)
 * @return Full 5G-S-TMSI (48 bits)
 * @note Combines Part2 (leftmost 9 bits) and Part1 (rightmost 39 bits)
 * @ref 3GPP TS 23.003 */
uint64_t nr_build_full_5g_s_tmsi(const uint64_t part1, const uint16_t part2)
{
  return ((uint64_t)part2 << 39) | part1;
}

/** @brief Deconstruct full 5G-S-TMSI into its components
 * @param fiveg_s_tmsi Full 5G-S-TMSI (48 bits)
 * @param[out] amf_set_id Pointer to store AMF Set ID (10 bits)
 * @param[out] amf_pointer Pointer to store AMF Pointer (6 bits)
 * @param[out] m_tmsi Pointer to store 5G-TMSI (32 bits)
 * @note The 5G-S-TMSI is deconstructed from a 48-bit value:
 *       - Bits 38-47: AMF Set ID (10 bits)
 *       - Bits 32-37: AMF Pointer (6 bits)
 *       - Bits 0-31:  5G-TMSI (32 bits)
 * @ref 3GPP TS 23.003 */
void nr_deconstruct_5g_s_tmsi(const uint64_t fiveg_s_tmsi, uint16_t *amf_set_id, uint8_t *amf_pointer, uint32_t *m_tmsi)
{
  DevAssert(amf_set_id != NULL);
  DevAssert(amf_pointer != NULL);
  DevAssert(m_tmsi != NULL);

  *amf_set_id = fiveg_s_tmsi >> 38;
  *amf_pointer = (fiveg_s_tmsi >> 32) & 0x3F;
  *m_tmsi = fiveg_s_tmsi;
}
