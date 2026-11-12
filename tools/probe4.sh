#!/bin/bash
echo "== ipv4 dns =="
getent ahostsv4 archive.ubuntu.com | head -5
echo "== curl v4 =="
timeout 15 curl -4 -sS -o /dev/null -w '%{http_code}\n' http://archive.ubuntu.com/ubuntu/dists/noble/Release 2>&1 | head -3
echo "== curl https =="
timeout 15 curl -4 -sS -o /dev/null -w '%{http_code}\n' https://archive.ubuntu.com/ubuntu/dists/noble/Release 2>&1 | head -3
echo "== win host ip =="
ip route | head -5
