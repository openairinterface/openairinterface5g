/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
*/
#include <ctype.h>

#include "common/platform_constants.h"
#include <openair3/UICC/usim_interface.h>
#include <openair3/NAS/COMMON/milenage.h>
extern uint16_t NB_UE_INST;

#define IMEISV_STR_MAX_LENGTH 16
#define UICC_CONFIG_HELP_OPTIONS     " list of comma separated options to interface a simulated (real UICC to be developped). Available options: \n"\
  "        imsi: user imsi\n"\
  "        key:  cyphering key\n"\
  "        opc:  cyphering OPc\n"\
  "        imiesv: string with IMEISV value\n"
#define DEFAULT_HOME_NETWORK_PUBLIC_KEY "5a8d38864820197c3394b92613b20b91633cbd897119273bf8e4a6f4eec0a650"
/*-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/
/*                                            configuration parameters for the rfsimulator device                                                                              */
/*   optname                     helpstr                     paramflags           XXXptr                               defXXXval                          type         numelt  */
/*-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/
#define UICC_PARAMS_DESC {                                                                                                                                                         \
      {"imsi",                       "USIM IMSI\n",                  0, .strptr=&uicc->imsiStr,                    .defstrval="2089900007487",                    TYPE_STRING, 0}, \
      {"nmc_size",                   "number of digits in NMC",      0, .iptr=&uicc->nmc_size,                     .defintval=2,                                  TYPE_INT,    0}, \
      {"key",                        "USIM Ki\n",                    0, .strptr=&uicc->keyStr,                     .defstrval="fec86ba6eb707ed08905757b1bb44b8f", TYPE_STRING, 0}, \
      {"opc",                        "USIM OPc\n",                   0, .strptr=&uicc->opcStr,                     .defstrval="c42449363bbad02b66d16bc975d77cc1", TYPE_STRING, 0}, \
      {"amf",                        "USIM amf\n",                   0, .strptr=&uicc->amfStr,                     .defstrval="8000",                             TYPE_STRING, 0}, \
      {"sqn",                        "USIM sqn\n",                   0, .strptr=&uicc->sqnStr,                     .defstrval="000000",                           TYPE_STRING, 0}, \
      {"imeisv",                     "IMEISV\n",                     0, .strptr=&uicc->imeisvStr,                  .defstrval="6754567890123413",                 TYPE_STRING, 0}, \
      {"routing_indicator", "Routing Indicator\n",                   0, .strptr = &uicc->routing_indicatorStr,     .defstrval="0000",                             TYPE_STRING, 0}, \
      {"protection_scheme", "SUCI Profile Scheme\n",                 0, .iptr = &uicc->protection_scheme,          .defintval=0,                                  TYPE_INT,    0}, \
      {"home_network_public_key_id", "Home Network Public Key ID\n", 0, .iptr=&uicc->home_network_public_key_id,   .defintval=1,                                  TYPE_INT,    0}, \
      {"home_network_public_key",    "Home Network Public Key\n",    0, .strptr=&uicc->home_network_public_keyStr, .defstrval=DEFAULT_HOME_NETWORK_PUBLIC_KEY,    TYPE_STRING, 0}, \
  };

static uicc_t** uiccArray=NULL;

const char *const hexTable = "0123456789abcdef";
static inline uint8_t mkDigit(unsigned char in) {
  for (int i=0; i<16; i++)
    if (tolower(in)==hexTable[i])
      return i;
  LOG_E(SIM,"Impossible hexa input: %c\n",in);
  return 0;
}

static inline void to_hex(char *in, uint8_t *out, int size) {
  for (size_t i=0; in[i]!=0 && i < size*2 ; i+=2) {
    *out++=(mkDigit(in[i]) << 4) | mkDigit(in[i+1]);
  }
}

uicc_t *init_uicc(char *sectionName) {
  uicc_t *uicc=(uicc_t *)calloc(sizeof(uicc_t),1);
  paramdef_t uicc_params[] = UICC_PARAMS_DESC;
  // here we call usim simulation, but calling actual usim is quite simple
  // the code is in open-cells.com => program_uicc open source
  // we can read the IMSI from the USIM
  // key, OPc, sqn, amf don't need to be read from the true USIM
  int ret = config_get(config_get_if(), uicc_params, sizeofArray(uicc_params), sectionName);
  AssertFatal(ret >= 0, "configuration couldn't be performed for uicc name: %s", sectionName);
  LOG_I(SIM, "UICC simulation: IMSI=%s, IMEISV=%s, Ki=%s, OPc=%s\n", uicc->imsiStr, uicc->imeisvStr, uicc->keyStr, uicc->opcStr);
  to_hex(uicc->keyStr,uicc->key, sizeof(uicc->key) );
  to_hex(uicc->opcStr,uicc->opc, sizeof(uicc->opc) );
  to_hex(uicc->sqnStr,uicc->sqn, sizeof(uicc->sqn) );
  to_hex(uicc->amfStr,uicc->amf, sizeof(uicc->amf) );
  to_hex(uicc->home_network_public_keyStr, uicc->home_network_public_key, sizeof(uicc->home_network_public_key));
  return uicc;
}

void uicc_milenage_generate(uint8_t *autn, uicc_t *uicc) {
  // here we call usim simulation, but calling actual usim is quite simple
  // the code is in open-cells.com => program_uicc open source
  milenage_generate(uicc->opc, uicc->amf, uicc->key,
                    uicc->sqn, uicc->rand, autn, uicc->ik, uicc->ck, uicc->milenage_res);
  log_dump(SIM,uicc->opc,sizeof(uicc->opc), LOG_DUMP_CHAR,"opc:");
  log_dump(SIM,uicc->amf,sizeof(uicc->amf), LOG_DUMP_CHAR,"amf:");
  log_dump(SIM,uicc->key,sizeof(uicc->key), LOG_DUMP_CHAR,"key:");
  log_dump(SIM,uicc->sqn,sizeof(uicc->sqn), LOG_DUMP_CHAR,"sqn:");
  log_dump(SIM,uicc->rand,sizeof(uicc->rand), LOG_DUMP_CHAR,"rand:");
  log_dump(SIM,uicc->ik,sizeof(uicc->ik), LOG_DUMP_CHAR,"milenage output ik:");
  log_dump(SIM,uicc->ck,sizeof(uicc->ck), LOG_DUMP_CHAR,"milenage output ck:");
  log_dump(SIM,uicc->milenage_res,sizeof(uicc->milenage_res), LOG_DUMP_CHAR,"milenage output res:");
  log_dump(SIM,autn,sizeof(autn), LOG_DUMP_CHAR,"milenage output autn:");
}

uicc_t * checkUicc(int Mod_id) {
  AssertFatal(Mod_id < NB_UE_INST, "Mod_id must be less than NB_UE_INST. Mod_id:%d NB_UE_INST:%d", Mod_id,NB_UE_INST);
  if(uiccArray==NULL){
    uiccArray=(uicc_t **)calloc(1,sizeof(uicc_t*)*NB_UE_INST);
  }
  if (!uiccArray[Mod_id]) {
    char uiccName[64];
    sprintf(uiccName,"uicc%d",  Mod_id);
    uiccArray[Mod_id]=(void*)init_uicc(uiccName);
    uicc_t *uicc = uiccArray[Mod_id];
    uicc->n_pdu_sessions = get_pdu_session_configs(uiccName, uicc->pdu_sessions, sizeofArray(uicc->pdu_sessions));
    if (uicc->n_pdu_sessions < 0)
      LOG_W(SIM,
            "could not load PDU session parameters. Make sure to define %s.pdu_sessions option in config file\n"
            "for further info, see doc/runmodem-nrue.md\n",
            uiccName);
    for (int i = 0; i < uicc->n_pdu_sessions; ++i) {
      const pdu_session_config_t *pdu = &uicc->pdu_sessions[i];
      LOG_I(SIM,
            "will request PDU session ID %d, type %d, DNN %s, NSSAI %d.%6x\n",
            pdu->id,
            pdu->type,
            pdu->dnn,
            pdu->nssai.sst,
            pdu->nssai.sd);
    }
  }
  return (uicc_t*) uiccArray[Mod_id];  
}

uint8_t getImeisvDigit(const uicc_t *uicc, uint8_t i)
{
  uint8_t r = 0;
  uint8_t l = strlen(uicc->imeisvStr);
  if (l > IMEISV_STR_MAX_LENGTH) {
    l=IMEISV_STR_MAX_LENGTH;
  }
  if((uicc->imeisvStr!=NULL) && (i<l))
  {
    char c = uicc->imeisvStr[i];
    if (isdigit(c))
    {
      r=c-'0';
    }
  }
  return (0x0f & r);
}
