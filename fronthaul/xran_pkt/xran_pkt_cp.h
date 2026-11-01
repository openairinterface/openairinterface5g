/*
 * SPDX-License-Identifier: Apache-2.0
 * Original file: Copyright 2020 Intel.
 * Copyright 2026 OpenAirInterface Authors
 */

#ifndef _XRAN_PKT_CP_H_
#define _XRAN_PKT_CP_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**********************************************************************
 * Common structures for C/U-plane
 **********************************************************************/
/**
 * @ingroup xran_cp_pkt
 *
 * @description
 *      user data compression header defined in 5.4.4.10 / 6.3.3.13
 */
struct xran_radioapp_udComp_header {
  uint8_t udCompMeth: 4; /**< Compression method, XRAN_COMPMETHOD_xxxx */
  uint8_t udIqWidth: 4; /**< IQ bit width, 1 ~ 16 */
} __attribute__((__packed__));

/**********************************************************************
 * Definition of C-Plane Protocol 5.4
 **********************************************************************/
/**
 * @ingroup xran_cp_pkt
 *
 * @description
 *      Common Radio Application Header for C-Plane
 */
struct xran_cp_radioapp_common_header { /* 6bytes, first 4bytes need the conversion for byte order */
  union {
    uint32_t all_bits;
    struct {
      uint32_t startSymbolId: 6; /**< 5.4.4.7 start symbol identifier */
      uint32_t slotId: 6; /**< 5.4.4.6 slot identifier */
      uint32_t subframeId: 4; /**< 5.4.4.5 subframe identifier */
      uint32_t frameId: 8; /**< 5.4.4.4 frame identifier */
      uint32_t filterIndex: 4; /**< 5.4.4.3 filter index, XRAN_FILTERINDEX_xxxx */
      uint32_t payloadVer: 3; /**< 5.4.4.2 payload version, should be 1 */
      uint32_t dataDirection: 1; /**< 5.4.4.1 data direction (gNB Tx/Rx) */
    };
  } field;
  uint8_t numOfSections; /**< 5.4.4.8 number of sections */
  uint8_t sectionType; /**< 5.4.4.9 section type */
} __attribute__((__packed__));

/**
 * @ingroup xran_cp_pkt
 *
 * @description
 *      frame structure defined in 5.4.4.13
 */
struct xran_cp_radioapp_frameStructure {
  uint8_t uScs: 4; /**< sub-carrier spacing, XRAN_SCS_xxx */
  uint8_t fftSize: 4; /**< FFT size,  XRAN_FFTSIZE_xxx */
} __attribute__((__packed__));

/**
 * @ingroup xran_cp_pkt
 *
 * @description
 *      Section headers definition for C-Plane.
 *      Section type 6 and 7 are not present since those have different fields.
 */
struct xran_cp_radioapp_section_header { /* 8bytes, need the conversion for byte order */
  union {
    uint32_t first_4byte;
    struct {
      uint32_t reserved: 16;
      uint32_t numSymbol: 4; /**< 5.4.5.7 number of symbols */
      uint32_t reMask: 12; /**< 5.4.5.5 resource element mask */
    } s0;
    struct {
      uint32_t beamId: 15; /**< 5.4.5.9 beam identifier */
      uint32_t ef: 1; /**< 5.4.5.8 extension flag */
      uint32_t numSymbol: 4; /**< 5.4.5.7 number of symbols */
      uint32_t reMask: 12; /**< 5.4.5.5 resource element mask */
    } s1;
    struct {
      uint32_t beamId: 15; /**< 5.4.5.9 beam identifier */
      uint32_t ef: 1; /**< 5.4.5.8 extension flag */
      uint32_t numSymbol: 4; /**< 5.4.5.7 number of symbols */
      uint32_t reMask: 12; /**< 5.4.5.5 resource element mask */
    } s3;
    struct {
      uint32_t ueId: 15; /**< 5.4.5.10 UE identifier */
      uint32_t ef: 1; /**< 5.4.5.8 extension flag */
      uint32_t numSymbol: 4; /**< 5.4.5.7 number of symbols */
      uint32_t reMask: 12; /**< 5.4.5.5 resource element mask */
    } s5;
  } u;
  union {
    uint32_t second_4byte;
    struct {
      uint32_t numPrbc: 8; /**< 5.4.5.6 number of contiguous PRBs per control section  0000 0000b = all PRBs */
      uint32_t startPrbc: 10; /**< 5.4.5.4 starting PRB of control section */
      uint32_t symInc: 1; /**< 5.4.5.3 symbol number increment command XRAN_SYMBOLNUMBER_xxxx */
      uint32_t rb: 1; /**< 5.4.5.2 resource block indicator, XRAN_RBIND_xxx */
      uint32_t sectionId: 12; /**< 5.4.5.1 section identifier */
    } common;
  } u1;
} __attribute__((__packed__));

struct xran_cp_radioapp_section_ext_hdr {
  /* 12 bytes, need to convert byte order for two parts respectively
   *  - 2 and 8 bytes, reserved1 would be OK if it is zero
   */
  uint16_t extLen: 8; /**< 5.4.6.3 extension length, in 32bits words */
  uint16_t extType: 7; /**< 5.4.6.1 extension type */
  uint16_t ef: 1; /**< 5.4.6.2 extension flag */
} __attribute__((__packed__));

/**
 * @ingroup xran_cp_pkt
 *
 * @description
 *      Beamforming Weights Extension Type(ExtType 1) defined in 5.4.7.1
 *      The structure is reordered for byte order conversion.
 */
struct xran_cp_radioapp_section_ext1 {
  /* variable length, need to be careful to convert byte order
   *   - does not need to convert first 3 bytes */
  uint8_t extType: 7; /**< 5.4.6.1 extension type */
  uint8_t ef: 1; /**< 5.4.6.2 extension flag */
  uint8_t extLen; /**< 5.4.6.3 extension length, in 32bits words */
  /* bfwCompHdr */
  uint8_t bfwCompMeth: 4; /**< 5.4.7.1.1 Beamforming weight Compression method */
  uint8_t bfwIqWidth: 4; /**< 5.4.7.1.1 Beamforming weight IQ bit width */

  /*
   *
   *
   * bfwCompParam
   * (bfwI,  bfwQ)+
   *    ......
   * zero padding for 4-byte alignment
   */
} __attribute__((__packed__));

/**
 * @ingroup xran_cp_pkt
 *
 * @description
 *      Beamforming Attributes Extension Type(ExtType 2) defined in 5.4.7.2
 *      The structure is reordered for byte order conversion.
 */
struct xran_cp_radioapp_section_ext2 {
  /* variable length, need to be careful to convert byte order
   *   - first 4 bytes can be converted at once
   */
  uint32_t bfZe3ddWidth: 3; /**< 5.4.7.2.1 beamforming zenith beamwidth parameter bitwidth, Table 5-21 */
  uint32_t bfAz3ddWidth: 3; /**< 5.4.7.2.1 beamforming azimuth beamwidth parameter bitwidth, Table 5-20 */
  uint32_t bfaCompResv1: 2;
  uint32_t bfZePtWidth: 3; /**< 5.4.7.2.1 beamforming zenith pointing parameter bitwidth, Table 5-19 */
  uint32_t bfAzPtWidth: 3; /**< 5.4.7.2.1 beamforming azimuth pointing parameter bitwidth, Table 5-18 */
  uint32_t bfaCompResv0: 2;
  uint32_t extLen: 8; /**< 5.4.6.3 extension length, in 32bits words */
  uint32_t extType: 7; /**< 5.4.6.1 extension type */
  uint32_t ef: 1; /**< 5.4.6.2 extension flag */

  /*
   * would be better to use bit manipulation directly to add these parameters
   *
   * bfAzPt: var by bfAzPtWidth
   * bfZePt: var by bfZePtWidth
   * bfAz3dd: var by bfAz3ddWidth
   * bfZe3dd: var by bfZe3ddWidth
   * bfAzSI:5 (including zero-padding for unused bits)
   * bfZeSI:3
   * padding for 4-byte alignment
   *
   */
} __attribute__((__packed__));

/**
 * @ingroup xran_cp_pkt
 *
 * @description
 *      DL Precoding Extension Type(ExtType 3) for first data layer.
 *      Defined in 5.4.7.3 Table 5-22.
 *      Only be used for LTE TM2-4 and not for other LTE TMs nor NR.
 *      The structure is reordered for byte order conversion.
 */
union xran_cp_radioapp_section_ext3_first {
  /* 16 bytes, need to convert byte order for two parts - 8/8 bytes */
  struct {
    uint64_t reserved1: 8;
    uint64_t crsSymNum: 4; /**< 5.4.7.3.6 CRS symbol number indication */
    uint64_t reserved0: 3;
    uint64_t crsShift: 1; /**< 5.4.7.3.7 CRS shift used for DL transmission */
    uint64_t crsReMask: 12; /**< 5.4.7.3.5 CRS resource element mask */
    uint64_t txScheme: 4; /**< 5.4.7.3.3 transmission scheme */
    uint64_t numLayers: 4; /**< 5.4.7.3.4 number of layers used for DL transmission */
    uint64_t layerId: 4; /**< 5.4.7.3.2 Layer ID for DL transmission */
    uint64_t codebookIndex: 8; /**< 5.4.7.3.1 precoder codebook used for transmission */
    uint64_t extLen: 8; /**< 5.4.6.3 extension length, in 32bits words */
    uint64_t extType: 7; /**< 5.4.6.1 extension type */
    uint64_t ef: 1; /**< 5.4.6.2 extension flag */

    uint64_t beamIdAP1: 16; /**< 5.4.7.3.8 beam id to be used for antenna port 1 */
    uint64_t beamIdAP2: 16; /**< 5.4.7.3.9 beam id to be used for antenna port 2 */
    uint64_t beamIdAP3: 16; /**< 5.4.7.3.10 beam id to be used for antenna port 3 */
    uint64_t reserved2: 16;
  } all_bits;

  struct {
    int32_t data_field1[4];
  } data_field;
} __attribute__((__packed__));

/**
 * @ingroup xran_cp_pkt
 *
 * @description
 *      DL Precoding Extension Type(ExtType 3) for non-first data layer.
 *      Defined in 5.4.7.3 Table 5-23.
 *      Only be used for LTE TM2-4 and not for other LTE TMs nor NR.
 *      The structure is reordered for byte order conversion.
 */
union xran_cp_radioapp_section_ext3_non_first {
  uint32_t data_field;
  struct {
    /* 4 bytes, need to convert byte order at once */
    uint32_t numLayers: 4; /**< 5.4.7.3.4 number of layers used for DL transmission */
    uint32_t layerId: 4; /**< 5.4.7.3.2 Layer ID for DL transmission */
    uint32_t codebookIndex: 8; /**< 5.4.7.3.1 precoder codebook used for transmission */

    uint32_t extLen: 8; /**< 5.4.6.3 extension length, in 32bits words */
    uint32_t extType: 7; /**< 5.4.6.1 extension type */
    uint32_t ef: 1; /**< 5.4.6.2 extension flag */
  } all_bits;
} __attribute__((__packed__));

/**
 * @ingroup xran_cp_pkt
 *
 * @description
 *      Flexible Beamforming Weights Extension Type(ExtType 11) defined in
 *      CUS v16.01 7.7.11 (Table 7.7.11.1-1), with bundleOffset and contInd.
 *      extLen is 16 bits, so the first 4 bytes are converted as one 32-bit word.
 *      The structure is reordered for byte order conversion.
 */
struct xran_cp_radioapp_section_ext11 {
  uint32_t bundleOffset: 6; /**< 7.7.11.10 PRB bundle offset (reserved in older versions) */
  uint32_t RAD: 1; /**< 7.7.11.8 reset after PRB discontinuity */
  uint32_t disableBFWs: 1; /**< 7.7.11.7 only beamIds are carried, no weights */
  uint32_t extLen: 16; /**< 7.6.2.3 extension length, in 32bits words */
  uint32_t extType: 7; /**< 7.6.2.1 extension type */
  uint32_t ef: 1; /**< 7.6.2.2 extension flag */
  uint8_t numBundPrb; /**< 7.7.11.4 number of bundled PRBs per beamforming weights, 0 is reserved */
  /* bfwCompHdr (7.7.11.2), absent if disableBFWs = 1 */
  uint8_t bfwCompMeth: 4; /**< Beamforming weight Compression method (Table 7.7.1.2-3) */
  uint8_t bfwIqWidth: 4; /**< Beamforming weight IQ bit width (Table 7.7.1.2-2) */
} __attribute__((__packed__));

/**********************************************************
 * Scheduling and Beam-forming Commands 5.4.2
 **********************************************************/
/**
 * @ingroup xran_cp_pkt
 *
 * @description
 *      Section header definition for type 0
 */
struct xran_cp_radioapp_section0_header { // 12bytes (6+2+1+2+1)
  struct xran_cp_radioapp_common_header cmnhdr;
  uint16_t timeOffset; /**< 5.4.4.12 time offset */

  struct xran_cp_radioapp_frameStructure frameStructure;
  uint16_t cpLength; /**< 5.4.4.14 cyclic prefix length */
  uint8_t reserved;
} __attribute__((__packed__));

/**
 * @ingroup xran_cp_pkt
 *
 * @description
 *      Section definition for type 0: Unused RB or Symbols in DL or UL (Table 5-2)
 *      Not supported in this release
 */
struct xran_cp_radioapp_section0 { // 8bytes (4+4)
  struct xran_cp_radioapp_section_header hdr;
} __attribute__((__packed__));

/**
 * @ingroup xran_cp_pkt
 *
 * @description
 *      Section header definition for type 1
 */
struct xran_cp_radioapp_section1_header { // 8bytes (6+1+1)
  struct xran_cp_radioapp_common_header cmnhdr;
  struct xran_radioapp_udComp_header udComp;
  uint8_t reserved;
} __attribute__((__packed__));

/**
 * @ingroup xran_cp_pkt
 *
 * @description
 *      Section definition for type 1: Most DL/UL Radio Channels (Table 5-3)
 */
struct xran_cp_radioapp_section1 { // 8bytes (4+4)
  struct xran_cp_radioapp_section_header hdr;

  // section extensions               // 5.4.6 & 5.4.7
  //  .........
} __attribute__((__packed__));

/**
 * @ingroup xran_cp_pkt
 *
 * @description
 *      Section header definition for type 3
 */
struct xran_cp_radioapp_section3_header { // 12bytes (6+2+1+2+1)
  struct xran_cp_radioapp_common_header cmnhdr;
  uint16_t timeOffset; /**< 5.4.4.12 time offset */

  struct xran_cp_radioapp_frameStructure frameStructure;
  uint16_t cpLength; /**< 5.4.4.14 cyclic prefix length */
  struct xran_radioapp_udComp_header udComp;
} __attribute__((__packed__));

/**
 * @ingroup xran_cp_pkt
 *
 * @description
 *      Section definition for type 3: PRACH and Mixed-numerology Channels (Table 5-4)
 */
struct xran_cp_radioapp_section3 { // 12bytes (4+4+4)
  struct xran_cp_radioapp_section_header hdr;
  uint32_t freqOffset: 24; /**< 5.4.5.11 frequency offset */
  uint32_t reserved: 8;

  // section extensions               // 5.4.6 & 5.4.7
  //  .........
} __attribute__((__packed__));

/** Control Plane section types, defined in 5.4 Table 5.1 */
enum xran_cp_sectiontype {
  XRAN_CP_SECTIONTYPE_0 = 0, /**< Unused RB or Symbols in DL or UL, not supported */
  XRAN_CP_SECTIONTYPE_1 = 1, /**< Most DL/UL Radio Channels */
  XRAN_CP_SECTIONTYPE_3 = 3, /**< PRACH and Mixed-numerology Channels */
  XRAN_CP_SECTIONTYPE_5 = 5, /**< UE scheduling information, not supported  */
  XRAN_CP_SECTIONTYPE_6 = 6, /**< Channel Information, not supported */
  XRAN_CP_SECTIONTYPE_7 = 7, /**< LAA, not supported */
  XRAN_CP_SECTIONTYPE_MAX
};

/** Filter index, defined in 5.4.4.3 */
enum xran_cp_filterindex {
  XRAN_FILTERINDEX_STANDARD = 0, /**< UL filter for standard channel */
  XRAN_FILTERINDEX_PRACH_012 = 1, /**< UL filter for PRACH preamble format 0, 1, 2 */
  XRAN_FILTERINDEX_PRACH_3 = 2, /**< UL filter for PRACH preamble format 3 */
  XRAN_FILTERINDEX_PRACH_ABC = 3, /**< UL filter for PRACH preamble format A1~3, B1~4, C0, C2 */
  XRAN_FILTERINDEX_NPRACH = 4, /**< UL filter for NPRACH */
  XRAN_FILTERINDEX_LTE4 = 5, /**< UL filter for PRACH preamble format LTE-4 */
  XRAN_FILTERINDEX_MAX
};

/** Maximum Slot Index, defined in 5.4.4.6 */
#define XRAN_SLOTID_MAX 16

/** FFT size in frame structure, defined in 5.4.4.13 Table 5.9 */
enum xran_cp_fftsize {
  XRAN_FFTSIZE_128 = 7, /* 128 */
  XRAN_FFTSIZE_256 = 8, /* 256 */
  XRAN_FFTSIZE_512 = 9, /* 512 */
  XRAN_FFTSIZE_1024 = 10, /* 1024 */
  XRAN_FFTSIZE_2048 = 11, /* 2048 */
  XRAN_FFTSIZE_4096 = 12, /* 4096 */
  XRAN_FFTSIZE_1536 = 13, /* 1536 */
  XRAN_FFTSIZE_MAX
};

/** Sub-carrier spacing, defined in 5.4.4.13 Table 5.10 */
enum xran_cp_subcarrierspacing { /*3GPP u,  SCS, Nslot, Slot len */
                                 XRAN_SCS_15KHZ = 0, /*  0,   15kHz,  1, 1ms */
                                 XRAN_SCS_30KHZ = 1, /*  1,   30kHz,  2, 500us */
                                 XRAN_SCS_60KHZ = 2, /*  2,   60kHz,  4, 250us */
                                 XRAN_SCS_120KHZ = 3, /*  3,  120kHz,  8, 125us */
                                 XRAN_SCS_240KHZ = 4, /*  4,  240kHz, 16, 62.5us */
                                 XRAN_SCS_1P25KHZ = 12, /* NA, 1.25kHz,  1, 1ms */
                                 XRAN_SCS_3P75KHZ = 13, /* NA, 3.75kHz,  1, 1ms */
                                 XRAN_SCS_5KHZ = 14, /* NA,    5kHz,  1, 1ms */
                                 XRAN_SCS_7P5KHZ = 15, /* NA,  7.5kHz,  1, 1ms */
                                 XRAN_SCS_MAX
};

/** Resource block indicator, defined in 5.4.5.2 */
enum xran_cp_rbindicator {
  XRAN_RBIND_EVERY = 0, /**< every RB used */
  XRAN_RBIND_EVERYOTHER = 1, /**< every other RB used */
  XRAN_RBIND_MAX
};

/** Symbol number increment command, defined in 5.4.5.3 */
enum xran_cp_symbolnuminc {
  XRAN_SYMBOLNUMBER_NOTINC = 0, /**< do not increment the current symbol number */
  XRAN_SYMBOLNUMBER_INC = 1, /**< increment the current symbol number and use that */
  XRAN_SYMBOLNUMBER_INC_MAX
};

/** Macro to convert the number of PRBs as defined in 5.4.5.6 */
#define XRAN_CONVERT_NUMPRBC(x) ((x) > 255 ? 0 : (x))

#define XRAN_CONVERT_IQWIDTH(x) ((x) > 15 ? 0 : (x))

/** Control Plane section extension commands, defined in 5.4.6 Table 5.13 */
enum xran_cp_sectionextcmd {
  XRAN_CP_SECTIONEXTCMD_0 = 0, /**< Reserved, for future use */
  XRAN_CP_SECTIONEXTCMD_1 = 1, /**< Beamforming weights */
  XRAN_CP_SECTIONEXTCMD_2 = 2, /**< Beamforming attributes */
  XRAN_CP_SECTIONEXTCMD_3 = 3, /**< DL Precoding configuration parameters and indications, not supported */
  XRAN_CP_SECTIONEXTCMD_4 = 4, /**< Modulation compression parameter */
  XRAN_CP_SECTIONEXTCMD_5 = 5, /**< Modulation compression additional scaling parameters */
  XRAN_CP_SECTIONEXTCMD_6 = 6, /**< Non-contiguous PRB allocation */
  XRAN_CP_SECTIONEXTCMD_7 = 7, /**< Multiple-eAxC designation */
  XRAN_CP_SECTIONEXTCMD_8 = 8, /**< MMSE parameters */
  XRAN_CP_SECTIONEXTCMD_9 = 9, /**< Dynamic Spectrum Sharing parameters */
  XRAN_CP_SECTIONEXTCMD_10 = 10, /**< Multiple ports grouping */
  XRAN_CP_SECTIONEXTCMD_11 = 11, /**< Flexible BF weights */
  XRAN_CP_SECTIONEXTCMD_19 = 19, /**< Compact beamforming information for multiple port (CUS v16.01 7.7.19) */
  XRAN_CP_SECTIONEXTCMD_20 = 20, /**< Puncturing extension (CUS v16.01 7.7.20) */
  XRAN_CP_SECTIONEXTCMD_MAX /* 12~18 and 21~127 are not listed */
};

/** Macro to convert bfwIqWidth defined in 5.4.7.1.1, Table 5-15 */
#define XRAN_CONVERT_BFWIQWIDTH(x) ((x) > 15 ? 0 : (x))

/** Beamforming Weights Compression Method 5.4.7.1.1, Table 5-16 */
enum xran_cp_bfw_compression_method {
  XRAN_BFWCOMPMETHOD_NONE = 0, /**< Uncopressed I/Q value */
  XRAN_BFWCOMPMETHOD_BLKFLOAT = 1, /**< I/Q mantissa value */
  XRAN_BFWCOMPMETHOD_BLKSCALE = 2, /**< I/Q scaled value */
  XRAN_BFWCOMPMETHOD_ULAW = 3, /**< compressed I/Q value */
  XRAN_BFWCOMPMETHOD_BEAMSPACE = 4, /**< beamspace I/Q coefficient */
  XRAN_BFWCOMPMETHOD_MAX /* reserved for future methods */
};

/** Beamforming Attributes Bitwidth 5.4.7.2.1 */
enum xran_cp_bfa_bitwidth {
  XRAN_BFABITWIDTH_NO = 0, /**< the filed is no applicable or the default value shall be used */
  XRAN_BFABITWIDTH_2BIT = 1, /**< the filed is 2-bit bitwidth */
  XRAN_BFABITWIDTH_3BIT = 2, /**< the filed is 3-bit bitwidth */
  XRAN_BFABITWIDTH_4BIT = 3, /**< the filed is 4-bit bitwidth */
  XRAN_BFABITWIDTH_5BIT = 4, /**< the filed is 5-bit bitwidth */
  XRAN_BFABITWIDTH_6BIT = 5, /**< the filed is 6-bit bitwidth */
  XRAN_BFABITWIDTH_7BIT = 6, /**< the filed is 7-bit bitwidth */
  XRAN_BFABITWIDTH_8BIT = 7, /**< the filed is 8-bit bitwidth */
};

/** Resource Block Group Size 5.4.7.6.1 */
enum xran_cp_rbgsize {
  XRAN_RBGSIZE_1RB = 1, /**< 1 RB */
  XRAN_RBGSIZE_2RB = 2, /**< 2 RBs */
  XRAN_RBGSIZE_3RB = 3, /**< 3 RBs */
  XRAN_RBGSIZE_4RB = 4, /**< 4 RBs */
  XRAN_RBGSIZE_6RB = 5, /**< 6 RBs */
  XRAN_RBGSIZE_8RB = 6, /**< 8 RBs */
  XRAN_RBGSIZE_16RB = 7, /**< 16 RBs */
};

/** Macro to convert the number of PRBs as defined in 5.4.5.6 */
#define XRAN_CONVERT_NUMPRBC(x) ((x) > 255 ? 0 : (x))

#define XRAN_CONVERT_IQWIDTH(x) ((x) > 15 ? 0 : (x))

#ifdef __cplusplus
}
#endif

#endif
