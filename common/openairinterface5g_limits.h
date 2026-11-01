/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#ifndef OPENAIRINTERFACE5G_LIMITS_H_
#define OPENAIRINTERFACE5G_LIMITS_H_

#        define MAX_MOBILES_PER_GNB 16
/* PDSCH/PUSCH scheduled in one slot: the MAC schedules at most MAX_DCI_CORESET UEs per slot, plus RA and
   broadcast messages. L1 preallocates its PDSCH/PUSCH structures from this, not from MAX_MOBILES_PER_GNB. */
#define NR_MAX_SCHED_PER_SLOT 32
#        define NUMBER_OF_eNB_MAX 1
#ifndef NUMBER_OF_gNB_MAX
#        define NUMBER_OF_gNB_MAX 1
#endif
#        define NUMBER_OF_RU_MAX 2
#        define NUMBER_OF_NR_RU_MAX 2
#        define NUMBER_OF_UCI_MAX 16
#        define NUMBER_OF_ULSCH_MAX 8
#        define NUMBER_OF_DLSCH_MAX 8 
#        define NUMBER_OF_SRS_MAX 16
#        define NUMBER_OF_SCH_STATS_MAX 16


#            ifndef UE_EXPANSION
#                    define NUMBER_OF_UE_MAX 40
#                    define NUMBER_OF_CONNECTED_eNB_MAX 1
#                    define NUMBER_OF_CONNECTED_gNB_MAX 1
#            else
#                    define NUMBER_OF_UE_MAX 256
#                    define NUMBER_OF_CONNECTED_eNB_MAX 1
#                    define NUMBER_OF_CONNECTED_gNB_MAX 1
#            endif

#endif /* OPENAIRINTERFACE5G_LIMITS_H_ */
