<!-- SPDX-License-Identifier: CC-BY-4.0 -->

This is a simple standalone tester for the Xn Application Protocol (XNAP) between gNBs. It runs several gNB instances in a single process, each with its own configuration file, and validates the Xn Setup procedure over real SCTP associations and real ASN.1 encoding/decoding, without overhead of any F1, or a full Radio Access Network stack (PHY/MAC/RLC).

[[_TOC_]]

# Motivation
- This tester is a lightweight, deterministic tool to **test the XNAP layer directly**.
- Developers can validate Xn Setup Request/Response/Failure handling between two or more gNBs without full Radio Access Network (PHY/MAC/RLC) or real radios.
- Multiple gNB instances (each a fully independent config file) run inside one process, so an N-way Xn mesh can be exercised with a single binary invocation.

# Overview
From a schematic point of view, the tester's instances and their Xn interconnections look like this (for 3-instance setup):
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

Each instance is a real gNB config (one `-O <file>` per instance) that lists the other instances as `candidate_gnb_ipv4_address_for_xnc`, so every instance opens an SCTP association and attempts Xn Setup toward every other one over loopback.

Inside the process, each instance is driven through the same ITTI tasks used by the real gNB, but the NGAP registration and RRC/F1 layers are replaced by minimal test stubs so only the XNAP stack is under test:
```
main()  --NGAP_REGISTER_GNB_REQ-->  TASK_NGAP
TASK_NGAP  --NGAP_REGISTER_GNB_CNF-->  TASK_GNB_APP (stub)
TASK_GNB_APP  --XNAP_REGISTER_GNB_REQ-->      TASK_XNAP  (create Xn instance, start listener, dial candidates)
TASK_XNAP <==SCTP/ASN.1==> peer instance's TASK_XNAP     (Xn Setup Req/Resp/Failure)
TASK_XNAP  --XNAP_SETUP_IND / XNAP_PEER_SHUTDOWN_IND-->  TASK_RRC_GNB (stub)
```
`TASK_XNAP` is the real, unmodified XNAP stack (`openair2/XNAP`). `TASK_GNB_APP` and `TASK_RRC_GNB` are test-only stubs defined in `xn-simulator.c`: the former just forwards the NGAP confirmation into the real XNAP registration and the latter counts `XNAP_SETUP_IND` across all instance pairs and exits the process once every pair has completed Xn Setup (or reports `XNAP_PEER_SHUTDOWN_IND` on SCTP teardown).

The following messages/procedures are integrated and tested:

* **XNAP:** Xn Setup Request/Response/Failure, over real SCTP associations between instances.
* **SCTP:** `SCTP_NEW_ASSOCIATION_IND/RESP`, `SCTP_CLOSE_ASSOCIATION`/shutdown notifications.
* **RRC (stub):** Adds/removes the Xn neighbour RB tree entries on Xn setup success / SCTP shutdown.

A test scenario is fixed to these steps:
1. The 5G Core Network is deployed via Docker.
2. The tester loads N independent config files (`-O file1 -O file2 ...`), one gNB instance per file.
3. The tester connects each gNB instance to the AMF and establishes the NGAP connection (NG Setup).
4. For each gNB instance, NG setup confirmation triggers Xn.
5. Each instance starts SCTP listener and opens an SCTP association to each of its configured candidate gNBs and performs Xn Setup Request/Response.
6. The tester waits until every ordered pair of instances has completed Xn Setup, then exits successfully.

# Usage
To run this test, you need to setup the 5G Core Network, build the tester and configuration file for each simulated gNB instance.

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

Start the tester with one `-O <config file>` per simulated gNB instance. The repository ships a ready-made 3-instance loopback mesh alongside this README:

```bash
    openairinterface5g/build$ ./openair2/XNAP/tests/xn-simulator/xn-simulator-test \
      -O ../openair2/XNAP/tests/xn-simulator/gnb.sa.band78.fr1.106PRB.pci0.xn.rfsim.conf \
      -O ../openair2/XNAP/tests/xn-simulator/gnb.sa.band78.fr1.106PRB.pci1.xn.rfsim.conf \
      -O ../openair2/XNAP/tests/xn-simulator/gnb.sa.band78.fr1.106PRB.pci2.xn.rfsim.conf
```

A single `-O <file>` also works (no peers configured in that file will simply have no one to set up Xn with).

Look for `Xn setup flow completed successfully for all N gNB instance(s)!` in the logs; the process exits with `EXIT_SUCCESS` once every configured pair has completed Xn Setup.

# Limitations
- The tester only exercises Xn Setup; it does not cover Handover-related Xn procedures (e.g. Handover Request/Ack, SN Status Transfer, UE Context Release).
- All instances share one process and one `xnap_sim` build (compiled with a raised `NUMBER_OF_gNB_MAX`); it is not representative of separate gNB processes/hosts.

# Future Enhancements
- Extend coverage to Xn-based Handover procedures.
