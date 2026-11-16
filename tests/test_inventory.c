#define main c3_inventory_game_state_fixture_main
#include "test_game_state.c"
#undef main
#include "colosseum3ds/inventory.h"
static void leput(uint8_t *p,unsigned v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);}
static uint8_t assets_memory[16384];static unsigned entries,asset_end;
static uint8_t *entry(unsigned kind,unsigned key,unsigned size){uint8_t *e=assets_memory+64+entries++*16;leput(e,kind);leput(e+4,key);leput(e+8,asset_end);leput(e+12,size);uint8_t *d=assets_memory+asset_end;asset_end+=size;return d;}
static C3PdaAssets inventory_assets(void){memset(assets_memory,0,sizeof assets_memory);entries=0;asset_end=64+32*16;uint8_t *d;
 d=entry(7,1,40);d[0]=2;d[4]=1;w32(d+28,0x80023b9c);
 d=entry(7,2,40);d[0]=2;d[4]=2;w32(d+28,0x80023968);
 d=entry(7,3,40);d[0]=2;d[4]=3;w32(d+28,0x80023b9c);
 d=entry(7,4,40);d[0]=5;d[1]=1;
 d=entry(7,5,40);d[0]=2;d[4]=4;w32(d+28,0x80023068);
 d=entry(7,6,40);d[0]=3;
 d=entry(7,7,40);d[0]=2;
 d=entry(7,8,40);d[0]=2;d[4]=5;w32(d+28,0x800232f0);
 d=entry(7,9,40);d[0]=2;w32(d+28,0x80022ee4);
 d=entry(7,10,40);d[0]=2;d[4]=3;w32(d+28,0x80023760);
 d=entry(7,11,40);d[0]=2;d[4]=6;w32(d+28,0x80023b9c);
 d=entry(9,0,2424);for(unsigned i=0;i<101;++i)w32(d+i*4,i*i*i);
 for(unsigned i=10;i<=14;++i){d=entry(10,i,56);d[1]=20;}
 d=entry(14,1,16);d[3]=0x30;d[10]=20;d[5]=5;d[6]=3;d[7]=2;
 d=entry(14,2,16);d[4]=0x20;d[11]=10;
 d=entry(14,3,16);d[4]=0x40;d[10]=0xfe;
 d=entry(14,4,16);d[4]=0x80;
 d=entry(14,5,16);d[3]=0x40;d[5]=5;d[6]=3;d[7]=2;
 d=entry(14,6,16);d[8]=10;d[5]=5;
 d=entry(15,197,284);for(unsigned i=0;i<6;++i)w16(d+0x84+i*2,60);d[0x9c]=4;w16(d+0x9e,26);w16(d+0xa0,198);d[0xa2]=7;w16(d+0xa4,9);w16(d+0xa6,198);d[0xba]=26;w16(d+0xbc,13);d[0xbe]=26;w16(d+0xc0,14);
 d=entry(15,198,284);for(unsigned i=0;i<6;++i)w16(d+0x84+i*2,70);
 d=entry(17,0,40);
 d=entry(18,0,16);d[0]=1;d[1]=1;
 CHECK(entries==27);return (C3PdaAssets){assets_memory,asset_end,entries};
}
static void putbag(uint8_t *body,unsigned slot,unsigned id,unsigned count){w16(body+0x7f0+slot*4,id);w16(body+0x7f2+slot*4,count);}
int main(int argc,char **argv){char directory[]="/tmp/c3-inventory-XXXXXX",path[1024],error[192];CHECK(mkdtemp(directory)!=NULL);uint8_t *pack=synthetic_pack();snprintf(path,sizeof path,"%s/game.c3g",directory);write_file(path,pack,PACK_SIZE);C3GameState state={0};CHECK(c3_game_state_new(&state,"WES",7,path,error));C3PdaAssets a=inventory_assets();uint8_t *body=(uint8_t*)c3_game_state_body(&state,NULL),*p=body+PARTY;memset(body+0x7f0,0,80);putbag(body,0,1,4);putbag(body,1,2,2);putbag(body,2,3,2);putbag(body,3,4,1);putbag(body,4,5,1);w16(p+0x8a,20);w16(p+0x8c,100);memset(p+0x78,0,16);w16(p+0x78,10);p[0x7a]=3;p[0x7b]=0;p[4]=p[5]=p[6]=p[7]=0;
 C3InventoryRequest q={.action=C3_INVENTORY_USE,.move=-1,.replacement=-1,.evolution=-1};C3InventoryResult r;CHECK(c3_inventory_apply(&state,&a,&q,&r)==C3_INVENTORY_OK);CHECK(r16(p+0x8a)==40&&r16(body+0x7f2)==3);
 w16(p+0x8a,100);uint8_t snapshot[122832];memcpy(snapshot,body,sizeof snapshot);CHECK(c3_inventory_apply(&state,&a,&q,&r)==C3_INVENTORY_NO_EFFECT);CHECK(!memcmp(snapshot,body,sizeof snapshot));
 q.slot=1;CHECK(c3_inventory_apply(&state,&a,&q,&r)==C3_INVENTORY_NEED_MOVE);CHECK(r.choice_count==1&&r.choices[0]==0);CHECK(!memcmp(snapshot,body,sizeof snapshot));q.move=0;CHECK(c3_inventory_apply(&state,&a,&q,&r)==C3_INVENTORY_OK);CHECK(p[0x7a]==13);
 q.slot=2;q.move=-1;CHECK(c3_inventory_apply(&state,&a,&q,&r)==C3_INVENTORY_NO_EFFECT);w16(p+0x8a,0);CHECK(c3_inventory_apply(&state,&a,&q,&r)==C3_INVENTORY_OK);CHECK(r16(p+0x8a)==50);
 q.slot=4;q.move=-1;CHECK(c3_inventory_apply(&state,&a,&q,&r)==C3_INVENTORY_NEED_MOVE);q.move=0;CHECK(c3_inventory_apply(&state,&a,&q,&r)==C3_INVENTORY_OK);CHECK(p[0x7b]==1&&p[0x7a]==17);
 q.action=C3_INVENTORY_TOSS;q.slot=3;q.quantity=1;CHECK(c3_inventory_apply(&state,&a,&q,&r)==C3_INVENTORY_FORBIDDEN);
 q.action=C3_INVENTORY_GIVE;q.slot=0;CHECK(c3_inventory_apply(&state,&a,&q,&r)==C3_INVENTORY_OK);CHECK(r16(p+0x88)==1&&r16(body+0x7f2)==2);
 q.action=C3_INVENTORY_TAKE;CHECK(c3_inventory_apply(&state,&a,&q,&r)==C3_INVENTORY_OK);CHECK(!r16(p+0x88)&&r16(body+0x7f2)==3);
 q.action=C3_INVENTORY_TOSS;q.slot=0;q.quantity=3;CHECK(c3_inventory_apply(&state,&a,&q,&r)==C3_INVENTORY_OK);CHECK(r16(body+0x7f0)==2);
 q.action=C3_INVENTORY_REORDER;q.slot=0;q.destination=1;CHECK(c3_inventory_apply(&state,&a,&q,&r)==C3_INVENTORY_OK);CHECK(r16(body+0x7f0)==3&&r16(body+0x7f4)==2);
 uint8_t oldparty[624];memcpy(oldparty,p,sizeof oldparty);q.pocket=6;q.slot=0;q.destination=1;CHECK(c3_inventory_apply(&state,&a,&q,&r)==C3_INVENTORY_OK);CHECK(!memcmp(p,oldparty+312,312)&&!memcmp(p+312,oldparty,312));
 /* Distinct native poison/sleep IDs clear their own cure bits and keep padding. */
 q=(C3InventoryRequest){.action=C3_INVENTORY_REORDER,.pocket=6,.slot=0,.destination=1};CHECK(c3_inventory_apply(&state,&a,&q,&r)==C3_INVENTORY_OK);
 memset(body+0x7f0,0,80);putbag(body,0,1,4);w16(p+0x8a,100);w16(p+0x8c,100);w16(p+0x64,8);p[0x6b]=0x91;p[0x72]=0x92;p[0x73]=0x93;
 q=(C3InventoryRequest){.action=C3_INVENTORY_USE,.move=-1,.replacement=-1,.evolution=-1};CHECK(c3_inventory_apply(&state,&a,&q,&r)==C3_INVENTORY_OK);CHECK(!r16(p+0x64)&&p[0x6b]==0x91&&p[0x72]==0x92&&p[0x73]==0x93);
 w16(p+0x64,3);CHECK(c3_inventory_apply(&state,&a,&q,&r)==C3_INVENTORY_OK);CHECK(!r16(p+0x64));
 /* Put party back so the existing repeat setup starts from the original. */
 q=(C3InventoryRequest){.action=C3_INVENTORY_REORDER,.pocket=6,.slot=0,.destination=1};CHECK(c3_inventory_apply(&state,&a,&q,&r)==C3_INVENTORY_OK);
 /* Repeat the staged transaction from an unchanged authoritative save. */
 q=(C3InventoryRequest){.action=C3_INVENTORY_REORDER,.pocket=6,.slot=0,.destination=1};CHECK(c3_inventory_apply(&state,&a,&q,&r)==C3_INVENTORY_OK);
 memset(body+0x7f0,0,80);putbag(body,0,8,2);w16(p,197);p[0x60]=25;w16(p+0xb0,50);for(unsigned i=0;i<4;++i){w16(p+0x78+i*4,10+i);p[0x7a+i*4]=20;p[0x7b+i*4]=0;}
 /* Only move14 is unknown now; make both13 and14 pending by repeating10. */
 w16(p+0x84,10);memcpy(snapshot,body,sizeof snapshot);q=(C3InventoryRequest){.action=C3_INVENTORY_USE,.move=-1,.replacement=-1,.evolution=-1};
 CHECK(c3_inventory_apply(&state,&a,&q,&r)==C3_INVENTORY_NEED_REPLACEMENT&&r.learning_index==0&&r.learned_move==13);CHECK(!memcmp(snapshot,body,sizeof snapshot));
 q.replacements[0]=0;q.replacement_count=1;CHECK(c3_inventory_apply(&state,&a,&q,&r)==C3_INVENTORY_NEED_REPLACEMENT&&r.learning_index==1&&r.learned_move==14);CHECK(!memcmp(snapshot,body,sizeof snapshot));
 q.replacements[1]=1;q.replacement_count=2;CHECK(c3_inventory_apply(&state,&a,&q,&r)==C3_INVENTORY_NEED_EVOLUTION&&r.choices[0]==198);CHECK(!memcmp(snapshot,body,sizeof snapshot));
 q.evolution=198;CHECK(c3_inventory_apply(&state,&a,&q,&r)==C3_INVENTORY_OK);CHECK(r16(p)==198&&p[0x60]==26&&r16(p+0x78)==13&&r16(p+0x7c)==14&&r16(p+0xb0)==55&&r16(body+0x7f2)==1);
 /* A capped vitamin must not consume solely because it has friendship. */
 putbag(body,0,11,1);w16(p+0x98,100);w16(p+0xb0,50);memcpy(snapshot,body,sizeof snapshot);q=(C3InventoryRequest){.action=C3_INVENTORY_USE,.move=-1,.replacement=-1,.evolution=-1};CHECK(c3_inventory_apply(&state,&a,&q,&r)==C3_INVENTORY_NO_EFFECT);CHECK(!memcmp(snapshot,body,sizeof snapshot));
 /* Sacred Ash restores all fainted valid party records with one charge. */
 putbag(body,0,10,1);w16(p+0x8a,0);w16(p+312+0x8a,0);w16(p+0x8c,100);w16(p+312+0x8c,80);CHECK(c3_inventory_apply(&state,&a,&q,&r)==C3_INVENTORY_OK);CHECK(r16(p+0x8a)==50&&r16(p+312+0x8a)==40&&!r16(body+0x7f0));
 /* Friendship pools unlock only at the original shadow heart-bar stage. */
 putbag(body,0,1,2);w16(p+0x8a,1);w16(p+0x8c,100);w16(p+0xd8,1);w32(p+0xdc,200);w16(body+0x1b014+24+0x4a6,10);w16(p+0xb0,50);w16(p+0xe4,10);
 CHECK(c3_inventory_apply(&state,&a,&q,&r)==C3_INVENTORY_OK);CHECK(r16(p+0xb0)==50&&r16(p+0xe4)==15);w32(p+0xdc,1000);CHECK(c3_inventory_apply(&state,&a,&q,&r)==C3_INVENTORY_OK);CHECK(r16(p+0xe4)==15);
 /* Imported HMs cannot be forgotten by either prompt or forged requests. */
 w16(p+0xd8,0);w32(p+0xdc,(unsigned)-100);w16(p,197);p[0x60]=25;putbag(body,0,8,1);for(unsigned i=0;i<4;++i){w16(p+0x78+i*4,i==3?10:10+i);p[0x7a+i*4]=20;p[0x7b+i*4]=0;}size_t move_n;uint8_t *hm=(uint8_t*)c3_pda_entry(&a,10,10,&move_n);CHECK(hm&&move_n==56);hm[0x12]=1;q=(C3InventoryRequest){.action=C3_INVENTORY_USE,.move=-1,.replacement=-1,.evolution=-1};CHECK(c3_inventory_apply(&state,&a,&q,&r)==C3_INVENTORY_NEED_REPLACEMENT);CHECK(r.choice_count==3&&r.choices[0]==1&&r.choices[1]==2&&r.choices[2]==4);memcpy(snapshot,body,sizeof snapshot);q.replacement=0;CHECK(c3_inventory_apply(&state,&a,&q,&r)==C3_INVENTORY_FORBIDDEN);CHECK(!memcmp(snapshot,body,sizeof snapshot));hm[0x12]=0;
 uint8_t *before=malloc(122832),*after=malloc(122832);CHECK(before&&after);memcpy(before,body,122832);memcpy(after,body,122832);after[0x7f3]^=1;body[0x7f3]^=2;CHECK(!c3_game_state_inventory_commit(&state,before,after,122832));memcpy(before,body,122832);memcpy(after,body,122832);after[0x10]^=1;CHECK(!c3_game_state_inventory_commit(&state,before,after,122832));
 if(argc>=3){size_t bytes;uint8_t *private_pack=read_file(argv[1],&bytes);C3PdaAssets actual={0};CHECK(c3_pda_assets_open(&actual,private_pack,bytes));C3GameState native={0};CHECK(c3_game_state_new(&native,"WES",7,argv[2],error));uint8_t *raw=(uint8_t*)c3_game_state_body(&native,NULL);uint8_t *mon=raw+PARTY;
  for(unsigned id=1;id<548;++id){C3InventoryItem info;CHECK(c3_inventory_item(&actual,id,&info));}
  memset(raw+0x7f0,0,80);putbag(raw,0,13,2);w16(mon+0x8a,1);q=(C3InventoryRequest){.action=C3_INVENTORY_USE,.move=-1,.replacement=-1,.evolution=-1};CHECK(c3_inventory_apply(&native,&actual,&q,&r)==C3_INVENTORY_OK);CHECK(r16(mon+0x8a)==21&&r16(raw+0x7f2)==1);
  /* Original Rare Candy with a real starter: growth, nature and stats. */
  putbag(raw,0,68,2);unsigned oldlevel=mon[0x60];unsigned oldspecies=r16(mon);C3InventoryCode code=c3_inventory_apply(&native,&actual,&q,&r);while(code==C3_INVENTORY_NEED_REPLACEMENT){q.replacements[r.learning_index]=4;q.replacement_count=r.learning_index+1;code=c3_inventory_apply(&native,&actual,&q,&r);}if(code==C3_INVENTORY_NEED_EVOLUTION){q.evolution=0;code=c3_inventory_apply(&native,&actual,&q,&r);}CHECK(code==C3_INVENTORY_OK);CHECK(mon[0x60]==oldlevel+1&&r16(mon)==oldspecies&&r16(raw+0x7f2)==1);
  /* Original Blue Flute is reusable; Hyper Mode rejects field medicine. */
  putbag(raw,0,39,1);w16(mon+0x64,8);w16(mon+0x8a,r16(mon+0x8c));q=(C3InventoryRequest){.action=C3_INVENTORY_USE,.move=-1,.replacement=-1,.evolution=-1};CHECK(c3_inventory_apply(&native,&actual,&q,&r)==C3_INVENTORY_OK);CHECK(!r16(mon+0x64)&&r16(raw+0x7f0)==39&&r16(raw+0x7f2)==1);
  putbag(raw,0,13,2);w16(mon+0x8a,1);w16(mon+0xe8,0x3e);memcpy(snapshot,raw,sizeof snapshot);CHECK(c3_inventory_apply(&native,&actual,&q,&r)==C3_INVENTORY_FORBIDDEN);CHECK(!memcmp(snapshot,raw,sizeof snapshot));w16(mon+0xe8,0);
  /* Use machine metadata, preserve imported HMs, and initialize learned PP. */
  unsigned machine=0,hm_id=0;for(unsigned id=1;id<548;++id){size_t in;const uint8_t *info=c3_pda_entry(&actual,7,id,&in);if(!info||in!=40||info[0]!=4)continue;unsigned index=(uint8_t)r32(info+24);size_t sn;const uint8_t *species=c3_pda_entry(&actual,15,1,&sn);if(index>=58||!species||sn!=284||!species[0x34+index])continue;if(index<50&&!machine)machine=id;if(index>=50&&!hm_id)hm_id=id;}
  CHECK(machine);w16(mon,1);memset(mon+0x78,0,16);w16(raw+0x92c,machine);w16(raw+0x92e,1);q=(C3InventoryRequest){.action=C3_INVENTORY_USE,.pocket=3,.move=-1,.replacement=-1,.evolution=-1};CHECK(c3_inventory_apply(&native,&actual,&q,&r)==C3_INVENTORY_OK);CHECK(r16(mon+0x78)&&mon[0x7a]&&!mon[0x7b]&&!r16(raw+0x92c));
  if(hm_id){memset(mon+0x78,0,16);w16(raw+0x92c,hm_id);w16(raw+0x92e,1);CHECK(c3_inventory_apply(&native,&actual,&q,&r)==C3_INVENTORY_OK);CHECK(r16(mon+0x78)&&r16(raw+0x92c)==hm_id&&r16(raw+0x92e)==1);}
  /* Original kind14 evolution creates its extra Pokemon in a free slot. */
  unsigned nincada=0,extra=0,atlevel=0;for(unsigned id=1;id<413&&!nincada;++id){size_t en;const uint8_t *sd=c3_pda_entry(&actual,15,id,&en);if(!sd||en!=284)continue;for(unsigned j=0;j<5;++j){const uint8_t *ev=sd+0x9c+j*6;if(ev[0]==14){nincada=id;extra=r16(ev+4);atlevel=r16(ev+2);break;}}}
  CHECK(nincada&&extra&&atlevel>1);w16(mon,nincada);mon[0x60]=(uint8_t)(atlevel-1);w16(mon+0x8a,r16(mon+0x8c));memset(mon+0x78,0,16);memset(raw+PARTY+2*312,0,4*312);putbag(raw,0,68,1);q=(C3InventoryRequest){.action=C3_INVENTORY_USE,.move=-1,.replacement=-1,.evolution=-1};code=c3_inventory_apply(&native,&actual,&q,&r);while(code==C3_INVENTORY_NEED_REPLACEMENT){q.replacements[r.learning_index]=4;q.replacement_count=r.learning_index+1;code=c3_inventory_apply(&native,&actual,&q,&r);}CHECK(code==C3_INVENTORY_NEED_EVOLUTION);q.evolution=r.choices[0];code=c3_inventory_apply(&native,&actual,&q,&r);while(code==C3_INVENTORY_NEED_REPLACEMENT){q.replacements[r.learning_index]=4;q.replacement_count=r.learning_index+1;code=c3_inventory_apply(&native,&actual,&q,&r);}CHECK(code==C3_INVENTORY_OK);CHECK(r16(raw+PARTY+2*312)==extra&&r16(raw+PARTY+2*312+0x8c)==1&&r16(raw+PARTY+2*312+0x8a)==1);
  /* Cologne runs the actual DP filter, including its native hypermode clear. */
  w16(mon+0xd8,1);w32(mon+0xdc,10000);w16(mon+0xe8,0x3e);w16(raw+0xae4,543);w16(raw+0xae6,1);q=(C3InventoryRequest){.action=C3_INVENTORY_USE,.pocket=5,.move=-1,.replacement=-1,.evolution=-1};CHECK(c3_inventory_apply(&native,&actual,&q,&r)==C3_INVENTORY_OK);CHECK(r32(mon+0xdc)<10000&&!r16(mon+0xe8)&&!r16(raw+0xae4));
  /* Cologne is the source-defined consumption exception at an empty gauge
   * and on ordinary Pokemon; it still clears Hyper Mode at zero DP. */
  w32(mon+0xdc,0);w16(mon+0xe8,0x3e);w16(raw+0xae4,543);w16(raw+0xae6,2);CHECK(c3_inventory_apply(&native,&actual,&q,&r)==C3_INVENTORY_OK);CHECK(!r32(mon+0xdc)&&!r16(mon+0xe8)&&r16(raw+0xae6)==1);
  w16(mon+0xd8,0);w32(mon+0xdc,(unsigned)-100);memcpy(snapshot,raw,sizeof snapshot);CHECK(c3_inventory_apply(&native,&actual,&q,&r)==C3_INVENTORY_OK);CHECK(!memcmp(snapshot+PARTY,raw+PARTY,6*312)&&!r16(raw+0xae4));
  printf("Original metadata:547items; Potion and Rare Candy apply against original growth/nature/starter bytes.\n");c3_game_state_free(&native);free(private_pack);
 }
 /* Script gifts: stacks grow to 99; a gift that does not fit adds nothing. */
 {uint8_t *bag=(uint8_t*)c3_game_state_body(&state,NULL);memset(bag+0x7f0,0,80);
  CHECK(c3_inventory_receive(&state,&a,1,3)==0&&r16(bag+0x7f0)==1&&r16(bag+0x7f2)==3);
  CHECK(c3_inventory_receive(&state,&a,2,1)==0&&r16(bag+0x7f4)==2&&r16(bag+0x7f6)==1);
  CHECK(c3_inventory_receive(&state,&a,1,96)==0&&r16(bag+0x7f2)==99);
  CHECK(c3_inventory_receive(&state,&a,1,1)==1&&r16(bag+0x7f2)==99);
  CHECK(c3_inventory_receive(&state,&a,2,99)==99&&r16(bag+0x7f6)==1);
  CHECK(c3_inventory_receive(&state,&a,999,1)==-1&&c3_inventory_receive(&state,&a,1,0)==-1);
  CHECK(c3_inventory_receive(NULL,&a,1,1)==-1&&c3_inventory_receive(&state,NULL,1,1)==-1);}
 /* Party restore: HP, PP of valid moves and a status condition record. */
 {uint8_t *q0=(uint8_t*)c3_game_state_body(&state,NULL)+PARTY;unsigned members=0;
  for(unsigned i=0;i<6;++i)members+=r16(q0+i*312)!=0;
  w16(q0+0x8a,1);w16(q0+0x8c,100);w16(q0+0x64,5);q0[0x66]=9;q0[0x6c]=7;
  uint8_t *m0=q0+0x78;unsigned full;w16(m0,10);m0[3]=0;m0[2]=0;
  CHECK(c3_inventory_restore_party(&state,&a)==(int)members&&members>0);
  full=m0[2];CHECK(r16(q0+0x8a)==100&&full>0&&!r16(q0+0x64)&&!q0[0x66]&&!q0[0x6c]);
  w16(q0+0x64,2);q0[0x66]=4;m0[2]=1;CHECK(c3_inventory_restore_party(&state,&a)==(int)members);
  CHECK(r16(q0+0x64)==2&&q0[0x66]==4&&m0[2]==full); /* condition 2 is not one of 3..8 */
  w16(q0+0x64,0);q0[0x66]=0;
  CHECK(c3_inventory_restore_party(NULL,&a)==-1&&c3_inventory_restore_party(&state,NULL)==-1);}
 free(after);free(before);c3_game_state_free(&state);free(pack);unlink(path);rmdir(directory);printf("Inventory transactions: %u checks passed.\n",checks);return 0;
}
