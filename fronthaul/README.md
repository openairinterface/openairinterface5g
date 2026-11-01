# Fronthaul Library

Fronthaul stack for 7.2x split using DPDK. Currently supports O-RU only; O-DU support is not implemented.

The shared receive, send and timer components are described in the
[fronthaul core documentation](./core/README.md).

## Architecture overview

### Software component diagram

```mermaid
graph TD
    O-RU
    subgraph integration ["Fronthaul API"]
        oru_fh["oru_fh"]
    end
    subgraph internals ["O-RU fronthaul internals"]
        oru_io["oru_io"]
        oru_pkt["oru_packet_processor"]
    end
    subgraph fronthaul_core ["Fronthaul Core (core/)"]
        direction LR
        fh_rx["fh_recv (RX)"]
        fh_tx["fh_send (TX)"]
        fh_timer["fh_timer (Clock)"]

        fh_rx --> fh_tx
        fh_tx --> fh_timer
    end
    subgraph protocol ["Protocol Layer (xran_pkt/)"]
        xran_api["xran_pkt_api"]
    end
    dpdk[("DPDK / NIC")]
    O-RU --> oru_fh
    oru_fh --> oru_pkt
    oru_fh --> oru_io

    oru_io --> oru_pkt
    oru_io --> fh_rx
    oru_io --> fh_tx
    oru_pkt --> xran_api
    fh_rx --> xran_api
    fh_tx --> xran_api
    fh_rx --> dpdk
    fh_tx --> dpdk
    fh_timer --> dpdk
```
### Threading

The library uses a simplified threading model designed for low-latency synchronization:

- **Single Worker Thread**: A single worker thread (pinned to `rx_core`) handles both packet reception and timing.
The `fh_timer` is driven by a `tick()` function called within the RX poll loop. This ensures that timing events are
processed with minimal context-switching overhead.
- **Immediate Transmission**: Packet transmission happens immediately when the application calls the send function.

#### Sequence Diagram

```mermaid
sequenceDiagram
    participant HW as Hardware / NIC
    participant FW as Fronthaul Worker (lcore)
    participant PP as Packet Processor
    participant DL as DL App Threads (Multi)
    participant UL as UL App Threads (Multi)
    Note over FW: Continuous Loop (fh_recv_run)
    FW->>HW: Poll for Packets
    HW-->>FW: eCPRI Packets (C/U-Plane)
    FW->>PP: handle_packet()

    alt [All IQ Samples Received]
        PP-->>DL: Enqueue Job (Multi-Consumer Ring)
    end

    FW->>FW: fh_timer_tick()
    FW->>PP: handle_symbol_tick()

    alt [Window Expired (Handling Missing Packets)]
        PP-->>DL: Enqueue Job (Multi-Consumer Ring)
    end

    Note over DL: Parallel consumption of DL IQ data
    DL->>PP: read_dl_iq()
    PP-->>DL: IQ Data (Blocking MC Dequeue)

    Note over UL: Parallel production of UL packets
    UL->>PP: write_ul_iq()
    PP->>HW: send_mbuf() (Direct Parallel TX)
    Note over HW,FW: rte_spinlock ensures thread-safe TX burst
```

## Core API (`oru_fh.h`)

- `oru_fh_init`: Initializes DPDK EAL, configures ports, and sets up memory pools based on PRB requirements.
- `oru_fh_start`: Launches the worker thread and starts the fronthaul processing loop.
- `oru_fh_stop`: Gracefully stops the worker thread and releases resources.
- `oru_fh_tx_read_symbol`: Intended for reading UPlane data for a specific slot/symbol in a synchronized manner.
- `oru_fh_rx_send_pusch`/`prach`: Sends IQ via UPlane to DU

## Configuration

See `oru_fh_config_t` for details

## Tools

### Packet Dissector - `xran_pcap_dump`
A tool used to verify that packets are correctly formed. It reads PCAP files and dissects them using the `xran_pkt`
library to ensure compatibility with O-RU/O-DU implementations.
