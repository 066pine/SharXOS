#!/bin/bash
echo "== os =="; tail -3 /etc/os-release
echo "== tools =="
for t in gcc g++ cc ld make nasm qemu-system-i386 grub-mkrescue grub-file xorriso mtools i686-elf-gcc i686-elf-g++ i686-elf-ld i686-elf-as; do
  printf '%-18s ' "$t"
  command -v "$t" || echo "MISSING"
done
echo "== versions =="
gcc --version 2>/dev/null | head -1
ld --version 2>/dev/null | head -1
nasm -v 2>/dev/null
qemu-system-i386 --version 2>/dev/null | head -1
echo "== sudo =="
sudo -n true 2>/dev/null && echo "passwordless sudo OK" || echo "sudo needs password"
echo "== dpkg =="
dpkg -l 2>/dev/null | grep -E 'nasm|qemu-system-x86|grub-pc-bin|grub-common|xorriso|mtools' | awk '{print $1, $2, $3}'
