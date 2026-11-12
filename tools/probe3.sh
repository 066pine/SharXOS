#!/bin/bash
echo "== apt cache debs =="
ls -1 /var/cache/apt/archives/*.deb 2>/dev/null | head -40 || echo none
echo "== proxy env =="
env | grep -i proxy || echo "no proxy env"
echo "== other toolchains =="
ls /opt 2>/dev/null; ls /usr/local/bin 2>/dev/null | head -20
echo "== network test =="
timeout 5 curl -sI http://archive.ubuntu.com/ubuntu/ 2>&1 | head -3 || echo "curl failed/absent"
timeout 5 getent hosts archive.ubuntu.com || echo "dns fail"
echo "== resolv =="
cat /etc/resolv.conf 2>/dev/null | head -10
echo "== apt sources =="
grep -rh '^[^#]' /etc/apt/sources.list /etc/apt/sources.list.d/ 2>/dev/null | head -10
