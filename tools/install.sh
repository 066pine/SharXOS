#!/bin/bash
set -x
export DEBIAN_FRONTEND=noninteractive
apt-get update -y 2>&1 | tail -5
apt-get install -y --no-install-recommends build-essential nasm xorriso mtools grub-pc-bin grub-common qemu-system-x86 gcc-multilib g++-multilib 2>&1 | tail -25
