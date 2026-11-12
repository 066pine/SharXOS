#!/bin/bash
set -e
mkdir -p /opt/cross
cd /tmp
curl -4 -L --retry 3 --max-time 1800 -o i686-elf-tools-linux.zip \
  https://github.com/lordmilko/i686-elf-tools/releases/download/15.2.0/i686-elf-tools-linux.zip
ls -l i686-elf-tools-linux.zip
rm -rf /opt/cross/i686-elf
mkdir -p /opt/cross/i686-elf
unzip -q -o i686-elf-tools-linux.zip -d /opt/cross/i686-elf
find /opt/cross/i686-elf -maxdepth 3 -name 'i686-elf-g++' -o -maxdepth 3 -name 'i686-elf-gcc' | head
echo "CROSS_INSTALL_DONE"
