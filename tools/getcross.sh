#!/bin/bash
set -x
cd /tmp
echo "== try lordmilko prebuilt =="
curl -4 -sSL --max-time 60 -o rel.json https://api.github.com/repos/lordmilko/i686-elf-tools/releases/latest
head -c 600 rel.json
echo
python3 - <<'EOF'
import json
try:
    d=json.load(open('/tmp/rel.json'))
    for a in d.get('assets',[]):
        print(a['name'], a['browser_download_url'], a.get('size'))
except Exception as e:
    print("ERR", e)
EOF
