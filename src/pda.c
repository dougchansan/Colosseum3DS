#include "colosseum3ds/pda.h"
#include "save_codec.h"
#include <string.h>

uint16_t c3_pda_be16(const uint8_t *p){return (uint16_t)((uint16_t)p[0]<<8|p[1]);}
uint32_t c3_pda_be32(const uint8_t *p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static uint32_t le32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}

int c3_pda_assets_open(C3PdaAssets *assets,const void *memory,size_t size)
{
    const uint8_t *d=memory;uint8_t digest[20];
    static const uint8_t pin[]={0x51,0xbf,0xe4,0x31,0x4a,0x21,0x8d,0xfa,0xf9,0x59,0x2f,0xc8,0x07,0x23,0xc5,0x04,0x98,0xd0,0xee,0x55};
    if(!assets||!d||size<64||size>8u*1024u*1024u||memcmp(d,"C3PD",4)||le32(d+4)!=1||le32(d+8)!=size||
       !le32(d+12)||le32(d+12)>8192||memcmp(d+36,pin,20)||le32(d+56)||le32(d+60))return 0;
    unsigned count=le32(d+12);size_t next=64u+count*16u;if(next>size)return 0;
    uint32_t last_kind=0,last_key=0;
    for(unsigned i=0;i<count;++i){const uint8_t *e=d+64+i*16u;
        uint32_t kind=le32(e),key=le32(e+4),off=le32(e+8),bytes=le32(e+12);
        if(kind<1||kind>19||kind<last_kind||(kind==last_kind&&key<=last_key)||off!=next||!bytes||off>size||bytes>size-off)return 0;
        if((kind==1&&bytes>8192)||(kind==2&&(key<1||key>412||(bytes!=64&&bytes!=65)))||
           (kind==3&&(key>=32||bytes!=44))||(kind==4&&(!key||key>96||bytes!=56))||
           (kind==5&&(key||bytes!=96))||(kind==7&&(key>=548||bytes!=40))||
           (kind==8&&(key||bytes!=312))||(kind==9&&(key||bytes!=2424))||(kind==10&&(key>=358||bytes!=56))||
           (kind==11&&(!key||key>96||bytes%8||bytes>1024))||(kind==12&&(key>=128||bytes!=12))||(kind==13&&bytes!=76)||
           (kind==14&&(key>=64||bytes!=16))||(kind==15&&(!key||key>=413||bytes!=284))||
           (kind==16&&(key||bytes!=464))||(kind==17&&(key>=25||bytes!=40))||
           (kind==18&&(key>2||bytes!=(key==0?16u:key==1?8u:36u)))||(kind==19&&(key||bytes!=80)))return 0;
        if(kind==6){
            if(bytes<8)return 0;
            uint32_t w=le32(d+off),h=le32(d+off+4);
            if(!w||w>1024||!h||h>1024||bytes!=8u+w*h*4u)return 0;
        }
        if(kind==4){const uint8_t *record=d+off;
            if(c3_pda_be16(record+2)>=413||c3_pda_be16(record+10)>96)return 0;
            static const unsigned fields[]={12,14,18,20,22,28};
            for(unsigned f=0;f<6;++f)if(c3_pda_be16(record+fields[f])>=3593)return 0;
        }
        next+=bytes;last_kind=kind;last_key=key;
    }
    if(next!=size)return 0;
    c3_save_codec_sha1(digest,d+64,(uint32_t)(size-64));if(memcmp(digest,d+16,20))return 0;
    *assets=(C3PdaAssets){d,size,count};return 1;
}
const uint8_t *c3_pda_entry(const C3PdaAssets *a,unsigned kind,unsigned key,size_t *size)
{
    if(size)*size=0;
    if(!a||!a->data)return NULL;
    unsigned lo=0,hi=a->count;
    while(lo<hi){unsigned mid=lo+(hi-lo)/2;const uint8_t *e=a->data+64+mid*16u;
        unsigned k=le32(e),id=le32(e+4);
        if(k==kind&&id==key){if(size)*size=le32(e+12);return a->data+le32(e+8);}
        if(k<kind||(k==kind&&id<key))lo=mid+1;else hi=mid;
    }return NULL;
}
static int append(char *out,size_t cap,size_t *used,const char *text)
{
    while(*text){if(*used+1u>=cap)return 0;out[(*used)++]=*text++;}
    out[*used]=0;return 1;
}
static int text_context(const C3PdaAssets *a,uint32_t id,const char *hero,const char *partner,const char *location,char *out,size_t cap)
{
    size_t size,pos=0,used=0;int ruby=0;const uint8_t *d=c3_pda_entry(a,1,id,&size);
    if(!out||!cap)return 0;
    out[0]=0;if(!d)return 0;
    while(pos+2<=size){unsigned code=c3_pda_be16(d+pos);pos+=2;char encoded[8]={0};const char *text=encoded;
        if(!code)return 1;
        if(code==0xffff){if(pos>=size)goto fail;unsigned control=d[pos++];
            switch(control){
            case 0:text="\n";break;
            case 2:break;
            case 3:text="\n\n";break;
            case 4:case 6:ruby=0;break;
            case 5:ruby=1;break;
            case 7:if(pos>=size)goto fail;++pos;break;
            case 0x2b:case 0x4d:text=hero?hero:"";break;
            case 0x2c:text=partner?partner:"";break;
            case 0x31:if(!location)goto fail;text=location;break;
            case 0x3d:break;
            default:goto fail;
            }
        }else if(!ruby){
            if(code>=32&&code<=126)encoded[0]=(char)code;
            else switch(code){
            case 0xe9:text="e";break;
            case 0xc9:text="E";break;
            case 0x2605:text="*";break;
            case 0x3000:text=" ";break;
            case 0x2031:text="???";break;
            case 0x2026:text="...";break;
            case 0x2018:case 0x2019:text="'";break;
            case 0x201c:case 0x201d:text="\"";break;
            case 0xff0d:text="-";break;
            case 0x25cb:text="O";break;
            case 0x2640:text="F";break;
            case 0x2642:text="M";break;
            default:goto fail;
            }
        }
        if(!append(out,cap,&used,text))goto fail;
    }
fail:out[0]=0;return 0;
}
int c3_pda_text(const C3PdaAssets *a,uint32_t id,const char *hero,const char *partner,char *out,size_t cap)
{return text_context(a,id,hero,partner,NULL,out,cap);}
int c3_pda_species(const C3PdaAssets *a,unsigned id,C3PdaSpecies *out)
{
    size_t size;const uint8_t *d=c3_pda_entry(a,2,id,&size);if(!d||!out||(size!=64&&size!=65))return 0;
    C3PdaSpecies s={0};s.id=(uint16_t)id;s.dex=c3_pda_be16(d+18);s.height=c3_pda_be16(d+10);s.weight=c3_pda_be16(d+12);
    s.growth=d[0];s.types[0]=d[48];s.types[1]=d[49];s.name=c3_pda_be32(d+24);s.category=c3_pda_be32(d+28);
    s.description=c3_pda_be32(d+32);s.seen_flag=c3_pda_be32(d+36);s.caught_flag=c3_pda_be32(d+40);
    for(unsigned i=0;i<6;++i)s.stats[i]=c3_pda_be16(d+52+i*2);
    s.abilities[0]=d[50];s.abilities[1]=d[51];s.color=size==65?d[64]:255;
    *out=s;return 1;
}
int c3_pda_experience(const C3PdaAssets *a,const uint8_t p[312],uint32_t *progress,uint32_t *range)
{
    C3PdaSpecies species;size_t size;
    if(!p||!progress||!range||!c3_pda_species(a,c3_pda_be16(p),&species)||p[0x60]<1||p[0x60]>100||species.growth>=6)return 0;
    if(c3_pda_be16(p+0xd8)&&!(c3_pda_be32(p+0xdc)&0x80000000u))return 0;
    const uint8_t *table=c3_pda_entry(a,9,0,&size);if(!table)return 0;
    *progress=*range=0;if(p[0x60]==100)return 1;
    table+=species.growth*404u;unsigned level=p[0x60];
    uint32_t lo=c3_pda_be32(table+level*4u),hi=c3_pda_be32(table+(level+1u)*4u),exp=c3_pda_be32(p+0x5c);
    if(hi<=lo)return 0;
    *range=hi-lo;*progress=exp>lo?exp-lo:0;
    if(*progress>*range)*progress=*range;
    return 1;
}
static int flag(const C3GameState *state,unsigned id,uint32_t *value)
{*value=0;return !id||c3_game_state_get_flag(state,id,value);}
int c3_pda_memo_caught(const C3PdaAssets *a,const C3GameState *state,unsigned id,int *caught)
{
    C3PdaSpecies s;uint32_t value;if(!caught||!c3_pda_species(a,id,&s)||!flag(state,s.caught_flag,&value))return 0;
    *caught=!!value;return 1;
}
int c3_pda_snag(const C3PdaAssets *a,const C3GameState *state,unsigned index,C3PdaSnag *out)
{
    size_t size;const uint8_t *order=c3_pda_entry(a,5,0,&size);if(!out||!order||index>=48)return 0;
    unsigned id=c3_pda_be16(order+index*2);const uint8_t *d=c3_pda_entry(a,4,id,&size);if(!d)return 0;
    C3PdaSnag s={0};s.id=(uint16_t)id;s.species=c3_pda_be16(d+2);s.level=c3_pda_be16(d+8);s.slot=c3_pda_be16(d+10);
    s.unlock_flag=c3_pda_be16(d+12);s.notified_flag=c3_pda_be16(d+14);s.snag_flag=c3_pda_be16(d+18);
    s.purified_flag=c3_pda_be16(d+20);s.seen_flag=c3_pda_be16(d+22);s.hint_flag=c3_pda_be16(d+28);
    uint32_t v;
    if(!flag(state,s.snag_flag,&v))return 0;
    s.snagged=(uint8_t)!!v;
    if(!flag(state,s.purified_flag,&v))return 0;
    s.purified=(uint8_t)!!v;
    if(!flag(state,s.seen_flag,&v))return 0;
    s.seen=(uint8_t)!!v;
    if(!flag(state,s.notified_flag,&v))return 0;
    s.notified=(uint8_t)!!v;
    if(!flag(state,s.hint_flag,&v))return 0;
    s.hint=(uint8_t)(v>255?255:v);
    const uint8_t *nodes=c3_pda_entry(a,11,id,&size);
    if(nodes)for(size_t i=0;i<size;i+=8){uint32_t message=c3_pda_be32(nodes+i+4);if(!message)break;
        unsigned condition=c3_pda_be16(nodes+i+2);
        if(!flag(state,condition,&v))return 0;
        if(!condition||v==nodes[i])s.hint_message=message;
    }
    const uint8_t *body=c3_game_state_body(state,NULL);if(!body)return 0;
    s.location=c3_pda_be32(body+0x1b014+s.slot*12u+12u);
    *out=s;return 1;
}
int c3_pda_snag_text(const C3PdaAssets *a,const C3GameState *state,const C3PdaSnag *snag,char *out,size_t cap)
{
    C3GameSummary summary={0};char location[128]={0};size_t bytes;
    if(!snag||!out||!cap||!c3_game_state_summary(state,&summary))return 0;
    if(c3_pda_text(a,snag->hint_message?snag->hint_message:0x18d1u,summary.name,summary.partner,out,cap))return 1;
    const uint8_t *floor=c3_pda_entry(a,13,snag->location,&bytes);
    unsigned label=floor&&snag->location!=0xfdu&&snag->location!=0xfeu&&snag->location!=0xffu?c3_pda_be32(floor+36):0x18d3u;
    if(!label)label=0x18d3u;
    if(!c3_pda_text(a,label,summary.name,summary.partner,location,sizeof location))return 0;
    return text_context(a,snag->hint_message?snag->hint_message:0x18d1u,summary.name,summary.partner,location,out,cap);
}
int c3_pda_mail_action(const C3PdaAssets *a,const C3GameState *state,unsigned id,C3PdaMailAction *out)
{
    size_t size;const uint8_t *d=c3_pda_entry(a,3,id,&size);if(!d||!out)return 0;
    C3PdaMailAction action={0};action.save.id=(uint16_t)id;action.save.tone=c3_pda_be16(d+8);
    unsigned read_flag=c3_pda_be32(d+32);
    if(read_flag&&read_flag!=UINT32_MAX)action.save.flags[action.save.flag_count++]=(C3GameFlagChange){read_flag,1};
    action.script=c3_pda_be32(d+36);if(action.script==UINT32_MAX)action.script=0;
    if(c3_pda_be32(d+40)&&c3_pda_be32(d+40)!=UINT32_MAX)return 0; /* Current canonical mail has no auto-event payload. */
    unsigned seq=c3_pda_be16(d+2);const uint8_t *nodes=c3_pda_entry(a,8,0,&size);
    if(!nodes)return 0;
    if(seq!=0xffffu){
        if(seq>=78)return 0;
        for(unsigned i=seq;i<78;++i){unsigned shadow=c3_pda_be16(nodes+i*4+2);if(!shadow)break;
            const uint8_t *s=c3_pda_entry(a,4,shadow,&size);if(!s||action.save.hint_count==C3_GAME_MAIL_HINT_MAX)return 0;
            unsigned notice=c3_pda_be16(s+14);
            if(notice){if(action.save.flag_count==C3_GAME_MAIL_FLAG_MAX)return 0;
            action.save.flags[action.save.flag_count++]=(C3GameFlagChange){notice,1};}
            action.save.hints[action.save.hint_count++]=(C3GameMailHint){(uint16_t)shadow,c3_pda_be16(s+12),c3_pda_be16(s+28),nodes[i*4]};
        }
    }
    if(!c3_game_state_mail_read((C3GameState*)state,&action.save,0))return 0;
    *out=action;return 1;
}
