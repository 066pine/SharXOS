#!/bin/bash
set -e
for f in /opt/cross/i686-elf/bin/*; do
  ln -sf "$f" "/usr/local/bin/$(basename "$f")"
done
ls -l /usr/local/bin/i686-elf-g++ /usr/local/bin/i686-elf-ld
i686-elf-g++ --version | head -2
i686-elf-ld --version | head -1
