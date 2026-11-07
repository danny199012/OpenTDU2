import sys
sys.path.insert(0, r'E:\Xbox 360 Games\Torrent\Minerva_Myrient\Redump\Microsoft - Xbox 360\Test-Drive-Unlimited-2-Recomp\tools')
import tdu2_bigfile as T

info, entries = T.parse_map(r'C:\Program Files (x86)\Steam\steamapps\common\Test Drive Unlimited 2\bigfile_EU_1.map')
print('entries:', info['total_files'])
hashes = {e['hash'] for e in entries}
print('unique hashes:', len(hashes))

bh = T.big_hash
files = [
    'sound/audiosetup.xmb', 'database/db_data.cpr', 'frontend/hires/main.bnk',
    'frontend/hires/press_start.bnk', 'game_weathers.xmb', 'systempc.cpr',
    'ibiza.bnk', 'hawai.bnk', 'casino.bnk', 'video/bumper_atari.mkv',
    'database/quality_settings.cpr', 'entities.xmb', 'spots.bin',
]
prefixes = ['', 'euro/bnk/', 'bnk/', 'Euro/Bnk/']
transforms = [
    ('lower+fwd', lambda s: s.lower()),
    ('as-is+fwd', lambda s: s),
    ('upper+fwd', lambda s: s.upper()),
    ('lower+back', lambda s: s.lower().replace('/', '\\')),
    ('as-is+back', lambda s: s.replace('/', '\\')),
    ('upper+back', lambda s: s.upper().replace('/', '\\')),
]

hits = {}
for f in files:
    for p in prefixes:
        for tname, tf in transforms:
            name = tf(p + f)
            v = bh(name)
            if v in hashes:
                hits.setdefault(f, []).append((tname, p, name))

for f, hs in hits.items():
    print('MATCH', f, '->', hs[:4])
print('files matched:', len(hits), '/', len(files))
