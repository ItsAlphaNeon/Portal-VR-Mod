# Extracts one file from a Valve VPK (v1/v2):  python vpkget.py <pak_dir.vpk> <path/in/vpk> [out]
import struct, sys, os

def read_cstr(f):
    b = bytearray()
    while True:
        c = f.read(1)
        if not c or c == b"\0":
            return b.decode("latin-1")
        b += c

def extract(dirpath, want, out=None):
    want = want.lower().replace("\\", "/")
    with open(dirpath, "rb") as f:
        sig, ver, tree = struct.unpack("<III", f.read(12))
        if ver == 2:
            f.read(16)
        header = f.tell()
        while True:
            ext = read_cstr(f)
            if not ext:
                break
            while True:
                path = read_cstr(f)
                if not path:
                    break
                while True:
                    name = read_cstr(f)
                    if not name:
                        break
                    crc, preload, arch, off, length, term = struct.unpack("<IHHIIH", f.read(18))
                    pre = f.read(preload)
                    full = ((path.strip() + "/") if path.strip() else "") + name + "." + ext
                    if full.lower() == want:
                        if arch == 0x7FFF:
                            f.seek(header + tree + off)
                            data = pre + f.read(length)
                        else:
                            arc = dirpath.replace("_dir.vpk", "_%03d.vpk" % arch)
                            with open(arc, "rb") as a:
                                a.seek(off)
                                data = pre + a.read(length)
                        if out:
                            os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
                            open(out, "wb").write(data)
                        else:
                            sys.stdout.buffer.write(data)
                        return True
    return False

if __name__ == "__main__":
    if not extract(sys.argv[1], sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else None):
        sys.exit("not found")
