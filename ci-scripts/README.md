<!-- SPDX-License-Identifier: CC-BY-4.0 -->

This document outlines how to use the OAI CI framework and gives a quickstart.

[[_TOC_]]

# Related documentation

- [Configuration-file naming conventions](./conf_files/README.md)
- [CI framework unit tests](./tests/README.md)

# Quickstart

There is a script `main.py` that

- reads scenario files from XML
- executes the steps (e.g., deployment of CN/gNB/UE, attach, ping, ...)

To simplify, a script `run_locally.sh` can be used to run a single scenario
locally.  First, download images for gNB&UE (we assume you already downloaded
the CN) and tag them "latest", which is expected by `run_locally.sh`:

    docker pull oaisoftwarealliance/oai-gnb:develop
    docker tag oaisoftwarealliance/oai-gnb:develop oai-gnb
    docker pull oaisoftwarealliance/oai-nr-ue:develop
    docker tag oaisoftwarealliance/oai-nr-ue:develop oai-nr-ue

Now, run the scenario:

    cd ~/openairinterface5g/ci-scripts/
    ./run_locally.sh xml_files/container_5g_rfsim_simple.xml

Output should look like

```
[2025-08-07 18:07:49,631]     INFO: ----------------------------------------
[2025-08-07 18:07:49,631]     INFO:   Creating HTML header 
[2025-08-07 18:07:49,631]     INFO: ----------------------------------------
[2025-08-07 18:07:49,766]     INFO: ----------------------------------------
[2025-08-07 18:07:49,767]     INFO:   Starting Scenario: xml_files/container_5g_rfsim_simple.xml
[2025-08-07 18:07:49,767]     INFO: ----------------------------------------
[2025-08-07 18:07:49,767]     INFO: placing all artifacts for this run in /home/richie/oai/cmake_targets/log/container_5g_rfsim_simple.xml.d/

[...]

[2025-08-07 18:07:49,771]     INFO: ----------------------------------------
[2025-08-07 18:07:49,771]     INFO:  Test ID: 1 (#1)
[2025-08-07 18:07:49,771]     INFO:  Deploy OAI 5G CoreNetwork
[2025-08-07 18:07:49,771]     INFO: ----------------------------------------
```

it will run an end-to-end test, connecting one UE to the gNB, and ping, before
undeploying. As shown, logs will be under
`/home/richie/oai/cmake_targets/log/container_5g_rfsim_simple.xml.d/`. The
python code also produces an HTML report in `ci-scripts/test_results.html`.

# Running with local changes

In order to run the tests as above, with local changes, please [build images
locally](../docker/README.md).

It is not yet possible to override an existing image with changes made locally
to a source file and run it directly through the CI framework. However, it is
possible to override an existing image with local changes, and run the scenario
manually following the console logs steps provided above. This is described in
the [5G RFsimulator
README](yaml_files/5g_rfsimulator/README.md#6-running-with-local-changes).
