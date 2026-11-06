<!-- SPDX-License-Identifier: CC-BY-4.0 -->

This is a simple standalone tester for 5G UE NAS signaling and NGAP messages. It connects to the AMF to perform NGAP and NAS encoding/decoding and validates the UE Registration and PDU Session Establishment processes without the overhead of a full Radio Access Network (PHY/MAC/RLC).

[[_TOC_]]

# Motivation
- This tester is a lightweight, deterministic tool to **test the UE NAS layer directly**. 
- Developers can easily inject specific NAS messages depending on the use-case.
- Developers can validate custom NAS/NGAP messages without the need to implement complete RRC/MAC etc procedures.

# Overview
From a schematic point of view, the tester and Core Network interaction looks like this:
```
        +-------+
        |  AMF  |
        +-------+
            |
 NGAP/NAS   | (N1/N2)
            |
        +-------+
        |tester | (nr-ue-nas-simulator-test)
        +-------+

```

The tester is designed to validate the integration of NAS and NGAP messages required for a 5G Standalone (SA) UE to attach to the network. It acts as a simulated gNB and a UE, bridging NAS messages directly into NGAP payloads and connecting to the 5G Core Network's AMF.

The following messages are integrated and tested:

* **NGAP:** NG Setup Request/Response, Initial UE Message, Downlink NAS Transport, Uplink NAS Transport, Initial Context Setup Request/Response, PDU Session Resource Setup Request/Response, UE Context Release Command/Complete.

* **NAS (5GMM/5GSM):** Registration Request/Accept/Complete, Authentication Request/Response, Security Mode Command/Complete, PDU Session Establishment Request/Accept, Deregistration Request/Accept.

* **RRC (Custom):** Forwards the uplink/downlink NAS messages directly between UE and AMF.

A test scenario is fixed to these steps:
1. The 5G Core Network is deployed via Docker.
2. The tester connects to the AMF and establishes the NGAP connection (NG Setup).
3. The tester automatically triggers an Initial UE Registration Request.
4. Authentication and NAS Security Mode procedures are completed.
5. The tester requests and successfully establishes an IPv4 PDU Session.
6. The tester finally initiates deregistration request and exits succesfully.

# Usage
To run this test, you need to set up the Core Network and build the tester.

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
    mkdir build && cd build && cmake .. -GNinja && ninja nr-ue-nas-simulator-test

**3. Running the Tester**

Start the tester with the dedicated configuration file:

    openairinterface5g/build$ ./tests/nr-ue-nas-simulator/nr-ue-nas-simulator-test -O ../tests/nr-ue-nas-simulator/test.conf

# Testing NAS procedures

## Registration with unknown 5G-GUTI

Optional `--identity-guti` or `identity-guti = 1` in the config file:
Registration Request is sent with an unknown 5G-GUTI so the AMF sends Identity
Request (SUCI) per TS 23.502 step 6, the simulator then continues the normal
attach flow. From `cmake_targets/ran_build/build`:

```bash
    ./tests/nr-ue-nas-simulator/nr-ue-nas-simulator-test \
      -O ../tests/nr-ue-nas-simulator/test.conf --identity-guti
```

Look for `Unknown-GUTI test:` then `IDENTITY REQUEST` / `IDENTITY RESPONSE` in NAS logs.

# Limitations
- The tester is limited to a fixed flow: Initial Attach -> PDU Session -> Deregistration.
- The tester doesnt create a GTP tunnel after PDU session and uses a harcoded teid.

# Future Enhancements
- A real GTP tunnel can be created to test various data related functionalities like QoS.
- Simulate the actual UE RRC layer, i.e., forwarding the CellGroupConfig, have the UE apply that (without actually using it).
- Make it scriptable to test custom message implementations.
