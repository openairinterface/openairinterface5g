/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

// this function fills the PHY_vars->PHY_measurement structure

#include "PHY/defs_UE.h"
#include "PHY/phy_extern_ue.h"
#include "PHY/phy_extern.h"
#include "common/utils/LOG/log.h"
#include "PHY/sse_intrin.h"

//#define k1 1000
#define k1 ((long long int) 1000)
#define k2 ((long long int) (1024-k1))

//#define DEBUG_MEAS_RRC
//#define DEBUG_MEAS_UE
//#define DEBUG_RANK_EST

int16_t cond_num_threshold = 0;

void print_shorts(char *s,short *x)
{


  printf("%s  : %d,%d,%d,%d,%d,%d,%d,%d\n",s,
         x[0],x[1],x[2],x[3],x[4],x[5],x[6],x[7]
        );

}
void print_ints(char *s,int *x)
{


  printf("%s  : %d,%d,%d,%d\n",s,
         x[0],x[1],x[2],x[3]
        );

}

int16_t get_PL(module_id_t Mod_id,uint8_t CC_id,uint8_t eNB_index)
{

  PHY_VARS_UE *ue = PHY_vars_UE_g[Mod_id][CC_id];
  /*
  int RSoffset;


  if (ue->frame_parms.mode1_flag == 1)
    RSoffset = 6;
  else
    RSoffset = 3;
  */

  LOG_D(PHY,"get_PL : Frame %d : rsrp %f dBm/RE (%f), eNB power %d dBm/RE\n", ue->proc.proc_rxtx[0].frame_rx,
        (1.0*dB_fixed_times10(ue->measurements.rsrp[eNB_index])-(10.0*ue->rx_total_gain_dB))/10.0,
        10*log10((double)ue->measurements.rsrp[eNB_index]),
        ue->frame_parms.pdsch_config_common.referenceSignalPower);

  return((int16_t)(((10*ue->rx_total_gain_dB) -
                    dB_fixed_times10(ue->measurements.rsrp[eNB_index])+
                    //        dB_fixed_times10(RSoffset*12*ue_g[Mod_id][CC_id]->frame_parms.N_RB_DL) +
                    (ue->frame_parms.pdsch_config_common.referenceSignalPower*10))/10));
}


uint8_t get_n_adj_cells (module_id_t Mod_id,uint8_t CC_id)
{

  PHY_VARS_UE *ue = PHY_vars_UE_g[Mod_id][CC_id];

  if (ue)
    return ue->measurements.n_adj_cells;
  else
    return 0;
}

uint32_t get_rx_total_gain_dB (module_id_t Mod_id,uint8_t CC_id)
{

  PHY_VARS_UE *ue = PHY_vars_UE_g[Mod_id][CC_id];

  if (ue)
    return ue->rx_total_gain_dB;

  return 0xFFFFFFFF;
}
uint32_t get_RSSI (module_id_t Mod_id,uint8_t CC_id)
{

  PHY_VARS_UE *ue = PHY_vars_UE_g[Mod_id][CC_id];

  if (ue)
    return ue->measurements.rssi;

  return 0xFFFFFFFF;
}
double get_RSRP(module_id_t Mod_id,uint8_t CC_id,uint8_t eNB_index)
{

  AssertFatal(PHY_vars_UE_g!=NULL,"PHY_vars_UE_g is null\n");
  AssertFatal(PHY_vars_UE_g[Mod_id]!=NULL,"PHY_vars_UE_g[%d] is null\n",Mod_id);
  AssertFatal(PHY_vars_UE_g[Mod_id][CC_id]!=NULL,"PHY_vars_UE_g[%d][%d] is null\n",Mod_id,CC_id);

  PHY_VARS_UE *ue = PHY_vars_UE_g[Mod_id][CC_id];

  if (ue)
    return ((dB_fixed_times10(ue->measurements.rsrp[eNB_index]))/10.0-
	    get_rx_total_gain_dB(Mod_id,0) -
	    10*log10(ue->frame_parms.N_RB_DL*12));
  return -140.0;
}

uint32_t get_RSRQ(module_id_t Mod_id,uint8_t CC_id,uint8_t eNB_index)
{

  PHY_VARS_UE *ue = PHY_vars_UE_g[Mod_id][CC_id];

  if (ue)
    return ue->measurements.rsrq[eNB_index];

  return 0xFFFFFFFF;
}

int8_t set_RSRP_filtered(module_id_t Mod_id,uint8_t CC_id,uint8_t eNB_index,float rsrp)
{

  PHY_VARS_UE *ue = PHY_vars_UE_g[Mod_id][CC_id];

  if (ue) {
    ue->measurements.rsrp_filtered[eNB_index]=rsrp;
    return 0;
  }

  LOG_W(PHY,"[UE%d] could not set the rsrp\n",Mod_id);
  return -1;
}

int8_t set_RSRQ_filtered(module_id_t Mod_id,uint8_t CC_id,uint8_t eNB_index,float rsrq)
{

  PHY_VARS_UE *ue = PHY_vars_UE_g[Mod_id][CC_id];

  if (ue) {
    ue->measurements.rsrq_filtered[eNB_index]=rsrq;
    return 0;
  }

  LOG_W(PHY,"[UE%d] could not set the rsrq\n",Mod_id);
  return -1;

}

void ue_rrc_measurements(PHY_VARS_UE *ue,
    uint8_t slot,
    uint8_t abstraction_flag)
{

  uint8_t subframe = slot>>1;
  int aarx,rb;
  uint8_t pss_symb;
  uint8_t sss_symb;

  int32_t **rxdataF;
  int16_t *rxF,*rxF_pss,*rxF_sss;

  uint16_t Nid_cell = ue->frame_parms.Nid_cell;
  uint8_t eNB_offset,nu,l,nushift,k;
  uint16_t off;
  uint8_t previous_thread_id = ue->current_thread_id[subframe]==0 ? (RX_NB_TH-1):(ue->current_thread_id[subframe]-1);

  //uint8_t isPss; // indicate if this is a slot for extracting PSS
  //uint8_t isSss; // indicate if this is a slot for extracting SSS
  //int32_t pss_ext[4][72]; // contain the extracted 6*12 REs for mapping the PSS
  //int32_t sss_ext[4][72]; // contain the extracted 6*12 REs for mapping the SSS
  //int32_t (*xss_ext)[72]; // point to either pss_ext or sss_ext for common calculation
  //int16_t *re,*im; // real and imag part of each 32-bit xss_ext[][] value

  //LOG_I(PHY,"UE RRC MEAS Start Subframe %d Frame Type %d slot %d \n",subframe,ue->frame_parms.frame_type,slot);
  for (eNB_offset = 0; eNB_offset<1+ue->measurements.n_adj_cells; eNB_offset++) {

    if (eNB_offset==0) {
      ue->measurements.rssi = 0;
      //ue->measurements.n0_power_tot = 0;

      if (abstraction_flag == 0) {
        if ( ((ue->frame_parms.frame_type == FDD) && ((subframe == 0) || (subframe == 5))) ||
             ((ue->frame_parms.frame_type == TDD) && ((subframe == 1) || (subframe == 6)))
                )
        {  // FDD PSS/SSS, compute noise in DTX REs
          if (ue->frame_parms.Ncp == NORMAL) {
            for (aarx=0; aarx<ue->frame_parms.nb_antennas_rx; aarx++) {
          if(ue->frame_parms.frame_type == FDD)
          {
	      rxF_sss = (int16_t *)&ue->common_vars.common_vars_rx_data_per_thread[ue->current_thread_id[subframe]].rxdataF[aarx][(5*ue->frame_parms.ofdm_symbol_size)];
	      rxF_pss = (int16_t *)&ue->common_vars.common_vars_rx_data_per_thread[ue->current_thread_id[subframe]].rxdataF[aarx][(6*ue->frame_parms.ofdm_symbol_size)];
          }
          else
          {
              rxF_sss = (int16_t *)&ue->common_vars.common_vars_rx_data_per_thread[previous_thread_id].rxdataF[aarx][(13*ue->frame_parms.ofdm_symbol_size)];
              rxF_pss = (int16_t *)&ue->common_vars.common_vars_rx_data_per_thread[ue->current_thread_id[subframe]].rxdataF[aarx][(2*ue->frame_parms.ofdm_symbol_size)];
          }
              //-ve spectrum from SSS

              //+ve spectrum from SSS
              ue->measurements.n0_power[aarx] += (((int32_t)rxF_sss[2+70]*rxF_sss[2+70])+((int32_t)rxF_sss[2+69]*rxF_sss[2+69]));
              ue->measurements.n0_power[aarx] += (((int32_t)rxF_sss[2+68]*rxF_sss[2+68])+((int32_t)rxF_sss[2+67]*rxF_sss[2+67]));
              ue->measurements.n0_power[aarx] += (((int32_t)rxF_sss[2+66]*rxF_sss[2+66])+((int32_t)rxF_sss[2+65]*rxF_sss[2+65]));
              //              ue->measurements.n0_power[aarx] += (((int32_t)rxF_sss[2+64]*rxF_sss[2+64])+((int32_t)rxF_sss[2+63]*rxF_sss[2+63]));
              //              printf("sssp32 %d\n",ue->measurements.n0_power[aarx]);
              //+ve spectrum from PSS
              ue->measurements.n0_power[aarx] += (((int32_t)rxF_pss[2+70]*rxF_pss[2+70])+((int32_t)rxF_pss[2+69]*rxF_pss[2+69]));
              ue->measurements.n0_power[aarx] += (((int32_t)rxF_pss[2+68]*rxF_pss[2+68])+((int32_t)rxF_pss[2+67]*rxF_pss[2+67]));
              ue->measurements.n0_power[aarx] += (((int32_t)rxF_pss[2+66]*rxF_pss[2+66])+((int32_t)rxF_pss[2+65]*rxF_pss[2+65]));
          //              ue->measurements.n0_power[aarx] += (((int32_t)rxF_pss[2+64]*rxF_pss[2+64])+((int32_t)rxF_pss[2+63]*rxF_pss[2+63]));
          //          printf("pss32 %d\n",ue->measurements.n0_power[aarx]);              //-ve spectrum from PSS
              if(ue->frame_parms.frame_type == FDD)
              {
                  rxF_sss = (int16_t *)&ue->common_vars.common_vars_rx_data_per_thread[ue->current_thread_id[subframe]].rxdataF[aarx][(6*ue->frame_parms.ofdm_symbol_size)];
                  rxF_pss = (int16_t *)&ue->common_vars.common_vars_rx_data_per_thread[ue->current_thread_id[subframe]].rxdataF[aarx][(7*ue->frame_parms.ofdm_symbol_size)];
              }
              else
              {
                  rxF_sss = (int16_t *)&ue->common_vars.common_vars_rx_data_per_thread[previous_thread_id].rxdataF[aarx][(14*ue->frame_parms.ofdm_symbol_size)];
                  rxF_pss = (int16_t *)&ue->common_vars.common_vars_rx_data_per_thread[ue->current_thread_id[subframe]].rxdataF[aarx][(3*ue->frame_parms.ofdm_symbol_size)];
              }
          //              ue->measurements.n0_power[aarx] += (((int32_t)rxF_pss[-72]*rxF_pss[-72])+((int32_t)rxF_pss[-71]*rxF_pss[-71]));
          //          printf("pssm36 %d\n",ue->measurements.n0_power[aarx]);
              ue->measurements.n0_power[aarx] += (((int32_t)rxF_pss[-70]*rxF_pss[-70])+((int32_t)rxF_pss[-69]*rxF_pss[-69]));
              ue->measurements.n0_power[aarx] += (((int32_t)rxF_pss[-68]*rxF_pss[-68])+((int32_t)rxF_pss[-67]*rxF_pss[-67]));
              ue->measurements.n0_power[aarx] += (((int32_t)rxF_pss[-66]*rxF_pss[-66])+((int32_t)rxF_pss[-65]*rxF_pss[-65]));

              ue->measurements.n0_power[aarx] = (((int32_t)rxF_sss[-70]*rxF_sss[-70])+((int32_t)rxF_sss[-69]*rxF_sss[-69]));
              ue->measurements.n0_power[aarx] += (((int32_t)rxF_sss[-68]*rxF_sss[-68])+((int32_t)rxF_sss[-67]*rxF_sss[-67]));
              ue->measurements.n0_power[aarx] += (((int32_t)rxF_sss[-66]*rxF_sss[-66])+((int32_t)rxF_sss[-65]*rxF_sss[-65]));

          //              ue->measurements.n0_power[aarx] += (((int32_t)rxF_pss[-64]*rxF_pss[-64])+((int32_t)rxF_pss[-63]*rxF_pss[-63]));
          //          printf("pssm32 %d\n",ue->measurements.n0_power[aarx]);
              ue->measurements.n0_power_dB[aarx] = dB_fixed(ue->measurements.n0_power[aarx]/12);
              ue->measurements.n0_power_tot /*+=*/ = ue->measurements.n0_power[aarx];
        }

            //LOG_I(PHY,"Subframe %d RRC UE MEAS Noise Level %d \n", subframe, ue->measurements.n0_power_tot);

        ue->measurements.n0_power_tot_dB = dB_fixed(ue->measurements.n0_power_tot/(12*aarx));
        ue->measurements.n0_power_tot_dBm = ue->measurements.n0_power_tot_dB - ue->rx_total_gain_dB - dB_fixed(ue->frame_parms.ofdm_symbol_size);
        } else {
            LOG_E(PHY, "Not yet implemented: noise power calculation when prefix length == EXTENDED\n");
        }
        }
        else if ((ue->frame_parms.frame_type == TDD) &&
                 ((subframe == 1) || (subframe == 6))) {  // TDD PSS/SSS, compute noise in DTX REs // 2016-09-29 wilson fix incorrect noise power calculation


          pss_symb = 2;
          sss_symb = ue->frame_parms.symbols_per_tti-1;
          if (ue->frame_parms.Ncp==NORMAL) {
            for (aarx=0; aarx<ue->frame_parms.nb_antennas_rx; aarx++) {

                rxdataF  =  ue->common_vars.common_vars_rx_data_per_thread[(ue->current_thread_id[subframe])].rxdataF;
                rxF_pss  = (int16_t *) &rxdataF[aarx][((pss_symb*(ue->frame_parms.ofdm_symbol_size)))];

                rxdataF  =  ue->common_vars.common_vars_rx_data_per_thread[previous_thread_id].rxdataF;
                rxF_sss  = (int16_t *) &rxdataF[aarx][((sss_symb*(ue->frame_parms.ofdm_symbol_size)))];

                //-ve spectrum from SSS
            //          printf("slot %d: SSS DTX: %d,%d, non-DTX %d,%d\n",slot,rxF_pss[-72],rxF_pss[-71],rxF_pss[-36],rxF_pss[-35]);

            //              ue->measurements.n0_power[aarx] = (((int32_t)rxF_pss[-72]*rxF_pss[-72])+((int32_t)rxF_pss[-71]*rxF_pss[-71]));
            //          printf("sssn36 %d\n",ue->measurements.n0_power[aarx]);
                ue->measurements.n0_power[aarx] = (((int32_t)rxF_pss[-70]*rxF_pss[-70])+((int32_t)rxF_pss[-69]*rxF_pss[-69]));
                ue->measurements.n0_power[aarx] += (((int32_t)rxF_pss[-68]*rxF_pss[-68])+((int32_t)rxF_pss[-67]*rxF_pss[-67]));
                ue->measurements.n0_power[aarx] += (((int32_t)rxF_pss[-66]*rxF_pss[-66])+((int32_t)rxF_pss[-65]*rxF_pss[-65]));
            //              ue->measurements.n0_power[aarx] += (((int32_t)rxF_pss[-64]*rxF_pss[-64])+((int32_t)rxF_pss[-63]*rxF_pss[-63]));
            //          printf("sssm32 %d\n",ue->measurements.n0_power[aarx]);
                //+ve spectrum from SSS
            ue->measurements.n0_power[aarx] += (((int32_t)rxF_sss[2+70]*rxF_sss[2+70])+((int32_t)rxF_sss[2+69]*rxF_sss[2+69]));
                ue->measurements.n0_power[aarx] += (((int32_t)rxF_sss[2+68]*rxF_sss[2+68])+((int32_t)rxF_sss[2+67]*rxF_sss[2+67]));
                ue->measurements.n0_power[aarx] += (((int32_t)rxF_sss[2+66]*rxF_sss[2+66])+((int32_t)rxF_sss[2+65]*rxF_sss[2+65]));
            //          ue->measurements.n0_power[aarx] += (((int32_t)rxF_sss[2+64]*rxF_sss[2+64])+((int32_t)rxF_sss[2+63]*rxF_sss[2+63]));
            //          printf("sssp32 %d\n",ue->measurements.n0_power[aarx]);
                //+ve spectrum from PSS
                ue->measurements.n0_power[aarx] += (((int32_t)rxF_pss[2+70]*rxF_pss[2+70])+((int32_t)rxF_pss[2+69]*rxF_pss[2+69]));
                ue->measurements.n0_power[aarx] += (((int32_t)rxF_pss[2+68]*rxF_pss[2+68])+((int32_t)rxF_pss[2+67]*rxF_pss[2+67]));
                ue->measurements.n0_power[aarx] += (((int32_t)rxF_pss[2+66]*rxF_pss[2+66])+((int32_t)rxF_pss[2+65]*rxF_pss[2+65]));
            //              ue->measurements.n0_power[aarx] += (((int32_t)rxF_pss[2+64]*rxF_pss[2+64])+((int32_t)rxF_pss[2+63]*rxF_pss[2+63]));
            //          printf("pss32 %d\n",ue->measurements.n0_power[aarx]);              //-ve spectrum from PSS
                rxF_pss = (int16_t *)&ue->common_vars.common_vars_rx_data_per_thread[ue->current_thread_id[subframe]].rxdataF[aarx][(7*ue->frame_parms.ofdm_symbol_size)];
            //              ue->measurements.n0_power[aarx] += (((int32_t)rxF_pss[-72]*rxF_pss[-72])+((int32_t)rxF_pss[-71]*rxF_pss[-71]));
            //          printf("pssm36 %d\n",ue->measurements.n0_power[aarx]);
                ue->measurements.n0_power[aarx] += (((int32_t)rxF_pss[-70]*rxF_pss[-70])+((int32_t)rxF_pss[-69]*rxF_pss[-69]));
                ue->measurements.n0_power[aarx] += (((int32_t)rxF_pss[-68]*rxF_pss[-68])+((int32_t)rxF_pss[-67]*rxF_pss[-67]));
                ue->measurements.n0_power[aarx] += (((int32_t)rxF_pss[-66]*rxF_pss[-66])+((int32_t)rxF_pss[-65]*rxF_pss[-65]));
            //              ue->measurements.n0_power[aarx] += (((int32_t)rxF_pss[-64]*rxF_pss[-64])+((int32_t)rxF_pss[-63]*rxF_pss[-63]));
            //          printf("pssm32 %d\n",ue->measurements.n0_power[aarx]);
                ue->measurements.n0_power_dB[aarx] = dB_fixed(ue->measurements.n0_power[aarx]/12);
                ue->measurements.n0_power_tot /*+=*/ = ue->measurements.n0_power[aarx];
        }

        ue->measurements.n0_power_tot_dB = dB_fixed(ue->measurements.n0_power_tot/(12*aarx));
        ue->measurements.n0_power_tot_dBm = ue->measurements.n0_power_tot_dB - ue->rx_total_gain_dB - dB_fixed(ue->frame_parms.ofdm_symbol_size);

        //LOG_I(PHY,"Subframe %d RRC UE MEAS Noise Level %d \n", subframe, ue->measurements.n0_power_tot);

          }
        }
      }
    }
    // recompute nushift with eNB_offset corresponding to adjacent eNB on which to perform channel estimation
    //    printf("[PHY][UE %d] Frame %d slot %d Doing ue_rrc_measurements rsrp/rssi (Nid_cell %d, Nid2 %d, nushift %d, eNB_offset %d)\n",ue->Mod_id,ue->frame,slot,Nid_cell,Nid2,nushift,eNB_offset);
    if (eNB_offset > 0)
      Nid_cell = ue->measurements.adj_cell_id[eNB_offset-1];


    nushift =  Nid_cell%6;



    ue->measurements.rsrp[eNB_offset] = 0;


    if (abstraction_flag == 0) {

      // compute RSRP using symbols 0 and 4-frame_parms->Ncp

      for (l=0,nu=0; l<=(4-ue->frame_parms.Ncp); l+=(4-ue->frame_parms.Ncp),nu=3) {
        k = (nu + nushift)%6;
	//#ifdef DEBUG_MEAS_RRC
        LOG_D(PHY,"[UE %d] Frame %d subframe %d Doing ue_rrc_measurements rsrp/rssi (Nid_cell %d, nushift %d, eNB_offset %d, k %d, l %d)\n",ue->Mod_id,ue->proc.proc_rxtx[subframe&1].frame_rx,subframe,Nid_cell,nushift,
              eNB_offset,k,l);
	//#endif

        for (aarx=0; aarx<ue->frame_parms.nb_antennas_rx; aarx++) {
          rxF = (int16_t *)&ue->common_vars.common_vars_rx_data_per_thread[ue->current_thread_id[subframe]].rxdataF[aarx][(l*ue->frame_parms.ofdm_symbol_size)];
          off  = (ue->frame_parms.first_carrier_offset+k)<<1;

          if (l==(4-ue->frame_parms.Ncp)) {
            for (rb=0; rb<ue->frame_parms.N_RB_DL; rb++) {

              //    printf("rb %d, off %d, off2 %d\n",rb,off,off2);

              ue->measurements.rsrp[eNB_offset] += (((int32_t)(rxF[off])*rxF[off])+((int32_t)(rxF[off+1])*rxF[off+1]));
              //        printf("rb %d, off %d : %d\n",rb,off,((((int32_t)rxF[off])*rxF[off])+((int32_t)(rxF[off+1])*rxF[off+1])));
              //              if ((ue->frame_rx&0x3ff) == 0)
              //                printf("rb %d, off %d : %d\n",rb,off,((rxF[off]*rxF[off])+(rxF[off+1]*rxF[off+1])));


              off+=12;

              if (off>=(ue->frame_parms.ofdm_symbol_size<<1))
                off = (1+k)<<1;

              ue->measurements.rsrp[eNB_offset] += (((int32_t)(rxF[off])*rxF[off])+((int32_t)(rxF[off+1])*rxF[off+1]));
              //    printf("rb %d, off %d : %d\n",rb,off,(((int32_t)(rxF[off])*rxF[off])+((int32_t)(rxF[off+1])*rxF[off+1])));
              /*
                if ((ue->frame_rx&0x3ff) == 0)
                printf("rb %d, off %d : %d\n",rb,off,((rxF[off]*rxF[off])+(rxF[off+1]*rxF[off+1])));
              */
              off+=12;

              if (off>=(ue->frame_parms.ofdm_symbol_size<<1))
                off = (1+k)<<1;

            }

            /*
            if ((eNB_offset==0)&&(l==0)) {
            for (i=0;i<6;i++,off2+=4)
            ue->measurements.rssi += ((rxF[off2]*rxF[off2])+(rxF[off2+1]*rxF[off2+1]));
            if (off2==(ue->frame_parms.ofdm_symbol_size<<2))
            off2=4;
            for (i=0;i<6;i++,off2+=4)
            ue->measurements.rssi += ((rxF[off2]*rxF[off2])+(rxF[off2+1]*rxF[off2+1]));
            }
            */
            //    printf("slot %d, rb %d => rsrp %d, rssi %d\n",slot,rb,ue->measurements.rsrp[eNB_offset],ue->measurements.rssi);
          }
        }
      }

      // 2 RE per PRB
      //      ue->measurements.rsrp[eNB_offset]/=(24*ue->frame_parms.N_RB_DL);
      ue->measurements.rsrp[eNB_offset]/=(2*ue->frame_parms.N_RB_DL*ue->frame_parms.ofdm_symbol_size);
      //      LOG_I(PHY,"eNB: %d, RSRP: %d \n",eNB_offset,ue->measurements.rsrp[eNB_offset]);
      if (eNB_offset == 0) {
        //  ue->measurements.rssi/=(24*ue->frame_parms.N_RB_DL);
        //  ue->measurements.rssi*=rx_power_correction;
        //  ue->measurements.rssi=ue->measurements.rsrp[0]*24/2;
        ue->measurements.rssi=ue->measurements.rsrp[0]*(12*ue->frame_parms.N_RB_DL);
      }

      if (ue->measurements.rssi>0)
        ue->measurements.rsrq[eNB_offset] = 100*ue->measurements.rsrp[eNB_offset]*ue->frame_parms.N_RB_DL/ue->measurements.rssi;
      else
        ue->measurements.rsrq[eNB_offset] = -12000;

      //((200*ue->measurements.rsrq[eNB_offset]) + ((1024-200)*100*ue->measurements.rsrp[eNB_offset]*ue->frame_parms.N_RB_DL/ue->measurements.rssi))>>10;
    } else { // Do abstraction of RSRP and RSRQ
      ue->measurements.rssi = ue->measurements.rx_power_avg[0];
      // dummay value for the moment
      ue->measurements.rsrp[eNB_offset] = -93 ;
      ue->measurements.rsrq[eNB_offset] = 3;

    }

    //#ifdef DEBUG_MEAS_RRC

    //    if (slot == 0) {

      if (eNB_offset == 0)
	
        LOG_D(PHY,"[UE %d] Frame %d, subframe %d RRC Measurements => rssi %3.1f dBm (digital: %3.1f dB, gain %d), N0 %d dBm\n",ue->Mod_id,
              ue->proc.proc_rxtx[subframe&1].frame_rx,subframe,10*log10(ue->measurements.rssi)-ue->rx_total_gain_dB,
              10*log10(ue->measurements.rssi),
              ue->rx_total_gain_dB,
              ue->measurements.n0_power_tot_dBm);

      LOG_D(PHY,"[UE %d] Frame %d, subframe %d RRC Measurements (idx %d, Cell id %d) => rsrp: %3.1f dBm/RE (%d), rsrq: %3.1f dB\n",
            ue->Mod_id,
            ue->proc.proc_rxtx[subframe&1].frame_rx,subframe,eNB_offset,
            (eNB_offset>0) ? ue->measurements.adj_cell_id[eNB_offset-1] : ue->frame_parms.Nid_cell,
            10*log10(ue->measurements.rsrp[eNB_offset])-ue->rx_total_gain_dB,
            ue->measurements.rsrp[eNB_offset],
            (10*log10(ue->measurements.rsrq[eNB_offset])));
      //LOG_D(PHY,"RSRP_total_dB: %3.2f \n",(dB_fixed_times10(ue->measurements.rsrp[eNB_offset])/10.0)-ue->rx_total_gain_dB-dB_fixed(ue->frame_parms.N_RB_DL*12));

      //LOG_D(PHY,"RSRP_dB: %3.2f \n",(dB_fixed_times10(ue->measurements.rsrp[eNB_offset])/10.0));
      //LOG_D(PHY,"gain_loss_dB: %d \n",ue->rx_total_gain_dB);
      //LOG_D(PHY,"gain_fixed_dB: %d \n",dB_fixed(ue->frame_parms.N_RB_DL*12));

      //    }

      //#endif
  }

}

void conjch0_mult_ch1(int *ch0,
                      int *ch1,
                      int32_t *ch0conj_ch1,
                      unsigned short nb_rb,
                      unsigned char output_shift0)
{
  //This function is used to compute multiplications in Hhermitian * H matrix
  simde__m128i *dl_ch0_128 = (simde__m128i *)ch0;
  simde__m128i *dl_ch1_128 = (simde__m128i *)ch1;

  simde__m128i *ch0conj_ch1_128 = (simde__m128i *)ch0conj_ch1;

  for (unsigned short rb=0; rb<3*nb_rb; rb++) {

    ch0conj_ch1_128[rb] = oai_mm_cpx_mult_conj(dl_ch0_128[rb], dl_ch1_128[rb], output_shift0);

#ifdef DEBUG_RANK_EST
    printf("\n Computing conjugates \n");
    print_shorts("ch0:",(int16_t*)&dl_ch0_128[rb]);
    print_shorts("ch1:",(int16_t*)&dl_ch1_128[rb]);
    print_shorts("pack:",(int16_t*)&ch0conj_ch1_128[rb]);
#endif

  }
}

void construct_HhH_elements(int *ch0conj_ch0, //00_00
                            int *ch1conj_ch1, //01_01
                            int *ch2conj_ch2, //11_11
                            int *ch3conj_ch3, //10_10
                            int *ch0conj_ch1, //00_01
                            int *ch1conj_ch0, //01_00
                            int *ch2conj_ch3, //10_11
                            int *ch3conj_ch2, //11_10
                            int32_t *after_mf_00,
                            int32_t *after_mf_01,
                            int32_t *after_mf_10,
                            int32_t *after_mf_11,
                            unsigned short nb_rb)
{

  simde__m128i *ch0conj_ch0_128, *ch1conj_ch1_128, *ch2conj_ch2_128, *ch3conj_ch3_128;
  simde__m128i *ch0conj_ch1_128, *ch1conj_ch0_128, *ch2conj_ch3_128, *ch3conj_ch2_128;
  simde__m128i *after_mf_00_128, *after_mf_01_128, *after_mf_10_128, *after_mf_11_128;

  ch0conj_ch0_128 = (simde__m128i *)ch0conj_ch0;
  ch1conj_ch1_128 = (simde__m128i *)ch1conj_ch1;
  ch2conj_ch2_128 = (simde__m128i *)ch2conj_ch2;
  ch3conj_ch3_128 = (simde__m128i *)ch3conj_ch3;
  ch0conj_ch1_128 = (simde__m128i *)ch0conj_ch1;
  ch1conj_ch0_128 = (simde__m128i *)ch1conj_ch0;
  ch2conj_ch3_128 = (simde__m128i *)ch2conj_ch3;
  ch3conj_ch2_128 = (simde__m128i *)ch3conj_ch2;
  after_mf_00_128 = (simde__m128i *)after_mf_00;
  after_mf_01_128 = (simde__m128i *)after_mf_01;
  after_mf_10_128 = (simde__m128i *)after_mf_10;
  after_mf_11_128 = (simde__m128i *)after_mf_11;

  for (unsigned short rb=0; rb<3*nb_rb; rb++) {
    after_mf_00_128[0] = simde_mm_adds_epi16(ch0conj_ch0_128[0], ch3conj_ch3_128[0]); //00_00 + 10_10 simde_mm_adds_epi32(ch0conj_ch0_128[0], ch3conj_ch3_128[0]);
    after_mf_11_128[0] = simde_mm_adds_epi16(ch1conj_ch1_128[0], ch2conj_ch2_128[0]); //01_01 + 11_11
    after_mf_01_128[0] = simde_mm_adds_epi16(ch0conj_ch1_128[0], ch2conj_ch3_128[0]); //00_01 + 10_11
    after_mf_10_128[0] = simde_mm_adds_epi16(ch1conj_ch0_128[0], ch3conj_ch2_128[0]); //01_00 + 11_10

#ifdef DEBUG_RANK_EST
    printf(" \n construct_HhH_elements \n");
    print_shorts("ch0conj_ch0_128:",(int16_t*)&ch0conj_ch0_128[0]);
    print_shorts("ch1conj_ch1_128:",(int16_t*)&ch1conj_ch1_128[0]);
    print_shorts("ch2conj_ch2_128:",(int16_t*)&ch2conj_ch2_128[0]);
    print_shorts("ch3conj_ch3_128:",(int16_t*)&ch3conj_ch3_128[0]);
    print_shorts("ch0conj_ch1_128:",(int16_t*)&ch0conj_ch1_128[0]);
    print_shorts("ch1conj_ch0_128:",(int16_t*)&ch1conj_ch0_128[0]);
    print_shorts("ch2conj_ch3_128:",(int16_t*)&ch2conj_ch3_128[0]);
    print_shorts("ch3conj_ch2_128:",(int16_t*)&ch3conj_ch2_128[0]);
    print_shorts("after_mf_00_128:",(int16_t*)&after_mf_00_128[0]);
    print_shorts("after_mf_01_128:",(int16_t*)&after_mf_01_128[0]);
    print_shorts("after_mf_10_128:",(int16_t*)&after_mf_10_128[0]);
    print_shorts("after_mf_11_128:",(int16_t*)&after_mf_11_128[0]);
#endif

    ch0conj_ch0_128+=1;
    ch1conj_ch1_128+=1;
    ch2conj_ch2_128+=1;
    ch3conj_ch3_128+=1;
    ch0conj_ch1_128+=1;
    ch1conj_ch0_128+=1;
    ch2conj_ch3_128+=1;
    ch3conj_ch2_128+=1;

    after_mf_00_128+=1;
    after_mf_01_128+=1;
    after_mf_10_128+=1;
    after_mf_11_128+=1;
  }
}

static void squared_matrix_element(int32_t *Hh_h_00, int32_t *Hh_h_00_sq, unsigned short nb_rb)
{

  simde__m128i *Hh_h_00_128,*Hh_h_00_sq_128;

  Hh_h_00_128 = (simde__m128i *)Hh_h_00;
  Hh_h_00_sq_128 = (simde__m128i *)Hh_h_00_sq;

  for (unsigned short rb=0; rb<3*nb_rb; rb++) {

    Hh_h_00_sq_128[0] = simde_mm_madd_epi16(Hh_h_00_128[0],Hh_h_00_128[0]);

#ifdef DEBUG_RANK_EST
    printf("\n Computing squared_matrix_element \n");
    print_shorts("Hh_h_00_128:",(int16_t*)&Hh_h_00_128[0]);
    print_ints("Hh_h_00_sq_128:",(int32_t*)&Hh_h_00_sq_128[0]);
#endif

    Hh_h_00_sq_128+=1;
    Hh_h_00_128+=1;
  }
}



void det_HhH(int32_t *after_mf_00,
             int32_t *after_mf_01,
             int32_t *after_mf_10,
             int32_t *after_mf_11,
             int32_t *det_fin,
             unsigned short nb_rb)

{
  unsigned short rb;
  simde__m128i *after_mf_00_128, *after_mf_01_128, *after_mf_11_128, ad_re_128, bc_re_128;
  //__m128i *after_mf_10_128; // the variable is only written, but leave it here for "symmetry" in the algorithm
  simde__m128i *det_fin_128, det_128;

  after_mf_00_128 = (simde__m128i *)after_mf_00;
  after_mf_01_128 = (simde__m128i *)after_mf_01;
  //after_mf_10_128 = (__m128i *)after_mf_10;
  after_mf_11_128 = (simde__m128i *)after_mf_11;

  det_fin_128 = (simde__m128i *)det_fin;

  for (rb=0; rb<3*nb_rb; rb++) {

    ad_re_128 = simde_mm_madd_epi16(after_mf_00_128[0],after_mf_11_128[0]);
    bc_re_128 = simde_mm_madd_epi16(after_mf_01_128[0],after_mf_01_128[0]);
    det_128 = simde_mm_sub_epi32(ad_re_128, bc_re_128);
    det_fin_128[0] = simde_mm_abs_epi32(det_128);

#ifdef DEBUG_RANK_EST
    printf("\n Computing denominator \n");
    print_shorts("after_mf_00_128:",(int16_t*)&after_mf_00_128[0]);
    print_shorts("after_mf_01_128:",(int16_t*)&after_mf_01_128[0]);
    //print_shorts("after_mf_10_128:",(int16_t*)&after_mf_10_128[0]);
    print_shorts("after_mf_11_128:",(int16_t*)&after_mf_11_128[0]);
    print_ints("ad_re_128:",(int32_t*)&ad_re_128);
    print_ints("bc_re_128:",(int32_t*)&bc_re_128);
    print_ints("det_fin_128:",(int32_t*)&det_fin_128[0]);
#endif 

    det_fin_128+=1;
    after_mf_00_128+=1;
    after_mf_01_128+=1;
    //after_mf_10_128+=1;
    after_mf_11_128+=1;
  }
}

void numer(int32_t *Hh_h_00_sq,
           int32_t *Hh_h_01_sq,
           int32_t *Hh_h_10_sq,
           int32_t *Hh_h_11_sq,
           int32_t *num_fin,
           unsigned short nb_rb)

{

  simde__m128i *h_h_00_sq_128, *h_h_01_sq_128, *h_h_10_sq_128, *h_h_11_sq_128;
  simde__m128i *num_fin_128, sq_a_plus_sq_d_128, sq_b_plus_sq_c_128;

  h_h_00_sq_128 = (simde__m128i *)Hh_h_00_sq;
  h_h_01_sq_128 = (simde__m128i *)Hh_h_01_sq;
  h_h_10_sq_128 = (simde__m128i *)Hh_h_10_sq;
  h_h_11_sq_128 = (simde__m128i *)Hh_h_11_sq;

  num_fin_128 = (simde__m128i *)num_fin;

  for (unsigned short rb=0; rb<3*nb_rb; rb++) {

    sq_a_plus_sq_d_128 = simde_mm_add_epi32(h_h_00_sq_128[0],h_h_11_sq_128[0]);
    sq_b_plus_sq_c_128 = simde_mm_add_epi32(h_h_01_sq_128[0],h_h_10_sq_128[0]);
    num_fin_128[0] = simde_mm_add_epi32(sq_a_plus_sq_d_128, sq_b_plus_sq_c_128);

#ifdef DEBUG_RANK_EST
    printf("\n Computing numerator \n");
    print_ints("h_h_00_sq_128:",(int32_t*)&h_h_00_sq_128[0]);
    print_ints("h_h_01_sq_128:",(int32_t*)&h_h_01_sq_128[0]);
    print_ints("h_h_10_sq_128:",(int32_t*)&h_h_10_sq_128[0]);
    print_ints("h_h_11_sq_128:",(int32_t*)&h_h_11_sq_128[0]);
    print_shorts("sq_a_plus_sq_d_128:",(int16_t*)&sq_a_plus_sq_d_128);
    print_shorts("sq_b_plus_sq_c_128:",(int16_t*)&sq_b_plus_sq_c_128);
    print_shorts("num_fin_128:",(int16_t*)&num_fin_128[0]);
#endif

    num_fin_128+=1;
    h_h_00_sq_128+=1;
    h_h_01_sq_128+=1;
    h_h_10_sq_128+=1;
    h_h_11_sq_128+=1;
  }
}

void dlsch_channel_level_TM34_meas(int *ch00,
                                   int *ch01,
                                   int *ch10,
                                   int *ch11,
                                   int *avg_0,
                                   int *avg_1,
                                   unsigned short nb_rb)
{

  unsigned char nre=12;
  simde__m128i *ch00_128, *ch01_128, *ch10_128, *ch11_128;
  simde__m128i avg_0_row0_128D, avg_1_row0_128D, avg_0_row1_128D, avg_1_row1_128D;
  simde__m128i ch00_128_tmp, ch01_128_tmp, ch10_128_tmp, ch11_128_tmp;

  avg_0[0] = 0;
  avg_0[1] = 0;
  avg_1[0] = 0;
  avg_1[1] = 0;

  ch00_128 = (simde__m128i *)ch00;
  ch01_128 = (simde__m128i *)ch01;
  ch10_128 = (simde__m128i *)ch10;
  ch11_128 = (simde__m128i *)ch11;

  avg_0_row0_128D = simde_mm_setzero_si128();
  avg_1_row0_128D = simde_mm_setzero_si128();
  avg_0_row1_128D = simde_mm_setzero_si128();
  avg_1_row1_128D = simde_mm_setzero_si128();

  for (short rb=0; rb<3*nb_rb; rb++) {
    ch00_128_tmp = simde_mm_load_si128(&ch00_128[0]);
    ch01_128_tmp = simde_mm_load_si128(&ch01_128[0]);
    ch10_128_tmp = simde_mm_load_si128(&ch10_128[0]);
    ch11_128_tmp = simde_mm_load_si128(&ch11_128[0]);

    avg_0_row0_128D = simde_mm_add_epi32(avg_0_row0_128D,simde_mm_madd_epi16(ch00_128_tmp,ch00_128_tmp));
    avg_1_row0_128D = simde_mm_add_epi32(avg_1_row0_128D,simde_mm_madd_epi16(ch01_128_tmp,ch01_128_tmp));
    avg_0_row1_128D = simde_mm_add_epi32(avg_0_row1_128D,simde_mm_madd_epi16(ch10_128_tmp,ch10_128_tmp));
    avg_1_row1_128D = simde_mm_add_epi32(avg_1_row1_128D,simde_mm_madd_epi16(ch11_128_tmp,ch11_128_tmp));

    ch00_128+=1;
    ch01_128+=1;
    ch10_128+=1;
    ch11_128+=1;
  }

  avg_0[0] = (((int*)&avg_0_row0_128D)[0])/(nb_rb*nre) +
           (((int*)&avg_0_row0_128D)[1])/(nb_rb*nre) +
           (((int*)&avg_0_row0_128D)[2])/(nb_rb*nre) +
           (((int*)&avg_0_row0_128D)[3])/(nb_rb*nre);

  avg_1[0] = (((int*)&avg_1_row0_128D)[0])/(nb_rb*nre) +
           (((int*)&avg_1_row0_128D)[1])/(nb_rb*nre) +
           (((int*)&avg_1_row0_128D)[2])/(nb_rb*nre) +
           (((int*)&avg_1_row0_128D)[3])/(nb_rb*nre);

  avg_0[1] = (((int*)&avg_0_row1_128D)[0])/(nb_rb*nre) +
           (((int*)&avg_0_row1_128D)[1])/(nb_rb*nre) +
           (((int*)&avg_0_row1_128D)[2])/(nb_rb*nre) +
           (((int*)&avg_0_row1_128D)[3])/(nb_rb*nre);

  avg_1[1] = (((int*)&avg_1_row1_128D)[0])/(nb_rb*nre) +
           (((int*)&avg_1_row1_128D)[1])/(nb_rb*nre) +
           (((int*)&avg_1_row1_128D)[2])/(nb_rb*nre) +
           (((int*)&avg_1_row1_128D)[3])/(nb_rb*nre);

  avg_0[0] = avg_0[0] + avg_0[1];
  avg_1[0] = avg_1[0] + avg_1[1];
  avg_0[0] = min (avg_0[0], avg_1[0]);
  avg_1[0] = avg_0[0];


}

uint8_t rank_estimation_tm3_tm4 (int *dl_ch_estimates_00, // please respect the order of channel estimates
                                 int *dl_ch_estimates_01,
                                 int *dl_ch_estimates_10,
                                 int *dl_ch_estimates_11,
                                 unsigned short nb_rb)
{

  int i=0;
  int rank=0;
  int N_RB=nb_rb;
  int *ch00_rank, *ch01_rank, *ch10_rank, *ch11_rank;

  int32_t shift;
  int avg_0[2];
  int avg_1[2];

  int count=0;

  /* we need at least alignment to 16 bytes, let's put 32 to be sure
   * (maybe not necessary but doesn't hurt)
   */
  int32_t conjch00_ch01[12*N_RB] __attribute__((aligned(32)));
  int32_t conjch01_ch00[12*N_RB] __attribute__((aligned(32)));
  int32_t conjch10_ch11[12*N_RB] __attribute__((aligned(32)));
  int32_t conjch11_ch10[12*N_RB] __attribute__((aligned(32)));
  int32_t conjch00_ch00[12*N_RB] __attribute__((aligned(32)));
  int32_t conjch01_ch01[12*N_RB] __attribute__((aligned(32)));
  int32_t conjch10_ch10[12*N_RB] __attribute__((aligned(32)));
  int32_t conjch11_ch11[12*N_RB] __attribute__((aligned(32)));
  int32_t af_mf_00[12*N_RB] __attribute__((aligned(32)));
  int32_t af_mf_00_sq[12*N_RB] __attribute__((aligned(32)));
  int32_t af_mf_01_sq[12*N_RB] __attribute__((aligned(32)));
  int32_t af_mf_10_sq[12*N_RB] __attribute__((aligned(32)));
  int32_t af_mf_11_sq[12*N_RB] __attribute__((aligned(32)));
  int32_t af_mf_01[12*N_RB] __attribute__((aligned(32)));
  int32_t af_mf_10[12*N_RB] __attribute__((aligned(32)));
  int32_t af_mf_11[12*N_RB] __attribute__((aligned(32)));
  int32_t determ_fin[12*N_RB] __attribute__((aligned(32)));
  int32_t denum_db[12*N_RB] __attribute__((aligned(32)));
  int32_t numer_fin[12*N_RB] __attribute__((aligned(32)));
  int32_t numer_db[12*N_RB] __attribute__((aligned(32)));
  int32_t cond_db[12*N_RB] __attribute__((aligned(32)));

  ch00_rank = dl_ch_estimates_00;
  ch01_rank = dl_ch_estimates_01;
  ch10_rank = dl_ch_estimates_10;
  ch11_rank = dl_ch_estimates_11;

  dlsch_channel_level_TM34_meas(ch00_rank,
                                ch01_rank,
                                ch10_rank,
                                ch11_rank,
                                avg_0,
                                avg_1,
                                N_RB);

  avg_0[0] = (log2_approx(avg_0[0])/2);
  shift = cmax(avg_0[0],0);

#ifdef DEBUG_RANK_EST
  printf("\n shift %d \n" , shift);
  printf("\n conj(ch00)ch01 \n");
#endif

  conjch0_mult_ch1(ch00_rank,
                   ch01_rank,
                   conjch00_ch01,
                   N_RB,
                   shift); // this is an arbitrary shift to avoid overflow. can be changed.

#ifdef DEBUG_RANK_EST
  printf("\n conj(ch01)ch00 \n");
#endif

  conjch0_mult_ch1(ch01_rank,
                   ch00_rank,
                   conjch01_ch00,
                   N_RB,
                   shift);

#ifdef DEBUG_RANK_EST
  printf("\n conj(ch10)ch11 \n");
#endif


  conjch0_mult_ch1(ch10_rank,
                   ch11_rank,
                   conjch10_ch11,
                   N_RB,
                   shift);

#ifdef DEBUG_RANK_EST
  printf("\n conj(ch11)ch10 \n");
#endif

  conjch0_mult_ch1(ch11_rank,
                   ch10_rank,
                   conjch11_ch10,
                   N_RB,
                   shift);

#ifdef DEBUG_RANK_EST
  printf("\n conj(ch00)ch00 \n");
#endif

  conjch0_mult_ch1(ch00_rank,
                   ch00_rank,
                   conjch00_ch00,
                   N_RB,
                   shift);

#ifdef DEBUG_RANK_EST
  printf("\n conj(ch01)ch01 \n");
#endif

  conjch0_mult_ch1(ch01_rank,
                   ch01_rank,
                   conjch01_ch01,
                   N_RB,
                   shift);

#ifdef DEBUG_RANK_EST
  printf("\n conj(ch10)ch10 \n");
#endif

  conjch0_mult_ch1(ch10_rank,
                   ch10_rank,
                   conjch10_ch10,
                   N_RB,
                   shift);
#ifdef DEBUG_RANK_EST
  printf("\n conj(ch11)ch11 \n");
#endif

  conjch0_mult_ch1(ch11_rank,
                   ch11_rank,
                   conjch11_ch11,
                   N_RB,
                   shift);

  construct_HhH_elements(conjch00_ch00,
                         conjch01_ch01,
                         conjch11_ch11,
                         conjch10_ch10,
                         conjch00_ch01,
                         conjch01_ch00,
                         conjch10_ch11,
                         conjch11_ch10,
                         af_mf_00,
                         af_mf_01,
                         af_mf_10,
                         af_mf_11,
                         N_RB);
#ifdef DEBUG_RANK_EST
  printf("\n |HhH00|^2 \n");
#endif

  squared_matrix_element(af_mf_00,
                         af_mf_00_sq,
                         N_RB);

#ifdef DEBUG_RANK_EST
  printf("\n |HhH01|^2 \n");
#endif

  squared_matrix_element(af_mf_01,
                         af_mf_01_sq,
                         N_RB);

#ifdef DEBUG_RANK_EST
  printf("\n |HhH10|^2 \n");
#endif

  squared_matrix_element(af_mf_10,
                         af_mf_10_sq,
                         N_RB);

#ifdef DEBUG_RANK_EST
  printf("\n |HhH11|^2 \n");
#endif

  squared_matrix_element(af_mf_11,
                         af_mf_11_sq,
                         N_RB);

  det_HhH(af_mf_00,
          af_mf_01,
          af_mf_10,
          af_mf_11,
          determ_fin,
          N_RB);

  numer(af_mf_00_sq,
        af_mf_01_sq,
        af_mf_10_sq,
        af_mf_11_sq,
        numer_fin,
        N_RB);

  for (i=1; i<12*N_RB; i++)
  {
    denum_db[i]=dB_fixed(determ_fin[i]);
    numer_db[i]=dB_fixed(numer_fin[i]);
    cond_db[i]=(numer_db[i]-denum_db[i]);
    if (cond_db[i] < cond_num_threshold)
      count++;
#ifdef DEBUG_RANK_EST
    printf("cond_num_threshold =%d \n", cond_num_threshold);
    printf("i %d  numer_db[i] = %d \n", i, numer_db[i]);
    printf("i %d  denum_db[i] = %d \n", i, denum_db[i]);
    printf("i %d  cond_db[i] =  %d \n", i, cond_db[i]);
    printf("i %d counter = %d \n", i, count);
#endif
  }

  if (count >= 6*N_RB) // conditional number is lower 10dB in half on more Res Blocks
    rank=1;

#ifdef DEBUG_RANK_EST
    printf(" rank = %d \n", rank);
#endif
   return(rank);
}







void lte_ue_measurements(PHY_VARS_UE *ue,
                         unsigned int subframe_offset,
                         unsigned char N0_symbol,
                         unsigned char abstraction_flag,
                         unsigned char rank_adaptation,
                         uint8_t subframe)
{


  int aarx,aatx,eNB_id=0; //,gain_offset=0;
  //int rx_power[NUMBER_OF_CONNECTED_eNB_MAX];
  int i;
  unsigned int limit,subband;
  simde__m128i *dl_ch0_128, *dl_ch1_128;
  int *dl_ch0=NULL,*dl_ch1=NULL;

  LTE_DL_FRAME_PARMS *frame_parms = &ue->frame_parms;
  int nb_subbands,subband_size,last_subband_size;
  int N_RB_DL = frame_parms->N_RB_DL;


  int rank_tm3_tm4=-1;


  ue->measurements.nb_antennas_rx = frame_parms->nb_antennas_rx;


  switch (N_RB_DL) {
  case 6:
    nb_subbands = 6;
    subband_size = 12;
    last_subband_size = 0;
    break;

  default:
  case 25:
    nb_subbands = 7;
    subband_size = 4*12;
    last_subband_size = 12;
    break;

  case 50:
    nb_subbands = 9;
    subband_size = 6*12;
    last_subband_size = 2*12;
    break;

  case 100:
    nb_subbands = 13;
    subband_size = 8*12;
    last_subband_size = 4*12;
    break;
  }

  // signal measurements
  for (eNB_id=0; eNB_id<ue->n_connected_eNB; eNB_id++) {
    for (aarx=0; aarx<frame_parms->nb_antennas_rx; aarx++) {
      for (aatx=0; aatx<frame_parms->nb_antenna_ports_eNB; aatx++) {
        ue->measurements.rx_spatial_power[eNB_id][aatx][aarx] =
          (signal_energy_nodc((c16_t*)&ue->common_vars.common_vars_rx_data_per_thread[ue->current_thread_id[subframe]].dl_ch_estimates[eNB_id][(aatx<<1) + aarx][0],
                              (N_RB_DL*12)));
        //- ue->measurements.n0_power[aarx];

        if (ue->measurements.rx_spatial_power[eNB_id][aatx][aarx]<0)
          ue->measurements.rx_spatial_power[eNB_id][aatx][aarx] = 0; //ue->measurements.n0_power[aarx];

        ue->measurements.rx_spatial_power_dB[eNB_id][aatx][aarx] = dB_fixed(ue->measurements.rx_spatial_power[eNB_id][aatx][aarx]);

        if (aatx==0)
          ue->measurements.rx_power[eNB_id][aarx] = ue->measurements.rx_spatial_power[eNB_id][aatx][aarx];
        else
          ue->measurements.rx_power[eNB_id][aarx] += ue->measurements.rx_spatial_power[eNB_id][aatx][aarx];
      } //aatx

      ue->measurements.rx_power_dB[eNB_id][aarx] = dB_fixed(ue->measurements.rx_power[eNB_id][aarx]);

      if (aarx==0)
        ue->measurements.rx_power_tot[eNB_id] = ue->measurements.rx_power[eNB_id][aarx];
      else
        ue->measurements.rx_power_tot[eNB_id] += ue->measurements.rx_power[eNB_id][aarx];
    } //aarx

    ue->measurements.rx_power_tot_dB[eNB_id] = dB_fixed(ue->measurements.rx_power_tot[eNB_id]);

  } //eNB_id

  eNB_id=0;
  if (ue->transmission_mode[0]==4 || ue->transmission_mode[0]==3){
    if (rank_adaptation == 1)
      rank_tm3_tm4 = rank_estimation_tm3_tm4(&ue->common_vars.common_vars_rx_data_per_thread[subframe&0x1].dl_ch_estimates[eNB_id][0][4],
                                             &ue->common_vars.common_vars_rx_data_per_thread[subframe&0x1].dl_ch_estimates[eNB_id][2][4],
                                             &ue->common_vars.common_vars_rx_data_per_thread[subframe&0x1].dl_ch_estimates[eNB_id][1][4],
                                             &ue->common_vars.common_vars_rx_data_per_thread[subframe&0x1].dl_ch_estimates[eNB_id][3][4],
                                             N_RB_DL);
    else
      rank_tm3_tm4=1;
#ifdef DEBUG_RANK_EST
  printf("rank tm3 or tm4 %d\n", rank_tm3_tm4);
#endif
  }

  if (ue->transmission_mode[eNB_id]!=4 && ue->transmission_mode[eNB_id]!=3)
    ue->measurements.rank[eNB_id] = 0;
  else
    ue->measurements.rank[eNB_id] = rank_tm3_tm4;
  //  printf ("tx mode %d\n", ue->transmission_mode[eNB_id]);
  //  printf ("rank %d\n", ue->PHY_measurements.rank[eNB_id]);

  // filter to remove jitter
  if (ue->init_averaging == 0) {
    for (eNB_id = 0; eNB_id < ue->n_connected_eNB; eNB_id++)
      ue->measurements.rx_power_avg[eNB_id] = (int)
          (((k1*((long long int)(ue->measurements.rx_power_avg[eNB_id]))) +
            (k2*((long long int)(ue->measurements.rx_power_tot[eNB_id]))))>>10);

    //LOG_I(PHY,"Noise Power Computation: k1 %d k2 %d n0 avg %d n0 tot %d\n", k1, k2, ue->measurements.n0_power_avg,
    //    ue->measurements.n0_power_tot);
    ue->measurements.n0_power_avg = (int)
        (((k1*((long long int) (ue->measurements.n0_power_avg))) +
          (k2*((long long int) (ue->measurements.n0_power_tot))))>>10);
  } else {
    for (eNB_id = 0; eNB_id < ue->n_connected_eNB; eNB_id++)
      ue->measurements.rx_power_avg[eNB_id] = ue->measurements.rx_power_tot[eNB_id];

    ue->measurements.n0_power_avg = ue->measurements.n0_power_tot;
    ue->init_averaging = 0;
  }

  for (eNB_id = 0; eNB_id < ue->n_connected_eNB; eNB_id++) {
    ue->measurements.rx_power_avg_dB[eNB_id] = dB_fixed( ue->measurements.rx_power_avg[eNB_id]);
    ue->measurements.wideband_cqi_tot[eNB_id] = dB_fixed2(ue->measurements.rx_power_tot[eNB_id],ue->measurements.n0_power_tot);
    ue->measurements.wideband_cqi_avg[eNB_id] = dB_fixed2(ue->measurements.rx_power_avg[eNB_id],ue->measurements.n0_power_avg);
    ue->measurements.rx_rssi_dBm[eNB_id] = ue->measurements.rx_power_avg_dB[eNB_id] - ue->rx_total_gain_dB;
#ifdef DEBUG_MEAS_UE
      LOG_I(PHY,"[eNB %d] Subframe %d, RSSI %d dBm, RSSI (digital) %d dB, WBandCQI %d dB, rxPwrAvg %d, n0PwrAvg %d\n",
            eNB_id,
            subframe,
            ue->measurements.rx_rssi_dBm[eNB_id],
            ue->measurements.rx_power_avg_dB[eNB_id],
            ue->measurements.wideband_cqi_avg[eNB_id],
            ue->measurements.rx_power_avg[eNB_id],
            ue->measurements.n0_power_tot);
#endif
  }

  ue->measurements.n0_power_avg_dB = dB_fixed( ue->measurements.n0_power_avg);

  for (eNB_id = 0; eNB_id < ue->n_connected_eNB; eNB_id++) {
    if (frame_parms->nb_antenna_ports_eNB!=1) {
      // cqi/pmi information

      for (aarx=0; aarx<frame_parms->nb_antennas_rx; aarx++) {
        dl_ch0    = &ue->common_vars.common_vars_rx_data_per_thread[ue->current_thread_id[subframe]].dl_ch_estimates[eNB_id][aarx][4];
        dl_ch1    = &ue->common_vars.common_vars_rx_data_per_thread[ue->current_thread_id[subframe]].dl_ch_estimates[eNB_id][2+aarx][4];
        ue->measurements.wideband_pmi_re[eNB_id][aarx] = 0;
        ue->measurements.wideband_pmi_im[eNB_id][aarx] = 0;

        for (subband=0; subband<nb_subbands; subband++) {

          // cqi
          if (aarx==0)
            ue->measurements.subband_cqi_tot[eNB_id][subband]=0;

          if ((subband<(nb_subbands-1))||(N_RB_DL==6)) {
            /*for (i=0;i<48;i++)
            msg("subband %d (%d) : %d,%d\n",subband,i,((short *)dl_ch0)[2*i],((short *)dl_ch0)[1+(2*i)]);
            */
            ue->measurements.subband_cqi[eNB_id][aarx][subband] =
              (signal_energy_nodc((c16_t*)dl_ch0,subband_size) + signal_energy_nodc((c16_t*)dl_ch1,subband_size));

            if ( ue->measurements.subband_cqi[eNB_id][aarx][subband] < 0)
              ue->measurements.subband_cqi[eNB_id][aarx][subband]=0;

            /*
            else
            ue->measurements.subband_cqi[eNB_id][aarx][subband]-=ue->measurements.n0_power[aarx];
            */

            ue->measurements.subband_cqi_tot[eNB_id][subband] += ue->measurements.subband_cqi[eNB_id][aarx][subband];
            ue->measurements.subband_cqi_dB[eNB_id][aarx][subband] = dB_fixed2(ue->measurements.subband_cqi[eNB_id][aarx][subband],
                ue->measurements.n0_power[aarx]);
          } else { // this is for the last subband which is smaller in size
            //      for (i=0;i<12;i++)
            //        printf("subband %d (%d) : %d,%d\n",subband,i,((short *)dl_ch0)[2*i],((short *)dl_ch0)[1+(2*i)]);
            ue->measurements.subband_cqi[eNB_id][aarx][subband] = (signal_energy_nodc((c16_t*)dl_ch0,last_subband_size) +
                signal_energy_nodc((c16_t*)dl_ch1,last_subband_size)); // - ue->measurements.n0_power[aarx];
            ue->measurements.subband_cqi_tot[eNB_id][subband] += ue->measurements.subband_cqi[eNB_id][aarx][subband];
            ue->measurements.subband_cqi_dB[eNB_id][aarx][subband] = dB_fixed2(ue->measurements.subband_cqi[eNB_id][aarx][subband],
                ue->measurements.n0_power[aarx]);
          }

          dl_ch1+=subband_size;
          dl_ch0+=subband_size;
          //    msg("subband_cqi[%d][%d][%d] => %d (%d dB)\n",eNB_id,aarx,subband,ue->measurements.subband_cqi[eNB_id][aarx][subband],ue->measurements.subband_cqi_dB[eNB_id][aarx][subband]);
        }

      }

      for (subband=0; subband<nb_subbands; subband++) {
        ue->measurements.subband_cqi_tot_dB[eNB_id][subband] = dB_fixed2(ue->measurements.subband_cqi_tot[eNB_id][subband],ue->measurements.n0_power_tot);
        //    msg("subband_cqi_tot[%d][%d] => %d dB (n0 %d)\n",eNB_id,subband,ue->measurements.subband_cqi_tot_dB[eNB_id][subband],ue->measurements.n0_power_tot);
      }

      for (aarx=0; aarx<frame_parms->nb_antennas_rx; aarx++) {
        //printf("aarx=%d", aarx);
        // skip the first 4 RE due to interpolation filter length of 5 (not possible to skip 5 due to 128i alignment, must be multiple of 128bit)

       simde__m128i pmi128_re,pmi128_im,mmtmpPMI0,mmtmpPMI1 /* ,mmtmpPMI2,mmtmpPMI3 */ ;

        dl_ch0_128    = (simde__m128i *)&ue->common_vars.common_vars_rx_data_per_thread[ue->current_thread_id[subframe]].dl_ch_estimates[eNB_id][aarx][4];
        dl_ch1_128    = (simde__m128i *)&ue->common_vars.common_vars_rx_data_per_thread[ue->current_thread_id[subframe]].dl_ch_estimates[eNB_id][2+aarx][4];
        for (subband=0; subband<nb_subbands; subband++) {


          // pmi

          pmi128_re = simde_mm_xor_si128(pmi128_re,pmi128_re);
          pmi128_im = simde_mm_xor_si128(pmi128_im,pmi128_im);
          // limit is the number of groups of 4 REs in a subband (12 = 4 RBs, 3 = 1 RB)
          // for 5 MHz channelization, there are 7 subbands, 6 of size 4 RBs and 1 of size 1 RB
          if ((N_RB_DL==6) || (subband<(nb_subbands-1)))
            limit = subband_size>>2;
          else
            limit = last_subband_size>>2;

          for (i=0; i<limit; i++) {

              mmtmpPMI0 = simde_mm_xor_si128(mmtmpPMI0,mmtmpPMI0);
              mmtmpPMI1 = simde_mm_xor_si128(mmtmpPMI1,mmtmpPMI1);

            // For each RE in subband perform ch0 * conj(ch1)
            // multiply by conjugated channel
                //  print_ints("ch0",&dl_ch0_128[0]);
                //  print_ints("ch1",&dl_ch1_128[0]);
            mmtmpPMI0 = simde_mm_madd_epi16(dl_ch0_128[0],dl_ch1_128[0]);
                 //  print_ints("re",&mmtmpPMI0);
            mmtmpPMI1 = simde_mm_madd_epi16(oai_mm_swap(oai_mm_conj(dl_ch1_128[0])),dl_ch0_128[0]);
               //   print_ints("mm_madd_epi16",&mmtmpPMI1);
            // mmtmpPMI1 contains imag part of 4 consecutive outputs (32-bit)
            pmi128_re = simde_mm_add_epi32(pmi128_re,mmtmpPMI0);
             //   print_ints(" pmi128_re 0",&pmi128_re);
            pmi128_im = simde_mm_add_epi32(pmi128_im,mmtmpPMI1);
               //   print_ints(" pmi128_im 0 ",&pmi128_im);

            /*  mmtmpPMI0 = simde_mm_xor_si128(mmtmpPMI0,mmtmpPMI0);
              mmtmpPMI1 = simde_mm_xor_si128(mmtmpPMI1,mmtmpPMI1);

              mmtmpPMI0 = simde_mm_madd_epi16(dl_ch0_128[1],dl_ch1_128[1]);
                   //  print_ints("re",&mmtmpPMI0);
              mmtmpPMI1 = simde_mm_shufflelo_epi16(dl_ch1_128[1], SIMDE_MM_SHUFFLE(2,3,0,1));
                //  print_ints("_mm_shufflelo_epi16",&mmtmpPMI1);
              mmtmpPMI1 = simde_mm_shufflehi_epi16(mmtmpPMI1, SIMDE_MM_SHUFFLE(2,3,0,1));
                  //  print_ints("_mm_shufflehi_epi16",&mmtmpPMI1);
              mmtmpPMI1 = simde_mm_sign_epi16(mmtmpPMI1,*(simde__m128i*)&conjugate);
                 //  print_ints("_mm_sign_epi16",&mmtmpPMI1);
              mmtmpPMI1 = simde_mm_madd_epi16(mmtmpPMI1,dl_ch0_128[1]);
                 //   print_ints("mm_madd_epi16",&mmtmpPMI1);
              // mmtmpPMI1 contains imag part of 4 consecutive outputs (32-bit)
              pmi128_re = simde_mm_add_epi32(pmi128_re,mmtmpPMI0);
                  //  print_ints(" pmi128_re 1",&pmi128_re);
              pmi128_im = simde_mm_add_epi32(pmi128_im,mmtmpPMI1);
              //print_ints(" pmi128_im 1 ",&pmi128_im);*/

            dl_ch0_128++;
            dl_ch1_128++;
          }

          ue->measurements.subband_pmi_re[eNB_id][subband][aarx] = (((int *)&pmi128_re)[0] + ((int *)&pmi128_re)[1] + ((int *)&pmi128_re)[2] + ((int *)&pmi128_re)[3])>>2;
          ue->measurements.subband_pmi_im[eNB_id][subband][aarx] = (((int *)&pmi128_im)[0] + ((int *)&pmi128_im)[1] + ((int *)&pmi128_im)[2] + ((int *)&pmi128_im)[3])>>2;
          ue->measurements.wideband_pmi_re[eNB_id][aarx] += ue->measurements.subband_pmi_re[eNB_id][subband][aarx];
          ue->measurements.wideband_pmi_im[eNB_id][aarx] += ue->measurements.subband_pmi_im[eNB_id][subband][aarx];
        } // subband loop
      } // rx antenna loop
    }  // if frame_parms->mode1_flag == 0
    else {
      // cqi information only for mode 1
      for (aarx=0; aarx<frame_parms->nb_antennas_rx; aarx++) {
        dl_ch0    = &ue->common_vars.common_vars_rx_data_per_thread[ue->current_thread_id[subframe]].dl_ch_estimates[eNB_id][aarx][4];

        for (subband=0; subband<7; subband++) {

          // cqi
          if (aarx==0)
            ue->measurements.subband_cqi_tot[eNB_id][subband]=0;

          if (subband<6) {
            //      for (i=0;i<48;i++)
            //        printf("subband %d (%d) : %d,%d\n",subband,i,((short *)dl_ch0)[2*i],((short *)dl_ch0)[1+(2*i)]);
            ue->measurements.subband_cqi[eNB_id][aarx][subband] =
              (signal_energy_nodc((c16_t*)dl_ch0,48) ) - ue->measurements.n0_power[aarx];

            ue->measurements.subband_cqi_tot[eNB_id][subband] += ue->measurements.subband_cqi[eNB_id][aarx][subband];
            ue->measurements.subband_cqi_dB[eNB_id][aarx][subband] = dB_fixed2(ue->measurements.subband_cqi[eNB_id][aarx][subband],
                ue->measurements.n0_power[aarx]);
          } else {
            //      for (i=0;i<12;i++)
            //        printf("subband %d (%d) : %d,%d\n",subband,i,((short *)dl_ch0)[2*i],((short *)dl_ch0)[1+(2*i)]);
            ue->measurements.subband_cqi[eNB_id][aarx][subband] = (signal_energy_nodc((c16_t*)dl_ch0,12) ) - ue->measurements.n0_power[aarx];
            ue->measurements.subband_cqi_tot[eNB_id][subband] += ue->measurements.subband_cqi[eNB_id][aarx][subband];
            ue->measurements.subband_cqi_dB[eNB_id][aarx][subband] = dB_fixed2(ue->measurements.subband_cqi[eNB_id][aarx][subband],
                ue->measurements.n0_power[aarx]);
          }

          dl_ch1+=48;
          //    msg("subband_cqi[%d][%d][%d] => %d (%d dB)\n",eNB_id,aarx,subband,ue->measurements.subband_cqi[eNB_id][aarx][subband],ue->measurements.subband_cqi_dB[eNB_id][aarx][subband]);
        }
      }

      for (subband=0; subband<nb_subbands; subband++) {
        ue->measurements.subband_cqi_tot_dB[eNB_id][subband] = dB_fixed2(ue->measurements.subband_cqi_tot[eNB_id][subband],ue->measurements.n0_power_tot);
      }
    }

    //ue->measurements.rank[eNB_id] = 0;

    for (i=0; i<nb_subbands; i++) {
      ue->measurements.selected_rx_antennas[eNB_id][i] = 0;

      if (frame_parms->nb_antennas_rx>1) {
        if (ue->measurements.subband_cqi_dB[eNB_id][0][i] >= ue->measurements.subband_cqi_dB[eNB_id][1][i])
          ue->measurements.selected_rx_antennas[eNB_id][i] = 0;
        else
          ue->measurements.selected_rx_antennas[eNB_id][i] = 1;
      } else
        ue->measurements.selected_rx_antennas[eNB_id][i] = 0;
    }

    // if(eNB_id==0)
    // printf("in lte_ue_measurements: selected rx_antenna[eNB_id==0]:%u\n", ue->measurements.selected_rx_antennas[eNB_id][i]);
  }  // eNB_id loop

}


void lte_ue_measurements_emul(PHY_VARS_UE *ue,uint8_t subframe,uint8_t eNB_id)
{

  LOG_D(PHY,"EMUL UE lte_ue_measurements_emul subframe %d, eNB_id %d\n",subframe,eNB_id);
}



