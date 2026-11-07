import sys, struct
sys.path.insert(0, r'E:\Xbox 360 Games\Torrent\Minerva_Myrient\Redump\Microsoft - Xbox 360\Test-Drive-Unlimited-2-Recomp\tools')
import tdu2_bigfile as T

base = r'C:\Program Files (x86)\Steam\steamapps\common\Test Drive Unlimited 2'
table = T.load_names(r'E:\Xbox 360 Games\Torrent\Minerva_Myrient\Redump\Microsoft - Xbox 360\Test-Drive-Unlimited-2-Recomp\tools\FileNames.list')
# merge the second list too
table2 = T.load_names(r'E:\Xbox 360 Games\Torrent\Minerva_Myrient\Redump\Microsoft - Xbox 360\Test-Drive-Unlimited-2-Recomp\tools\TDU2Names.list')
table.update(table2)
print('dictionary:', len(table))

total_stored = total_comp = total_unresolved = 0
comp_bytes = stored_bytes = 0
all_roots = set()
unresolved_by_map = {}
for n in range(1, 6):
    mp = rf'{base}\bigfile_EU_{n}.map'
    info, entries = T.parse_map(mp)
    stored = sum(1 for e in entries if e['uncomp'] == e['comp'])
    comp = len(entries) - stored
    unresolved = sum(1 for e in entries if e['hash'] not in table)
    total_stored += stored; total_comp += comp; total_unresolved += unresolved
    comp_bytes += sum(e['comp'] for e in entries if e['uncomp'] != e['comp'])
    stored_bytes += sum(e['comp'] for e in entries if e['uncomp'] == e['comp'])
    print(f'EU_{n}: {len(entries):5d} entries  stored={stored:5d} lzx={comp:5d} unresolved={unresolved}')
    # sample roots of resolved names to see the tree shape
    for e in entries:
        nm = table.get(e['hash'])
        if nm:
            all_roots.add(nm.split('/')[1] if '/' in nm else nm)

import shutil
free = shutil.disk_usage('C:').free
print()
print('TOTAL stored:', total_stored, ' lzx:', total_comp, ' unresolved:', total_unresolved)
print(f'lzx bytes: {comp_bytes/1e9:.2f} GB   stored bytes: {stored_bytes/1e9:.2f} GB')
print('uncompressed total (approx):', f'{(comp_bytes*1.4+stored_bytes)/1e9:.1f} GB (lzx est. x1.4)')
print('C: free:', f'{free/1e9:.1f} GB')
print('second-level roots:', sorted(all_roots)[:30])
