#!/usr/bin/env python3
"""按索引从 resources.gpak 直取文件。"""
import json, os, sys
GPAK, IDX, OUT = sys.argv[1], sys.argv[2], sys.argv[3]
ents = json.load(open(IDX))
off = 824693
want_prefix = ('data/', 'levels/', 'textures/', 'shaders/')
want_swf = {'swfs/ui.swf', 'swfs/house.swf', 'swfs/static_objects.swf', 'swfs/npcs.swf',
            'swfs/backgrounds.swf', 'swfs/events.swf'}
os.makedirs(OUT, exist_ok=True)
n = 0; tot = 0
with open(GPAK, 'rb') as f:
    for name, size in ents:
        take = name.startswith(want_prefix) or name in want_swf
        if take:
            f.seek(off); d = f.read(size)
            dst = os.path.join(OUT, name)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            open(dst, 'wb').write(d)
            n += 1; tot += size
        off += size
print(f'写出 {n} 个文件 {tot/1e6:.1f} MB')
