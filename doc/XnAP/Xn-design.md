<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Xn design

[[_TOC_]]

## 1. Introduction

Xn is the interface between two NG-RAN nodes (gNBs): its control plane (Xn-C)
carries XnAP over SCTP, its user plane (Xn-U) carries data forwarding over
GTP-U (3GPP TS 38.420 to 38.425). OAI implements XnAP according to TS 38.423
v16.2.0 (`openair2/XNAP/MESSAGES/ASN1/xnap_R16.2.0`), on SCTP port `38422`
with payload protocol identifier `61` (`xnap_default_values.h`).

The main use of Xn in OAI is the **Xn handover**, prepared directly between
two gNBs (or CU-CPs) without the AMF; only the final NGAP Path Switch Request
involves the AMF. See the
[handover tutorial](../handover-tutorial.md#xn-handover) to run it, and the
[RRC developer documentation](../RRC/rrc-dev.md#inter-gnb-handover-xn) for the
RRC handover flow.

## 2. OAI implementation status

Supported:

* Monolithic gNB and CU (CU-CP in an E1 split): XnAP runs where RRC runs.
* One Xn-C association per peer gNB, up to 8 configured candidates
  (`XNAP_MAX_NB_CANDIDATES`).
* Xn Setup.
* Xn handover: Handover Request / Acknowledge / Preparation Failure, SN Status
  Transfer, UE Context Release, Handover Cancel.
* Handover trigger by A3 measurement event or telnet, for a UE with an
  established PDU session, no handover ongoing and no target XnAP UE ID left
  (see [XnAP UE IDs](#33-xnap-ue-ids)).

Not supported:

* Xn-U (no data forwarding during handover).
* Xn Removal, NG-RAN node Configuration Update, Reset, Error Indication, RAN
  Paging, Retrieve UE Context, dual connectivity. The XnAP library
  (`openair2/XNAP/lib/`) can encode/decode Handover Success, RAN Paging and
  Retrieve UE Context, but the gNB does not use them yet.
* XnAP timers TXnRELOCprep and TXnRELOCoverall.
* NGAP Path Switch Request Failure, and E1 Bearer Context Setup Failure or F1
  UE Context Setup Failure at the target.

## 3. Architecture

### 3.1 Tasks and message flow

XnAP runs in its own ITTI task, `TASK_XNAP`, started by `gNB_app_task()` when
`Xn_INTERFACE.gnb_ipv4_address_for_xnc` is configured. XnAP owns everything
specific to Xn (SCTP associations, ASN.1, XnAP UE IDs); RRC refers to a
handover by its RRC UE ID and exchanges ITTI messages with XnAP
(`xnap_messages_def.h`). RRC also calls `xnap_ids.c` directly, which is
thread-safe: the target RRC binds the UE it creates, and RRC resolves and
removes the XnAP UE IDs of a handover it ends itself (Handover Preparation
Failure, Handover Cancel, removal of the UE), as the UE may be removed before
XnAP sends the message.

* Outgoing: RRC sends an ITTI message (e.g. `XNAP_HANDOVER_REQ`, with the
  target peer as `target_assoc_id`) to `TASK_XNAP`, which looks up the XnAP UE
  IDs, encodes the PDU and sends it to `TASK_SCTP`.
* Incoming: `xnap_gNB_handle_message()` decodes the PDU from `SCTP_DATA_IND`
  and calls the handler of the procedure and direction, which maps the XnAP
  UE IDs to the RRC UE ID and forwards an ITTI message to `TASK_RRC_GNB`.

### 3.2 Peers and Xn candidates

On `XNAP_REGISTER_GNB_REQ`, sent by GNB_APP after NG Setup, XnAP listens on
`gnb_ipv4_address_for_xnc` and connects to each
`candidate_gnb_ipv4_address_for_xnc`. A peer (`xnap_peer_t`), keyed by its
SCTP association ID, is created when an association comes up in either
direction. If both gNBs connect to each other at the same time, the
redundant association is closed. A failed connection or a lost association is
not retried; see [Xn Setup](./xnap_procedures.md#xn-setup).

When Xn Setup completes, RRC adds the peer's gNB ID to its Xn candidates
(`XNAP_SETUP_IND`), and removes it when the association is lost
(`XNAP_PEER_SHUTDOWN_IND`). A handover towards a neighbour cell uses Xn if the
neighbour's `gNB_ID` is an Xn candidate, N2 otherwise.

### 3.3 XnAP UE IDs

The Source and Target NG-RAN node UE XnAP IDs are allocated and stored only in
XnAP (`xnap_ids.c`, model in `xnap_ids.h`). A UE has at most one entry, as
this gNB is either the source or the target of its handover. The source
allocates its ID when sending the Handover Request and stores the target's on
the Acknowledge; the target allocates its ID when receiving the Handover
Request, and RRC binds it to the UE it creates.

The entry is removed when the handover ends (UE Context Release, Handover
Preparation Failure or Cancel, loss of the peer), when a message of the
handover cannot be sent, and when RRC removes the UE, so that a late XnAP
message does not reach a new UE reusing the RRC UE ID. RRC refuses a new Xn
handover of a UE that still has a target entry, i.e. before the Path Switch
Request Acknowledge, as its UE Context Release to the source is still due.

## 4. Configuration

Xn is enabled by an `Xn_INTERFACE` section in the gNB configuration (under
`gNBs.[0]`):

```
Xn_INTERFACE :
{
  gnb_ipv4_address_for_xnc           = "127.0.0.10";
  candidate_gnb_ipv4_address_for_xnc = ({ ip = "127.0.0.20"; },
                                        { ip = "127.0.0.30"; });
};
```

* `gnb_ipv4_address_for_xnc`: local Xn-C address. Without it, Xn is disabled.
* `candidate_gnb_ipv4_address_for_xnc`: peer gNBs this gNB connects to. It can
  be empty; this gNB then only accepts incoming connections.

Both gNBs must have the same PLMN (otherwise Xn Setup fails) and a unique
`gNB_ID`. For an Xn handover, the source also needs a `neighbour_list` entry
for the target cell, as for N2 handover; see the [neighbour configuration
example](../handover-tutorial.md#example-neighbour-configuration).

## 5. Running Xn handover

See the [handover tutorial](../handover-tutorial.md#xn-handover) to run an Xn
handover with two rfsim gNBs and the OAI UE. To test without RAN or UE, the
[Xn simulator](../../openair2/XNAP/tests/xn-simulator/README.md) runs several
gNB instances and a NAS-only UE stub in one process, and hands the UE over
round-robin across the gNBs, with the Path Switch to the AMF.
