import sys, zlib, struct, os
# usage: tile.py out.png cols scale file...
def read_ppm(p):
    d = open(p,'rb').read(); parts = d.split(b'\n', 3)
    w, h = map(int, parts[1].split()); return w, h, parts[3]
def png(path, w, h, rows):
    def chunk(t, b): return struct.pack('>I', len(b)) + t + b + struct.pack('>I', zlib.crc32(t + b) & 0xffffffff)
    raw = b''.join(b'\x00' + r for r in rows)
    open(path,'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))
out_path, cols, scale = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]); files = sys.argv[4:]
imgs = [read_ppm(f) for f in files]; W, H = imgs[0][0], imgs[0][1]
tw, th = W // scale, H // scale; rows_n = (len(imgs) + cols - 1) // cols
gap = 4
out = [bytearray((tw + gap) * cols * 3) for _ in range((th + gap) * rows_n)]
for r in out:
    for i in range(0, len(r), 3): r[i] = 40; r[i+1] = 40; r[i+2] = 48
for idx, (w, h, px) in enumerate(imgs):
    cx, cy = (idx % cols) * (tw + gap), (idx // cols) * (th + gap)
    for y in range(th):
        src = px[(y*scale)*w*3:(y*scale)*w*3 + w*3]; row = out[cy + y]
        for x in range(tw): row[(cx + x)*3:(cx + x)*3 + 3] = src[x*scale*3:x*scale*3 + 3]
png(out_path, (tw + gap) * cols, (th + gap) * rows_n, [bytes(r) for r in out])
print(out_path, [os.path.basename(f) for f in files])
