#ifndef C3_FIELD_ITEMS_TEST_FIXTURE_H
#define C3_FIELD_ITEMS_TEST_FIXTURE_H
/* Deliberately synthetic, matching the existing game-state fixture's format.
 * Never exported as a retail pack or installed on a user's console. */
#define PACK_SIZE 176722u
#define BODY 128u
#define PREFIX 122960u
#define STARTERS 147600u
#define NATURES 147728u
#define FLAGS 147978u
static void sign_pack(unsigned char *p)
{ c3_save_codec_sha1(p+64,p+128,PACK_SIZE-128); }
static unsigned char *synthetic_pack(void)
{
    static const unsigned fields[]={1,PACK_SIZE,BODY,C3_SAVE_BODY_SIZE,PREFIX,0x6040,
                                   STARTERS,2,64,NATURES,25,10,FLAGS,3593,8};
    static const unsigned char pin[]={0x51,0xbf,0xe4,0x31,0x4a,0x21,0x8d,0xfa,
        0xf9,0x59,0x2f,0xc8,0x07,0x23,0xc5,0x04,0x98,0xd0,0xee,0x55};
    unsigned char *p=calloc(1,PACK_SIZE+1);
    unsigned i,j,bit=0;
    CHECK(p!=NULL); memcpy(p,"C3GS",4); memcpy(p+84,pin,20); w32(p+104,1);
    for (i=0;i<15;++i) w32(p+4+i*4,fields[i]);
    memcpy(p+BODY,"\x0b\x03\x02\x02",4);
    w32(p+BODY+0x1c,2); w32(p+BODY+0x24,1); p[BODY+0x28]=1;
    w32(p+BODY+0xaf4,10000);
    memcpy(p+PREFIX,"GC6E01",6);
    memcpy(p+PREFIX+8,"pokemon_colosseum",18); w16(p+PREFIX+0x38,48);
    for (i=0;i<2;++i) {
        unsigned char *q=p+STARTERS+i*64;
        w16(q,i?196:197); q[2]=(unsigned char)(i?25:26);q[3]=31;
        w32(q+4,i?0x3ff4:0x4a6f);w16(q+8,220);
        for (j=0;j<6;++j) w16(q+10+j*2,100);
        for (j=0;j<4;++j) { w16(q+22+j*3,1+j);q[24+j*3]=10; }
        w16(q+34,'S'); q[56]=1;
    }
    for (i=0;i<25*5;++i) p[NATURES+i*2]=p[NATURES+i*2+1]=100;
    for (i=0;i<3593;++i) {
        unsigned width=(i==31)?32:1;
        p[FLAGS+i*8]=(unsigned char)(0xc0|width);
        w16(p+FLAGS+i*8+4,bit); bit+=width;
    }
    sign_pack(p);return p;
}


#endif
