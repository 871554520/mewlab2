#!/usr/bin/env python3
"""按索引从 resources.gpak 取出全部 swfs/*.swf，逐个跑 scanmain 并汇总。"""
import json, os, subprocess, sys, collections
gpak, idx_path, scan_bin, out_dir = sys.argv[1:5]
idx = json.load(open(idx_path))
off = 824693
os.makedirs(out_dir, exist_ok=True)
rows = []
n = 0
with open(gpak, 'rb') as f:
    for name, size in idx:
        if name.startswith('swfs/') and name.endswith('.swf'):
            f.seek(off); data = f.read(size)
            p = os.path.join(out_dir, 'x.swf')
            open(p, 'wb').write(data)
            r = subprocess.run([scan_bin, p], capture_output=True, text=True, timeout=120)
            line = (r.stdout or r.stderr or '').strip().replace('x.swf', name)
            rows.append(line)
            n += 1
        off += size
print('\n'.join(rows))
ok = [r for r in rows if '\tOK\t' in r]
err = [r for r in rows if '\tERR\t' in r]
print(f'\n== 汇总：{n} 个 SWF，解析成功 {len(ok)}，失败 {len(err)}')
