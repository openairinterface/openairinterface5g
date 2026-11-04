/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#ifndef OAI_VRTSIM_CIRDB_PROVIDER_H
#define OAI_VRTSIM_CIRDB_PROVIDER_H

#include <stdbool.h>
#include <stdint.h>
#include "SIMULATION/TOOLS/sim.h"

/*
  Selection and timing options:

  want_model_id: TDL family index. Allowed: 0 (TDL-A), 1 (TDL-B), 2 (TDL-C), 3 (TDL-D), 4 (TDL-E).
                 Set < 0 to express no preference.
  want_ds_ns:    RMS delay spread in ns. Typical values: 30, 100, 300. Nonnegative float.
                 Set < 0 to express no preference.
  want_speed_mps: UE speed in m/s. Typical: walking ≈ 1.5, urban 3..10, vehicular 10..30.
                  Set < 0 to express no preference.
*/
typedef struct {
  const char *yaml_path; /* optional absolute path to vrtsim.yaml */
  const char *bin_path; /* optional absolute path to cir_db.bin  */

  int want_model_id; /* 0..4, or <0 for no preference */
  float want_ds_ns; /* nonnegative, or <0 for no preference */
  float want_speed_mps; /* nonnegative, or <0 for no preference */
  float want_aoa_deg; /* degrees; TDL-D/E only; 0.0 = broadside default */
} cirdb_select_opts_t;

/* Opaque handle for CIRDB provider */
typedef struct cirdb_provider_s cirdb_provider_t;

/* Initialize provider and publish snapshot 0 through channel_desc_out. */
cirdb_provider_t *cirdb_connect(int num_tx_antennas,
                                int num_rx_antennas,
                                const cirdb_select_opts_t *sel,
                                channel_desc_t **channel_desc_out);

/* Advance snapshot selection based on elapsed nanoseconds since the start. Returns true if a new
 * snapshot was published. */
bool cirdb_update(cirdb_provider_t *provider, uint64_t ns_since_start);

/* Tear down and free resources. */
void cirdb_stop(cirdb_provider_t *provider);

#endif /* OAI_VRTSIM_CIRDB_PROVIDER_H */
