<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# LDPC coding implementation
This document gives an overview of the different LDPC coding implementations (functional or not) available with Open Air Interface.

[[_TOC_]]

The LDPC encoder and decoder are implemented in a shared library, dynamically loaded at run-time using the [oai shared library loader](../../../../common/utils/DOC/loader.md).
Two types of library are available with two different interfaces. There are libraries implementing the encoder and decoder of slots and libraries implementing the encoder and decoder of code segments.

## LDPC slot coding
The interface of the library is defined in [nrLDPC_coding_interface.h](../nrLDPC_coding/nrLDPC_coding_interface.h).
The code loading the LDPC library is in [nrLDPC_coding_interface_load.c](../nrLDPC_coding/nrLDPC_coding_interface_load.c), in function `load_nrLDPC_coding_interface`, which must be called at init time.

### Selecting the LDPC library at run time

By default the function `int load_nrLDPC_coding_interface(void)` looks for `libldpc.so`.\
This default behavior can be changed using the oai loader configuration options in the configuration file or from the command line as shown below:

#### Examples of ldpc shared lib selection when running nr softmodem's:

loading default `libldpc.so`:

```
./nr-softmodem -O libconfig:gnb.band78.tm1.106PRB.usrpx300.conf:dbgl5
```

`libldpc.so` has its decoder implemented in [nrLDPC_coding_segment_decoder.c](../nrLDPC_coding/nrLDPC_coding_segment/nrLDPC_coding_segment_decoder.c).\
Its encoder is implemented in [nrLDPC_coding_segment_encoder.c](../nrLDPC_coding/nrLDPC_coding_segment/nrLDPC_coding_segment_encoder.c).

loading `libldpc_aal.so` instead of `libldpc.so`:

`make ldpc_aal`

This command creates the `libldpc_aal.so` shared library.

```
Building C object CMakeFiles/ldpc_aal.dir/openair1/PHY/CODING/nrLDPC_coding/nrLDPC_coding_aal/nrLDPC_coding_aal.c.o
Linking C shared module libldpc_aal.so
```

At runtime, to successfully use LDPC accelerators (e.g., Xilinx T2/Intel ACCs), you will need to install the corresponding drivers and tools.
Please refer to the dedicated documentation at [LDPC_OFFLOAD_SETUP.md](../../../../doc/LDPC_OFFLOAD_SETUP.md).

```
./nr-softmodem -O  libconfig:gnb.band78.sa.fr1.106PRB.usrpb210.conf:dbgl5 --rfsim --rfsimulator.[0].serveraddr server  --log_config.gtpu_log_level info  --loader.ldpc.shlibversion _aal --nrLDPC_coding_aal.dpdk_dev 01:00.0 --nrLDPC_coding_aal.dpdk_core_list 0-1
```

`libldpc_aal.so` has its decoder and its encoder implemented in [nrLDPC_coding_aal.c](../nrLDPC_coding/nrLDPC_coding_aal/nrLDPC_coding_aal.c).

#### Examples of ldpc shared lib selection when running ldpctest:

Slot coding libraries cannot be used yet within ldpctest.

But they can be used within nr_ulsim, nr_dlsim, nr_ulschsim and nr_dlschsim.\
In these PHY simulators, using the slot coding libraries is enabled in the exact same way as in nr-softmodem.

### LDPC libraries
Libraries implementing the slotwise LDPC coding must be named `libldpc<_version>.so`. They must implement four functions: `nrLDPC_coding_init`, `nrLDPC_coding_shutdown`, `nrLDPC_coding_decoder` and `nrLDPC_coding_encoder`. The prototypes for these functions is defined in [nrLDPC_coding_interface.h](../nrLDPC_coding/nrLDPC_coding_interface.h).

`libldpc.so` is completed.

`libldpc_aal.so` is completed.

## LDPC segment coding
The interface of the library is defined in [nrLDPC_defs.h](../nrLDPC_defs.h) as typedefs of the functions of the interface.
The name of the functions implementing these typedefs can be found in [nrLDPC_extern.h](../nrLDPC_extern.h).

LDPC segment coding libraries are not loaded directly by OAI but are statically linked in libraries implementing the LDPC slot coding interface using an adaptation layer between the slot coding interface and the segment coding interface.  
This way, these libraries can be loaded to implement the slot coding interface as well as the segment coding interface in `ldpctest`.  
`ldpctest` therefore loads directly the segment coding interface with the loader implemented in [nrLDPC_load.c](../nrLDPC_load.c) as function `load_nrLDPClib`.

### Selecting the LDPC library at run time

By default the function `int load_nrLDPClib(void)` looks for `libldpc.so`, this default behavior can be changed using the oai loader configuration options in the configuration file or from the command line as shown below:

#### Examples of ldpc shared lib selection when running nr softmodem's:

loading `libldpc_orig.so` instead of `libldpc.so`:

```
./nr-softmodem -O libconfig:gnb.band78.tm1.106PRB.usrpx300.conf:dbgl5  --loader.ldpc.shlibversion _orig
```

A mechanism to select ldpc implementation is also available in the `ldpctest` phy simulator via the `-v` option, which can be used to specify the version of the ldpc shared library to be used.

#### Examples of ldpc shared lib selection when running ldpctest:

Loading libldpc_orig.so, the legacy implementation of the ldpc decoder:

```
$ ./ldpctest -v _orig
```

### LDPC libraries
The prototypes for the functions of the interface are defined in [nrLDPC_defs.h](../nrLDPC_defs.h) as typedefs.
Libraries implementing the LDPC algorithms must be named `libldpc<_version>.so`, they must implement four functions with the names and types:
* `LDPCinit` implementing type `LDPC_initfunc_t *`
* `LDPCshutdown` implementing type `LDPC_shutdownfunc_t *`
* `LDPCdecoder` implementing type `LDPC_decoderfunc_t *`
* `LDPCencoder` implementing type `LDPC_encoderfunc_t *`
* `LDPCencoder32` implementing type `LDPC_encoderfunc32_t *`, specifically for
  CUDA offload. This is not used by the LDPC slot coding interface, but must be
  defined in a shared library to load successfully.
