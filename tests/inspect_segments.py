import ctypes, struct, sys
from pathlib import Path
from PIL import Image

DLL = Path("build/Release/jbig2_wic.dll").resolve()
lib = ctypes.WinDLL(str(DLL))

class Page(ctypes.Structure):
    _fields_ = [("pixels", ctypes.c_void_p), ("width", ctypes.c_uint32),
                ("height", ctypes.c_uint32), ("stride", ctypes.c_uint32),
                ("dpi_x", ctypes.c_double), ("dpi_y", ctypes.c_double)]

lib.jbig2_encode_multipage_1bpp.argtypes = [
    ctypes.POINTER(Page), ctypes.c_uint32, ctypes.c_int,
    ctypes.POINTER(ctypes.POINTER(ctypes.c_uint8)), ctypes.POINTER(ctypes.c_size_t)
]
lib.jbig2_free_buffer.argtypes = [ctypes.c_void_p]

def encode(raw, w, h, stride, flags):
    pg = Page(ctypes.addressof(raw), w, h, stride, 300.0, 300.0)
    ptr, sz = ctypes.POINTER(ctypes.c_uint8)(), ctypes.c_size_t(0)
    rc = lib.jbig2_encode_multipage_1bpp(ctypes.byref(pg), 1, flags, ctypes.byref(ptr), ctypes.byref(sz))
    assert rc == 0
    data = ctypes.string_at(ptr, sz.value)
    lib.jbig2_free_buffer(ptr)
    return data

def parse_segments(data: bytes):
    pos = 13  # 8B Magic + 1B Flags + 4B PageCount
    stats = {"dict": 0, "text": 0, "res": 0, "syms": 0, "inst": 0}
    while pos + 11 <= len(data):
        seg_num = struct.unpack_from(">I", data, pos)[0]; pos += 4
        flags = data[pos]; pos += 1
        seg_type = flags & 0x3F
        ref_cnt = data[pos] >> 5; pos += 1
        ref_sz = 1 if seg_num <= 255 else (2 if seg_num <= 65535 else 4)
        pos += ref_cnt * ref_sz
        pos += 4 if (flags & 0x40) else 1
        dlen = struct.unpack_from(">I", data, pos)[0]; pos += 4
        payload = data[pos:pos + dlen]
        pos += dlen
        if seg_type == 0:
            stats["dict"] += dlen
            stats["syms"] = struct.unpack_from(">I", payload, 10)[0]
        elif seg_type == 7:
            stats["text"] += dlen
            stats["inst"] = struct.unpack_from(">I", payload, 19)[0]
        elif seg_type == 39:
            stats["res"] += dlen
    return stats

folder = Path(sys.argv[1])
print(f"{'Datei':<28} | {'Generic':>7} | {'Dict':>7} | {'Text':>7} | {'Resid':>7} | {'Syms':>5} | {'Inst':>5} | {'Delta':>6}")
print("-" * 92)
for p in sorted(folder.glob("*.png")):
    img = Image.open(p).convert("1")
    w, h = img.size
    stride = (w + 7) // 8
    raw = ctypes.create_string_buffer(img.tobytes(), stride * h)
    g_bytes = encode(raw, w, h, stride, 0)
    s_bytes = encode(raw, w, h, stride, 2)
    st = parse_segments(s_bytes)
    delta = (len(s_bytes) - len(g_bytes)) / len(g_bytes) * 100.0
    print(f"{p.name[:28]:<28} | {len(g_bytes)/1024:6.1f}K | {st['dict']/1024:6.1f}K | {st['text']/1024:6.1f}K | {st['res']/1024:6.1f}K | {st['syms']:5d} | {st['inst']:5d} | {delta:+5.1f}%")
