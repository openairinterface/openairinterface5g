<!-- SPDX-License-Identifier: CC-BY-4.0 -->

This document describes the basic functioning of the 5G RRC layer, describes
the periodic output, and explains the various configuration options that
influence its behavior.

Developer documentation, such as UE connection control flow, reestablishment, or handover, are described in [a separate page](./rrc-dev.md).

[[_TOC_]]

## General

The RRC layer controls the basic connection setup of UEs as well as additional
procedures. It is the fundamental building block of OAI's CU-CP, and interacts
with lower layers (DU, basically MAC and RLC) through F1AP messages, with
the CU-UP through E1AP messages, and with the RRC/CU-CP of other gNBs
through XnAP messages. More information can be found in the respective
[F1AP page](../F1AP/F1-design.md), [E1AP page](../E1AP/E1-design.md) and
[XnAP page](../XnAP/Xn-design.md).

## Periodic output and interpretation

Similarly to the scheduler, the RRC periodically prints information about
connected UEs and DUs into file `nrRRC_stats.log` in the current working
directory of the executable running the RRC (typically, `nr-softmodem`). The
output lists first all UEs that are currently connected, and then all DUs, in
order.

For each UE, it prints:

```
UE 0 CU UE ID 1 DU UE ID 40352 RNTI 9da0 random identity c0f1ac9824000000:
    last RRC activity: 5 seconds ago
    PDU session 0 ID 10 status established
    associated DU:  DU assoc ID 8
```

where `UE 0` is the UE index, CU UE ID and DU UE IDs are the IDs used to
exchange over F1 (cf. scheduler logs). Further, it shows RNTI, when the last
RRC activity happened, the status of PDU sessions and which DU is associated
(through the SCTP association ID).


For each DU, it prints:

```
1 connected DUs
[1] DU ID 3584 (gNB-OAI) integrated DU-CU: 1 cell
  [1] nrCellID 12345678, PCI 0, Mode TDD, SSB ARFCN 641280
      TDD: band 78 ARFCN 640008 SCS 30 (kHz) PRB 106
```

The output shows:
- Total number of connected DUs
- For each DU: an index (`[1]`), the DU ID and name, connection type (either `integrated DU-CU` for monolithic deployments or `assoc_id X` for split deployments), and the number of cells served by this DU
- For each cell served by the DU: a cell index (`[1]`), the NR Cell ID, Physical Cell ID (PCI), operating mode (TDD or FDD), and SSB ARFCN
- Cell-specific frequency information: for TDD mode, the band, Point A ARFCN, subcarrier spacing (SCS), and number of resource blocks (PRB); for FDD mode, separate DL and UL frequency information

The RRC enables the support of multiple cells per DU (though currently each DU
typically serves one cell in practice).

As of now, it does not print information about connected CU-UPs or AMFs.

## Configuration of the RRC

### Split-related options (when running in a CU or CU-CP)

See [F1 documentation](../F1AP/F1-design.md) for information about the F1 split.
See [E1 documentation](../E1AP/E1-design.md) for information about the E1 split.

### RRC-specific configuration options

In the `gNBs` section of the gNB/CU/CU-CP configuration file is the
RRC-specific configuration

#### cell-specific options

Note that some SIBS are configured at the CU and some at the DU; please consult
the [MAC configuration](../MAC/mac-usage.md) as well for SIB configuration.

- `gNB_ID` and `gNB_name`: ID and name of the gNB
- `tracking_area_code`: the current tracking area code in the range `[0x0001,
  0xfffd]`
- `plmn`: the PLMN, which is a list of entries consisting of:
  - `mcc`: mobile country code
  - `mnc`: mobile network code
  - `mnc_length`: length of mobile network code, allowed values: 2, 3
  - `snssaiList`: list of NSSAI (network selection slice assistence
    information, "slice ID"), which itself consists in
    - `sst`: slice service type, in `[1,255]`
    - `sd` (default `0xffffff`): slice differentiator, in `[0,0xffffff]`,
      `0xffffff` is a reserved value and means "no SD"
    Note that: SST=1, no SD is "eMBB"; SST=2, no SD is "URLLC"; SST=3, no SD
    is "mMTC"
- `enable_sdap` (default: true): set `sdap-HeaderUL` and `sdap-HeaderDL` to
  present in the RRC `SDAP-Config` IE for SA PDU sessions. If false, both
  headers are absent (per DRB). SDAP entities are still created, SDAP layer always enabled.
- `cu_sibs` (default: `[]`) list of SIBs to give to the DU for transmission.
  Currently supported:
  - SIB2: serving-cell reselection parameters (configured in `sib2_config`)
  - SIB3: intra-frequency neighbour cell list (neighbours on the same SSB ARFCN as the serving cell)
  - SIB4: inter-frequency carriers + neighbour lists, grouped per `(absoluteFrequencySSB, subcarrierSpacing)`. Per-frequency fields (e.g. `cellReselectionPriority`, `threshX_HighP`, `threshX_LowP`, `q_OffsetFreq`) come from `frequency_list`

Example activation:
```
cu_sibs = ( 2, 3, 4 );
```
SIB2 is configured per-gNB in `sib2_config` (see below). SIB3/SIB4 are derived from the neighbour configuration:
- `neighbour_list` / `neighbour_cell_configuration`: neighbour identity + per-neighbour offsets (`q_OffsetCell`, etc.)
- `frequency_list`: per-frequency SIB4 reselection parameters (priority/thresholds/`q_OffsetFreq`)

Example `gNBs.[0].sib2_config`:
```
cu_sibs = ( 2 );

sib2_config : {
  q_Hyst                   = 0;
  cellReselectionPriority  = 0;
  threshServingLowP        = 0;
  threshServingLowQ        = 4;
  s_NonIntraSearchP        = 10;
  s_NonIntraSearchQ        = 8;
  q_RxLevMin               = -56;
  q_QualMin                = -18;
  s_IntraSearchP           = 22;
  s_IntraSearchQ           = 20;
  t_ReselectionNR          = 1;
  deriveSSB_IndexFromCell  = 1;

  speed_t_Evaluation       = 0;
  speed_t_HystNormal       = 0;
  speed_n_CellChangeMedium = 1;
  speed_n_CellChangeHigh   = 2;
  speed_sf_Medium          = 1;
  speed_sf_High            = 0;
};
```

#### SIB3/SIB4 and measurement gaps

This section summarizes how SIB3/SIB4 and measurement gaps relate in NR and how OAI currently implements them.

From 3GPP TS 38.331:

- `SIB3` carries intra-frequency reselection information (`intraFreqNeighCellList`).
- `SIB4` carries inter-frequency reselection information (`interFreqCarrierFreqList`
  and per-carrier neighbour lists).
- `MeasGapConfig` is part of dedicated `MeasConfig` (typically sent in
  `RRCReconfiguration`) and controls measurement gaps in connected mode.

In other words:

- SIB3/SIB4 are broadcast SI for idle/inactive (`RRC_IDLE` / `RRC_INACTIVE`)
  reselection behavior: UE performs autonomous cell reselection using broadcast
  SI (SIB3 for intra-frequency, SIB4 for inter-frequency).
- `MeasGapConfig` is a dedicated UE measurement behavior and applies to
  connected mode (`RRC_CONNECTED`): the network configures what the UE
  measures and reports (periodic and event-based, e.g., A3), and those reports
  are used by CU-CP mobility logic (including handover decisions).

MeasGap does not depend on SIB3/SIB4, however they share the same underlying
neighbour/frequency data model, which is the common source of serving +
neighbour frequency information (see also [Neighbor-gNB configuration](#neighbor-gnb-configuration)):

- SIB3/SIB4 generation is done on the CU-CP side (inside `rrc_gNB_du.c`) from
  neighbour/frequency configuration and serving-cell MTC-derived ARFCN.
- Measurement-gap configuration also starts from the same neighbour/frequency
  model: CU uses neighbour fields (frequency/PCI/band) to build UE
  `MeasConfig` measurement objects, then DU derives/encodes gap parameters
  from CU-provided timing (`meas_timing_config`) and returns `meas_gap_config`
  for CU forwarding in dedicated `RRCReconfiguration`.

Detailed implementation flow and sequence diagrams are documented in [`rrc-dev.md`](./rrc-dev.md).

#### UE-specific configuration

- `um_on_default_drb` (default: false): use RLC UM instead of RLC AM on default
  bearers

#### Neighbor-gNB configuration

Refer to the [handover tutorial](../handover-tutorial.md) for detailed information about gNB neighbors and handover procedures.

##### Configuration structure and key semantics

The neighbour configuration is a 2-level structure:

- Outer list: `neighbour_list`
  - Key: `nr_cellid` of the serving cell
  - One entry per serving cell
- Inner list: `neighbour_cell_configuration`
  - Actual neighbour cells for that serving cell
  - Contains neighbour fields such as `gNB_ID`, neighbour `nr_cellid`, `physical_cellId`, frequency, PLMN, etc.

This same core configuration model is reused by multiple RRC procedures.

- SIB3/SIB4 generation: uses neighbour identity/frequency/offset fields to derive
  intra/inter-frequency SI.
- Connected-mode measurement config: uses neighbour frequency/PCI/band fields to build
  UE `MeasConfig` measurement objects.
- Handover-related procedures: reuse neighbour identity fields (e.g., cell ID/PCI/PLMN
  TAC, gNB ID) for target selection and for populating target-cell information carried
  in NGAP and XnAP handover messages. The neighbour `gNB_ID` also selects the
  handover type: Xn handover if Xn Setup with that gNB completed, N2 handover
  otherwise (see [Xn interface configuration](#xn-interface-configuration)).

Conceptually, for each serving cell the RRC keeps:

- A per-frequency table (`inter_freqs`): one entry per `(absoluteFrequencySSB,
  subcarrierSpacing)` used for SIB4, containing the SIB4 per-frequency fields
  (priority, thresholds, `q_OffsetFreq`, `q_RxLevMin`, `t_ReselectionNR`).
- A per-neighbour list (`neighbour_cells`): one entry per neighbour, with
  identity (cell ID, PCI, PLMN, TAC), frequency (`absoluteFrequencySSB`,
  `subcarrierSpacing`, `band`), and SIB3/SIB4 per-neighbour offsets
  (`q_OffsetCell`, `q_RxLevMinOffsetCell`, `q_QualMinOffsetCell`).
- A link from neighbours to frequencies: each neighbour implicitly points to
  the matching `inter_freqs` entry via its `(absoluteFrequencySSB,
  subcarrierSpacing)`; if no such frequency exists, it is treated as having no
  SIB4 per-frequency configuration.

Notes:

- In `neighbour_list`, only `nr_cellid` is used as the key for lookup.
- `physical_cellId` belongs to neighbour-cell entries in `neighbour_cell_configuration` (inner list).
- `nr_cellid` entries in `neighbour_list` should be unique to avoid ambiguous lookup.
- Intra-frequency neighbours (SIB3) are derived only from the per-cell
  `neighbour_cell_configuration` on the serving carrier.

At configuration time (`gnb_config.c`), neighbours are parsed into `neighbour_cells`,
per-neighbour SIB3/SIB4 offsets are validated, and a per-frequency array `inter_freqs`
is built by grouping neighbours by `(absoluteFrequencySSB, subcarrierSpacing)` and
in-range SIB4 per-frequency fields (`cellReselectionPriority`, `threshX_HighP/L`,
`q_OffsetFreq`) across neighbours on the same ARFCN. At SIB4 build time, the RRC uses
`inter_freqs` to create one `InterFreqCarrierFreqInfo` per ARFCN for inter-frequency
carriers (ARFCN different from the serving SSB ARFCN) and attaches all neighbours whose
`inter_freq_idx` points to that frequency entry.

##### Required configuration parameters

To define a neighbour cell in the configuration file, the following parameters are required:

- `gNB_ID` - identifier of the neighbour gNB (e.g., `0xe01`)
- `nr_cellid` - cell identifier of the neighbour cell (e.g., `11111111`)
- `physical_cellId` - physical cell ID for radio identification (e.g., `1`)
- `absoluteFrequencySSB` - SSB frequency in ARFCN notation (e.g., `643296`)
- `subcarrierSpacing` - numerology index: 0=15kHz, 1=30kHz, 2=60kHz, 3=120kHz
- `band` - 3GPP frequency band number (e.g., `78` for 3.5GHz)
- `plmn` - PLMN configuration object with:
  - `mcc` - mobile country code (3 digits, e.g., `001`)
  - `mnc` - mobile network code (2-3 digits, e.g., `01`)
  - `mnc_length` - number of digits in MNC (must be `2` or `3`)
- `tracking_area_code` - tracking area identifier (e.g., `1`)

Example configuration structure:
```
# Per-frequency SIB4 configuration (one entry per ARFCN), shared by all cells
frequency_list = (
  {
    absoluteFrequencySSB = 643296;
    subcarrierSpacing    = 1;    # 30 kHz
    band                 = 78;

    frequency_config = (
      {
        cellReselectionPriority = 5;
        threshX_HighP = 10;
        threshX_LowP = 6;
        q_OffsetFreq = 0;
        # Optional: threshX_HighQ, threshX_LowQ, etc.
      }
    );
  }
);

# Per-cell neighbour configuration; neighbours reference frequency_list via ARFCN/SCS
neighbour_list = (
  {
    nr_cellid = 12345678;

    neighbour_cell_configuration = (
      {
        gNB_ID = 0xe01;
        nr_cellid = 11111111;
        physical_cellId = 1;
        absoluteFrequencySSB = 643296;   # ARFCN used to look up matching entry in frequency_list
        subcarrierSpacing = 1;           # 30 kHz
        band = 78;
        plmn = { mcc = 001; mnc = 01; mnc_length = 2 };
        tracking_area_code = 1;

        # Example per-neighbour offsets (SIB3/SIB4)
        q_OffsetCell = 0;
        q_RxLevMinOffsetCell = -1;
        q_QualMinOffsetCell = -1;
      }
    );
  }
);
```

Refer to the [handover tutorial](../handover-tutorial.md) for complete examples and detailed setup instructions.

#### Xn interface configuration

The Xn interface towards other gNBs is enabled with the `Xn_INTERFACE`
section, see the [Xn design](../XnAP/Xn-design.md#4-configuration). After Xn
Setup with a peer, RRC adds the peer's gNB ID to its Xn candidates. A handover
towards a neighbour cell whose `gNB_ID` (in `neighbour_list`) is an Xn
candidate uses Xn; otherwise, N2 is used. Refer to the
[handover tutorial](../handover-tutorial.md#xn-handover) for an example.
