<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Duranta openairinterface5g Documentation Overview

This page is the entry point to the Duranta openairinterface5g documentation. It
links to general information, tutorials, design documents, images, libraries,
radios, testing and developer tools.

> [!IMPORTANT]
> Keep your repository rebased on `develop`, or read the documentation of the
> version you are using, so that it matches the features available in your
> repository. If you pulled `develop` some time ago, your repository may now be
> behind it.

More general information is available in the
[OpenAirInterface GitLab wiki](https://gitlab.eurecom.fr/oai/openairinterface5g/-/wikis/home).

[[_TOC_]]

## General

- [Project overview](../README.md): introduction, licensing, repository structure and community support
- [Feature set](./FEATURE_SET.md): supported features
- [Supported hardware and operating systems](./Supported_Hardware_Operating_System.md): tested CPUs, NICs and accelerators
- [Key performance indicators](./kpi.md): KPIs of the RAN and UE stack
- [Getting the sources](./GET_SOURCES.md): how to download the sources
- [Building OAI](./BUILD.md): how to build the sources
- [Packages](./packages.md): build and install Deb/RPM packages of OAI
- [Tuning and security](./tuning_and_security.md): performance and security considerations
- [Security policy](../SECURITY.md): report vulnerabilities and find supported release information
- [Environment variables](./environment-variables.md): environment variables used by OAI
- [Roadmap](../ROADMAP.md): planned development milestones and priorities
- [Changelog](../CHANGELOG.md): notable changes delivered in each release

## Tutorials

### Getting started with 5G

- [OAI 5G core network](./NR_SA_Tutorial_OAI_CN5G.md): deploy the OAI 5G core network with Docker
- [OAI gNB with a COTS UE](./NR_SA_Tutorial_COTS_UE.md): end-to-end 5G SA setup with a commercial UE (Quectel module)
- [OAI NR-UE](./NR_SA_Tutorial_OAI_nrUE.md): end-to-end 5G SA setup with the OAI UE
- [Multiple OAI NR-UEs with the RFsimulator](./NR_SA_Tutorial_OAI_multi_UE.md): run several OAI UEs in one setup,
  with namespaces or Docker

### Running OAI

- [Running the gNB](./RUNMODEM.md): simulators, hardware, SDAP and custom DRBs,
  IF setups and arbitrary frequencies, MIMO
- [Running the UE](./runmodem-nrue.md): configuration and modes (phy-test, do-ra, noS1)
- [gNB MAC usage](./MAC/mac-usage.md): scheduler, periodic output and configuration options
- [gNB RRC usage](./RRC/rrc-usage.md): RRC functions, periodic output and configuration options
- [Setting the gNB frequency](./gNB_frequency_setup.md): configure the gNB carrier frequency and bandwidth (ARFCN)
- [5G NSA setup](./TESTING_OAI_NSA_COTS_UE.md): non-standalone setup with an OAI eNB and gNB (may be outdated)
- [NTN setup](./ntn-configuration.md): non-terrestrial network setup for LEO and GEO satellites
- [Handover](./handover-tutorial.md): F1 (intra-gNB) and N2 (inter-gNB) handovers
- [F1-based NR-DC](./nr-dc.md): NR dual connectivity with one CU and FR1/FR2 DUs
- [Positioning reference signal (PRS)](./RUN_NR_PRS.md): Rel-16 PRS positioning with the gNB and NR-UE
- [E2 agent](../openair2/E2AP/README.md): deploy the gNB with the E2 agent and the FlexRIC near-RT RIC
- [E3 agent](../openair2/E3AP/README.md): build, configure and connect real-time dApps integrated in the gNB

### Functional splits

- [F1 design](./F1AP/F1-design.md): CU/DU split and implementation status
- [E1 design](./E1AP/E1-design.md): CU-CP/CU-UP split and how to run it
- [O-RAN 7.2 fronthaul](./ORAN_FHI7.2_Tutorial.md): run the gNB with O-RAN 7.2 radio units
- [OAI O-RU](./ORAN_FHI7.2_ORU_Tutorial.md): configure, build and run the native OAI O-RU executable
- [Native 7.2x fronthaul library](../fronthaul/README.md): architecture and implementation of the DPDK-based fronthaul stack
- [OAI-VNF and OAI-PNF](./nfapi.md): FAPI/nFAPI split, with general remarks on FAPI/nFAPI
- [nFAPI internals](./NR_NFAPI_archi.md): VNF/PNF P5/P7 message processing and flowcharts
- [NVIDIA Aerial and Foxconn](./Aerial_FAPI_Split_Tutorial.md): OAI L2 with the NVIDIA Aerial L1 over FAPI and a Foxconn RU

### Hardware acceleration

- [LDPC accelerators (AMD/Xilinx T2, Intel ACCs, ORS Aurora)](./LDPC_OFFLOAD_SETUP.md): LDPC offload through
  O-RAN AAL/DPDK BBDEV or XDMA (Aurora)
- [NR-UE with the GPU LDPC decoder](./NR_SA_Tutorial_OAI_nrUE_GPU_offload.md): offload the UE LDPC decoder to a GPU
- [GPU LDPC decoder](../openair1/PHY/CODING/nrLDPC_coding/nrLDPC_coding_cuda/README.md): test and profile the CUDA/HIP decoder
- [GPU-accelerated channel simulation](../openair1/SIMULATION/TOOLS/DOC/gpu_acceleration.md): run the channel model
  on the GPU with CUDA

### Simulators

- [Physical simulators](./physical-simulators.md): standalone PHY test programs (physims) and how to run them
- [Channel simulator](../openair1/SIMULATION/TOOLS/DOC/channel_simulation.md): channel models and their configuration
- [UE positioning in a digital twin](./UE_Positioning_in_DigitalTwin_Tutorial.md): localize a UE using ray-tracing channel emulation
- [L2 simulator](./L2NFAPI.md): test L2 and above over nFAPI with simulated UEs (4G)

### Legacy

These documents are unmaintained and no longer valid:

- [L2 simulator without S1](./L2NFAPI_NOS1.md): two-host deployment without a core network
- [L2 simulator with S1](./L2NFAPI_S1.md): two-host deployment with an EPC
- [4G L1 simulator](./L1SIM.md): we recommend the RFsimulator instead
- [4G device-to-device (D2D) emulator](./d2d_emulator_setup.md): unfinished, steps may not be enough

## Designs

- [Software architecture](./SW_archi.md): general notes
- [L1 flow graph in the gNB](./SW-archi-graph.md): overview of the gNB L1 threads
- [L1 threads in the NR-UE](./nr-ue-design.md): UE PHY/MAC interface and multi-threading design
- [F1AP message library](./F1AP/F1AP-lib.md): encoding, decoding and testing of F1AP messages
- [GTP-U design](./GTP/gtp-design.md): user-plane tunnels for S1-U, N3 and F1-U
- [NRPPa call flow](./NRPPA/NRPPA-call-flow.md): UL-TDOA positioning call flow
- [UE 5G NAS](./5Gnas.md): 5GS NAS protocol and OAI implementation status
- [Beamforming](./beamforming.md): configuration and scheduler implementation
- [gNB RACH processing](./rach_processing_in_gNB.md): receive and process random-access channel samples
- [UL MIMO](./UL_MIMO.md): uplink MIMO and how to test it with the ULSCH simulator

## Container Images

- [Building images](../docker/README.md): build the OAI Docker/Podman images
- [5G with the RFsimulator](../ci-scripts/yaml_files/5g_rfsimulator/README.md): deploy a full 5G SA setup with containers
- [4G with the RFsimulator](../ci-scripts/yaml_files/4g_rfsimulator_fdd_05MHz/README.md): deploy a full 4G setup with containers
- [O-RAN 7.2 with a VVDN O-RU](../ci-scripts/yaml_files/sa_fhi_7.2_vvdn_gnb/README.md): deploy the gNB and O-RU
  integration with containers
- [Physical simulators in OpenShift](../openshift/README.md): build images on an OpenShift cluster

## Libraries

### General

- [T tracer](../common/utils/T/DOC/T.md): generic tracing tool (VCD, Wireshark,
  GUI, recording for later analysis)
- [OPT](../openair2/UTIL/OPT/README.txt): tracing to Wireshark
- [Configuration module](../common/config/DOC/config.md): API to read parameters from configuration files and the command line
- [Logging module](../common/utils/LOG/DOC/log.md): console logging for all OAI components
- [Shared object loader](../common/utils/DOC/loader.md): load optional libraries (radios, coding, ...) at run time
- [Thread pool](../common/utils/threadPool/thread-pool.md): used in L1
- [Actor utility](../common/utils/actor/README.md): single-threaded actors with single-producer/single-consumer input queues
- [Inter-task interface (ITTI)](../common/utils/ocp_itti/itti.md): typed message queues, timers and socket events between threads
- [LDPC implementation](../openair1/PHY/CODING/DOC/LDPCImplementation.md): built as a shared library
- [Time management](./time_management.md): timers in realtime and simulation modes

### Radios

Radio drivers under `radio/` with their own documentation. The other SDRs
(AW2S, LimeSDR, ...) have none.

- [RFsimulator](../radio/rfsimulator/README.md): replaces the radio to test OAI without RF hardware
- [USRP](../radio/USRP/README.md): Ettus USRP devices through UHD
- [BladeRF](../radio/BLADERF/README.md): BladeRF 2.0 micro setup and configuration
- [I/Q player](../radio/iqplayer/DOC/iqrecordplayer_usage.md): driver usage
- [I/Q record and replay](./iqrecordplayer_usage.md): general documentation
- [O-RAN 7.2 fronthaul driver (fhi_72)](../radio/fhi_72/README.md): developer notes on the xran-based driver
- [Ethernet fronthaul drivers](../radio/ETHERNET/ethernet.md): eCPRI split 8 and IF4.5 split 7 transport over Ethernet
- [vrtsim](../radio/vrtsim/README.md): shared memory radio interface
- [RF emulator](../radio/emulator/README.md): runs without a radio, drops TX and generates noise as RX
- [ZeroMQ (zmq)](../radio/zmq/README.md): exchange samples between processes over ZeroMQ

### Special-purpose libraries

- [Scopes](../openair1/PHY/TOOLS/readme.md): xForms and imgui based scopes
- [Telnet server](../common/utils/telnetsrv/DOC/telnethelp.md): monitoring and control
- [Web server](../common/utils/websrv/DOC/websrv.md): web interface for monitoring and debugging
- [RT data recording app](./data_recording.md): record synchronized gNB and UE data sets in SigMF format

## Testing

- [Unit tests](./UnitTests.md): unit testing setup
- [Test benches](./TESTBenches.md): CI setup and links to pipelines
- [CI framework](../ci-scripts/README.md): custom framework to run end-to-end tests
- Component tests (in `tests/`):
    - [CU-UP tester](../tests/nr-cuup/README.md): performance test of the CU-UP over E1 and GTP
    - [CU-NRPPa tester](../tests/nr-cu-nrppa/README.md): test NRPPa and NGAP positioning messages with the AMF
    - [UE NAS tester](../tests/nr-ue-nas-simulator/README.md): test UE NAS procedures with the AMF, without RAN
    - [XNAP tester](../openair2/XNAP/tests/xn-simulator/README.md): validate Xn setup over real SCTP and ASN.1
      without a full RAN stack

## Developer tools

- [Repository agent guidelines](../AGENTS.md): build, coding and verification conventions for coding agents
- [Contribution requirements](../CONTRIBUTING.md): licensing, DCO, commit and contribution requirements
- [Contribution guidelines](./code-style-contrib.md): working practices, code style and review process
- [Git guide](./git-guide.md): commit signing, branch management, submodules,
  recovering from mistakes, reusing conflict resolutions (rerere)
- [Cross-compilation](./cross-compile.md): cross-compiling OAI for ARM
- [clang-format](./clang-format.md): code formatting
- [Formatting check](../tools/formatting/README.md): detects clang-format errors
- [cppcheck](../tools/cppcheck/README.md): static code analysis
- [Sanitizers](./dev_tools/sanitizers.md): running with ASan, UBSan, MSan and TSan
- [include-what-you-use (iwyu)](../tools/iwyu/README.md): detects `#include` errors
- [Docker development environment](../tools/docker-dev-env/README.md): Ubuntu 24 based
- [Plotting tools](../tools/plots/README.md): generate PHY performance plots from simulator results
- [Performance monitoring](./dev_tools/performance_monitoring.md): tools to monitor the computing performance of OAI
- [Documentation best practices](./doc_best_practices.md): guidelines for writing documentation
