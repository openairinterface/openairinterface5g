<!-- SPDX-License-Identifier: CC-BY-4.0 -->

This is a simple standalone tester for NRPPA and NGAP positioning messages. It connects to the AMF to perform NGAP and NRPPA encoding/decoding and validates the exchange of positioning data.

[[_TOC_]]

# Overview
From a schematic point of view, the tester and Core Network interaction looks like this:
```
        +-------+                 +-------+
        |  LMF  |<--- HTTP POST---| User  |
        +-------+                 +-------+
            |
            | Nlmf
            |
        +-------+
        |  AMF  |
        +-------+
            |
 NGAP/NRPPA |
            |
        +-------+
        |tester | (nr-cu-nrppa-test)
        +-------+
```
The tester is designed to validate the integration of NRPPA and NGAP messages required for 5G positioning (*3GPP 38.413 V16.0.0* and *3GPP 38.455 V16.7.1*). It acts as a simulated RAN node (like a CU) connecting to the 5G Core Network's AMF.

The following messages are integrated and tested:
* **NGAP:** Uplink/Downlink UE Associated NRPPA Transport, Uplink/Downlink Non-UE Associated NRPPA Transport.
* **NRPPA:** TRP Information Request/Response, Positioning Information Request/Response, Positioning Activation Request/Response, Measurement Request/Response.

A test scenario is fixed to these steps:
1. The 5G Core Network, specifically including the Location Management Function (LMF), is deployed via Docker.
2. The tester connects to the AMF and establishes the NGAP connection.
3. The user initiates a positioning procedure via a API call to the LMF.
4. The AMF and the tester exchange the required NGAP and NRPPA messages to determine location.

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
    docker pull oaisoftwarealliance/oai-lmf:v2.2.2

Deploy the network using the positioning compose file:

    cd doc/tutorial_resources/oai-cn5g
    docker compose -f docker-compose-positioning.yaml up -d

*(To undeploy later, run `docker compose -f docker-compose-positioning.yaml down -t 0`)*

**2. Building the Tester**

You can build the NRPPA tester like so:

    cd ~/openairinterface5g
    mkdir build && cd build && cmake .. -GNinja && ninja nr-cu-nrppa-test

**3. Running the Tester**

Start the tester with the dedicated configuration file:

    LD_LIBRARY_PATH=. ./tests/nr-cu-nrppa/nr-cu-nrppa-test -O ../tests/nr-cu-nrppa/nr-nrppa-test.conf

**4. Initiating the Procedure**

Trigger the location determination by sending an HTTP POST request to the LMF:

    cd ~/openairinterface5g/doc/tutorial_resources/positioning/
    curl --http2-prior-knowledge -H "Content-Type: application/json" -d "@InputData.json" -X POST http://192.168.70.141:8080/nlmf-loc/v1/determine-location

# Limitations
- The tester is a standalone validation tool (`nr-cu-nrppa-test`) and does not represent a fully integrated, end-to-end gNB positioning setup.
