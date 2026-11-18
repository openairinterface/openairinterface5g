<!-- SPDX-License-Identifier: CC-BY-4.0 -->

This is a simple standalone tester for the Xn Application Protocol (XNAP)
between gNBs. It runs several gNB instances in a single process, each with its
own configuration file, and validates the Xn Setup and Xn handover procedures
over real SCTP associations and real ASN.1 encoding/decoding, with a UE stub,
without overhead of any F1, or a full Radio Access Network stack (PHY/MAC/RLC).

[[_TOC_]]

# Motivation
- This tester is a lightweight, deterministic tool to **test the XNAP layer
  directly**.
- Developers can validate Xn Setup Request/Response/Failure handling and Xn
  handovers (with the NGAP Path Switch) between two or more gNBs without full
  Radio Access Network (PHY/MAC/RLC), real radios or a real UE.
- Multiple gNB instances (each a fully independent config file) run inside one
  process, so an N-way Xn mesh can be exercised with a single binary invocation.

# Overview
From a schematic point of view, the tester's instances and their Xn
interconnections look like this (for 3-instance setup):
```
        gNB0 (0xe00, 127.0.0.10:38422)
          |        \
          Xn         Xn
          |            \
        gNB1 ---- Xn ---- gNB2
    (0xe01, 127.0.0.20)  (0xe02, 127.0.0.30)

        +---------------------------+
        | xn-simulator-test process |
        |  gNB0 | gNB1 | gNB2       |
        +---------------------------+
```

Each instance is a real gNB config (one `-O <file>` per instance) that lists the
other instances as `candidate_gnb_ipv4_address_for_xnc`, so every instance opens
an SCTP association and attempts Xn Setup toward every other one over loopback.

Inside the process, each instance is driven through the same ITTI tasks used by
the real gNB, with the real SCTP, NGAP and XnAP tasks; only GNB_APP and the gNB
RRC are replaced by test stubs (and the UE RRC in handover mode):
```
main()  --NGAP_REGISTER_GNB_REQ-->  TASK_NGAP
TASK_NGAP  --NGAP_REGISTER_GNB_CNF-->  TASK_GNB_APP (stub)
TASK_GNB_APP  --XNAP_REGISTER_GNB_REQ-->  TASK_XNAP
    (create Xn instance, start listener, dial candidates)
TASK_XNAP <==SCTP/ASN.1==> peer instance's TASK_XNAP
    (Xn Setup Req/Resp/Failure)
TASK_XNAP  --XNAP_SETUP_IND / XNAP_PEER_SHUTDOWN_IND-->
    TASK_RRC_GNB (stub)
```
`TASK_XNAP` is the real, unmodified XNAP stack (`openair2/XNAP`). `TASK_GNB_APP`
is a test-only stub defined in `xn-simulator.c` that forwards the NGAP
confirmation into the real XNAP registration. `TASK_RRC_GNB` is the stub of
`xn-ho-sim.c` in the Xn handover test (see below), and with `--xn-setup-only`
the stub of `xn-simulator.c`, which counts `XNAP_SETUP_IND` across all instance
pairs and exits the process once every pair has completed Xn Setup.

The following messages/procedures are integrated and tested:

* **XNAP:** Xn Setup Request/Response/Failure, over real SCTP associations
  between instances.
* **SCTP:** `SCTP_NEW_ASSOCIATION_IND/RESP`, `SCTP_CLOSE_ASSOCIATION`/shutdown
  notifications.
* **XNAP (handover test):** Handover Request/Acknowledge, SN Status Transfer, UE
  Context Release.
* **NGAP (handover test):** Initial UE Message, Uplink/Downlink NAS Transport,
  Initial Context Setup, PDU Session Resource Setup, Path Switch
  Request/Acknowledge, UE Context Release, UE context local release, with NAS
  registration and deregistration of the UE stub.

With `--xn-setup-only`, a test scenario is fixed to these steps:
1. The 5G Core Network is deployed via Docker.
2. The tester loads N independent config files (`-O file1 -O file2 ...`), one
   gNB instance per file.
3. The tester connects each gNB instance to the AMF and establishes the NGAP
   connection (NG Setup).
4. For each gNB instance, NG setup confirmation triggers Xn.
5. Each instance starts SCTP listener and opens an SCTP association to each of
   its configured candidate gNBs and performs Xn Setup Request/Response.
6. The tester waits until every ordered pair of instances has completed Xn
   Setup, then exits successfully.

## Xn handover (default)

By default, the tester also runs one UE stub and hands it over between the gNB
instances with the Xn handover procedures. As in the [NR UE NAS
simulator](../../../../tests/nr-ue-nas-simulator/README.md), the UE stub is the
real NR UE NAS (`nas_nrue_task`) without RRC or radio: its NAS messages go
directly to the AMF over the NGAP of the serving gNB instance. `TASK_RRC_GNB` is
then the stub gNB RRC of `xn-ho-sim.c`, which uses the real XnAP and NGAP
stacks:
1. The tester loads N (2 or more) independent config files, one gNB instance per
   file.
2. gNB0 (first `-O` file) completes NG Setup with the AMF.
3. The UE stub registers through gNB0 and establishes a PDU session.
4. gNB1..gNB(N-1) complete NG Setup, then Xn Setup between all instances.
5. The UE is handed over round-robin gNB0 -> gNB1 -> ... -> gNB(N-1) -> gNB0 ->
   ..., with max(5, N) Xn handovers, e.g. gNB0 -> gNB1 -> gNB0 -> gNB1 -> gNB0
   -> gNB1 with 2 gNBs, gNB0 -> gNB1 -> gNB2 -> gNB0 -> gNB1 -> gNB2 with 3
   gNBs:
```
source gNB                                  target gNB                    AMF
   |--- Handover Request (Xn) ----------------->|                           |
   |<-- Handover Request Acknowledge (Xn) ------|                           |
   |    (handover command to the UE)            |                           |
   |--- SN Status Transfer (Xn) --------------->|  (UE arrives)             |
   |                                            |--- Path Switch Request -->|
   |                                            |<-- Path Switch Req Ack ---|
   |<-- UE Context Release (Xn) ----------------|                           |
   |    (UE released locally, NGAP UE context removed without AMF signalling)
```
6. The UE stub deregisters, and the tester exits with `EXIT_SUCCESS`; a Handover
   Preparation Failure or Cancel, an Xn peer shutdown, a UE release by the AMF
   before the last handover, or no completion within 120 s exits with
   `EXIT_FAILURE`.

The RRC containers of the XnAP messages (HandoverPreparationInformation,
HandoverCommand, UE history) are dummy bytes, there is no user plane (dummy N3
tunnels), and the SN Status Transfer carries dummy PDCP COUNTs. As the XnAP and
NGAP UE tables are per process, the RRC UE ID of the UE stub (also its RAN UE
NGAP ID) is unique across the gNB instances: it changes at every handover.

The identity of the UE stub (`uicc0` section) is taken from
`tests/nr-ue-nas-simulator/test.conf` (IMSI `001010000000001`), which has to be
provisioned in the core; another file can be given with `--ue-conf <file>`.

# Usage
To run this test, you need to setup the 5G Core Network, build the tester and
configuration file for each simulated gNB instance.

**1. Core Network Deployment**

Pull the required Docker images for the OAI 5G Core:

    docker pull oaisoftwarealliance/ims:latest
    docker pull oaisoftwarealliance/oai-amf:v2.2.2
    docker pull oaisoftwarealliance/oai-nrf:v2.2.2
    docker pull oaisoftwarealliance/oai-smf:v2.2.2
    docker pull oaisoftwarealliance/oai-udr:v2.2.2
    docker pull oaisoftwarealliance/oai-upf:v2.2.2
    docker pull oaisoftwarealliance/oai-udm:v2.2.2
    docker pull oaisoftwarealliance/oai-ausf:v2.2.2

Deploy the network using the docker compose file:

    cd doc/tutorial_resources/oai-cn5g
    docker compose -f docker-compose.yaml up -d

At the end of all experiments, you can also undeploy the network with:
    docker compose -f docker-compose.yaml down -t 0

**2. Building the Tester**

You can build the tester as follows:

    cd ~/openairinterface5g
    mkdir build && cd build && cmake .. -GNinja && ninja xn-simulator-test

**3. Running the Tester**

Start the tester with one `-O <config file>` per simulated gNB instance. The
repository ships a ready-made 3-instance loopback mesh alongside this README:

```bash
    cd openairinterface5g/build
    X=../openair2/XNAP/tests/xn-simulator
    ./openair2/XNAP/tests/xn-simulator/xn-simulator-test \
      -O $X/gnb.sa.band78.fr1.106PRB.pci0.xn.rfsim.conf \
      -O $X/gnb.sa.band78.fr1.106PRB.pci1.xn.rfsim.conf \
      -O $X/gnb.sa.band78.fr1.106PRB.pci2.xn.rfsim.conf
```

This runs the Xn handover test (at least 2 `-O` files): look for
`Xn Setup completed between all N gNBs`,
`=== Xn handover M/M done: UE stub ... served by gNB...` and
`Xn handover test completed successfully: M/M handovers` (M = max(5, N)) in the
logs.

Other options are passed to the configuration module with the first `-O` file,
e.g. `--log_config.xnap_log_level debug` to also log the XnAP UE ID table
changes. The Xn handover steps are logged with `Xn HO source:` or
`Xn HO target:`, and the XnAP UE IDs of both sides (`s_xnap_ue_id`,
`t_xnap_ue_id`).

Add `--xn-setup-only` to only run Xn Setup (also at least 2 `-O` files): look
for `Xn setup flow completed successfully for all N gNB instance(s)!` in the
logs; the process exits with `EXIT_SUCCESS` once every configured pair has
completed Xn Setup.

# Limitations
- The Xn handover test uses a UE stub (NAS only): the RRC containers are dummy,
  and there is no user plane.
- At most 8 `-O` files; extra ones are ignored with a warning.
- The gNB RRC is a stub: the real RRC Xn handover code (`rrc_gNB_XNAP.c`,
  `rrc_gNB_mobility.c`) is not exercised.
- All instances share one process and one `xnap_sim` build (compiled with a
  raised `NUMBER_OF_gNB_MAX`); it is not representative of separate gNB
  processes/hosts.
