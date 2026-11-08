#!/usr/bin/env python3
"""Independently verify the source-created ARM probe's complete save body.

Uses the privately prepared original tables plus Python integer arithmetic,
struct and hashlib. It never calls the C initializer, C codec or preparer.
No original game bytes or output GCI are included in the repository.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct


def require(ok, reason):
    if not ok:
        raise ValueError(reason)


def u16(data, offset):
    return struct.unpack_from(">H", data, offset)[0]


def u32(data, offset):
    return struct.unpack_from(">I", data, offset)[0]


def put16(data, offset, value):
    struct.pack_into(">H", data, offset, value)


def put32(data, offset, value):
    struct.pack_into(">I", data, offset, value)


def source_body(pack, save_count, initial_seed=0x12345678, player_name="WES",
                play_seconds=3661.25, arrival_complete=False, sound_mono=False):
    require(len(pack)==176722 and pack[:4]==b"C3GS" and u32(pack,4)==1,
            "Wrong prepared data/version")
    require(hashlib.sha1(pack[128:]).digest()==pack[64:84],"Prepared-data checksum")
    require(pack[84:104].hex()=="51bfe4314a218dfaf9592fc80723c50498d0ee55","Wrong source pin")
    body=bytearray(pack[128:128+0x1dfd0])
    seed=initial_seed
    golden=initial_seed==0x12345678

    def draw():
        nonlocal seed
        seed=(seed*214013+2531011)&0xffffffff
        return seed>>16

    # _fadeEffectGetRandom__: first draw low, second high, modulo0xffffffff.
    for index in range(500):
        first,second=draw(),draw()
        put32(body,0x82a8+12+index*12,((second<<16)|first)%0xffffffff)
    first,second=draw(),draw()
    trainer=(second<<16)|first
    if golden:
        require(trainer==0x1c465252,"Independent trainer-ID golden changed")
    require(1<=len(player_name)<=7 and all(32<=ord(c)<=126 for c in player_name),"Invalid expected name")
    name=player_name.encode("utf-16-be").ljust(22,b"\0")
    body[0x70:0x86]=name
    put32(body,0x9c,trainer)
    reference_stats=[]
    for index in range(2):
        data=pack[147600+64*index:147664+64*index]
        pokemon=0xa0+0x138*index
        species,level=u16(data,0),data[2]
        draw(),draw()  # pokemonCreate initial PID, replaced by RndFit
        iv_a,iv_b,ability=draw(),draw(),draw()
        for _ in range(65536):
            high,low=draw(),draw()
            pid=(high<<16)|low
            rare=(trainer>>16)^(trainer&65535)^high^low
            if low&255>=data[3] and rare>=8:
                break
        else:
            raise ValueError("Independent starter search exhausted")
        if golden:
            require(pid==(0x3f5932e4,0x0990627b)[index],"Independent PID golden changed")
        ivs=(iv_a&31,(iv_a>>5)&31,(iv_a>>10)&31,(iv_b>>5)&31,(iv_b>>10)&31,iv_b&31)
        put16(body,pokemon,species)
        put32(body,pokemon+4,pid)
        body[pokemon+8:pokemon+12]=body[:4]
        put16(body,pokemon+12,0xfe)
        body[pokemon+14:pokemon+17]=bytes((level,4,0))
        put32(body,pokemon+0x14,trainer)
        body[pokemon+0x18:pokemon+0x2e]=name
        body[pokemon+0x2e:pokemon+0x44]=data[34:56]
        body[pokemon+0x44:pokemon+0x5a]=data[34:56]
        put32(body,pokemon+0x5c,u32(data,4))
        body[pokemon+0x60]=level
        for move in range(4):
            off=pokemon+0x78+4*move
            put16(body,off,u16(data,22+3*move))
            body[off+2:off+4]=bytes((data[24+3*move],0))
        stats=[]
        for stat in range(6):
            base=u16(data,10+2*stat)
            core=((2*base+ivs[stat])*level)//100
            if stat==0:
                value=core+level+10
            else:
                nature=147728+10*(pid%25)+2*(stat-1)
                value=((core+5)*pack[nature])//pack[nature+1]
            stats.append(value)
            put16(body,pokemon+0x8c+2*stat,value)
            put16(body,pokemon+0xa4+2*stat,ivs[stat])
        put16(body,pokemon+0x8a,stats[0])
        put16(body,pokemon+0xb0,u16(data,8))
        body[pokemon+0xcc]=(ability&1) if data[57] else 0
        put16(body,0x82a8+4+12*index,0x8000|species)
        put32(body,0x82a8+8+12*index,trainer)
        put32(body,0x82a8+12+12*index,pid)
        reference_stats.append(stats)
    if golden:
        require(reference_stats==[[86,37,66,47,79,46],[75,42,39,71,52,71]],"Independent stat golden changed")
        require(seed==0x627b7db0,"Independent final RNG state changed")
    put16(body,0x82a8,2)
    # Source opening event and explicit native entry checkpoint.
    definition=147978+8*0x8d0
    require(pack[definition]==0xc1 and u16(pack,definition+4)==4439,
            "Opening flag layout changed")
    flag_word=0x1be5c+(4439//32)*4
    require(u32(body,flag_word)==0,"Opening flag is not initially clear")
    put32(body,flag_word,1<<(4439%32))
    if arrival_complete:
        definition=147978+8*0xaf8
        require(pack[definition]==0xc1,"Arrival flag layout changed")
        bit=u16(pack,definition+4)
        offset=0x1be5c+(bit//32)*4
        put32(body,offset,u32(body,offset)|(1<<(bit%32)))
    put32(body,0x1c,1)
    put32(body,0x24,0x3a0)
    body[0x28]=0
    struct.pack_into(">f",body,0x20,play_seconds)
    body[0x2a]=int(sound_mono)
    put32(body,0x10,save_count)
    body[4:8]=bytes(4)
    body[0x18:0x1c]=bytes(4)
    return bytes(body)


def newest_body(gci):
    require(len(gci)==0x60040 and gci[:6]==b"GC6E01","Wrong GCI envelope")
    require(gci[8:26]==b"pokemon_colosseum\0" and u16(gci,0x38)==48,
            "Wrong GCI filename/block count")
    candidates=[]
    for index in range(3):
        slot=bytearray(gci[0x6040+index*0x1e000:0x6040+(index+1)*0x1e000])
        count=u32(slot,4)
        require(slot[:2]==b"\1\1","Wrong slot flags")
        require(sum(struct.unpack(">8I",slot[:32]))&0xffffffff==0,
                "Slot encrypted header checksum failed")
        if count==0:
            continue  # original initialized-but-unused slot
        require(count<=0x7fffffff,"Slot counter outside original signed range")
        digest=bytes(slot[0x1dfec:])
        slot[12:16]=bytes(4)
        key=bytes(value^255 for value in digest)
        for offset in range(0x18,0x1dfd8,20):
            encrypted=bytes(slot[offset:offset+20])
            slot[offset:offset+20]=bytes(a^b for a,b in zip(encrypted,key))
            key=hashlib.sha1(encrypted).digest()
        require(hashlib.sha1(slot[:0x1dfd8]).digest()==digest,"Slot payload SHA-1 failed")
        candidates.append((count,bytes(slot[8:0x1dfd8])))
    require(bool(candidates),"No saved slot")
    return max(candidates,key=lambda candidate:candidate[0])


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pack",type=Path)
    parser.add_argument("gci",type=Path)
    parser.add_argument("--save-count",type=int,default=1)
    parser.add_argument("--seed",type=lambda x:int(x,0),default=0x12345678)
    parser.add_argument("--name",default="WES")
    parser.add_argument("--play-seconds",type=float,default=3661.25)
    parser.add_argument("--time-bits",help="Exact saved binary32 clock, eight hex digits from game_save trace")
    parser.add_argument("--arrival-complete",action="store_true")
    parser.add_argument("--sound-mono",action="store_true")
    args=parser.parse_args()
    try:
        data=args.gci.read_bytes()
        generation,actual=newest_body(data)
        require(0<=args.seed<=0xffffffff,"Seed is outside the source RNG range")
        seconds=args.play_seconds
        if args.time_bits:
            require(len(args.time_bits)==8,"Clock bits must contain eight hex digits")
            seconds=struct.unpack(">f",bytes.fromhex(args.time_bits))[0]
        require(0<=seconds<=0xffffffff,"Invalid expected play time")
        expected=source_body(args.pack.read_bytes(),args.save_count,args.seed,args.name,
                             seconds,args.arrival_complete,args.sound_mono)
        differences=[i for i,(a,b) in enumerate(zip(actual,expected)) if a!=b]
        require(not differences,"Body differs from independent source calculation at "+
                ", ".join(hex(i) for i in differences[:12]))
        print(json.dumps({"source_created_test_state":True,"seed":"%08x"%args.seed,
              "name":args.name,"play_seconds":seconds,"arrival_complete":args.arrival_complete,
              "whole_body_verified":True,"body_bytes":len(actual),"outer_generation":generation,
              "saved_count":args.save_count,"body_sha256":hashlib.sha256(actual).hexdigest(),
              "gci_sha256":hashlib.sha256(data).hexdigest()},indent=2))
    except (OSError,ValueError,struct.error) as error:
        parser.exit(1,f"Game-state probe verification failed: {error}\n")


if __name__=="__main__":
    main()
