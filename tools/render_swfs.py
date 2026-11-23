#!/usr/bin/env python3
import json, os, subprocess, sys
gpak, idx, rbin, outdir = sys.argv[1:5]
targets = [('swfs/ui.swf', [0, 60]), ('swfs/events.swf', [0]), ('swfs/npcs.swf', [0]),
           ('swfs/house.swf', [0]), ('swfs/backgrounds.swf', [0])]
idxd = json.load(open(idx))
off = 824693
os.makedirs(outdir, exist_ok=True)
found = {}
with open(gpak, 'rb') as f:
    for name, size in idxd:
        for t, frames in targets:
            if name == t:
                f.seek(off); data = f.read(size)
                p = os.path.join(outdir, os.path.basename(t))
                open(p, 'wb').write(data)
                found[t] = (p, frames)
        off += size
for t, (p, frames) in found.items():
    for fr in frames:
        out = os.path.join(outdir, f'{os.path.basename(t)}_{fr}.png')
        r = subprocess.run([rbin, p, str(fr), out, '0.25'], capture_output=True, text=True, timeout=300)
        print(os.path.basename(t), fr, (r.stdout or r.stderr).strip()[:120])
print('done', list(found))
