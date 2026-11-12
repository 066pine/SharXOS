#!/bin/bash
set -e
export DEBIAN_FRONTEND=noninteractive
sed -i 's|http://archive.ubuntu.com|https://archive.ubuntu.com|g; s|http://security.ubuntu.com|https://security.ubuntu.com|g' /etc/apt/sources.list.d/ubuntu.sources /etc/apt/sources.list 2>/dev/null || true
grep -h 'URIs' /etc/apt/sources.list.d/ubuntu.sources
apt-get -o Acquire::ForceIPv4=true update
apt-get install -y --no-install-recommends build-essential nasm xorriso mtools grub-pc-bin grub-common qemu-system-x86
echo "=== DONE ==="
for t in gcc g++ nasm make qemu-system-i386 grub-mkrescue xorriso; do printf '%-18s ' "$t"; command -v "$t" || echo MISSING; done
