#!/bin/bash
set -e
export DEBIAN_FRONTEND=noninteractive
apt-get install -y --no-install-recommends unzip >/dev/null 2>&1 || apt-get install -y unzip
rm -rf /opt/cross/i686-elf
mkdir -p /opt/cross/i686-elf
cd /tmp
unzip -q -o i686-elf-tools-linux.zip -d /opt/cross/i686-elf
echo "== layout =="
find /opt/cross/i686-elf -maxdepth 3 -name 'i686-elf-g++' -o -maxdepth 3 -name 'i686-elf-gcc' | head
