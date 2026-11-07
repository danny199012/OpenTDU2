"""Read a Windows minidump without a debugger: the exception, the modules and the
faulting thread's call chain (module + offset, since these builds ship no PDB).

    python tools_probe/dump_info.py "%LOCALAPPDATA%\\CrashDumps\\opentdu2-win.exe.1234.dmp"
    python tools_probe/dump_info.py <dump> --stack --full

Why it exists: the owner's crashes are `nvoglv64.dll` access violations and the
event log only gives the fault offset. This names the faulting module and offset,
lists the loaded modules (third-party Vulkan layers included) and walks the
faulting thread's stack so the app-side return addresses are visible. Addresses
are printed as `module+offset` - resolving them to a function needs a PDB, which
the builds do not produce.
"""
import argparse
import struct
import sys

EXCEPTION_STREAM = 6
THREAD_LIST_STREAM = 3
MODULE_LIST_STREAM = 4
MINIDUMP_SIGNATURE = 0x504D444D
THREAD_RECORD_SIZE = 48
MODULE_RECORD_SIZE = 108
RSP_IN_CONTEXT = 0x98
RIP_IN_CONTEXT = 0xF8


def read_streams(data):
    signature, _version, count, rva = struct.unpack_from("<IIII", data, 0)
    if signature != MINIDUMP_SIGNATURE:
        raise SystemExit("not a minidump (signature %08x)" % signature)
    streams = {}
    for i in range(count):
        stream_type, size, stream_rva = struct.unpack_from("<III", data, rva + i * 12)
        streams[stream_type] = (size, stream_rva)
    return streams


def read_modules(data, streams):
    _size, rva = streams[MODULE_LIST_STREAM]
    count, = struct.unpack_from("<I", data, rva)
    modules = []
    offset = rva + 4
    for _ in range(count):
        base, size, _checksum, _timestamp, name_rva = struct.unpack_from("<QIIII", data, offset)
        length, = struct.unpack_from("<I", data, name_rva)
        name = data[name_rva + 4:name_rva + 4 + length].decode("utf-16-le")
        modules.append((base, size, name))
        offset += MODULE_RECORD_SIZE
    return modules


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("dump")
    parser.add_argument("--stack", action="store_true", help="walk the faulting thread's stack")
    parser.add_argument("--full", action="store_true", help="up to 60 frames instead of 26")
    args = parser.parse_args()

    data = open(args.dump, "rb").read()
    streams = read_streams(data)
    modules = read_modules(data, streams)

    def owner(value):
        for base, size, name in modules:
            if base <= value < base + size:
                return base, name
        return None, None

    def where(value):
        base, name = owner(value)
        return "%s+%#x" % (name.split("\\")[-1], value - base) if name else "%#x" % value

    print("%s (%d bytes)" % (args.dump, len(data)))
    if EXCEPTION_STREAM in streams:
        _size, rva = streams[EXCEPTION_STREAM]
        fault_thread, = struct.unpack_from("<I", data, rva)
        code, flags, _record, address, count = struct.unpack_from("<IIQQI", data, rva + 8)
        params = struct.unpack_from("<15Q", data, rva + 8 + 8 + 4 * 8 + 8)
        print("  exception %#010x (flags %#x), %d parameters %s" % (
            code, flags, count, ["%#x" % p for p in params[:min(count, 15)]]))
        print("  address   %#x  ->  %s" % (address, where(address)))
        print("  faulting thread %d" % fault_thread)
    print("  %d modules:" % len(modules))
    for base, size, name in sorted(modules, key=lambda m: m[2].lower()):
        if any(key in name.lower() for key in ("opentdu2", "nvoglv", "nvgpucomp", "vulkan",
                                               "layer", "d3d", "dxvk", "nvapi")):
            print("    %#018x %10d  %s" % (base, size, name))

    if not args.stack or THREAD_LIST_STREAM not in streams:
        return 0
    _size, rva = streams[THREAD_LIST_STREAM]
    count, = struct.unpack_from("<I", data, rva)
    for i in range(count):
        record = rva + 4 + i * THREAD_RECORD_SIZE
        if struct.unpack_from("<I", data, record)[0] != fault_thread:
            continue
        stack_start, stack_size, stack_rva = struct.unpack_from("<QII", data, record + 24)
        context_size, context_rva = struct.unpack_from("<II", data, record + 40)
        context = data[context_rva:context_rva + context_size]
        rsp, = struct.unpack_from("<Q", context, RSP_IN_CONTEXT)
        rip, = struct.unpack_from("<Q", context, RIP_IN_CONTEXT)
        print("  faulting thread stack %#x (%d bytes), rsp %#x, rip %#x (%s)" % (
            stack_start, stack_size, rsp, rip, where(rip)))
        print("  stack (nearest first, executable-looking slots only):")
        shown = 0
        for k in range(min(stack_size, 0x4000) // 8):
            slot = stack_start + k * 8
            if slot < rsp:
                continue
            value, = struct.unpack_from("<Q", data, stack_rva + (slot - stack_start))
            base, name = owner(value)
            if name and value != base:
                print("    [rsp+%#06x] %#018x  %s" % (slot - rsp, value, where(value)))
                shown += 1
                if shown >= (60 if args.full else 26):
                    break
        break
    return 0


if __name__ == "__main__":
    sys.exit(main())
