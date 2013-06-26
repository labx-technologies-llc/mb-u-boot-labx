#!/bin/sh

usage ()
{
  echo "Usage : $0 <size in KiB> <outfile>"
}

if [ $# != 2 ]
then
  usage
  exit
fi

# Simple script for creating an "empty" file suitable for mapping
# an N-KiB file into the MTD Flash bridge's address space.  This
# is useful, for example, for mimicking an empty U-Boot environment
tr "\000" "\377" < /dev/zero | dd ibs=1k count=$1 of=$2