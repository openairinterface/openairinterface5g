# SPDX-License-Identifier: LicenseRef-CSSL-1.0

# This defines a cmake file with timing thresholds to include (empty by default)
set(PHYSIM_CHECK_FILES "" CACHE STRING "CMake files ('\;' separated) with timing thresholds to include for timed physim tests")

if(PHYSIM_CHECK_FILES)
  foreach(PHYSIM_CHECK_FILE IN LISTS PHYSIM_CHECK_FILES)
    # Construct full path relative to current source dir
    set(CHECK_FILE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/${PHYSIM_CHECK_FILE}")
    if(EXISTS "${CHECK_FILE_PATH}")
      message(STATUS "Including file for physim timing threshold check: ${PHYSIM_CHECK_FILE}")
    else()
      message(FATAL_ERROR "Threshold file '${PHYSIM_CHECK_FILE}' not found at ${CHECK_FILE_PATH}")
    endif()
  endforeach()
endif()

####################################################################################
######                           dlsim unit test                              ######
####################################################################################

add_physim_test(physim.4g.dlsim.basic.test1 "10 MHz, R2.FDD (MCS 5), EVA5, -1dB" dlsim -m=5 -g=F -s=-1 -w=1.0 -f=.2 -n=1500 -B=50 -c=2 -z=2 -Tperf=60)
add_physim_test(physim.4g.dlsim.basic.test2 "1.4 MHz, R4.FDD (MCS 4), EVA5, 0dB (70%)" dlsim -m=4 -g=F -s=0 -w=1.0 -f=.2 -n=1500 -B=6 -c=4 -z=2 -Tperf=60)
add_physim_test(physim.4g.dlsim.basic.test3 "10 MHz, R3.FDD (MCS 15), EVA5, 6.7dB (70%)" dlsim -m=15 -g=F -s=6.7 -w=1.0 -f=.2 -n=1500 -B=50 -c=2 -z=2 -Tperf=60)
add_physim_test(physim.4g.dlsim.basic.test4 "5 MHz, R3-1.FDD (MCS 15), EVA5, 6.7dB (70%)" dlsim -m=15 -g=F -s=6.7 -w=1.0 -f=.2 -n=1500 -B=25 -c=2 -z=2 -Tperf=60)
add_physim_test(physim.4g.dlsim.basic.test5 "10 MHz, R3-1.FDD (MCS 15), EVA5, 6.7dB (30%)" dlsim -m=15 -g=G -s=1.4 -w=1.0 -f=.2 -n=1500 -B=50 -c=2 -z=2 -Tperf=25)
add_physim_test(physim.4g.dlsim.basic.test6 "5 MHz, R3-1.FDD (MCS 15), ETU70, 1.4 dB (30%)" dlsim -m=15 -g=G -s=1.4 -w=1.0 -f=.2 -n=1500 -B=25 -c=2 -z=2 -Tperf=25)
add_physim_test(physim.4g.dlsim.basic.test7 "5 MHz, R6.FDD (MCS 25), EVA5, 17.4 dB (70%)" dlsim -m=25 -g=F -s=17.4 -w=1.0 -f=.2 -n=1500 -B=25 -c=3 -z=2 -Tperf=60)
add_physim_test(physim.4g.dlsim.basic.test8 "5 MHz, R6-1.FDD (MCS 24,18 PRB), EVA5, 17.5dB (70%)" dlsim -m=25 -g=F -s=17.5 -w=1.0 -f=.2 -n=1500 -B=25 -c=3 -z=2 -r=1022 -Tperf=60)
add_physim_test(physim.4g.dlsim.basic.test9 "10 MHz, R7.FDD (MCS 25), EVA5, 17.7dB (70%)" dlsim -m=26 -g=F -s=17.7 -w=1.0 -f=.2 -n=1500 -B=50 -c=2 -z=2 -Tperf=60)
add_physim_test(physim.4g.dlsim.100rb.tm2.test1 "20 MHz, R.9 FDD (MCS 26), EVA5, 17.6dB (70%)" dlsim -m=26 -g=F -s=17.6 -w=1.0 -f=.2 -n=1500 -B=100 -c=2 -z=2 -Tperf=60)
add_physim_test(physim.4g.dlsim.100rb.tm2.test2 "20 MHz, R.9-2 FDD (MCS 26, 17PRB), EVA5, 17.3dB (70%)" dlsim -m=26 -g=F -s=17.3 -w=1.0 -f=.2 -n=1500 -B=100 -c=2 -z=2 -r=1600 -Tperf=60)
add_physim_test(physim.4g.dlsim.100rb.tm2.test3 "20 MHz, R.9-1 FDD (MCS 26, 83 PRB), EVA5, 16.6dB (70%)" dlsim -m=26 -g=F -s=16.6 -w=1.0 -f=.2 -n=1500 -B=100 -c=2 -z=2 -r=1899 -Tperf=60)
add_physim_test(physim.4g.dlsim.100rb.tm2.test4 "10 MHz, R.11 FDD (MCS 14), EVA5, 6.8 dB (70%)" dlsim -m=14 -g=F -s=6.8 -w=1.0 -f=.2 -n=1500 -B=50 -c=2 -x=2 -y=2 -z=2 -Tperf=60)
add_physim_test(physim.4g.dlsim.100rb.tm2.test5 "5 MHz, R.11-2 FDD (MCS 13), EVA5, 5.9 dB (70%)" dlsim -m=13 -g=F -s=5.9 -w=1.0 -f=.2 -n=1500 -B=25 -c=3 -x=2 -y=2 -z=2 -Tperf=60)

####################################################################################
######                           ulsim unit test                              ######
####################################################################################

add_physim_test(physim.4g.ulsim.test1 "5 MHz, FDD (MCS 5), AWGN, 6dB" ulsim -BnbRBs=25 -mcs=5 -yN_rx=1 -gchannel=N -xTransmission=1 -snr=6 -wsnrInterrupt=1.0 -e_snr_step=.1 -P -nb_frame=1500 -Tperf=70)
add_physim_test(physim.4g.ulsim.test2 "5 MHz, FDD (MCS 16), AWGN , 12dB (70%)" ulsim -BnbRBs=25 -mcs=16 -yN_rx=1 -gchannel=N -xTransmission=1 -snr=12 -wsnrInterrupt=1.0 -e_snr_step=.1 -P -nb_frame=1500 -Tperf=70)
add_physim_test(physim.4g.ulsim.test3 "10 MHz, R3.FDD (MCS 5), AWGN, 6dB (70%)" ulsim -BnbRBs=50 -mcs=5 -yN_rx=1 -gchannel=N -xTransmission=1 -snr=6 -wsnrInterrupt=1.0 -e_snr_step=.1 -P -nb_frame=1500 -Tperf=70)
add_physim_test(physim.4g.ulsim.test4 "10 MHz, R3-1.FDD (MCS 16), AWGN, 12dB (70%)" ulsim -BnbRBs=50 -mcs=16 -yN_rx=1 -gchannel=N -xTransmission=1 -snr=12 -wsnrInterrupt=1.0 -e_snr_step=.1 -P -nb_frame=1500 -Tperf=70)
add_physim_test(physim.4g.ulsim.test5 "20 MHz, FDD (MCS 5), AWGN, 6dB (70%)" ulsim -BnbRBs=100 -mcs=5 -yN_rx=1 -gchannel=N -xTransmission=1 -snr=6 -wsnrInterrupt=1.0 -e_snr_step=.1 -P -nb_frame=1500 -Tperf=70)
add_physim_test(physim.4g.ulsim.test6 "20 MHz, FDD (MCS 16), AWGN, 12 dB (70%)" ulsim -BnbRBs=100 -mcs=16 -yN_rx=1 -gchannel=N -xTransmission=1 -snr=12 -wsnrInterrupt=1.0 -e_snr_step=.1 -P -nb_frame=1500 -Tperf=70)

####################################################################################
######                           ldpctest unit test                           ######
####################################################################################

add_physim_test(physim.5g.ldpctest.test1 "block length = 3872, BG1" ldpctest -l3872 -s10 -n300)
add_physim_test(physim.5g.ldpctest.test2 "block length = 4224, BG1" ldpctest -l4224 -s10 -n300)
add_physim_test(physim.5g.ldpctest.test3 "block length = 4576, BG1" ldpctest -l4576 -s10 -n300)
add_physim_test(physim.5g.ldpctest.test4 "block length = 4928, BG1" ldpctest -l4928 -s10 -n300)
add_physim_test(physim.5g.ldpctest.test5 "block length = 5280, BG1" ldpctest -l5280 -s10 -n300)
add_physim_test(physim.5g.ldpctest.test6 "block length = 5632, BG1" ldpctest -l5632 -s10 -n300)
add_physim_test(physim.5g.ldpctest.test7 "block length = 6336, BG1" ldpctest -l6336 -s10 -n300)
add_physim_test(physim.5g.ldpctest.test8 "block length = 7040, BG1" ldpctest -l7040 -s10 -n300)
add_physim_test(physim.5g.ldpctest.test9 "block length = 7744, BG1" ldpctest -l7744 -s10 -n300)
add_physim_test(physim.5g.ldpctest.test10 "block length = 8448, BG1" ldpctest -l8448 -s10 -n300)
add_physim_test(physim.5g.ldpctest.test11 "block length = 1, BG2" ldpctest -l1 -s10 -n300)
add_physim_test(physim.5g.ldpctest.test12 "block length = 100, BG2" ldpctest -l100 -s10 -n300)
add_physim_test(physim.5g.ldpctest.test13 "block length = 193, BG2" ldpctest -l193 -s10 -n300)
add_physim_test(physim.5g.ldpctest.test14 "block length = 500, BG2" ldpctest -l500 -s10 -n300)
add_physim_test(physim.5g.ldpctest.test15 "block length = 561, BG2" ldpctest -l561 -s10 -n300)
add_physim_test(physim.5g.ldpctest.test16 "block length = 600, BG2" ldpctest -l600 -s10 -n300)
add_physim_test(physim.5g.ldpctest.test17 "block length = 641, BG2" ldpctest -l641 -s10 -n300)
add_physim_test(physim.5g.ldpctest.test18 "block length = 2000, BG1" ldpctest -l2000 -s10 -n300)
add_physim_test(physim.5g.ldpctest.test19 "block length = 3000, BG1" ldpctest -l3000 -s10 -n300)
add_physim_test(physim.5g.ldpctest.test20 "block length = 3840, BG1" ldpctest -l3840 -s10 -n300)

####################################################################################
######                           polartest unit test                          ######
####################################################################################

add_physim_test(physim.5g.polartest.test1 "PBCH polar test" polartest -q -s-10 -f0)
add_physim_test(physim.5g.polartest.test2 "DCI polar test" polartest -q -s-10 -f0 -m1)
add_physim_test(physim.5g.polartest.test3 "UCI polar test,6-bit CRC" polartest -q -s-2 -f2 -m2 -k12)
add_physim_test(physim.5g.polartest.test4 "UCI polar test,11-bit CRC" polartest -q -s-2 -f2 -m2 -k20)

####################################################################################
######                           nr_pbchsim unit test                         ######
####################################################################################

add_physim_test(physim.5g.nr_pbchsim.106rb.test1 "PBCH-only, 106 PRB, NID 2" nr_pbchsim -s-11 -S-8 -n30 -R106 -N2)
add_physim_test(physim.5g.nr_pbchsim.106rb.test2 "PBCH and synchronization, 106PBR" nr_pbchsim -s-11 -S-8 -n30 -o8000 -I -R106)
add_physim_test(physim.5g.nr_pbchsim.106rb.test3 "PBCH and synchronization, 106PBR, SSB SC OFFSET 6" nr_pbchsim -s-11 -S-8 -n30 -R106 -c6)
add_physim_test(physim.5g.nr_pbchsim.217rb.test1 "PBCH-only, 217 PRB" nr_pbchsim -s-10 -S-8 -n30 -R217)
add_physim_test(physim.5g.nr_pbchsim.217rb.test2 "PBCH and synchronization, 217 RPB" nr_pbchsim -s-10 -S-8 -n30 -o8000 -I -R217)
add_physim_test(physim.5g.nr_pbchsim.273rb.test1 "PBCH-only, 273 PRB" nr_pbchsim -s-10 -S-8 -n30 -R273)
add_physim_test(physim.5g.nr_pbchsim.273rb.test2 "PBCH and synchronization, 273 PRB" nr_pbchsim -s-10 -S-8 -n30 -o8000 -I -R273)
add_physim_test(physim.5g.nr_pbchsim.otherSCS.test1 "PBCH-only, 15Khz, 5Mhz" nr_pbchsim -s-10 -S-8 -n30 -m0 -R25)
add_physim_test(physim.5g.nr_pbchsim.otherSCS.test2 "PBCH-only, 120Khz, 50Mhz" nr_pbchsim -s-10 -S-8 -n30 -m3 -R32)

####################################################################################
######                           nr_dlsim unit test                           ######
####################################################################################

add_physim_test(physim.5g.nr_dlsim.basic.test1 "106 PRB" nr_dlsim -P -n300 -R106 -b106 -s5)
add_physim_test(physim.5g.nr_dlsim.basic.test2 "217 PRB" nr_dlsim -P -n300 -R217 -b217 -s5)
add_physim_test(physim.5g.nr_dlsim.basic.test3 "273 PRB" nr_dlsim -P -n300 -R273 -b273 -s5)
add_physim_test(physim.5g.nr_dlsim.basic.test4 "HARQ test 25% TP 4 rounds" nr_dlsim -P -n300 -s1 -S2 -t25)
add_physim_test(physim.5g.nr_dlsim.basic.test5 "HARQ test 33% TP 3 rounds" nr_dlsim -P -n300 -s1 -S2 -t33)
add_physim_test(physim.5g.nr_dlsim.basic.test6 "HARQ test 50% TP 2 rounds" nr_dlsim -P -n300 -s5 -S7 -t50)
add_physim_test(physim.5g.nr_dlsim.basic.test7 "25 PRBs, 15 kHz SCS" nr_dlsim -P -n300 -m0 -e0 -R25 -b25 -i 2 1 0)
add_physim_test(physim.5g.nr_dlsim.basic.test8 "32 PRBs, 120 kHz SCS" nr_dlsim -P -n300 -s5 -m3 -R32 -b32)
add_physim_test(physim.5g.nr_dlsim.basic.test9 "HARQ retx on 60 of 106 PRBs (E < Foffset) 50% TP 2 rounds" nr_dlsim -P -n100 -e27 -s17 -S18 -K60 -t50)
add_physim_test(physim.5g.nr_dlsim.offset.test1 "106 PRB 25 PDSCH-Offset" nr_dlsim -P -n300 -R106 -a25 -s5)
add_physim_test(physim.5g.nr_dlsim.offset.test2 "106 PRB 51 PDSCH-Offset" nr_dlsim -P -n300 -R106 -a51 -s5)
add_physim_test(physim.5g.nr_dlsim.offset.test3 "217 PRB 100 PDSCH-PRBs" nr_dlsim -P -n300 -R217 -b100 -s5)
add_physim_test(physim.5g.nr_dlsim.offset.test4 "217 PRB 80 PDSCH-Offset" nr_dlsim -P -n300 -R217 -a80 -s5)
add_physim_test(physim.5g.nr_dlsim.offset.test5 "217 PRB 100 PDSCH-PRBs 110 PDSCH-Offset" nr_dlsim -P -n300 -R217 -a110 -s5 -b100)
add_physim_test(physim.5g.nr_dlsim.offset.test6 "106 PRB TYPE0 32:47 64:79" nr_dlsim -P -n300 -R106 -D 32:16,64:16 -s5)
add_physim_test(physim.5g.nr_dlsim.mcs.mimo.test1 "106 PRBs 50 PDSCH-PRBs MCS Index 27" nr_dlsim -P -n300 -e27 -s30)
add_physim_test(physim.5g.nr_dlsim.mcs.mimo.test2 "106 PRBs 50 PDSCH-PRBs MCS Index 16" nr_dlsim -P -n300 -e16 -s11 -S13)
add_physim_test(physim.5g.nr_dlsim.mcs.mimo.test3 "106 MCS-TABLE 256 QAM MCS Index 26" nr_dlsim -P -n300 -q1 -e26 -s30)
add_physim_test(physim.5g.nr_dlsim.mcs.mimo.test4 "106 MCS-TABLE 256 QAM Fading Channel" nr_dlsim -P -n300 -q1 -e24 -s30 -gA)
add_physim_test(physim.5g.nr_dlsim.mcs.mimo.test5 "MCS 0, low SNR performance" nr_dlsim -P -n300 -e0 -t95 -S-1.0 -i 2 1 0)
add_physim_test(physim.5g.nr_dlsim.mcs.mimo.test6 "4x4 MIMO, 1 Layer" nr_dlsim -P -n30 -s20 -U 3 0 0 2 -gA -x1 -y4 -z4)
add_physim_test(physim.5g.nr_dlsim.mcs.mimo.test7 "4x4 MIMO, 2 Layers" nr_dlsim -P -n30 -s20 -U 3 0 0 2 -gA -x2 -y4 -z4)
add_physim_test(physim.5g.nr_dlsim.mcs.mimo.test8 "4x4 MIMO, 2 Layers 256 QAM" nr_dlsim -P -n30 -s25 -e22 -q1 -U 3 0 0 2 -gA -x2 -y4 -z4)
add_physim_test(physim.5g.nr_dlsim.mcs.mimo.test9 "4x4 MIMO, 4 Layers" nr_dlsim -P -n30 -s20 -U 3 0 0 2 -x4 -y4 -z4)
add_physim_test(physim.5g.nr_dlsim.dmrs.ptrs.test1 "3 PTRS, 8 Interpolated Symbols" nr_dlsim -P -n300 -s5 -T 2 2 2)
add_physim_test(physim.5g.nr_dlsim.dmrs.ptrs.test2 "6 PTRS, 5 Interpolated Symbols" nr_dlsim -P -n300 -s5 -T 2 1 2)
add_physim_test(physim.5g.nr_dlsim.dmrs.ptrs.test3 "11 PTRS, 0 Interpolated Symbols" nr_dlsim -P -n300 -s5 -T 2 0 4)
add_physim_test(physim.5g.nr_dlsim.dmrs.ptrs.test4 "Mapping type A, 2 DMRS Symbols" nr_dlsim -P -n300 -s5 -S7 -U 2 0 1)
add_physim_test(physim.5g.nr_dlsim.dmrs.ptrs.test5 "Mapping type A, 3 DMRS Symbols" nr_dlsim -P -n300 -s5 -S7 -U 2 0 2)
add_physim_test(physim.5g.nr_dlsim.dmrs.ptrs.test6 "Mapping type B, 4 DMRS Symbols" nr_dlsim -P -n300 -s5 -S7 -U 2 1 3)

####################################################################################
######                           nr_dlschsim unit test                        ######
####################################################################################

add_physim_test(physim.5g.nr_dlschsim.test1 "106 PRBs" nr_dlschsim -R 106 -m9 -s13 -n300)
add_physim_test(physim.5g.nr_dlschsim.test2 "217 PRBs" nr_dlschsim -R 217 -m15 -s15 -n300)
add_physim_test(physim.5g.nr_dlschsim.test3 "273 PRBs" nr_dlschsim -R 273 -m19 -s20 -n300)

####################################################################################
######                           smallblocktest unit test                     ######
####################################################################################

add_physim_test(physim.5g.smallblocktest.test1 "3 bits" smallblocktest -l 3 -s -4 -d 1 -i 10000)
add_physim_test(physim.5g.smallblocktest.test2 "6 bits" smallblocktest -l 6 -s -4 -d 1 -i 10000)
add_physim_test(physim.5g.smallblocktest.test3 "7 bits" smallblocktest -l 7 -s -4 -d 1 -i 10000)
add_physim_test(physim.5g.smallblocktest.test4 "11 bits" smallblocktest -l 11 -s -4 -d 1 -i 10000)
add_physim_test(physim.5g.smallblocktest.test5 "2 bits" smallblocktest -l 2 -s -4 -f 7 -d 1 -i 10000)
add_physim_test(physim.5g.smallblocktest.test6 "1 bit Qm 6" smallblocktest -l 1 -s -4 -f 10 -d 1 -q 6 -i 10000)

####################################################################################
######                           nr_ulschsim unit test                        ######
####################################################################################

add_physim_test(physim.5g.nr_ulschsim.test1 "106 PRBs" nr_ulschsim -R 106 -m9 -s13 -n300)
add_physim_test(physim.5g.nr_ulschsim.test2 "217 PRBs" nr_ulschsim -R 217 -m15 -s15 -n300)
add_physim_test(physim.5g.nr_ulschsim.test3 "273 PRBs" nr_ulschsim -R 273 -m19 -s20 -n300)
add_physim_test(physim.5g.nr_ulschsim.test4 "106 PRBs 4-layer MIMO" nr_ulschsim -R 106 -m9 -s13 -n300 -y4 -z4 -W4)

####################################################################################
######                           nr_pucchsim unit test                        ######
####################################################################################

add_physim_test(physim.5g.nr_pucchsim.test1 "Format 0 1-bit ACK miss 106 PRB" nr_pucchsim -R 106 -i 1 -P 0 -b 1 -s-2 -n3000)
add_physim_test(physim.5g.nr_pucchsim.test2 "Format 0 2-bit ACK miss 106 PRB" nr_pucchsim -R 106 -i 1 -P 0 -b 2 -s-2 -n3000)
add_physim_test(physim.5g.nr_pucchsim.test3 "Format 0 2-bit ACK miss, 1-bit SR 106 PRB" nr_pucchsim -R 106 -i 1 -P 0 -b 2 -s-2 -c -n3000)
add_physim_test(physim.5g.nr_pucchsim.test4 "Format 2 3-bit 2/106 PRB" nr_pucchsim -R 106 -i 1 -P 2 -b 3 -s0 -n3000)
add_physim_test(physim.5g.nr_pucchsim.test5 "Format 2 4-bit 2/106 PRB" nr_pucchsim -R 106 -i 1 -P 2 -b 4 -s0 -n3000)
add_physim_test(physim.5g.nr_pucchsim.test6 "Format 2 5-bit 2/106 PRB" nr_pucchsim -R 106 -i 1 -P 2 -b 5 -s1 -n3000)
add_physim_test(physim.5g.nr_pucchsim.test7 "Format 2 6-bit 2/106 PRB" nr_pucchsim -R 106 -i 1 -P 2 -b 6 -s2 -n3000)
add_physim_test(physim.5g.nr_pucchsim.test8 "Format 2 7-bit 2/106 PRB" nr_pucchsim -R 106 -i 1 -P 2 -b 7 -s3 -n3000)
add_physim_test(physim.5g.nr_pucchsim.test9 "Format 2 8-bit 2/106 PRB" nr_pucchsim -R 106 -i 1 -P 2 -b 8 -s4 -n3000)
add_physim_test(physim.5g.nr_pucchsim.test10 "Format 2 9-bit 2/106 PRB" nr_pucchsim -R 106 -i 1 -P 2 -b 9 -s5 -n3000)
add_physim_test(physim.5g.nr_pucchsim.test11 "Format 2 10-bit 2/106 PRB" nr_pucchsim -R 106 -i 1 -P 2 -b 10 -s6 -n3000)
add_physim_test(physim.5g.nr_pucchsim.test12 "Format 2 11-bit 2/106 PRB" nr_pucchsim -R 106 -i 1 -P 2 -b 11 -s6 -n3000)
add_physim_test(physim.5g.nr_pucchsim.test13 "Format 2 12-bit 4/106 PRB" nr_pucchsim -R 106 -i 1 -P 2 -q4 -b 12 -s-3 -n3000)
add_physim_test(physim.5g.nr_pucchsim.test14 "Format 2 19-bit 4/106 PRB" nr_pucchsim -R 106 -i 1 -P 2 -q4 -b 19 -s-3 -n3000)
add_physim_test(physim.5g.nr_pucchsim.test15 "Format 2 12-bit 8/106 PRB" nr_pucchsim -R 106 -i 1 -P 2 -q8 -b 12 -s-3 -n3000)
add_physim_test(physim.5g.nr_pucchsim.test16 "Format 2 19-bit 8/106 PRB" nr_pucchsim -R 106 -i 1 -P 2 -q8 -b 19 -s-3 -n3000)
add_physim_test(physim.5g.nr_pucchsim.test17 "Format 2 32-bit 8/106 PRB" nr_pucchsim -R 106 -i 1 -P 2 -q8 -b 32 -s-3 -n3000)
add_physim_test(physim.5g.nr_pucchsim.test18 "Format 2 32-bit 16/106 PRB" nr_pucchsim -R 106 -i 1 -P 2 -q16 -b 32 -s-3 -n3000)
add_physim_test(physim.5g.nr_pucchsim.test19 "Format 2 64-bit 16/106 PRB" nr_pucchsim -R 106 -i 1 -P 2 -q16 -b 64 -s-3 -n3000)
add_physim_test(physim.5g.nr_pucchsim.test20 "Format 0 1-bit Ack miss 273 PRB" nr_pucchsim -R 273 -z8 -i 1 -P 0 -b 1 -s-2 -n3000)
add_physim_test(physim.5g.nr_pucchsim.test21 "Format 0 2-bit Ack miss 273 PRB" nr_pucchsim -R 273 -z8 -i 1 -P 0 -b 2 -s-2 -n3000)
add_physim_test(physim.5g.nr_pucchsim.test22 "Format 0 2-bit Ack miss+SR 273 PRB" nr_pucchsim -R 273 -z8 -i 1 -P 0 -b 2 -s-2 -c -n3000)
add_physim_test(physim.5g.nr_pucchsim.test23 "Format 2 4-bit 2/273 PRB" nr_pucchsim -R 273 -z8 -i 1 -P 2 -b 4 -s-8 -S0 -n3000)
add_physim_test(physim.5g.nr_pucchsim.test24 "Format 2 7-bit 2/273 PRB" nr_pucchsim -R 273 -z8 -i 1 -P 2 -b 7 -s3 -n3000)
add_physim_test(physim.5g.nr_pucchsim.test25 "Format 2 11-bit 2/273 PRB" nr_pucchsim -R 273 -z8 -i 1 -P 2 -b 11 -s6 -n3000 -C)
add_physim_test(physim.5g.nr_pucchsim.test26 "Format 2 12-bit 8/273 PRB" nr_pucchsim -R 273 -z8 -i 1 -P 2 -q8 -b 12 -s-3 -n3000 -C)
add_physim_test(physim.5g.nr_pucchsim.test27 "Format 2 19-bit 8/273 PRB" nr_pucchsim -R 273 -z8 -i 1 -P 2 -q8 -b 19 -s-3 -n3000)
add_physim_test(physim.5g.nr_pucchsim.test28 "Format 2 64-bit 16/273 PRB" nr_pucchsim -R 273 -z8 -i 1 -P 2 -q16 -b 64 -s-3 -n3000)
add_physim_test(physim.5g.nr_pucchsim.test29 "Format 2 64-bit 16/273 PRB Delay 2us" nr_pucchsim -R 273 -z8 -i 1 -P 2 -q16 -b 64 -s0 -S7 -d 2 -n3000)
add_physim_test(physim.5g.nr_pucchsim.test30 "Format 1 NACK number of symbols = 4 273PRB" nr_pucchsim -R 273 -z8 -i 4  -P 1 -b 1 -B 0 -s-8 -n3000)
add_physim_test(physim.5g.nr_pucchsim.test31 "Format 1 ACK number of symbols = 4 273PRB"  nr_pucchsim -R 273 -z8 -i 4  -P 1 -b 1 -B 1 -s-8 -n3000)
add_physim_test(physim.5g.nr_pucchsim.test32 "Format 1 (NACK,NACK) number of symbols = 4, 273PRB" nr_pucchsim -R 273 -z8 -i 4  -P 1 -b 2 -B 0 -s-8 -n1000)
add_physim_test(physim.5g.nr_pucchsim.test33 "Format 1 (ACK,NACK) number of symbols = 4, 273PRB with SR"  nr_pucchsim -R 273 -z8 -i 4  -P 1 -b 2 -B 1 -s-8 -c -n1000)
add_physim_test(physim.5g.nr_pucchsim.test34 "Format 1 (NACK,ACK) number of symbols = 4, 273PRB"  nr_pucchsim -R 273 -z8 -i 4  -P 1 -b 2 -B 2 -s-8 -n1000)
add_physim_test(physim.5g.nr_pucchsim.test35 "Format 1 (ACK,ACK) number of symbols = 4, 273PRB"   nr_pucchsim -R 273 -z8 -i 4  -P 1 -b 2 -B 3 -s-8 -n1000)
add_physim_test(physim.5g.nr_pucchsim.test36 "Format 1 NACK number of symbols = 8, 273PRB" nr_pucchsim -R 273 -z8 -i 8  -P 1 -b 1 -B 0 -s-8 -n1000)
add_physim_test(physim.5g.nr_pucchsim.test37 "Format 1 ACK number of symbols = 8, 273PRB"  nr_pucchsim -R 273 -z8 -i 8  -P 1 -b 1 -B 1 -s-8 -n1000)
add_physim_test(physim.5g.nr_pucchsim.test38 "Format 1 (NACK,NACK) number of symbols = 8, 273PRB" nr_pucchsim -R 273 -z8 -i 8  -P 1 -b 2 -B 0 -s-8 -n1000)
add_physim_test(physim.5g.nr_pucchsim.test39 "Format 1 (ACK,NACK) number of symbols = 8, 273PRB"  nr_pucchsim -R 273 -z8 -i 8  -P 1 -b 2 -B 1 -s-8 -n1000)
add_physim_test(physim.5g.nr_pucchsim.test40 "Format 1 (NACK,ACK) number of symbols = 8, 273PRB with SR"  nr_pucchsim -R 273 -z8 -i 8  -P 1 -b 2 -B 2 -s-8 -c -n1000)
add_physim_test(physim.5g.nr_pucchsim.test41 "Format 1 (ACK,ACK) number of symbols = 8, 273PRB"   nr_pucchsim -R 273 -z8 -i 8  -P 1 -b 2 -B 3 -s-8 -n1000)
add_physim_test(physim.5g.nr_pucchsim.test42 "Format 1 NACK number of symbols = 14, 273PRB" nr_pucchsim -R 273 -z8 -i 14 -P 1 -b 1 -B 0 -s-8 -n1000)
add_physim_test(physim.5g.nr_pucchsim.test43 "Format 1 ACK number of symbols = 14, 273PRB"  nr_pucchsim -R 273 -z8 -i 14 -P 1 -b 1 -B 1 -s-8 -n1000)
add_physim_test(physim.5g.nr_pucchsim.test44 "Format 1 (NACK,NACK) number of symbols = 14, 273PRB" nr_pucchsim -R 273 -z8 -i 14 -P 1 -b 2 -B 0 -s-8 -n1000)
add_physim_test(physim.5g.nr_pucchsim.test45 "Format 1 (ACK,NACK) number of symbols = 14, 273PRB"  nr_pucchsim -R 273 -z8 -i 14 -P 1 -b 2 -B 1 -s-8 -n1000)
add_physim_test(physim.5g.nr_pucchsim.test46 "Format 1 (NACK,ACK) number of symbols = 14, 273PRB with SR"  nr_pucchsim -R 273 -z8 -i 14 -P 1 -b 2 -B 2 -s-8 -c -n1000)
add_physim_test(physim.5g.nr_pucchsim.test47 "Format 1 (ACK,ACK) number of symbols = 14, 273PRB"   nr_pucchsim -R 273 -z8 -i 14 -P 1 -b 2 -B 3 -s-8 -n1000)
add_physim_test(physim.5g.nr_pucchsim.test48 "Format 2 13-bit 16/273 PRB" nr_pucchsim -R 273 -i 1 -P 2 -q 16 -b 13 -n300)

# Two-symbol format 0 without and with frequency hopping; a nonzero hopping ID gives a nonzero sequence group u[0].
add_physim_test(physim.5g.nr_pucchsim.test49 "Format 0 2-symbol non-hopping ACK and SR"
  nr_pucchsim -R 106 -N 40 -i 2 -P 0 -b 2 -c -s 10 -S 10 -n 100)
add_physim_test(physim.5g.nr_pucchsim.test50 "Format 0 2-symbol hopping"
  nr_pucchsim -R 106 -N 40 -i 2 -P 0 -b 2 -H -s 10 -S 10 -n 100)

####################################################################################
######                           nr_ulsim unit test                           ######
####################################################################################

add_physim_test(physim.5g.nr_ulsim.misc.test1 "MCS 9 106 PRBs" nr_ulsim -P -n300 -m9 -r106 -s5)
add_physim_test(physim.5g.nr_ulsim.misc.test2 "MCS 16 50 PRBs" nr_ulsim -P -n300 -m16 -s10)
add_physim_test(physim.5g.nr_ulsim.misc.test3 "MCS 28 50 PRBs" nr_ulsim -P -n300 -m28 -s20)
add_physim_test(physim.5g.nr_ulsim.misc.test4 "MCS 27 50 PRBs 256QAM" nr_ulsim -P -n300 -m27 -s25 -q1)
add_physim_test(physim.5g.nr_ulsim.misc.test5 "MCS 9 217 PRBs" nr_ulsim -P -n300 -m9 -R217 -r217 -s5)
add_physim_test(physim.5g.nr_ulsim.misc.test6 "MCS 9 273 PRBs" nr_ulsim -P -n300 -m9 -R273 -r273 -s5)
add_physim_test(physim.5g.nr_ulsim.misc.test7 "PUSCH Type A, 2 DMRS Symbols" nr_ulsim -P -n300 -s5 -U 0,1,1,1)
add_physim_test(physim.5g.nr_ulsim.misc.test8 "PUSCH Type A, 3 DMRS, 4 PTRS, 5 Interpolated Symbols" nr_ulsim -P -n300 -s5 -T 1,2 -U 0,2,1,1)
add_physim_test(physim.5g.nr_ulsim.misc.test9 "PUSCH Type B, 3 DMRS, 2 PTRS, 7 Interpolated Symbols" nr_ulsim -P -n300 -s5 -T 2,2 -U 1,2,1,1)
add_physim_test(physim.5g.nr_ulsim.misc.test10 "PUSCH Type B, 3 DMRS, 2 PTRS, 3 Interpolated Symbols" nr_ulsim -P -n300 -s5 -a4 -b8 -T 1,2 -U 1,3,1,1)
add_physim_test(physim.5g.nr_ulsim.misc.test11 "25 PRBs, 15 kHz SCS" nr_ulsim -P -n300 -u0 -m0 -R25 -r25 -i 1,0)
add_physim_test(physim.5g.nr_ulsim.misc.test12 "32 PRBs, 120 kHz SCS" nr_ulsim -P -n300 -s5 -r32 -R32 -u3)
add_physim_test(physim.5g.nr_ulsim.misc.test13 "MCS 0, low SNR performance" nr_ulsim -P -n300 -m0 -S -0.6 -i 1,0)
add_physim_test(physim.5g.nr_ulsim.misc.test14 "MCS 28, 106 PRBs, Time shift 8" nr_ulsim -P -n300 -m28 -R106 -r106 -t90 -s24 -S24 -d 8)
add_physim_test(physim.5g.nr_ulsim.misc.test15 "SRS, SNR 40 dB" nr_ulsim -P -n300 -s40 -E 1)
add_physim_test(physim.5g.nr_ulsim.sc-fdma.test1 "SC-FDMA, 50 PRBs" nr_ulsim -P -n300 -s5 -Z)
add_physim_test(physim.5g.nr_ulsim.sc-fdma.test2 "SC-FDMA, 75 PRBs" nr_ulsim -P -n300 -s5 -Z -r75)
add_physim_test(physim.5g.nr_ulsim.sc-fdma.test3 "SC-FDMA, 216 PRBs" nr_ulsim -P -n150 -s5 -Z -r216 -R217)
add_physim_test(physim.5g.nr_ulsim.sc-fdma.test4 "SC-FDMA, 273 PRBs" nr_ulsim -P -n150 -s5 -Z -r270 -R273)
add_physim_test(physim.5g.nr_ulsim.sc-fdma.test5 "SC-FDMA, 3 DMRS" nr_ulsim -P -n300 -s5 -Z -U 0,2,1,2)
add_physim_test(physim.5g.nr_ulsim.mimo.test1 "MCS 19 50 PRBs 2 RX_Antenna" nr_ulsim -P -n300 -m19 -s10 -S15 -z2)
add_physim_test(physim.5g.nr_ulsim.mimo.test2 "MCS 9 106 PRBs MIMO 2 layers" nr_ulsim -P -n300 -m9 -r106 -s8 -W2 -y2 -z2)
add_physim_test(physim.5g.nr_ulsim.mimo.test3 "MCS 10 106 PRBs MIMO 2 layers" nr_ulsim -P -n300 -m10 -r106 -s12 -W2 -y2 -z2)
add_physim_test(physim.5g.nr_ulsim.mimo.test4 "MCS 19 106 PRBs MIMO 2 layers" nr_ulsim -P -n300 -m19 -r106 -s22 -W2 -y2 -z2)
add_physim_test(physim.5g.nr_ulsim.mimo.test5 "MCS 9 106 PRBs MIMO 4 layers" nr_ulsim -P -n300 -m9 -r106 -s10 -W4 -y4 -z4)
add_physim_test(physim.5g.nr_ulsim.uci-pusch.test1 "MCS 9 106 PRBs 1 HARQ BIT" nr_ulsim -P -n300 -m9 -r106 -s10 -o 1,0,0)
add_physim_test(physim.5g.nr_ulsim.uci-pusch.test2 "MCS 16 51 PRBs 3 HARQ BITS" nr_ulsim -P -n300 -m16 -r51 -s15 -o 3,0,0)
add_physim_test(physim.5g.nr_ulsim.uci-pusch.test3 "MCS 27 106 PRBs 7 CSI BITS" nr_ulsim -P -n300 -m27 -r106 -s25 -o 0,7,0)
add_physim_test(physim.5g.nr_ulsim.uci-pusch.test4 "MCS 9 106 PRBs MIMO 2 layers 2 HARQ BITS 7 CSI BITS" nr_ulsim -P -n300 -m9 -r106 -s8 -W2 -y2 -z2 -o 2,7,0)
add_physim_test(physim.5g.nr_ulsim.3gpp.test1 "3GPP G-FR1-A5-13, PUSCH Type A, 40 MHz BW, 30 kHz SCS, 2 RX Antennas Requirements Test" nr_ulsim -P -n300 -b14 -I7 -i 0,1 -g A,l,10 -t70 -u 1 -m20 -R106 -r106 -U 0,1,1,2 -z2 -s12.4 -S12.4)
add_physim_test(physim.5g.nr_ulsim.3gpp.test2 "3GPP G-FR1-A5-13, PUSCH Type A, 40 MHz BW, 30 kHz SCS, 4 RX Antennas Requirements Test" nr_ulsim -P -n300 -b14 -I7 -i 0,1 -g A,l,10 -t70 -u 1 -m20 -R106 -r106 -U 0,1,1,2 -z4 -s8.5 -S8.5)
add_physim_test(physim.5g.nr_ulsim.3gpp.test3 "3GPP G-FR1-A5-13, PUSCH Type A, 40 MHz BW, 30 kHz SCS, 8 RX Antennas Requirements Test" nr_ulsim -P -n300 -b14 -I7 -i 0,1 -g A,l,10 -t70 -u 1 -m20 -R106 -r106 -U 0,1,1,2 -z8 -s5.4 -S5.4)
add_physim_test(physim.5g.nr_ulsim.3gpp.test4 "3GPP G-FR1-A5-8, PUSCH Type B, 5 MHz BW, 15 kHz SCS, 2 RX Antennas Requirements Test" nr_ulsim -P -n300 -b14 -I7 -i 0,1 -g A,l,10 -t70 -u 0 -m20 -R25 -r25 -U 1,1,1,2 -z2 -s12.5 -S12.5)
add_physim_test(physim.5g.nr_ulsim.3gpp.test5 "3GPP G-FR1-A5-8, PUSCH Type B, 5 MHz BW, 15 kHz SCS, 4 RX Antennas Requirements Test" nr_ulsim -P -n300 -b14 -I7 -i 0,1 -g A,l,10 -t70 -u 0 -m20 -R25 -r25 -U 1,1,1,2 -z4 -s8.9 -S8.9)
add_physim_test(physim.5g.nr_ulsim.3gpp.test6 "3GPP G-FR1-A5-8, PUSCH Type B, 5 MHz BW, 15 kHz SCS, 8 RX Antennas Requirements Test" nr_ulsim -P -n300 -b14 -I7 -i 0,1 -g A,l,10 -t70 -u 0 -m20 -R25 -r25 -U 1,1,1,2 -z8 -s5.7 -S5.7)
add_physim_test(physim.5g.nr_ulsim.3gpp.test7 "3GPP G-FR1-A5-9, PUSCH Type B, 10 MHz BW, 15 kHz SCS, 2 RX Antennas Requirements Test" nr_ulsim -P -n300 -b14 -I7 -i 0,1 -g A,l,10 -t70 -u 0 -m20 -R52 -r52 -U 1,1,1,2 -z2 -s12.6 -S12.6)
add_physim_test(physim.5g.nr_ulsim.3gpp.test8 "3GPP G-FR1-A5-9, PUSCH Type B, 10 MHz BW, 15 kHz SCS, 4 RX Antennas Requirements Test" nr_ulsim -P -n300 -b14 -I7 -i 0,1 -g A,l,10 -t70 -u 0 -m20 -R52 -r52 -U 1,1,1,2 -z4 -s8.9 -S8.9)
add_physim_test(physim.5g.nr_ulsim.3gpp.test9 "3GPP G-FR1-A5-9, PUSCH Type B, 10 MHz BW, 15 kHz SCS, 8 RX Antennas Requirements Test" nr_ulsim -P -n300 -b14 -I7 -i 0,1 -g A,l,10 -t70 -u 0 -m20 -R52 -r52 -U 1,1,1,2 -z8 -s5.8 -S5.8)
add_physim_test(physim.5g.nr_ulsim.3gpp.test10 "3GPP G-FR1-A5-10, PUSCH Type B, 20 MHz BW, 15 kHz SCS, 2 RX Antennas Requirements Test" nr_ulsim -P -n300 -b14 -I7 -i 0,1 -g A,l,10 -t70 -u 0 -m20 -R106 -r106 -U 1,1,1,2 -z2 -s12.3 -S12.3)
add_physim_test(physim.5g.nr_ulsim.3gpp.test11 "3GPP G-FR1-A5-10, PUSCH Type B, 20 MHz BW, 15 kHz SCS, 4 RX Antennas Requirements Test" nr_ulsim -P -n300 -b14 -I7 -i 0,1 -g A,l,10 -t70 -u 0 -m20 -R106 -r106 -U 1,1,1,2 -z4 -s8.8 -S8.8)
add_physim_test(physim.5g.nr_ulsim.3gpp.test12 "3GPP G-FR1-A5-10, PUSCH Type B, 20 MHz BW, 15 kHz SCS, 8 RX Antennas Requirements Test" nr_ulsim -P -n300 -b14 -I7 -i 0,1 -g A,l,10 -t70 -u 0 -m20 -R106 -r106 -U 1,1,1,2 -z8 -s5.7 -S5.7)
add_physim_test(physim.5g.nr_ulsim.3gpp.test13 "3GPP G-FR1-A5-11, PUSCH Type B, 10 MHz BW, 30 kHz SCS, 2 RX Antennas Requirements Test" nr_ulsim -P -n300 -b14 -I7 -i 0,1 -g A,l,10 -t70 -u 1 -m20 -R24 -r24 -U 1,1,1,2 -z2 -s12.5 -S12.5)
add_physim_test(physim.5g.nr_ulsim.3gpp.test14 "3GPP G-FR1-A5-11, PUSCH Type B, 10 MHz BW, 30 kHz SCS, 4 RX Antennas Requirements Test" nr_ulsim -P -n300 -b14 -I7 -i 0,1 -g A,l,10 -t70 -u 1 -m20 -R24 -r24 -U 1,1,1,2 -z4 -s8.6 -S8.6)
add_physim_test(physim.5g.nr_ulsim.3gpp.test15 "3GPP G-FR1-A5-11, PUSCH Type B, 10 MHz BW, 30 kHz SCS, 8 RX Antennas Requirements Test" nr_ulsim -P -n300 -b14 -I7 -i 0,1 -g A,l,10 -t70 -u 1 -m20 -R24 -r24 -U 1,1,1,2 -z8 -s5.6 -S5.6)
add_physim_test(physim.5g.nr_ulsim.3gpp.test16 "3GPP G-FR1-A5-12, PUSCH Type B, 20 MHz BW, 30 kHz SCS, 2 RX Antennas Requirements Test" nr_ulsim -P -n300 -b14 -I7 -i 0,1 -g A,l,10 -t70 -u 1 -m20 -R51 -r51 -U 1,1,1,2 -z2 -s12.5 -S12.5)
add_physim_test(physim.5g.nr_ulsim.3gpp.test17 "3GPP G-FR1-A5-12, PUSCH Type B, 20 MHz BW, 30 kHz SCS, 4 RX Antennas Requirements Test" nr_ulsim -P -n300 -b14 -I7 -i 0,1 -g A,l,10 -t70 -u 1 -m20 -R51 -r51 -U 1,1,1,2 -z4 -s8.6 -S8.6)
add_physim_test(physim.5g.nr_ulsim.3gpp.test18 "3GPP G-FR1-A5-12, PUSCH Type B, 20 MHz BW, 30 kHz SCS, 8 RX Antennas Requirements Test" nr_ulsim -P -n300 -b14 -I7 -i 0,1 -g A,l,10 -t70 -u 1 -m20 -R51 -r51 -U 1,1,1,2 -z8 -s5.6 -S5.6)
add_physim_test(physim.5g.nr_ulsim.3gpp.test19 "3GPP G-FR1-A5-13, PUSCH Type B, 40 MHz BW, 30 kHz SCS, 2 RX Antennas Requirements Test" nr_ulsim -P -n300 -b14 -I7 -i 0,1 -g A,l,10 -t70 -u 1 -m20 -R106 -r106 -U 1,1,1,2 -z2 -s12.5 -S12.5)
add_physim_test(physim.5g.nr_ulsim.3gpp.test20 "3GPP G-FR1-A5-13, PUSCH Type B, 40 MHz BW, 30 kHz SCS, 4 RX Antennas Requirements Test" nr_ulsim -P -n300 -b14 -I7 -i 0,1 -g A,l,10 -t70 -u 1 -m20 -R106 -r106 -U 1,1,1,2 -z4 -s8.7 -S8.7)
add_physim_test(physim.5g.nr_ulsim.3gpp.test21 "3GPP G-FR1-A5-13, PUSCH Type B, 40 MHz BW, 30 kHz SCS, 8 RX Antennas Requirements Test" nr_ulsim -P -n300 -b14 -I7 -i 0,1 -g A,l,10 -t70 -u 1 -m20 -R106 -r106 -U 1,1,1,2 -z8 -s5.5 -S5.5)
add_physim_test(physim.5g.nr_ulsim.3gpp.test22 "3GPP G-FR1-A5-14, PUSCH Type B, 100 MHz BW, 30 kHz SCS, 2 RX Antennas Requirements Test" nr_ulsim -P -n300 -b14 -I7 -i 0,1 -g A,l,10 -t70 -u 1 -m20 -R273 -r273 -U 1,1,1,2 -z2 -s13.1 -S13.1)
add_physim_test(physim.5g.nr_ulsim.3gpp.test23 "3GPP G-FR1-A5-14, PUSCH Type B, 100 MHz BW, 30 kHz SCS, 4 RX Antennas Requirements Test" nr_ulsim -P -n300 -b14 -I7 -i 0,1 -g A,l,10 -t70 -u 1 -m20 -R273 -r273 -U 1,1,1,2 -z4 -s9.2 -S9.2)
add_physim_test(physim.5g.nr_ulsim.3gpp.test24 "3GPP G-FR1-A5-14, PUSCH Type B, 100 MHz BW, 30 kHz SCS, 8 RX Antennas Requirements Test" nr_ulsim -P -n300 -b14 -I8 -i 0,1 -g A,l,10 -t70 -u 1 -m20 -R273 -r273 -U 1,1,1,2 -z8 -s5.9 -S5.9)
add_physim_test(physim.5g.nr_ulsim.3gpp.test25 "3GPP G-FR1-A3-27, PUSCH Type B, 40 MHz BW, 30 kHz SCS, 2 RX Antennas Requirements Test, 2 layers" nr_ulsim -P -n300 -b14 -I15 -i 0,1 -g B,l -t70 -u 1 -m2 -R106 -r106 -U 1,1,1,2 -W2 -y2 -z2 -s1.7 -S1.7)
add_physim_test(physim.5g.nr_ulsim.3gpp.test26 "3GPP G-FR1-A3-27, PUSCH Type B, 40 MHz BW, 30 kHz SCS, 4 RX Antennas Requirements Test, 2 layers" nr_ulsim -P -n300 -b14 -I15 -i 0,1 -g B,l -t70 -u 1 -m2 -R106 -r106 -U 1,1,1,2 -W2 -y2 -z4 -s-2.1 -S-2.1)
add_physim_test(physim.5g.nr_ulsim.3gpp.test27 "3GPP G-FR1-A4-27, PUSCH Type B, 40 MHz BW, 30 kHz SCS, 2 RX Antennas Requirements Test, 2 layers" nr_ulsim -P -n300 -b14 -I15 -i 0,1 -g C,l -t70 -u 1 -m16 -R106 -r106 -U 1,1,1,2 -W2 -y2 -z2 -s18.7 -S18.7)
add_physim_test(physim.5g.nr_ulsim.3gpp.test28 "3GPP G-FR1-A4-27, PUSCH Type B, 40 MHz BW, 30 kHz SCS, 4 RX Antennas Requirements Test, 2 layers" nr_ulsim -P -n300 -b14 -I15 -i 0,1 -g C,l -t70 -u 1 -m16 -R106 -r106 -U 1,1,1,2 -W2 -y2 -z4 -s11.2 -S11.2)

####################################################################################
######                           nr_ulsim_mu_mimo unit test                   ######
####################################################################################

add_physim_test(physim.5g.nr_ulsim_mu_mimo.test1 "UEs 2 MCS 2 PRBs 106 SNR 1.7" nr_ulsim_mu_mimo -P -n300 -b14 -I15 -i 0,1 -g B,l -t90 -u 1 -m2 -R106 -r106 -U 1,1,1,2 -W1 -y1 -z2 -s1.7 -S1.7 -N2)
add_physim_test(physim.5g.nr_ulsim_mu_mimo.test2 "UEs 2 MCS 2 PRBs 106 SNR -2.1" nr_ulsim_mu_mimo -P -n300 -b14 -I15 -i 0,1 -g B,l -t90 -u 1 -m2 -R106 -r106 -U 1,1,1,2 -W1 -y1 -z4 -s-2.1 -S-2.1 -N2)
add_physim_test(physim.5g.nr_ulsim_mu_mimo.test3 "UEs 2 MCS 16 PRBs 217 SNR 18.7" nr_ulsim_mu_mimo -P -n300 -b14 -I15 -i 0,1 -g C,l -t90 -u 1 -m16 -R217 -r217 -U 1,1,1,2 -W1 -y1 -z2 -s18.7 -S18.7 -N2)
add_physim_test(physim.5g.nr_ulsim_mu_mimo.test4 "UEs 2 MCS 16 PRBs 273 SNR 11.2" nr_ulsim_mu_mimo -P -n300 -b14 -I15 -i 0,1 -g C,l -t90 -u 1 -m16 -R273 -r273 -U 1,1,1,2 -W1 -y1 -z4 -s11.2 -S11.2 -N2)
add_physim_test(physim.5g.nr_ulsim_mu_mimo.test5 "UEs 2 MCS 27 PRBs 106 SNR 30 QAM 256 " nr_ulsim_mu_mimo -P -n300 -b14 -I15 -i 0,1 -g C,l -t65 -u 1 -m27 -q1 -R106 -r106 -U 1,1,1,2 -W1 -y1 -z4 -s30 -S30 -N2)


####################################################################################
######                           nr_prachsim unit test                        ######
####################################################################################

add_physim_test(physim.5g.nr_prachsim.test1 "30kHz SCS, 106 PRBs, Prach format A2" nr_prachsim -a -s -30 -n 300 -p 63 -R 106)
add_physim_test(physim.5g.nr_prachsim.test2 "30kHz SCS, 217 PRBs, Prach format A2" nr_prachsim -a -s -30 -n 300 -p 63 -R 217)
add_physim_test(physim.5g.nr_prachsim.test3 "30kHz SCS, 273 PRBs, Prach format A2" nr_prachsim -a -s -30 -n 300 -p 63 -R 273)
add_physim_test(physim.5g.nr_prachsim.test4 "30kHz SCS, 106 PRBs, Prach format 0" nr_prachsim -a -s -30 -n 300 -p 63 -R 106 -c 4)
add_physim_test(physim.5g.nr_prachsim.test5 "120kHz SCS, 32 PRBs, Prach format A2" nr_prachsim -a -s -30 -n 300 -p 32 -R 32 -m 3 -c52)
add_physim_test(physim.5g.nr_prachsim.test6 "120kHz SCS, 66 PRBs, Prach format A2" nr_prachsim -a -s -30 -n 300 -p 32 -R 66 -m 3 -c52)
add_physim_test(physim.5g.nr_prachsim.test7 "120kHz SCS, 66 PRBs, High Speed Enabled" nr_prachsim -a -s -30 -n 300 -R 66 -m 3 -c52 -H)
add_physim_test(physim.5g.nr_prachsim.test8 "15kHz SCS, 25 PRBs" nr_prachsim -a -s -30 -n 300 -p 99 -R 25 -m 0)
add_physim_test(physim.5g.nr_prachsim.test9 "30kHz SCS, 106 PRBs, Prach format A2, NCS_config 0" nr_prachsim -a -s -30 -n 300 -p 63 -R 106 -Z 0)

####################################################################################
######                           nr_psbchsim unit test                        ######
####################################################################################

add_physim_test(physim.5g.nr_psbchsim.test1 "SLSS Search" nr_psbchsim -I)
add_physim_test(physim.5g.nr_psbchsim.test2 "PSBCH TxRx" nr_psbchsim -n 30)

####################################################################################
######                           nr_srssim unit test                          ######
####################################################################################

add_physim_test(physim.5g.nr_srssim.test1 "SRS: 30KHz SCS, 106 PRBs, ToA 24, Combsize 2" nr_srssim -n300 -R106 -s0 -S5 -y2 -z2 -d 24)
add_physim_test(physim.5g.nr_srssim.test2 "SRS: 30KHz SCS, 106 PRBs, ToA 15, Combsize 2, Threequarter sampling" nr_srssim -n300 -R106 -s0 -S5 -y2 -z2 -d 15 -k)
add_physim_test(physim.5g.nr_srssim.test3 "SRS: 30KHz SCS, 217 PRBs, ToA 8, Combsize 2" nr_srssim -n300 -R217 -s0 -S5 -y2 -z2 -d 8)
add_physim_test(physim.5g.nr_srssim.test4 "SRS: 30KHz SCS, 273 PRBs, TOA 30, Combsize 2" nr_srssim -n300 -R273 -s0 -S5 -y2 -z2 -d 30)
add_physim_test(physim.5g.nr_srssim.test5 "SRS: 30KHz SCS, 106 PRBs, ToA 16, Combsize 2, Cyclic shift 3" nr_srssim -n300 -R106 -s0 -S5 -y2 -z2 -d 16 -i 1)
add_physim_test(physim.5g.nr_srssim.test6 "SRS: 30KHz SCS, 106 PRBs, ToA 23, Combsize 2, Comb offset 1" nr_srssim -n300 -R106 -s0 -S5 -y2 -z2 -d 23 -f 1)
add_physim_test(physim.5g.nr_srssim.test7 "SRS: 30KHz SCS, 106 PRBs, ToA 8, Combsize 2, Symbol start 9" nr_srssim -n300 -R106 -s0 -S5 -y2 -z2 -d 8 -a 9)
add_physim_test(physim.5g.nr_srssim.test8 "SRS: 120KHz SCS, 32 PRBs, ToA 19, Combsize 2" nr_srssim -n300 -R32 -s0 -S5 -y2 -z2 -d 19 -u 3)
add_physim_test(physim.5g.nr_srssim.test9 "SRS: 30KHz SCS, 106 PRBs, ToA 11, Combsize 4" nr_srssim -n300 -R106 -s5 -S10 -y2 -z2 -d 11 -e 1)
add_physim_test(physim.5g.nr_srssim.test10 "SRS: 30KHz SCS, 106 PRBs, ToA 7, Combsize 4, Comb offset 1" nr_srssim -n300 -R106 -s5 -S10 -y2 -z2 -d 7 -e 1 -f 1)
add_physim_test(physim.5g.nr_srssim.test11 "SRS: 30KHz SCS, 106 PRBs, ToA 24, Combsize 2, SRS symbols 2, SRS start symbol 8" nr_srssim -n300 -R106 -s0 -S5 -y2 -z2 -d 24 -b 2 -a 8)
add_physim_test(physim.5g.nr_srssim.test12 "SRS: 30KHz SCS, 106 PRBs, ToA 24, Combsize 2, SRS symbols 4, SRS start symbol 8" nr_srssim -n300 -R106 -s0 -S5 -y2 -z2 -d 24 -b 4 -a 8)
add_physim_test(physim.5g.nr_srssim.test13 "SRS: 30KHz SCS, 106 PRBs, ToA 24, Combsize 4, SRS symbols 2, SRS start symbol 8" nr_srssim -n300 -R106 -s5 -S10 -y2 -z2 -d 24 -b 2 -e1 -a 8)
add_physim_test(physim.5g.nr_srssim.test14 "SRS: 30KHz SCS, 106 PRBs, ToA 24, Combsize 4, SRS symbols 4, SRS start symbol 8" nr_srssim -n300 -R106 -s5 -S10 -y2 -z2 -d 24 -b 4 -e1 -a 8)
add_physim_test(physim.5g.nr_srssim.test15 "SRS: 30KHz SCS, 217 PRBs, ToA 8, Combsize 2, SRS symbols 4, SRS start symbol 8" nr_srssim -n300 -R217 -s0 -S5 -y2 -z2 -d 8 -b 4 -a 8)
add_physim_test(physim.5g.nr_srssim.test16 "SRS: 30KHz SCS, 106 PRBs, SNR 2 dB to 30 dB in steps of 5 dB, ToA 24, Combsize 2" nr_srssim -n300 -R106 -s2 -S30 -y2 -z2 -d 24)

####################################################################################
######                           nr_prssim unit test                          ######
####################################################################################

add_physim_test(physim.5g.nr_prssim.test1 "PRS: 30KHz SCS, 106 PRBs, Combsize 4, 4 symbols" nr_prssim -n300 -R106 -s0 -S5 -z2)
add_physim_test(physim.5g.nr_prssim.test2 "PRS: 30KHz SCS, 106 PRBs, Combsize 2, 2 symbols" nr_prssim -n300 -R106 -s0 -S5 -z2 -e 2 -b 2)
add_physim_test(physim.5g.nr_prssim.test3 "PRS: 30KHz SCS, 106 PRBs, Combsize 4, Threequarter sampling" nr_prssim -n300 -R106 -s0 -S5 -z2 -k)
add_physim_test(physim.5g.nr_prssim.test4 "PRS: 30KHz SCS, 273 PRBs, Combsize 4" nr_prssim -n300 -R273 -s0 -S5 -z2)
add_physim_test(physim.5g.nr_prssim.test5 "PRS: 15KHz SCS, 52 PRBs, Combsize 4" nr_prssim -n300 -u0 -R52 -s0 -S5 -z2)
add_physim_test(physim.5g.nr_prssim.test6 "PRS: 120KHz SCS, 32 PRBs, Combsize 4" nr_prssim -n300 -u3 -R32 -s0 -S5 -z2)
add_physim_test(physim.5g.nr_prssim.test7 "PRS: 30KHz SCS, 106 PRBs, Combsize 4, RE offset 3, PRS id 123, start symbol 2" nr_prssim -n300 -R106 -s0 -S5 -z2 -f 3 -N 123 -a 2)
add_physim_test(physim.5g.nr_prssim.test8 "PRS: 30KHz SCS, 106 PRBs, Combsize 4, 12 symbols, start symbol 1" nr_prssim -n300 -R106 -s0 -S5 -z2 -b 12 -a 1)
add_physim_test(physim.5g.nr_prssim.test9 "PRS: 30KHz SCS, 106 PRBs, Combsize 2, 12 symbols, start symbol 1" nr_prssim -n300 -R106 -s0 -S5 -z2 -e 2 -b 12 -a 1)
add_physim_test(physim.5g.nr_prssim.test10 "PRS: 30KHz SCS, 106 PRBs (PRS on 48 PRBs from PRB 20), Combsize 4" nr_prssim -n300 -R106 -s0 -S5 -z2 -r 48 -o 20)
add_physim_test(physim.5g.nr_prssim.test11 "PRS: 30KHz SCS, 106 PRBs, 2 gNBs with random delays" nr_prssim -n300 -R106 -s0 -S5 -z2 -G 2)
add_physim_test(physim.5g.nr_prssim.test12 "PRS: 120KHz SCS, 66 PRBs, 8 PRS resources in 8 slots, 6 symbols from symbol 8 (FR2)" nr_prssim -n300 -u3 -R66 -s0 -S5 -z2 -H 0 -a 8 -b 6 -M 8)
add_physim_test(physim.5g.nr_prssim.test13 "PRS: 30KHz SCS, 106 PRBs, 4 gNBs with the same RE offset, random delays" nr_prssim -n300 -R106 -s0 -S5 -z2 -G 4 -c)

####################################################################################
######                           offload-specific tests                       ######
####################################################################################

if (ENABLE_LDPC_AAL)

add_physim_test(physim.5g-offload.nr_dlsim.testCPU.1 "CPU: SNR = 30, MCS = 5, 106 PRBs, 1 layer"             nr_dlsim -n1000 -s30 -S30.2 -e5 -b106 -R106 -X 20,21,22,23,24 -P)
add_physim_test(physim.5g-offload.nr_dlsim.testT2.1  "T2 LDPC offload: SNR = 30, MCS = 5, 106 PRBs, 1 layer" nr_dlsim -n1000 -s30 -S30.2 -e5 -b106 -R106 -X 20,21,22,23,24 -P --loader.ldpc.shlibversion _aal --nrLDPC_coding_aal.dpdk_dev af:00.0 --nrLDPC_coding_aal.dpdk_core_list 18-19 --nrLDPC_coding_aal.is_t2 1)
add_physim_test(physim.5g-offload.nr_dlsim.testCPU.2 "CPU: SNR = 30, MCS = 15, 106 PRBs, 1 layer"             nr_dlsim -n1000 -s30 -S30.2 -e15 -b106 -R106 -X 20,21,22,23,24 -P)
add_physim_test(physim.5g-offload.nr_dlsim.testT2.2  "T2 LDPC offload: SNR = 30, MCS = 15, 106 PRBs, 1 layer" nr_dlsim -n1000 -s30 -S30.2 -e15 -b106 -R106 -X 20,21,22,23,24 -P --loader.ldpc.shlibversion _aal --nrLDPC_coding_aal.dpdk_dev af:00.0 --nrLDPC_coding_aal.dpdk_core_list 18-19 --nrLDPC_coding_aal.is_t2 1)
add_physim_test(physim.5g-offload.nr_dlsim.testCPU.3 "CPU: SNR = 30, MCS = 25, 106 PRBs, 1 layer"             nr_dlsim -n1000 -s30 -S30.2 -e25 -b106 -R106 -X 20,21,22,23,24 -P)
add_physim_test(physim.5g-offload.nr_dlsim.testT2.3  "T2 LDPC offload: SNR = 30, MCS = 25, 106 PRBs, 1 layer" nr_dlsim -n1000 -s30 -S30.2 -e25 -b106 -R106 -X 20,21,22,23,24 -P --loader.ldpc.shlibversion _aal --nrLDPC_coding_aal.dpdk_dev af:00.0 --nrLDPC_coding_aal.dpdk_core_list 18-19 --nrLDPC_coding_aal.is_t2 1)
add_physim_test(physim.5g-offload.nr_dlsim.testCPU.4 "CPU: SNR = 30, MCS = 5, 273 PRBs, 1 layer"             nr_dlsim -n1000 -s30 -S30.2 -e5 -b273 -R273 -X 20,21,22,23,24 -P)
add_physim_test(physim.5g-offload.nr_dlsim.testT2.4  "T2 LDPC offload: SNR = 30, MCS = 5, 273 PRBs, 1 layer" nr_dlsim -n1000 -s30 -S30.2 -e5 -b273 -R273 -X 20,21,22,23,24 -P --loader.ldpc.shlibversion _aal --nrLDPC_coding_aal.dpdk_dev af:00.0 --nrLDPC_coding_aal.dpdk_core_list 18-19 --nrLDPC_coding_aal.is_t2 1)
add_physim_test(physim.5g-offload.nr_dlsim.testCPU.5 "CPU: SNR = 30, MCS = 15, 273 PRBs, 1 layer"             nr_dlsim -n1000 -s30 -S30.2 -e15 -b273 -R273 -X 20,21,22,23,24 -P)
add_physim_test(physim.5g-offload.nr_dlsim.testT2.5  "T2 LDPC offload: SNR = 30, MCS = 15, 273 PRBs, 1 layer" nr_dlsim -n1000 -s30 -S30.2 -e15 -b273 -R273 -X 20,21,22,23,24 -P --loader.ldpc.shlibversion _aal --nrLDPC_coding_aal.dpdk_dev af:00.0 --nrLDPC_coding_aal.dpdk_core_list 18-19 --nrLDPC_coding_aal.is_t2 1)
add_physim_test(physim.5g-offload.nr_dlsim.testCPU.6 "CPU: SNR = 30, MCS = 25, 273 PRBs, 1 layer"             nr_dlsim -n1000 -s30 -S30.2 -e25 -b273 -R273 -X 20,21,22,23,24 -P)
add_physim_test(physim.5g-offload.nr_dlsim.testT2.6  "T2 LDPC offload: SNR = 30, MCS = 25, 273 PRBs, 1 layer" nr_dlsim -n1000 -s30 -S30.2 -e25 -b273 -R273 -X 20,21,22,23,24 -P --loader.ldpc.shlibversion _aal --nrLDPC_coding_aal.dpdk_dev af:00.0 --nrLDPC_coding_aal.dpdk_core_list 18-19 --nrLDPC_coding_aal.is_t2 1)
add_physim_test(physim.5g-offload.nr_dlsim.testCPU.7 "CPU: SNR = 30, MCS = 5, 273 PRBs, 2 layers"             nr_dlsim -n1000 -s30 -S30.2 -e5 -b273 -R273 -X 20,21,22,23,24 -x2 -z2 -y2 -P)
add_physim_test(physim.5g-offload.nr_dlsim.testT2.7  "T2 LDPC offload: SNR = 30, MCS = 5, 273 PRBs, 2 layers" nr_dlsim -n1000 -s30 -S30.2 -e5 -b273 -R273 -X 20,21,22,23,24 -x2 -z2 -y2 -P --loader.ldpc.shlibversion _aal --nrLDPC_coding_aal.dpdk_dev af:00.0 --nrLDPC_coding_aal.dpdk_core_list 18-19 --nrLDPC_coding_aal.is_t2 1)
add_physim_test(physim.5g-offload.nr_dlsim.testCPU.8 "CPU: SNR = 30, MCS = 15, 273 PRBs, 2 layers"             nr_dlsim -n1000 -s30 -S30.2 -e15 -b273 -R273 -X 20,21,22,23,24 -x2 -z2 -y2 -P)
add_physim_test(physim.5g-offload.nr_dlsim.testT2.8  "T2 LDPC offload: SNR = 30, MCS = 15, 273 PRBs, 2 layers" nr_dlsim -n1000 -s30 -S30.2 -e15 -b273 -R273 -X 20,21,22,23,24 -x2 -z2 -y2 -P --loader.ldpc.shlibversion _aal --nrLDPC_coding_aal.dpdk_dev af:00.0 --nrLDPC_coding_aal.dpdk_core_list 18-19 --nrLDPC_coding_aal.is_t2 1)
add_physim_test(physim.5g-offload.nr_dlsim.testCPU.9 "CPU: SNR = 30, MCS = 25, 273 PRBs, 2 layers"             nr_dlsim -n1000 -s30 -S30.2 -e25 -b273 -R273 -X 20,21,22,23,24 -x2 -z2 -y2 -P)
add_physim_test(physim.5g-offload.nr_dlsim.testT2.9  "T2 LDPC offload: SNR = 30, MCS = 25, 273 PRBs, 2 layers" nr_dlsim -n1000 -s30 -S30.2 -e25 -b273 -R273 -X 20,21,22,23,24 -x2 -z2 -y2 -P --loader.ldpc.shlibversion _aal --nrLDPC_coding_aal.dpdk_dev af:00.0 --nrLDPC_coding_aal.dpdk_core_list 18-19 --nrLDPC_coding_aal.is_t2 1)
add_physim_test(physim.5g-offload.nr_dlsim.testCPU.10 "CPU: SNR = 30, MCS = 5, 273 PRBs, 2 layers, 4 antennas"             nr_dlsim -n1000 -s30 -S30.2 -e5 -b273 -R273 -X8,9,10,11,12 -x2 -z4 -y4 -P)
add_physim_test(physim.5g-offload.nr_dlsim.testT2.10  "T2 LDPC offload: SNR = 30, MCS = 5, 273 PRBs, 2 layers, 4 antennas" nr_dlsim -n1000 -s30 -S30.2 -e5 -b273 -R273 -X 20,21,22,23,24 -x2 -z4 -y4 -P --loader.ldpc.shlibversion _aal --nrLDPC_coding_aal.dpdk_dev af:00.0 --nrLDPC_coding_aal.dpdk_core_list 18-19 --nrLDPC_coding_aal.is_t2 1)
add_physim_test(physim.5g-offload.nr_dlsim.testCPU.11 "CPU: SNR = 30, MCS = 20, 273 PRBs, 2 layers, 4 antennas"             nr_dlsim -n1000 -s30 -S30.2 -e15 -b273 -R273 -X8,9,10,11,12 -x2 -z4 -y4 -P)
add_physim_test(physim.5g-offload.nr_dlsim.testT2.11  "T2 LDPC offload: SNR = 30, MCS = 20, 273 PRBs, 2 layers, 4 antennas" nr_dlsim -n1000 -s30 -S30.2 -e15 -b273 -R273 -X 20,21,22,23,24 -x2 -z4 -y4 -P --loader.ldpc.shlibversion _aal --nrLDPC_coding_aal.dpdk_dev af:00.0 --nrLDPC_coding_aal.dpdk_core_list 18-19 --nrLDPC_coding_aal.is_t2 1)
add_physim_test(physim.5g-offload.nr_dlsim.testCPU.12 "CPU: SNR = 30, MCS = 27, 273 PRBs, 2 layers, 4 antennas"             nr_dlsim -n1000 -s30 -S30.2 -e25 -b273 -R273 -X8,9,10,11,12 -x2 -z4 -y4 -P)
add_physim_test(physim.5g-offload.nr_dlsim.testT2.12  "T2 LDPC offload: SNR = 30, MCS = 27, 273 PRBs, 2 layers, 4 antennas" nr_dlsim -n1000 -s30 -S30.2 -e25 -b273 -R273 -X 20,21,22,23,24 -x2 -z4 -y4 -P --loader.ldpc.shlibversion _aal --nrLDPC_coding_aal.dpdk_dev af:00.0 --nrLDPC_coding_aal.dpdk_core_list 18-19 --nrLDPC_coding_aal.is_t2 1)

add_physim_test(physim.5g-offload.nr_ulsim.testCPU.1 "CPU: SNR = 30, MCS = 5, 106 PRBs, 1 layer"             nr_ulsim -n1000 -s30 -S30.2 -m5 -r106 -R106 -C10 -P)
add_physim_test(physim.5g-offload.nr_ulsim.testT2.1  "T2 LDPC offload: SNR = 30, MCS = 5, 106 PRBs, 1 layer" nr_ulsim -n1000 -s30 -S30.2 -m5 -r106 -R106 -C10 -P --loader.ldpc.shlibversion _aal --nrLDPC_coding_aal.dpdk_dev af:00.0 --nrLDPC_coding_aal.dpdk_core_list 18-19 --nrLDPC_coding_aal.is_t2 1)
add_physim_test(physim.5g-offload.nr_ulsim.testCPU.2 "CPU: SNR = 30, MCS = 15, 106 PRBs, 1 layer"             nr_ulsim -n1000 -s30 -S30.2 -m15 -r106 -R106 -C10 -P)
add_physim_test(physim.5g-offload.nr_ulsim.testT2.2  "T2 LDPC offload: SNR = 30, MCS = 15, 106 PRBs, 1 layer" nr_ulsim -n1000 -s30 -S30.2 -m15 -r106 -R106 -C10 -P --loader.ldpc.shlibversion _aal --nrLDPC_coding_aal.dpdk_dev af:00.0 --nrLDPC_coding_aal.dpdk_core_list 18-19 --nrLDPC_coding_aal.is_t2 1)
add_physim_test(physim.5g-offload.nr_ulsim.testCPU.3 "CPU: SNR = 30, MCS = 25, 106 PRBs, 1 layer"             nr_ulsim -n1000 -s30 -S30.2 -m25 -r106 -R106 -C10 -P)
add_physim_test(physim.5g-offload.nr_ulsim.testT2.3  "T2 LDPC offload: SNR = 30, MCS = 25, 106 PRBs, 1 layer" nr_ulsim -n1000 -s30 -S30.2 -m25 -r106 -R106 -C10 -P --loader.ldpc.shlibversion _aal --nrLDPC_coding_aal.dpdk_dev af:00.0 --nrLDPC_coding_aal.dpdk_core_list 18-19 --nrLDPC_coding_aal.is_t2 1)
add_physim_test(physim.5g-offload.nr_ulsim.testCPU.4 "CPU: SNR = 30, MCS = 5, 273 PRBs, 1 layer"             nr_ulsim -n1000 -s30 -S30.2 -m5 -r273 -R273 -C10 -P)
add_physim_test(physim.5g-offload.nr_ulsim.testT2.4  "T2 LDPC offload: SNR = 30, MCS = 5, 273 PRBs, 1 layer" nr_ulsim -n1000 -s30 -S30.2 -m5 -r273 -R273 -C10 -P --loader.ldpc.shlibversion _aal --nrLDPC_coding_aal.dpdk_dev af:00.0 --nrLDPC_coding_aal.dpdk_core_list 18-19 --nrLDPC_coding_aal.is_t2 1)
add_physim_test(physim.5g-offload.nr_ulsim.testCPU.5 "CPU: SNR = 30, MCS = 15, 273 PRBs, 1 layer"             nr_ulsim -n1000 -s30 -S30.2 -m15 -r273 -R273 -C10 -P)
add_physim_test(physim.5g-offload.nr_ulsim.testT2.5  "T2 LDPC offload: SNR = 30, MCS = 15, 273 PRBs, 1 layer" nr_ulsim -n1000 -s30 -S30.2 -m15 -r273 -R273 -C10 -P --loader.ldpc.shlibversion _aal --nrLDPC_coding_aal.dpdk_dev af:00.0 --nrLDPC_coding_aal.dpdk_core_list 18-19 --nrLDPC_coding_aal.is_t2 1)
add_physim_test(physim.5g-offload.nr_ulsim.testCPU.6 "CPU: SNR = 30, MCS = 25, 273 PRBs, 1 layer"             nr_ulsim -n1000 -s30 -S30.2 -m25 -r273 -R273 -C10 -P)
add_physim_test(physim.5g-offload.nr_ulsim.testT2.6  "T2 LDPC offload: SNR = 30, MCS = 25, 273 PRBs, 1 layer" nr_ulsim -n1000 -s30 -S30.2 -m25 -r273 -R273 -C10 -P --loader.ldpc.shlibversion _aal --nrLDPC_coding_aal.dpdk_dev af:00.0 --nrLDPC_coding_aal.dpdk_core_list 18-19 --nrLDPC_coding_aal.is_t2 1)
add_physim_test(physim.5g-offload.nr_ulsim.testCPU.7 "CPU: SNR = 30, MCS = 5, 273 PRBs, 2 layers"             nr_ulsim -n1000 -s30 -S30.2 -m5 -r273 -R273 -C10 -W2 -z2 -y2 -P)
add_physim_test(physim.5g-offload.nr_ulsim.testT2.7  "T2 LDPC offload: SNR = 30, MCS = 5, 273 PRBs, 2 layers" nr_ulsim -n1000 -s30 -S30.2 -m5 -r273 -R273 -C10 -W2 -z2 -y2 -P --loader.ldpc.shlibversion _aal --nrLDPC_coding_aal.dpdk_dev af:00.0 --nrLDPC_coding_aal.dpdk_core_list 18-19 --nrLDPC_coding_aal.is_t2 1)
add_physim_test(physim.5g-offload.nr_ulsim.testCPU.8 "CPU: SNR = 30, MCS = 15, 273 PRBs, 2 layers"             nr_ulsim -n1000 -s30 -S30.2 -m15 -r273 -R273 -C10 -W2 -z2 -y2 -P)
add_physim_test(physim.5g-offload.nr_ulsim.testT2.8  "T2 LDPC offload: SNR = 30, MCS = 15, 273 PRBs, 2 layers" nr_ulsim -n1000 -s30 -S30.2 -m15 -r273 -R273 -C10 -W2 -z2 -y2 -P --loader.ldpc.shlibversion _aal --nrLDPC_coding_aal.dpdk_dev af:00.0 --nrLDPC_coding_aal.dpdk_core_list 18-19 --nrLDPC_coding_aal.is_t2 1)
add_physim_test(physim.5g-offload.nr_ulsim.testCPU.9 "CPU: SNR = 30, MCS = 25, 273 PRBs, 2 layers"             nr_ulsim -n1000 -s30 -S30.2 -m25 -r273 -R273 -C10 -W2 -z2 -y2 -P)
add_physim_test(physim.5g-offload.nr_ulsim.testT2.9  "T2 LDPC offload: SNR = 30, MCS = 25, 273 PRBs, 2 layers" nr_ulsim -n1000 -s30 -S30.2 -m25 -r273 -R273 -C10 -W2 -z2 -y2 -P --loader.ldpc.shlibversion _aal --nrLDPC_coding_aal.dpdk_dev af:00.0 --nrLDPC_coding_aal.dpdk_core_list 18-19 --nrLDPC_coding_aal.is_t2 1)

endif()

if(ENABLE_LDPC_ORS)

add_physim_test(physim.5g-offload.nr_ulsim.testAurora.1  "Aurora LDPC offload: SNR = 30, MCS = 5, 106 PRBs, 1 layer" nr_ulsim -n1000 -s30 -S30.2 -m5 -r106 -R106 -P -C10 --loader.ldpc.shlibversion _ors)
add_physim_test(physim.5g-offload.nr_ulsim.testAurora.2  "Aurora LDPC offload: SNR = 30, MCS = 15, 106 PRBs, 1 layer" nr_ulsim -n1000 -s30 -S30.2 -m15 -r106 -R106 -P -C10 --loader.ldpc.shlibversion _ors)
add_physim_test(physim.5g-offload.nr_ulsim.testAurora.3  "Aurora LDPC offload: SNR = 30, MCS = 25, 106 PRBs, 1 layer" nr_ulsim -n1000 -s30 -S30.2 -m25 -r106 -R106 -P -C10 --loader.ldpc.shlibversion _ors)
add_physim_test(physim.5g-offload.nr_ulsim.testAurora.4  "Aurora LDPC offload: SNR = 30, MCS = 5, 273 PRBs, 1 layer" nr_ulsim -n1000 -s30 -S30.2 -m5 -r273 -R273 -P -C10 --loader.ldpc.shlibversion _ors)
add_physim_test(physim.5g-offload.nr_ulsim.testAurora.5  "Aurora LDPC offload: SNR = 30, MCS = 15, 273 PRBs, 1 layer" nr_ulsim -n1000 -s30 -S30.2 -m15 -r273 -R273 -P -C10 --loader.ldpc.shlibversion _ors)
add_physim_test(physim.5g-offload.nr_ulsim.testAurora.6  "Aurora LDPC offload: SNR = 30, MCS = 25, 273 PRBs, 1 layer" nr_ulsim -n1000 -s30 -S30.2 -m25 -r273 -R273 -P -C10 --loader.ldpc.shlibversion _ors)
add_physim_test(physim.5g-offload.nr_ulsim.testAurora.7  "Aurora LDPC offload: SNR = 30, MCS = 5, 273 PRBs, 2 layers" nr_ulsim -n1000 -s30 -S30.2 -m5 -r273 -R273 -W2 -z2 -y2 -P -C10 --loader.ldpc.shlibversion _ors)
add_physim_test(physim.5g-offload.nr_ulsim.testAurora.8  "Aurora LDPC offload: SNR = 30, MCS = 15, 273 PRBs, 2 layers" nr_ulsim -n1000 -s30 -S30.2 -m15 -r273 -R273 -W2 -z2 -y2 -P -C10 --loader.ldpc.shlibversion _ors)
add_physim_test(physim.5g-offload.nr_ulsim.testAurora.9  "Aurora LDPC offload: SNR = 30, MCS = 25, 273 PRBs, 2 layers" nr_ulsim -n1000 -s30 -S30.2 -m25 -r273 -R273 -W2 -z2 -y2 -P -C10 --loader.ldpc.shlibversion _ors)

endif()

####################################################################################
######                           cuda-specific tests                       ######
####################################################################################

if (ENABLE_CHANNEL_SIM_CUDA)

# --- Test Suite 1: 61440 samples, 2/4/8/32 TX - 2/4/8/32 RX  ---
add_physim_test(cuda.5g.channelsim.test_channel_scalability.test1  "16 channels 61440 samples 2T2R Batch"  test_channel_scalability -c 16 -t 2 -r 2 -s 61440 -l 32 -m batch  -n 12)
add_physim_test(cuda.5g.channelsim.test_channel_scalability.test2  "16 channels 61440 samples 2T2R Stream" test_channel_scalability -c 16 -t 2 -r 2 -s 61440 -l 32 -m stream -n 12)
add_physim_test(cuda.5g.channelsim.test_channel_scalability.test3  "16 channels 61440 samples 2T2R Serial" test_channel_scalability -c 16 -t 2 -r 2 -s 61440 -l 32 -m serial -n 12)
add_physim_test(cuda.5g.channelsim.test_channel_scalability.test4  "16 channels 61440 samples 4T2R Batch"  test_channel_scalability -c 16 -t 4 -r 2 -s 61440 -l 32 -m batch  -n 12)
add_physim_test(cuda.5g.channelsim.test_channel_scalability.test5  "16 channels 61440 samples 4T2R Stream" test_channel_scalability -c 16 -t 4 -r 2 -s 61440 -l 32 -m stream -n 12)
add_physim_test(cuda.5g.channelsim.test_channel_scalability.test6  "16 channels 61440 samples 4T2R Serial" test_channel_scalability -c 16 -t 4 -r 2 -s 61440 -l 32 -m serial -n 12)
add_physim_test(cuda.5g.channelsim.test_channel_scalability.test7  "16 channels 61440 samples 8T2R Batch"  test_channel_scalability -c 16 -t 8 -r 2 -s 61440 -l 32 -m batch  -n 12)
add_physim_test(cuda.5g.channelsim.test_channel_scalability.test8  "16 channels 61440 samples 8T2R Stream" test_channel_scalability -c 16 -t 8 -r 2 -s 61440 -l 32 -m stream -n 12)
add_physim_test(cuda.5g.channelsim.test_channel_scalability.test9  "16 channels 61440 samples 8T2R Serial" test_channel_scalability -c 16 -t 8 -r 2 -s 61440 -l 32 -m serial -n 12)
add_physim_test(cuda.5g.channelsim.test_channel_scalability.test10  "16 channels 61440 samples 32T2R Batch"  test_channel_scalability -c 16 -t 32 -r 2 -s 61440 -l 32 -m batch  -n 12)
add_physim_test(cuda.5g.channelsim.test_channel_scalability.test11  "16 channels 61440 samples 32T2R Stream" test_channel_scalability -c 16 -t 32 -r 2 -s 61440 -l 32 -m stream -n 12)
add_physim_test(cuda.5g.channelsim.test_channel_scalability.test12  "16 channels 61440 samples 32T2R Serial" test_channel_scalability -c 16 -t 32 -r 2 -s 61440 -l 32 -m serial -n 12)
add_physim_test(cuda.5g.channelsim.test_channel_scalability.test13  "16 channels 61440 samples 2T4R Batch"  test_channel_scalability -c 16 -t 2 -r 4 -s 61440 -l 32 -m batch  -n 12)
add_physim_test(cuda.5g.channelsim.test_channel_scalability.test14  "16 channels 61440 samples 2T4R Stream" test_channel_scalability -c 16 -t 2 -r 4 -s 61440 -l 32 -m stream -n 12)
add_physim_test(cuda.5g.channelsim.test_channel_scalability.test15  "16 channels 61440 samples 2T4R Serial" test_channel_scalability -c 16 -t 2 -r 4 -s 61440 -l 32 -m serial -n 12)
add_physim_test(cuda.5g.channelsim.test_channel_scalability.test16  "16 channels 61440 samples 2T8R Batch"  test_channel_scalability -c 16 -t 2 -r 8 -s 61440 -l 32 -m batch  -n 12)
add_physim_test(cuda.5g.channelsim.test_channel_scalability.test17  "16 channels 61440 samples 2T8R Stream" test_channel_scalability -c 16 -t 2 -r 8 -s 61440 -l 32 -m stream -n 12)
add_physim_test(cuda.5g.channelsim.test_channel_scalability.test18  "16 channels 61440 samples 2T8R Serial" test_channel_scalability -c 16 -t 2 -r 8 -s 61440 -l 32 -m serial -n 12)
add_physim_test(cuda.5g.channelsim.test_channel_scalability.test19  "16 channels 61440 samples 2T32R Batch"  test_channel_scalability -c 16 -t 2 -r 32 -s 61440 -l 32 -m batch  -n 12)
add_physim_test(cuda.5g.channelsim.test_channel_scalability.test20  "16 channels 61440 samples 2T32R Stream" test_channel_scalability -c 16 -t 2 -r 32 -s 61440 -l 32 -m stream -n 12)
add_physim_test(cuda.5g.channelsim.test_channel_scalability.test21  "16 channels 61440 samples 2T32R Serial" test_channel_scalability -c 16 -t 2 -r 32 -s 61440 -l 32 -m serial -n 12)

# --- Test Suite 2: 122880 samples, 2/4/8/32 TX - 2/4/8/32 RX  ---
add_physim_test(cuda.5g.channelsim.test_channel_scalability.test22  "16 channels 122880 samples 2T2R Batch"  test_channel_scalability -c 16 -t 2 -r 2 -s 122880 -l 32 -m batch  -n 12)
add_physim_test(cuda.5g.channelsim.test_channel_scalability.test23  "16 channels 122880 samples 2T2R Stream" test_channel_scalability -c 16 -t 2 -r 2 -s 122880 -l 32 -m stream -n 12)
add_physim_test(cuda.5g.channelsim.test_channel_scalability.test24  "16 channels 122880 samples 2T2R Serial" test_channel_scalability -c 16 -t 2 -r 2 -s 122880 -l 32 -m serial -n 12)
add_physim_test(cuda.5g.channelsim.test_channel_scalability.test25  "16 channels 122880 samples 4T2R Batch"  test_channel_scalability -c 16 -t 4 -r 2 -s 122880 -l 32 -m batch  -n 12)
add_physim_test(cuda.5g.channelsim.test_channel_scalability.test26  "16 channels 122880 samples 4T2R Stream" test_channel_scalability -c 16 -t 4 -r 2 -s 122880 -l 32 -m stream -n 12)
add_physim_test(cuda.5g.channelsim.test_channel_scalability.test27  "16 channels 122880 samples 4T2R Serial" test_channel_scalability -c 16 -t 4 -r 2 -s 122880 -l 32 -m serial -n 12)
add_physim_test(cuda.5g.channelsim.test_channel_scalability.test28  "16 channels 122880 samples 8T2R Batch"  test_channel_scalability -c 16 -t 8 -r 2 -s 122880 -l 32 -m batch  -n 12)
add_physim_test(cuda.5g.channelsim.test_channel_scalability.test29  "16 channels 122880 samples 8T2R Stream" test_channel_scalability -c 16 -t 8 -r 2 -s 122880 -l 32 -m stream -n 12)
add_physim_test(cuda.5g.channelsim.test_channel_scalability.test30  "16 channels 122880 samples 8T2R Serial" test_channel_scalability -c 16 -t 8 -r 2 -s 122880 -l 32 -m serial -n 12)
add_physim_test(cuda.5g.channelsim.test_channel_scalability.test31  "16 channels 122880 samples 32T2R Batch"  test_channel_scalability -c 16 -t 32 -r 2 -s 122880 -l 32 -m batch  -n 12)
add_physim_test(cuda.5g.channelsim.test_channel_scalability.test32  "16 channels 122880 samples 32T2R Stream" test_channel_scalability -c 16 -t 32 -r 2 -s 122880 -l 32 -m stream -n 12)
add_physim_test(cuda.5g.channelsim.test_channel_scalability.test33  "16 channels 122880 samples 32T2R Serial" test_channel_scalability -c 16 -t 32 -r 2 -s 122880 -l 32 -m serial -n 12)
add_physim_test(cuda.5g.channelsim.test_channel_scalability.test34  "16 channels 122880 samples 2T4R Batch"  test_channel_scalability -c 16 -t 2 -r 4 -s 122880 -l 32 -m batch  -n 12)
add_physim_test(cuda.5g.channelsim.test_channel_scalability.test35  "16 channels 122880 samples 2T4R Stream" test_channel_scalability -c 16 -t 2 -r 4 -s 122880 -l 32 -m stream -n 12)
add_physim_test(cuda.5g.channelsim.test_channel_scalability.test36  "16 channels 122880 samples 2T4R Serial" test_channel_scalability -c 16 -t 2 -r 4 -s 122880 -l 32 -m serial -n 12)
add_physim_test(cuda.5g.channelsim.test_channel_scalability.test37  "16 channels 122880 samples 2T8R Batch"  test_channel_scalability -c 16 -t 2 -r 8 -s 122880 -l 32 -m batch  -n 12)
add_physim_test(cuda.5g.channelsim.test_channel_scalability.test38  "16 channels 122880 samples 2T8R Stream" test_channel_scalability -c 16 -t 2 -r 8 -s 122880 -l 32 -m stream -n 12)
add_physim_test(cuda.5g.channelsim.test_channel_scalability.test39  "16 channels 122880 samples 2T8R Serial" test_channel_scalability -c 16 -t 2 -r 8 -s 122880 -l 32 -m serial -n 12)
add_physim_test(cuda.5g.channelsim.test_channel_scalability.test40  "16 channels 122880 samples 2T32R Batch"  test_channel_scalability -c 16 -t 2 -r 32 -s 122880 -l 32 -m batch  -n 12)
add_physim_test(cuda.5g.channelsim.test_channel_scalability.test41  "16 channels 122880 samples 2T32R Stream" test_channel_scalability -c 16 -t 2 -r 32 -s 122880 -l 32 -m stream -n 12)
add_physim_test(cuda.5g.channelsim.test_channel_scalability.test42  "16 channels 122880 samples 2T32R Serial" test_channel_scalability -c 16 -t 2 -r 32 -s 122880 -l 32 -m serial -n 12)

# --- Test Suite 3: 61440 samples, 128 channel length, 2/4/8/32 TX - 2/4/8/32 RX --
add_physim_test(cuda.5g.channelsim.test_channel_scalability.test43  "16 channels 61440 samples long channel 2T2R Batch"  test_channel_scalability -c 16 -t 2 -r 2 -s 61440 -l 128 -m batch  -n 12)
add_physim_test(cuda.5g.channelsim.test_channel_scalability.test44  "16 channels 61440 samples long channel 2T2R Stream" test_channel_scalability -c 16 -t 2 -r 2 -s 61440 -l 128 -m stream -n 12)
add_physim_test(cuda.5g.channelsim.test_channel_scalability.test45  "16 channels 61440 samples long channel 2T2R Serial" test_channel_scalability -c 16 -t 2 -r 2 -s 61440 -l 128 -m serial -n 12)
add_physim_test(cuda.5g.channelsim.test_channel_scalability.test46  "16 channels 61440 samples long channel 4T2R Batch"  test_channel_scalability -c 16 -t 4 -r 2 -s 61440 -l 128 -m batch  -n 12)
add_physim_test(cuda.5g.channelsim.test_channel_scalability.test47  "16 channels 61440 samples long channel 4T2R Stream" test_channel_scalability -c 16 -t 4 -r 2 -s 61440 -l 128 -m stream -n 12)
add_physim_test(cuda.5g.channelsim.test_channel_scalability.test48  "16 channels 61440 samples long channel 4T2R Serial" test_channel_scalability -c 16 -t 4 -r 2 -s 61440 -l 128 -m serial -n 12)
add_physim_test(cuda.5g.channelsim.test_channel_scalability.test49  "16 channels 61440 samples long channel 8T2R Batch"  test_channel_scalability -c 16 -t 8 -r 2 -s 61440 -l 128 -m batch  -n 12)
add_physim_test(cuda.5g.channelsim.test_channel_scalability.test50  "16 channels 61440 samples long channel 8T2R Stream" test_channel_scalability -c 16 -t 8 -r 2 -s 61440 -l 128 -m stream -n 12)
add_physim_test(cuda.5g.channelsim.test_channel_scalability.test51  "16 channels 61440 samples long channel 8T2R Serial" test_channel_scalability -c 16 -t 8 -r 2 -s 61440 -l 128 -m serial -n 12)
add_physim_test(cuda.5g.channelsim.test_channel_scalability.test52  "16 channels 61440 samples long channel 2T4R Batch"  test_channel_scalability -c 16 -t 2 -r 4 -s 61440 -l 128 -m batch  -n 12)
add_physim_test(cuda.5g.channelsim.test_channel_scalability.test53  "16 channels 61440 samples long channel 2T4R Stream" test_channel_scalability -c 16 -t 2 -r 4 -s 61440 -l 128 -m stream -n 12)
add_physim_test(cuda.5g.channelsim.test_channel_scalability.test54  "16 channels 61440 samples long channel 2T4R Serial" test_channel_scalability -c 16 -t 2 -r 4 -s 61440 -l 128 -m serial -n 12)
add_physim_test(cuda.5g.channelsim.test_channel_scalability.test55  "16 channels 61440 samples long channel 2T8R Batch"  test_channel_scalability -c 16 -t 2 -r 8 -s 61440 -l 128 -m batch  -n 12)
add_physim_test(cuda.5g.channelsim.test_channel_scalability.test56  "16 channels 61440 samples long channel 2T8R Stream" test_channel_scalability -c 16 -t 2 -r 8 -s 61440 -l 128 -m stream -n 12)
add_physim_test(cuda.5g.channelsim.test_channel_scalability.test57  "16 channels 61440 samples long channel 2T8R Serial" test_channel_scalability -c 16 -t 2 -r 8 -s 61440 -l 128 -m serial -n 12)

add_physim_test(cuda.5g.physims.nr_dlsim.test1 "SNR = 30, MCS = 25, 106 PRBs, 1 layer, TDL-A channel, GPU channel simulation"               nr_dlsim -n500 -s30 -S30.2 -e25 -b106 -R273 -X 8,9,10,11,12 -P -g A -t 85 -f cuda)
add_physim_test(cuda.5g.physims.nr_dlsim.test2 "SNR = 30, MCS = 25, 106 PRBs, 2 layers, TDL-A channel, GPU channel simulation"              nr_dlsim -n500 -s30 -S30.2 -e25 -b106 -R273 -X 8,9,10,11,12 -P -g A -t 49 -f cuda -x2 -z2 -y2)
add_physim_test(cuda.5g.physims.nr_dlsim.test3 "SNR = 30, MCS = 25, 106 PRBs, 2 layers, 4 antennas, TDL-A channel, GPU channel simulation"  nr_dlsim -n500 -s30 -S30.2 -e25 -b106 -R273 -X 8,9,10,11,12 -P -g A -t 95 -f cuda -x2 -z4 -y4)
add_physim_test(cuda.5g.physims.nr_dlsim.test4 "SNR = 30, MCS = 25, 273 PRBs, 2 layers, TDL-A channel, GPU channel simulation"              nr_dlsim -n500 -s30 -S30.2 -e25 -b273 -R273 -X 8,9,10,11,12 -P -g A -t 32 -f cuda -x2 -z2 -y2)
add_physim_test(cuda.5g.physims.nr_dlsim.test5 "SNR = 30, MCS = 25, 273 PRBs, 2 layers, 4 antennas, TDL-A channel, GPU channel simulation"  nr_dlsim -n500 -s30 -S30.2 -e25 -b273 -R273 -X 8,9,10,11,12 -P -g A -t 93 -f cuda -x2 -z4 -y4)
add_physim_test(cuda.5g.physims.nr_dlsim.test6 "SNR = 30, MCS = 25, 106 PRBs, 2 layers, TDL-B channel, GPU channel simulation"              nr_dlsim -n500 -s30 -S30.2 -e25 -b106 -R273 -X 8,9,10,11,12 -P -g B -t 43 -f cuda -x2 -z2 -y2)
add_physim_test(cuda.5g.physims.nr_dlsim.test7 "SNR = 30, MCS = 25, 106 PRBs, 2 layers, 4 antennas, TDL-B channel, GPU channel simulation"  nr_dlsim -n500 -s30 -S30.2 -e25 -b106 -R273 -X 8,9,10,11,12 -P -g B -t 95 -f cuda -x2 -z4 -y4)
add_physim_test(cuda.5g.physims.nr_dlsim.test8 "SNR = 30, MCS = 25, 273 PRBs, 2 layers, TDL-B channel, GPU channel simulation"              nr_dlsim -n500 -s30 -S30.2 -e25 -b273 -R273 -X 8,9,10,11,12 -P -g B -t 32 -f cuda -x2 -z2 -y2)
add_physim_test(cuda.5g.physims.nr_dlsim.test9 "SNR = 30, MCS = 25, 273 PRBs, 2 layers, 4 antennas, TDL-B channel, GPU channel simulation"  nr_dlsim -n500 -s30 -S30.2 -e25 -b273 -R273 -X 8,9,10,11,12 -P -g B -t 94 -f cuda -x2 -z4 -y4)

add_physim_test(cuda.5g.physims.nr_ulsim.test1 "SNR = 30, MCS = 25, 106 PRBs, 1 layer, TDL-A channel, GPU channel simulation"              nr_ulsim -n500 -s30 -S30 -m25 -r106 -R106 -C10 -P -g A -t 84 -f cuda)
add_physim_test(cuda.5g.physims.nr_ulsim.test2 "SNR = 30, MCS = 25, 106 PRBs, 2 layers, TDL-A channel, GPU channel simulation"             nr_ulsim -n500 -s30 -S30 -m25 -r106 -R106 -C10 -P -g A -t 90 -f cuda -W2 -z2 -y2)
add_physim_test(cuda.5g.physims.nr_ulsim.test3 "SNR = 30, MCS = 25, 273 PRBs, 2 layers, TDL-A channel, GPU channel simulation"             nr_ulsim -n500 -s30 -S30 -m25 -r273 -R273 -C10 -P -g A -t 83 -f cuda -W2 -z2 -y2)
add_physim_test(cuda.5g.physims.nr_ulsim.test4 "SNR = 30, MCS = 25, 106 PRBs, 2 layers, TDL-B channel, GPU channel simulation"             nr_ulsim -n500 -s30 -S30 -m25 -r106 -R106 -C10 -P -g B -t 78 -f cuda -W2 -z2 -y2)
add_physim_test(cuda.5g.physims.nr_ulsim.test5 "SNR = 30, MCS = 25, 273 PRBs, 2 layers, TDL-B channel, GPU channel simulation"             nr_ulsim -n500 -s30 -S30 -m25 -r273 -R273 -C10 -P -g B -t 63 -f cuda -W2 -z2 -y2)

endif()

if(PHYSIM_CHECK_FILES)
  foreach(PHYSIM_CHECK_FILE IN LISTS PHYSIM_CHECK_FILES)
    include("${CMAKE_CURRENT_SOURCE_DIR}/${PHYSIM_CHECK_FILE}")
  endforeach()
endif()

####################################################################################
######   "quick_physim" subset: fast, representative cases across each        ######
######   physim executable, meant to run in well under 3 minutes total        ######
####################################################################################

if(ENABLE_PHYSIM_TESTS)
  set(QUICK_PHYSIM_TESTS
    physim.5g.nr_pbchsim.106rb.test1
    physim.5g.nr_pbchsim.217rb.test1
    physim.5g.nr_pbchsim.otherSCS.test1
    physim.5g.nr_pbchsim.otherSCS.test2
    physim.5g.nr_dlsim.basic.test1
    physim.5g.nr_dlsim.basic.test2
    physim.5g.nr_dlsim.basic.test4
    physim.5g.nr_dlsim.offset.test1
    physim.5g.nr_dlsim.mcs.mimo.test1
    physim.5g.nr_dlsim.mcs.mimo.test6
    physim.5g.nr_dlsim.dmrs.ptrs.test1
    physim.5g.nr_dlschsim.test1
    physim.5g.nr_dlschsim.test2
    physim.5g.nr_dlschsim.test3
    physim.5g.nr_ulsim.misc.test1
    physim.5g.nr_ulsim.misc.test2
    physim.5g.nr_ulsim.misc.test5
    physim.5g.nr_ulsim.sc-fdma.test1
    physim.5g.nr_ulsim.mimo.test2
    physim.5g.nr_ulschsim.test1
    physim.5g.nr_ulschsim.test2
    physim.5g.nr_ulschsim.test4
    physim.5g.nr_pucchsim.test1
    physim.5g.nr_pucchsim.test49
    physim.5g.nr_pucchsim.test50
    physim.5g.nr_pucchsim.test2
    physim.5g.nr_pucchsim.test4
    physim.5g.nr_pucchsim.test5
    physim.5g.ldpctest.test1
    physim.5g.ldpctest.test5
    physim.5g.ldpctest.test11
    physim.5g.polartest.test1
    physim.5g.polartest.test2
    physim.5g.polartest.test3
    physim.5g.polartest.test4
    physim.5g.smallblocktest.test1
    physim.5g.smallblocktest.test4
  )
  foreach(QUICK_TEST IN LISTS QUICK_PHYSIM_TESTS)
    if(TEST ${QUICK_TEST})
      set_property(TEST ${QUICK_TEST} APPEND PROPERTY LABELS "quick_physim")
    endif()
  endforeach()
endif()
