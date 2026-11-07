#!/usr/bin/env python3
"""Dump the VehicleConfigExport of a CarVSTConfig.xmb (XMBF little endian).

Layout is the packed data image documented in source/core/xmb.h and the
schema printed by otdu2_bnk_tool xmb:
  VehicleConfigExport: +0 SampleFiles (String[]), +8 nCarID, +12 SamplesConfig (SampleCInfo[])
  SampleCInfo: +0 Bank, +4 SampleID, +8 nWaveIndex, +12 Links (PhysicsLinks[]),
               +20 fBaseVolume, +24 PanelID, +28 bIsTrigger, +29 nMustBeLooped,
               +30 InstancePosition (V3[]), +38 fClipNear, +42 fClipFar,
               +46 fRefractionFactor, +50 fTrackingClipNear, +54 fTrackingClipFar,
               +58 fTrackingRefractionFactor, +62 TrackingClippingFactors (F[]),
               +70 PlaybackStateVolume (F[]), +78 sSampleName
  PhysicsLinks: +0 Physic (I[]), +8 Audio (I), +12 CurveSet (F[])
Header: 0x00 "XMBF", 0x08 strings, 0x0C types, 0x10 data, 0x14 unk, 0x18 root.
"""
import struct
import sys


def main(path):
    data = open(path, "rb").read()
    assert data[:4] == b"XMBF", data[:4]
    str_off, type_off, data_off = struct.unpack_from("<III", data, 8)
    root_ref = struct.unpack_from("<I", data, 0x18)[0]

    def cstr(off):
        end = data.index(b"\0", off)
        return data[off:end].decode("utf-8", "replace")

    # walk the type table to find field offsets of the composite types we need
    types = {}
    pos = type_off
    # count unknown; iterate until we hit the data section heuristically:
    # parse records until offset reaches data_off
    def read_type(pos):
        w1, name_off = struct.unpack_from("<II", data, pos)
        return w1, name_off, pos + 8

    # find root type index
    root_type = root_ref >> 1
    print(f"root type index {root_type}")

    # Parse the type table linearly: composite records are followed by field
    # records. Collect names/offsets until data section.
    records = []
    p = type_off
    while p + 8 <= data_off:
        w1, name_off = struct.unpack_from("<II", data, p)
        records.append((w1, name_off))
        p += 8
    # Reconstruct types: a composite starts a type; field records follow it.
    # Simplest: scan for the field names we need by offset arithmetic on the
    # known schema instead — hardcode offsets from the schema dump.
    def arr(off):
        count, aoff = struct.unpack_from("<II", data, off)
        return count, data_off + aoff

    # Root object is at data_off (offset 0 in data section)
    root = data_off
    files_count, files_off = arr(root + 0)
    print(f"SampleFiles ({files_count}):")
    for i in range(files_count):
        (so,) = struct.unpack_from("<I", data, files_off + 4 * i)
        print(f"  [{i}] {cstr(data_off + so)}")
    ncar = struct.unpack_from("<i", data, root + 8)[0]
    print(f"nCarID {ncar}")
    scount, soff = arr(root + 12)
    print(f"SamplesConfig ({scount})")
    for i in range(scount):
        s = soff + 82 * i
        bank, sample_id, wave = struct.unpack_from("<iii", data, s)
        links_count, links_off = arr(s + 12)
        base_vol = struct.unpack_from("<f", data, s + 20)[0]
        panel = struct.unpack_from("<i", data, s + 24)[0]
        is_trigger, looped = data[s + 28], data[s + 29]
        near, far, refr = struct.unpack_from("<fff", data, s + 38)
        tnear, tfar, trefr = struct.unpack_from("<fff", data, s + 50)
        psv_count, psv_off = arr(s + 70)
        (nso,) = struct.unpack_from("<I", data, s + 78)
        name = cstr(data_off + nso)
        print(f"  [{i}] '{name}' bank={bank} id={sample_id} wave={wave} "
              f"vol={base_vol:.3f} panel={panel} trigger={is_trigger} loop={looped} "
              f"clip={near:.1f}/{far:.1f} refr={refr:.2f} psv={psv_count}")
        for j in range(links_count):
            lk = links_off + 20 * j
            ph_count, ph_off = arr(lk + 0)
            audio = struct.unpack_from("<i", data, lk + 8)[0]
            cs_count, cs_off = arr(lk + 12)
            physics = [struct.unpack_from("<i", data, ph_off + 4 * k)[0] for k in range(ph_count)]
            curves = [struct.unpack_from("<f", data, cs_off + 4 * k)[0] for k in range(cs_count)]
            fmt = lambda vs: "[" + ", ".join(f"{v:.2f}" if isinstance(v, float) else str(v) for v in vs) + "]"
            print(f"      link {j}: audio={audio} physic={fmt(physics)} curve={fmt(curves)}")


if __name__ == "__main__":
    main(sys.argv[1])
