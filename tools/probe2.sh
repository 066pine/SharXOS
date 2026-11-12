#!/bin/bash
echo "whoami: $(whoami)"
echo "== apt root test =="
wslcmd=""
id -u
echo "== try sudo -u root =="
su -s /bin/bash root -c 'whoami' 2>&1 | head -3
