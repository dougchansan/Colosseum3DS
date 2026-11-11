#include "colosseum3ds/inventory.h"
#include "colosseum3ds/pokemon.h"
#include <stdlib.h>
#include <string.h>
static const unsigned offsets[]={0x7f0,0x840,0x8ec,0x92c,0xa2c,0xae4};
static const unsigned capacities[]={20,43,16,64,46,3};
static uint16_t r16(const uint8_t *p){return c3_pda_be16(p);}
static void w16(uint8_t *p,unsigned v){p[0]=(uint8_t)(v>>8);p[1]=(uint8_t)v;}
unsigned c3_inventory_capacity(unsigned p){return p<6?capacities[p]:0;}
int c3_inventory_item(const C3PdaAssets *a,unsigned id,C3InventoryItem *out){size_t n;const uint8_t *d=c3_pda_entry(a,7,id,&n);if(!out||!id||!d||n!=40)return 0;memset(out,0,sizeof *out);out->id=(uint16_t)id;out->kind=d[0];out->important=d[1];out->param=d[4];out->name=c3_pda_be32(d+16);out->description=c3_pda_be32(d+20);out->field_function=c3_pda_be32(d+28);return 1;}
static int pocket(unsigned kind){static const int map[]={-1,2,0,4,3,1,5};return kind<7?map[kind]:-1;}
static uint8_t *bag(uint8_t *b,unsigned p,unsigned s){return b+offsets[p]+s*4;}
static void remove_item(uint8_t *b,unsigned p,unsigned s,unsigned q){uint8_t *d=bag(b,p,s);unsigned old=r16(d+2);if(q<old){w16(d+2,old-q);return;}memmove(d,d+4,(capacities[p]-s-1)*4);memset(bag(b,p,capacities[p]-1),0,4);}
static int add_item(uint8_t *b,const C3PdaAssets *a,unsigned id){C3InventoryItem info;int p;if(!c3_inventory_item(a,id,&info)||(p=pocket(info.kind))<0)return 0;unsigned max=p==4?999:99;for(unsigned i=0;i<capacities[p];++i){uint8_t *d=bag(b,(unsigned)p,i);unsigned at=r16(d),q=r16(d+2);if(at==id){if(q>=max)return 0;w16(d+2,q+1);return 1;}if(!at){w16(d,id);w16(d+2,1);return 1;}}return 0;}
static unsigned max_pp(const C3PdaAssets *a,uint8_t *m){size_t n;const uint8_t *d=c3_pda_entry(a,10,r16(m),&n);return d&&n==56?d[1]*(5+m[3])/5:0;}
static int is_shadow(const uint8_t *p){return r16(p+0xd8)&&(int32_t)c3_pda_be32(p+0xdc)>=0;}
static int valid_move(unsigned id){return id&&id!=0x163&&id!=0x164&&id!=0x165;}
static void w32(uint8_t *p,uint32_t v){p[0]=(uint8_t)(v>>24);p[1]=(uint8_t)(v>>16);p[2]=(uint8_t)(v>>8);p[3]=(uint8_t)v;}
static int recalculate(uint8_t *p,const C3PdaAssets *a,unsigned level){
 static const unsigned map[]={0,1,2,4,5,3};static const unsigned nature_offset[]={0,5,6,9,7,8};
 size_t n,nn,rn;const uint8_t *d=c3_pda_entry(a,15,r16(p),&n),*nature=c3_pda_entry(a,17,c3_pda_be32(p+4)%25,&nn),*rates=c3_pda_entry(a,18,0,&rn);
 if(!d||n!=284||!nature||nn!=40||!rates||rn!=16)return 0;
 C3Pokemon pk={0};pk.species_id=r16(p);pk.current_hp=r16(p+0x8a);c3_pokemon_neutral_nature(&pk);
 for(unsigned i=0;i<6;++i){unsigned k=map[i];pk.base_stats[k]=r16(d+0x84+i*2);pk.iv[k]=(uint8_t)r16(p+0xa4+i*2);pk.ev[k]=r16(p+0x98+i*2);pk.stats[k]=r16(p+0x8c+i*2);}
 for(unsigned i=1;i<6;++i){unsigned rate=nature[nature_offset[i]];if(rate>=8)return 0;pk.nature[i].numerator=rates[rate*2];pk.nature[i].denominator=rates[rate*2+1];}
 if(c3_pokemon_recalculate(&pk,(uint8_t)level)!=C3_POKEMON_OK)return 0;
 for(unsigned i=0;i<6;++i)w16(p+0x8c+i*2,pk.stats[map[i]]);
 p[0x60]=(uint8_t)level;w16(p+0x8a,pk.current_hp);return 1;
}
static C3InventoryCode learn(uint8_t *p,const C3PdaAssets *a,unsigned move,const C3InventoryRequest *q,C3InventoryResult *r){
 int empty=-1;for(unsigned i=0;i<4;++i){unsigned known=r16(p+0x78+i*4);if(known==move)return C3_INVENTORY_NO_EFFECT;if(!known&&empty<0)empty=(int)i;}
 if(empty<0&&q->replacement<0){r->learned_move=(uint16_t)move;r->choice_count=0;for(unsigned i=0;i<4;++i){size_t hn;const uint8_t *known=c3_pda_entry(a,10,r16(p+0x78+i*4),&hn);if(known&&hn==56&&!known[0x12])r->choices[r->choice_count++]=(uint16_t)i;}r->choices[r->choice_count++]=4;return C3_INVENTORY_NEED_REPLACEMENT;}
 int slot=empty<0?q->replacement:empty;if(slot==4)return C3_INVENTORY_NO_EFFECT;if(slot<0||slot>3)return C3_INVENTORY_INVALID;
 uint8_t *m=p+0x78+slot*4;size_t hn;const uint8_t *known=c3_pda_entry(a,10,r16(m),&hn);if(r16(m)&&(!known||hn!=56||known[0x12]))return C3_INVENTORY_FORBIDDEN;size_t n;const uint8_t *d=c3_pda_entry(a,10,move,&n);if(!d||n!=56||!d[1])return C3_INVENTORY_FORBIDDEN;
 w16(m,move);m[2]=d[1];m[3]=0;r->learned_move=(uint16_t)move;return C3_INVENTORY_OK;
}
static void friendship(uint8_t *p,const C3PdaAssets *a,const uint8_t *param,const uint8_t *body){
 int shadow=is_shadow(p);unsigned base=r16(p+0xb0),old=base;
 if(shadow){unsigned id=r16(p+0xd8);if(id>96)return;unsigned initial=r16(body+0x1b014+id*24+0x4a6);if(!initial)initial=1;if(c3_pda_be32(p+0xdc)>=60*initial)return;old=r16(p+0xe4);}
 unsigned effective=base+(shadow?old:0),which=effective<100?0:effective<200?1:2;int delta=(int8_t)param[which];
 if(delta>0){size_t n;const uint8_t *held=c3_pda_entry(a,7,r16(p+0x88),&n);if(held&&n==40&&r16(held+10)==0x1b)delta=delta*150/100;if(p[15]==11)++delta;}
 int next=(int)old+delta;if(next<0)next=0;if(next>255)next=255;w16(p+(shadow?0xe4:0xb0),(unsigned)next);
}
static C3InventoryCode learn_level(uint8_t *p,const C3PdaAssets *a,const C3InventoryRequest *q,C3InventoryResult *r,unsigned *pending){
 size_t n;const uint8_t *d=c3_pda_entry(a,15,r16(p),&n);if(!d||n!=284)return C3_INVENTORY_FORBIDDEN;
 for(unsigned i=0;i<20;++i){const uint8_t *e=d+0xba+i*4;if(e[0]!=p[0x60]||!r16(e+2))continue;unsigned move=r16(e+2);int known=0;for(unsigned k=0;k<4;++k)if(r16(p+0x78+k*4)==move)known=1;if(known)continue;
  C3InventoryRequest selection=*q;selection.replacement=-1;
  if(*pending<q->replacement_count)selection.replacement=q->replacements[*pending];else if(*pending==0)selection.replacement=q->replacement;
  r->learning_index=(*pending)++;C3InventoryCode code=learn(p,a,move,&selection,r);if(code!=C3_INVENTORY_OK&&code!=C3_INVENTORY_NO_EFFECT)return code;
 }
 return C3_INVENTORY_OK;
}
static unsigned evolution_target(uint8_t *p,const C3PdaAssets *a,unsigned item){
 size_t n;const uint8_t *d=c3_pda_entry(a,15,r16(p),&n);if(!d||n!=284)return 0;
 const uint8_t *held=c3_pda_entry(a,7,r16(p+0x88),&n);if(held&&n==40&&r16(held+10)==0x26)return 0;
 unsigned result=0;for(unsigned i=0;i<5;++i){const uint8_t *e=d+0x9c+i*6;unsigned kind=e[0],v=r16(e+2);int match=0;
  if(item)match=kind==7&&v==item;
  else if(kind==1)match=r16(p+0xb0)>=220;
  else if(kind==4)match=p[0x60]>=v;
  else if(kind>=8&&kind<=10&&p[0x60]>=v){unsigned atk=r16(p+0x8e),def=r16(p+0x90);match=kind==8?atk>def:kind==9?atk==def:atk<def;}
  else if((kind==11||kind==12)&&p[0x60]>=v){unsigned digit=(c3_pda_be32(p+4)>>16)%10;match=kind==11?digit<5:digit>=5;}
  else if(kind==15)match=p[0xb3]>=v;
  if(match)result=r16(e+4);
 }
 return result;
}
static C3InventoryCode evolve(uint8_t *p,const C3PdaAssets *a,const C3InventoryRequest *q,C3InventoryResult *r,unsigned target,unsigned *pending){
 if(!target)return C3_INVENTORY_OK;
 if(q->evolution<0){r->choices[0]=(uint16_t)target;r->choice_count=1;return C3_INVENTORY_NEED_EVOLUTION;}
 if(q->evolution==0)return C3_INVENTORY_OK;
 if((unsigned)q->evolution!=target)return C3_INVENTORY_INVALID;
 size_t n;const uint8_t *old=c3_pda_entry(a,15,r16(p),&n);if(!old||n!=284)return C3_INVENTORY_FORBIDDEN;
 size_t name_n;const uint8_t *name=c3_pda_entry(a,1,c3_pda_be32(old+24),&name_n);int default_name=name&&name_n>=2&&!memcmp(p+0x2e,name,name_n<22?name_n:22);
 w16(p,target);if(!recalculate(p,a,p[0x60]))return C3_INVENTORY_FORBIDDEN;
 if(default_name){const uint8_t *next=c3_pda_entry(a,15,target,&n);const uint8_t *text=next?c3_pda_entry(a,1,c3_pda_be32(next+24),&name_n):NULL;if(text){memset(p+0x2e,0,22);memcpy(p+0x2e,text,name_n<22?name_n:22);}}
 r->evolved_species=(uint16_t)target;return learn_level(p,a,q,r,pending);
}
static C3InventoryCode special_use(uint8_t *p,const C3PdaAssets *a,const C3InventoryRequest *q,C3InventoryResult *r,const C3InventoryItem *item,const uint8_t *body){
 size_t n;const uint8_t *d=c3_pda_entry(a,15,r16(p),&n);if(!d||n!=284||is_shadow(p))return C3_INVENTORY_FORBIDDEN;
 if(item->field_function==0x80022834u){size_t tn;const uint8_t *tm=c3_pda_entry(a,16,0,&tn);size_t in;const uint8_t *info=c3_pda_entry(a,7,item->id,&in);if(item->kind!=4||!info||in!=40||!tm||tn!=58*8)return C3_INVENTORY_FORBIDDEN;unsigned index=(uint8_t)c3_pda_be32(info+24);if(index>=58)return C3_INVENTORY_FORBIDDEN;if(!d[0x34+index])return C3_INVENTORY_NO_EFFECT;C3InventoryRequest selection=*q;if(q->replacement_count)selection.replacement=q->replacements[0];C3InventoryCode code=learn(p,a,c3_pda_be32(tm+index*8+4),&selection,r);if(code==C3_INVENTORY_OK){size_t fn;const uint8_t *filters=c3_pda_entry(a,18,2,&fn);if(filters&&fn==36)friendship(p,a,filters+12,body);}return code;}
 if(item->field_function==0x80022ee4u){unsigned target=evolution_target(p,a,item->id),pending=0;if(!target||q->evolution==0)return C3_INVENTORY_NO_EFFECT;return evolve(p,a,q,r,target,&pending);}
 if(item->field_function==0x800232f0u){unsigned level=p[0x60];if(level>=100)return C3_INVENTORY_NO_EFFECT;size_t gn;const uint8_t *growth=c3_pda_entry(a,9,0,&gn);if(d[0]>=6||!growth||gn!=2424||!recalculate(p,a,level+1))return C3_INVENTORY_FORBIDDEN;
  w32(p+0x5c,c3_pda_be32(growth+d[0]*404+(level+1)*4));
  unsigned pending=0;C3InventoryCode code=learn_level(p,a,q,r,&pending);if(code!=C3_INVENTORY_OK)return code;
  size_t pn;const uint8_t *param=c3_pda_entry(a,14,item->param,&pn);if(param&&pn==16)friendship(p,a,param+5,body);
  return evolve(p,a,q,r,evolution_target(p,a,0),&pending);
 }
 return C3_INVENTORY_FORBIDDEN;
}
static C3InventoryCode use_item(uint8_t *b,const C3PdaAssets *a,const C3InventoryRequest *q,C3InventoryResult *r,const C3InventoryItem *item){
 uint8_t *p=b+0xa0+q->party*312;uint8_t original[312];memcpy(original,p,312);size_t n;const uint8_t *param=c3_pda_entry(a,14,item->param,&n);unsigned fn=item->field_function;
 if(p[0xcb]||!r16(p))return C3_INVENTORY_FORBIDDEN;
 if(fn==0x80022e54u){
  /* The field wrapper consumes cologne for every valid target. The script
   * clears Hyper Mode even when there is no heart gauge left to reduce. */
  int32_t dp=(int32_t)c3_pda_be32(p+0xdc);
  if(is_shadow(p)&&dp>0){size_t nn,rn,fnn,in;const uint8_t *nature=c3_pda_entry(a,17,c3_pda_be32(p+4)%25,&nn),*rates=c3_pda_entry(a,18,0,&rn),*filter=c3_pda_entry(a,18,1,&fnn),*info=c3_pda_entry(a,7,item->id,&in);
   if(!nature||nn!=40||!rates||rn!=16||!filter||fnn!=8||!info||in!=40||nature[4]>=8||!rates[nature[4]*2+1])return C3_INVENTORY_FORBIDDEN;
   float delta=(float)(int8_t)filter[4]*(float)c3_pda_be32(info+24);delta*=rates[nature[4]*2];delta/=rates[nature[4]*2+1];float next=(float)dp/100.0f+delta;if(next<0)next=0;w32(p+0xdc,(uint32_t)(int32_t)(next*100.0f));
  }
  if(r16(p+0xe8)==0x3e){memset(p+0xe8,0,7);memset(p+0xf0,0,6);}return C3_INVENTORY_OK;
 }
 if(fn==0x80022834u||fn==0x80022ee4u||fn==0x800232f0u)return special_use(p,a,q,r,item,b);
 if(r16(p+0xe8)==0x3e)return C3_INVENTORY_FORBIDDEN;
 if(fn!=0x80023b9cu&&fn!=0x80023968u&&fn!=0x80023068u&&fn!=0x80022478u)return C3_INVENTORY_FORBIDDEN;
 if(!param||n!=16||!item->param)return C3_INVENTORY_FORBIDDEN;
 if((param[3]&0x40)||(param[4]&0x10))return C3_INVENTORY_FORBIDDEN;
 if((param[4]&0x88)||((param[4]&0x20)&&param[11])){
  if(q->move<0){for(unsigned i=0;i<4;++i)if(valid_move(r16(p+0x78+i*4)))r->choices[r->choice_count++]=(uint16_t)i;return r->choice_count?C3_INVENTORY_NEED_MOVE:C3_INVENTORY_NO_EFFECT;}
  if(q->move>3)return C3_INVENTORY_INVALID;
 }
 /* Native persistent condition is one 16-byte status record, whose ID is
  * the first big-endian halfword. Clearing preserves no stale counters. */
 unsigned status=r16(p+0x64),mask=0;
 if(status==8)mask=0x20;else if(status==3||status==4)mask=0x10;else if(status==6)mask=8;else if(status==7)mask=4;else if(status==5)mask=2;
 if(mask&&(param[3]&mask)){memset(p+0x64,0,7);memset(p+0x6c,0,6);}
 unsigned hp=r16(p+0x8a),max=r16(p+0x8c),boost=param[10];int revive=(param[4]&0x40)!=0;
 if(boost&&hp<max&&((hp==0)==revive)){unsigned gain=boost==255?max:boost==254?(max/2?max/2:1):boost==253?0:boost;unsigned next=hp+gain;w16(p+0x8a,next>max?max:next);}
 for(unsigned i=0;i<4;++i){uint8_t *m=p+0x78+i*4;unsigned id=r16(m);if(!valid_move(id))continue;unsigned maxpp=max_pp(a,m);if(!maxpp)continue;
  if((param[4]&0x88)&&q->move==(int)i&&m[3]<3){unsigned old=maxpp;m[3]=(param[4]&8)?3:(uint8_t)(m[3]+1);maxpp=max_pp(a,m);m[2]=(uint8_t)(m[2]+maxpp-old);}
  if(param[11]&&(!(param[4]&0x20)||q->move==(int)i)&&m[2]<maxpp){unsigned next=m[2]+param[11];m[2]=(uint8_t)(next>maxpp?maxpp:next);}
 }
 unsigned total=0;for(unsigned i=0;i<6;++i)total+=r16(p+0x98+i*2);
 static const unsigned evparam[]={8,9,12,15,14,13};int changed_ev=0;
 for(unsigned i=0;i<6;++i){unsigned delta=param[evparam[i]],old=r16(p+0x98+i*2);if(!delta||old>=100||total>=510||(i==0&&r16(p)==0x12f))continue;unsigned gain=delta;if(gain>100-old)gain=100-old;if(gain>510-total)gain=510-total;w16(p+0x98+i*2,old+gain);total+=gain;changed_ev=1;}
 if(changed_ev&&!recalculate(p,a,p[0x60]))return C3_INVENTORY_FORBIDDEN;
 if(memcmp(original,p,312))friendship(p,a,param+5,b);
 return C3_INVENTORY_OK;
}
static int memo_evolution(uint8_t *body,const uint8_t *p){
 uint8_t *memo=body+0x82a8;unsigned count=r16(memo),species=r16(p);if(count>500)return 0;
 for(unsigned i=0;i<count;++i)if((r16(memo+4+i*12)&0x3fff)==species)return 1;
 if(count==500)return 0;
 w16(memo+4+count*12,species|0x8000);memcpy(memo+8+count*12,p+0x14,4);memcpy(memo+12+count*12,p+4,4);w16(memo,count+1);return 1;
}
static int evolution_extra(uint8_t *after,const uint8_t *before,const C3PdaAssets *a,const C3InventoryRequest *q,const uint8_t *evolved){
 size_t n;const uint8_t *data=c3_pda_entry(a,15,r16(before+0xa0+q->party*312),&n);if(!data||n!=284)return 0;
 unsigned species=0;for(unsigned i=0;i<5;++i){const uint8_t *e=data+0x9c+i*6;if(e[0]==14&&evolved[0x60]>=r16(e+2))species=r16(e+4);}if(!species)return 1;
 unsigned slot;for(slot=0;slot<6;++slot)if(!r16(after+0xa0+slot*312))break;if(slot==6)return 1;
 uint8_t *added=after+0xa0+slot*312;memcpy(added,evolved,312);w16(added,species);w16(added+0x88,0);added[0xcf]=0;w16(added+0xd4,0);memset(added+0xb7,0,5);memset(added+0xbd,0,13);added[0xd0]=0;
 memset(added+0x64,0,7);memset(added+0x6c,0,6);w32(added+0x74,0);
 const uint8_t *next=c3_pda_entry(a,15,species,&n);size_t text_n;const uint8_t *text=next&&n==284?c3_pda_entry(a,1,c3_pda_be32(next+24),&text_n):NULL;if(text){memset(added+0x2e,0,22);memcpy(added+0x2e,text,text_n<22?text_n:22);}
 if(!recalculate(added,a,added[0x60]))return 0;
 return memo_evolution(after,added);
}
C3InventoryCode c3_inventory_apply(C3GameState *state,const C3PdaAssets *a,const C3InventoryRequest *q,C3InventoryResult *out){C3InventoryResult local={0};C3InventoryResult *r=out?out:&local;memset(r,0,sizeof *r);r->code=C3_INVENTORY_INVALID;if(!state||!a||!q)return r->code;size_t n;const uint8_t *body=c3_game_state_body(state,&n);if(!body||n!=122832||q->party>=6||q->action>C3_INVENTORY_REORDER||q->replacement_count>20)return r->code;
 uint8_t *before=malloc(n),*after=malloc(n);if(!before||!after){free(before);free(after);return r->code;}memcpy(before,body,n);memcpy(after,body,n);uint8_t *p=after+0xa0+q->party*312;C3InventoryCode code=C3_INVENTORY_INVALID;C3InventoryItem item;unsigned id=0;
 if(q->action==C3_INVENTORY_REORDER&&q->pocket==6){if(q->slot>=6||q->destination>=6)goto done;if(q->slot==q->destination){code=C3_INVENTORY_NO_EFFECT;goto done;}uint8_t tmp[312];memcpy(tmp,after+0xa0+q->slot*312,312);memcpy(after+0xa0+q->slot*312,after+0xa0+q->destination*312,312);memcpy(after+0xa0+q->destination*312,tmp,312);code=C3_INVENTORY_OK;goto done;}
 if(q->action==C3_INVENTORY_TAKE){id=r16(p+0x88);if(!r16(p)||!id){code=C3_INVENTORY_NO_EFFECT;goto done;}if(!add_item(after,a,id)){code=C3_INVENTORY_FULL;goto done;}w16(p+0x88,0);code=C3_INVENTORY_OK;goto done;}
 if(q->pocket>=6||q->slot>=capacities[q->pocket])goto done;
 uint8_t *d=bag(after,q->pocket,q->slot);id=r16(d);unsigned count=r16(d+2);r->item=(uint16_t)id;if(!id||!count||!c3_inventory_item(a,id,&item))goto done;
 if(q->action==C3_INVENTORY_REORDER){if(q->destination>=capacities[q->pocket])goto done;if(q->destination==q->slot){code=C3_INVENTORY_NO_EFFECT;goto done;}uint8_t tmp[4];memcpy(tmp,d,4);if(q->destination>q->slot)memmove(d,d+4,(q->destination-q->slot)*4);else memmove(bag(after,q->pocket,q->destination+1),bag(after,q->pocket,q->destination),(q->slot-q->destination)*4);memcpy(bag(after,q->pocket,q->destination),tmp,4);code=C3_INVENTORY_OK;goto done;}
 if(q->action==C3_INVENTORY_TOSS){if(item.important||item.kind==5){code=C3_INVENTORY_FORBIDDEN;goto done;}if(!q->quantity||q->quantity>count)goto done;remove_item(after,q->pocket,q->slot,q->quantity);code=C3_INVENTORY_OK;goto done;}
 if(q->action==C3_INVENTORY_GIVE){if(!r16(p)||p[0xcb]||item.important||item.kind==5||item.kind==6){code=C3_INVENTORY_FORBIDDEN;goto done;}unsigned held=r16(p+0x88);if(held==id){code=C3_INVENTORY_NO_EFFECT;goto done;}remove_item(after,q->pocket,q->slot,1);if(held&&!add_item(after,a,held)){code=C3_INVENTORY_FULL;goto done;}w16(p+0x88,id);code=C3_INVENTORY_OK;goto done;}
 if(item.field_function==0x80023760u){C3InventoryItem ash=item;ash.field_function=0x80023b9c;code=C3_INVENTORY_OK;for(unsigned i=0;i<6;++i){if(!r16(after+0xa0+i*312)||r16(after+0xa0+i*312+0x8a)||after[0xa0+i*312+0xcb]||r16(after+0xa0+i*312+0xe8)==0x3e)continue;C3InventoryRequest target=*q;target.party=i;C3InventoryCode effect=use_item(after,a,&target,r,&ash);if(effect!=C3_INVENTORY_OK){code=effect;break;}}}
 else code=use_item(after,a,q,r,&item);
 if(code==C3_INVENTORY_OK){if(item.field_function!=0x80022e54u&&!memcmp(before+0xa0,after+0xa0,6*312)){code=C3_INVENTORY_NO_EFFECT;goto done;}if(r->evolved_species&&(!memo_evolution(after,p)||(item.field_function==0x800232f0u&&!evolution_extra(after,before,a,q,p)))){code=C3_INVENTORY_FULL;goto done;}size_t item_n;const uint8_t *definition=c3_pda_entry(a,7,id,&item_n);int hm=item.kind==4&&definition&&item_n==40&&(uint8_t)c3_pda_be32(definition+24)>=50;int flute=item.field_function==0x80023b9cu&&id>=39&&id<=43;if(!hm&&!flute)remove_item(after,q->pocket,q->slot,1);}
 done:if(code==C3_INVENTORY_NEED_MOVE||code==C3_INVENTORY_NEED_REPLACEMENT||code==C3_INVENTORY_NEED_EVOLUTION)memcpy(r->pokemon,p,312);if(code==C3_INVENTORY_OK&&!c3_game_state_inventory_commit(state,before,after,n))code=C3_INVENTORY_CONFLICT;r->code=code;free(before);free(after);return code;
}

int c3_inventory_receive(C3GameState *state,const C3PdaAssets *assets,unsigned id,unsigned count)
{
    size_t bytes=0;const uint8_t *body=c3_game_state_body(state,&bytes);C3InventoryItem info;
    if(!body||!assets||!count||count>999||!c3_inventory_item(assets,id,&info)||pocket(info.kind)<0)return -1;
    uint8_t *before=malloc(bytes),*after=malloc(bytes);int result=-1;
    if(before&&after){
        unsigned added=0;
        memcpy(before,body,bytes);memcpy(after,body,bytes);
        while(added<count&&add_item(after,assets,id))++added;
        if(added!=count)result=(int)count;
        else result=c3_game_state_inventory_commit(state,before,after,bytes)?0:-1;
    }
    free(before);free(after);return result;
}

int c3_inventory_restore_party(C3GameState *state,const C3PdaAssets *assets)
{
    size_t bytes=0;const uint8_t *body=c3_game_state_body(state,&bytes);
    if(!body||!assets)return -1;
    uint8_t *before=malloc(bytes),*after=malloc(bytes);int restored=-1;
    if(before&&after){
        memcpy(before,body,bytes);memcpy(after,body,bytes);restored=0;
        for(unsigned slot=0;slot<6;++slot){
            uint8_t *p=after+0xa0+slot*312;unsigned status=r16(p+0x64);
            if(!r16(p))continue;
            w16(p+0x8a,r16(p+0x8c));
            for(unsigned i=0;i<4;++i){uint8_t *m=p+0x78+i*4;unsigned maximum;
                if(!valid_move(r16(m))||!(maximum=max_pp(assets,m)))continue;
                m[2]=(uint8_t)maximum;
            }
            if(status>=3&&status<=8){memset(p+0x64,0,7);memset(p+0x6c,0,6);}
            ++restored;
        }
        if(!c3_game_state_inventory_commit(state,before,after,bytes))restored=-1;
    }
    free(before);free(after);return restored;
}
