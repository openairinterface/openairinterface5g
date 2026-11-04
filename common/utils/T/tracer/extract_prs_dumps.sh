#!/bin/bash
# SPDX-License-Identifier: LicenseRef-CSSL-1.0

: ${1?"Usage: $0 [-g num_gnb] [-n num_rsc] [-f filename] [-c count]"}

# Usage info
show_help()
{
echo "
    Usage of script with arguments: [-g num_gnb] [-n num_rsc] [-f filename] [-c count]

    -g num_gnb    Number of active gNBs
    -n num_rsc    Number of PRS resources
    -f filename   T tracer recorded .raw filename
    -c count      Number of dump instances to be extacted

    For Example:  ./extract_prs_dumps.sh -g 1 -n 8 -f fr2_64_prbs.raw -c 100

    -h            Help
"
exit 0
}

while getopts g:n:f:c:h flag
do
   case "${flag}" in
      g) num_gnb=${OPTARG};;
      n) num_rsc=${OPTARG};;
      f) file=${OPTARG};;
      c) count=${OPTARG};;
      h) show_help;;
      *) exit -1;;
   esac
done
echo "num_gnb: $num_gnb";
echo "num_rsc: $num_rsc";
echo "filename: $file";
echo "count: $count";

for (( i = 0; i < $num_gnb; i++ ))
do
   for (( j = 0; j < $num_rsc; j++ ))
   do
      name=chF_gnb${i}_${j}.raw
      echo "Extracting $name"
      ./extract -d ../T_messages.txt $file UE_PHY_PRS_CHANNEL_ESTIMATE chestF_t -f gNB_ID $i -f rsc_id $j -o $name -count $count
      name=chT_gnb${i}_${j}.raw
      echo "Extracting $name"
      ./extract -d ../T_messages.txt $file UE_PHY_PRS_CHANNEL_ESTIMATE chest_t -f gNB_ID $i -f rsc_id $j -o $name -count $count
   done
done

# zip the extracted dumps
name=prs_dumps.tgz
tar cvzf $name chF_gnb* chT_gnb*
echo "created a zip file $name"
