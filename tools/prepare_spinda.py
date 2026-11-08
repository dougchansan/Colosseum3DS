"""Pinned original Spinda RGB565 inputs for saved-personality spot synthesis."""
import hashlib
import struct
from pathlib import Path
from prepare_outskirt import member

ARCHIVE_SHA256 = '9b56b8093eb237386e82c894e2ab8e6389373d1d3f649184383ed3c475d979c4'
MEMBERS = ((0x10561200,128,128,'4da656b80e83bafa0fd2705c95dc9cb65e7b09996e6a15a050c921437130a408'),
           (0x10571200,32,32,'9483c3ad95cd3a0e27dcb2b6adb6bb637f1284d980b28947e3f2d1d163f8a6e2'))
ANCHORS = bytes.fromhex('06075b061e414441')

def fnv(data):
    h=2166136261
    for v in data: h=((h^v)*16777619)&0xffffffff
    return h

def linear_rgb16(data, width, height):
    if len(data)!=128+width*height*2 or struct.unpack_from('>HH',data)!=(width,height) or struct.unpack_from('>I',data,8)[0]!=0x44 or struct.unpack_from('>I',data,40)[0]!=128:
        raise ValueError('Invalid original Spinda RGB16 texture')
    out=[0]*(width*height)
    for y in range(height):
        for x in range(width):
            tile=((y//4)*(width//4)+x//4)*16+(y%4)*4+x%4
            out[y*width+x]=struct.unpack_from('>H',data,128+tile*2)[0]
    return out

def rgba(words):
    return bytes(c for v in words for c in ((v>>11)*8,((v>>5)&63)*4,(v&31)*8,255))

def synthesize(base, spot, pid):
    if len(base)!=128*128 or len(spot)!=32*32 or not 0<=pid<=0xffffffff:
        raise ValueError('Invalid Spinda synthesis input')
    out=list(base)
    for i in range(4):
        b=(pid>>(i*8))&255
        # Retail single-precision multiplication, then truncation toward zero.
        scale=struct.unpack('<f',struct.pack('<f',3.4))[0]
        x=ANCHORS[i*2]+int(struct.unpack('<f',struct.pack('<f',scale*((b&15)-8)))[0])
        y=ANCHORS[i*2+1]+int(struct.unpack('<f',struct.pack('<f',scale*((b>>4)-8)))[0])
        for row in range(32):
            start=x+(y+row)*128
            # Retail bounds are linear, intentionally allowing row crossing.
            if 0<=start<len(out):
                for col in range(32):
                    if start+col<len(out): out[start+col]=max(out[start+col],spot[row*32+col])
    return out

def export_spinda(archive, dol, output):
    if hashlib.sha256(archive).hexdigest()!=ARCHIVE_SHA256:
        raise ValueError('Spinda archive differs from pinned original')
    found=False
    for i in range(18):
        off=struct.unpack_from('>I',dol,i*4)[0]; addr=struct.unpack_from('>I',dol,0x48+i*4)[0]; size=struct.unpack_from('>I',dol,0x90+i*4)[0]
        if addr<=0x80478BA8 and 0x80478BB0<=addr+size:
            found=dol[off+0x80478BA8-addr:off+0x80478BB0-addr]==ANCHORS
    if not found: raise ValueError('Original Spinda anchor table differs')
    words=[]; sources=[]
    for identity,w,h,digest in MEMBERS:
        raw=member(archive,identity)
        if hashlib.sha256(raw).hexdigest()!=digest: raise ValueError('Original Spinda member differs')
        words.append(linear_rgb16(raw,w,h)); sources.append({'resource':identity,'sha256':digest})
    payload=b''.join(struct.pack('<%dH'%len(a),*a) for a in words)
    header=struct.pack('<4s5I8s',b'C3SP',1,32+len(payload),fnv(payload[:32768]),fnv(payload[32768:]),fnv(rgba(words[0])),ANCHORS)
    Path(output).mkdir(parents=True,exist_ok=True)
    (Path(output)/'spinda.c3s').write_bytes(header+payload)
    return {'archive_sha256':ARCHIVE_SHA256,'sources':sources,'anchors':ANCHORS.hex(),'base_rgba_fnv1a':fnv(rgba(words[0])),'algorithm':'original RGB16 unsigned max; four PID bytes, float32 3.4 shifts, linear row bounds'}

def bind_spinda_base(model, archive):
    """Mirror fn_80140190's part2/material0 image replacement only.

    GSmaterialSetTexture (0x800DF028) changes the first TObj's image descriptor;
    it does not remove the four later texture stages or alter their TEV state.
    The ordinary exporter must still admit that complete material expression.
    """
    import copy
    if hashlib.sha256(archive).hexdigest()!=ARCHIVE_SHA256:
        raise ValueError('Spinda archive differs from pinned original')
    targets=[i for i,(bone,dobj,_) in enumerate(model.meshes) if bone==2 and dobj.address==26848]
    if targets!=[7]:raise ValueError('Original Spinda part2 material0 topology differs')
    index=targets[0];_,dobj,_=model.meshes[index]
    textures=[];texture=dobj.mobject.texture
    while texture:
        textures.append(texture);texture=texture.next
    if [t.flags for t in textures]!=[0x50010,0x10010,0x40010,0x40010,0x10010] or [t.source for t in textures]!=[4,5,6,7,8]:
        raise ValueError('Original Spinda five-stage material differs')
    mesh=model.model.meshes[index]
    if len(mesh.material.texture_layers)!=5 or len(mesh.uv_layers)!=5:
        raise ValueError('Original Spinda texture coordinates differ')
    words=linear_rgb16(member(archive,MEMBERS[0][0]),128,128)
    pixels=rgba(words);stride=128*4
    layer=copy.copy(mesh.material.texture_layers[0]);image=copy.copy(layer.image)
    image.width=image.height=128
    image.pixels=b''.join(pixels[y*stride:(y+1)*stride] for y in range(127,-1,-1))
    layer.image=image
    mesh.material=copy.copy(mesh.material)
    mesh.material.texture_layers=[layer,*mesh.material.texture_layers[1:]]
    return {'part':2,'material':0,'dobj_address':26848,'texture_stages_preserved':5}
