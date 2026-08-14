#!/usr/bin/env python3
"""Generate the missing npc_handmaiden.png (deterministic, stdlib only).
Handmaiden = villager body tinted lavender with a small crown dot."""
import struct, zlib, os

W = H = 32
def px(buf, x, y, r, g, b, a=255):
    if 0 <= x < W and 0 <= y < H:
        i = (y*W+x)*4; buf[i]=r; buf[i+1]=g; buf[i+2]=b; buf[i+3]=a
def rect(buf, x0,y0,x1,y1, r,g,b,a=255):
    for y in range(max(0,y0),min(H,y1)):
        for x in range(max(0,x0),min(W,x1)):
            px(buf,x,y,r,g,b,a)
def circle(buf, cx,cy,rad, r,g,b,a=255):
    for y in range(H):
        for x in range(W):
            if (x-cx)**2+(y-cy)**2 <= rad*rad:
                px(buf,x,y,r,g,b,a)
def png(buf):
    def chunk(t,d):
        c=t+d; return struct.pack(">I",len(d))+c+struct.pack(">I",zlib.crc32(c)&0xffffffff)
    raw=bytearray()
    for y in range(H):
        raw.append(0); raw+=bytes(buf[y*W*4:(y+1)*W*4])
    return b"\x89PNG\r\n\x1a\n"+chunk(b"IHDR",struct.pack(">IIBBBBB",W,H,8,6,0,0,0))+chunk(b"IDAT",zlib.compress(bytes(raw),9))+chunk(b"IEND",b"")

buf=bytearray(W*H*4)
# body (lavender)
circle(buf,16,20,11, 200,180,230,255)
rect(buf,10,26,22,30, 200,180,230,255)
# head
circle(buf,16,12,7, 245,225,250,255)
# crown dot (gold)
rect(buf,14,4,18,7, 230,200,90,255)
# eyes
px(buf,13,12,40,40,60); px(buf,18,12,40,40,60)
out=os.path.join(os.path.dirname(__file__),"..","assets","sprites","npc_handmaiden.png")
os.makedirs(os.path.dirname(out),exist_ok=True)
with open(out,"wb") as f: f.write(png(buf))
print("wrote",out)
