#!/usr/bin/env python3
import json, os, subprocess, sys
gpak, idx, ebin, outdir = sys.argv[1:5]
targets = ['swfs/ui.swf', 'swfs/static_objects.swf', 'swfs/house.swf']
idxd = json.load(open(idx)); off = 824693
os.makedirs(outdir, exist_ok=True)
found = {}
with open(gpak, 'rb') as f:
    for name, size in idxd:
        if name in targets:
            f.seek(off); data = f.read(size)
            p = os.path.join(outdir, os.path.basename(name))
            open(p, 'wb').write(data); found[name] = p
        off += size
for name, p in found.items():
    sub = os.path.join(outdir, os.path.basename(name).replace('.swf', ''))
    os.makedirs(sub, exist_ok=True)
    r = subprocess.run([ebin, p, sub, '0.6', '24'], capture_output=True, text=True, timeout=600)
    print(name, (r.stdout or r.stderr).strip().splitlines()[-1] if (r.stdout or r.stderr).strip() else 'no output')
