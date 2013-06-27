#!/bin/sh

# Wrap the U-Boot configuration script with a header, making it suitable
# for TFTP transfer and subsequent execution for initial environment setup.
../../../tools/mkimage -T script -C none -n "U-Boot Env Script" -d ./ub.config.scr ./ub.config.img

