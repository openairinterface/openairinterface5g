// SPDX-License-Identifier: LicenseRef-CSSL-1.0
// Open5GS subscriber DB init (mongo /docker-entrypoint-initdb.d) for the rfsim NR-UE (nrue.uicc.conf)
db = db.getSiblingDB("open5gs");
db.subscribers.insertOne({
  imsi: "208990100001100",
  msisdn: [],
  imeisv: [],
  mme_host: [],
  mme_realm: [],
  purge_flag: [],
  security: {
    k: "fec86ba6eb707ed08905757b1bb44b8f",
    opc: "C42449363BBAD02B66D16BC975D77CC1",
    op: null,
    amf: "8000",
  },
  ambr: { downlink: { value: 1, unit: 3 }, uplink: { value: 1, unit: 3 } },
  slice: [{
    sst: 1,
    default_indicator: true,
    session: [{
      name: "oai",
      type: 1, // IPv4
      qos: { index: 9, arp: { priority_level: 8, pre_emption_capability: 1, pre_emption_vulnerability: 1 } },
      ambr: { downlink: { value: 1, unit: 3 }, uplink: { value: 1, unit: 3 } },
      pcc_rule: [],
    }],
  }],
  access_restriction_data: 32,
  subscriber_status: 0,
  network_access_mode: 0,
  subscribed_rau_tau_timer: 12,
  schema_version: 1,
  __v: 0,
});
