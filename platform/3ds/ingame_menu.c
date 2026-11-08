#include "ingame_menu.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t le32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static const char *const pockets[]={"ITEMS","KEY ITEMS","POKE BALLS","TMs","BERRIES","COLOGNES"};
static const unsigned pocket_size[]={20,43,16,64,46,3};
static const char *const sorts[]={"Newest","Oldest","Sender","Subject"};
static const char *const tones[]={"Bell 1","Bell 2","Phone","Alarm","Melody 1","Melody 2","Melody 3","Melody 4","Melody 5","Melody 6"};
static const char *const types[]={"NORMAL","FIGHTING","FLYING","POISON","GROUND","ROCK","BUG","GHOST","STEEL","???","FIRE","WATER","GRASS","ELECTRIC","PSYCHIC","ICE","DRAGON","DARK"};
static const char *const colors[]={"RED","BLUE","YELLOW","GREEN","BLACK","BROWN","PURPLE","GRAY","WHITE","PINK"};
static void page(C3IngameMenu *m,C3IngamePage next,unsigned selection)
{m->page=next;m->selection=selection;m->pager=(C3UiPager){0};m->dirty=1;m->held_seconds=m->repeat_seconds=0;}
void c3_ingame_error(C3IngameMenu *m,const char *text)
{if(!m)return;
m->parent=m->page;m->notice_selection=m->selection;snprintf(m->error,sizeof m->error,"%s",text?text:"Unable to open this page.");page(m,C3_GAME_NOTICE,0);}
void c3_ingame_init(C3IngameMenu *m,const C3GameState *state)
{
    if(!m)return;
    memset(m,0,sizeof *m);m->active=m->dirty=1;
    C3GameSummary summary;if(c3_game_state_summary(state,&summary)){m->option_mono=summary.sound_mono;m->option_rumble=summary.rumble_disabled;}
}
void c3_ingame_close(C3IngameMenu *m)
{if(m){free(m->asset_memory);memset(m,0,sizeof *m);}}
int c3_ingame_load(C3IngameMenu *m,const char *path)
{
    if(!m||!path)return 0;
    FILE *f=fopen(path,"rb");if(!f)return 0;
    if(fseek(f,0,SEEK_END)){fclose(f);return 0;}long n=ftell(f);
    if(n<64||n>8*1024*1024||fseek(f,0,SEEK_SET)){fclose(f);return 0;}
    uint8_t *data=malloc((size_t)n);C3PdaAssets a={0};
    int ok=data&&fread(data,1,(size_t)n,f)==(size_t)n&&!ferror(f)&&c3_pda_assets_open(&a,data,(size_t)n);
    if(ok){size_t bytes=0;
        ok=c3_pda_entry(&a,2,197,&bytes)&&bytes==65&&c3_pda_entry(&a,3,31,&bytes)&&
            c3_pda_entry(&a,5,0,&bytes)&&c3_pda_entry(&a,6,0,&bytes)&&c3_pda_entry(&a,9,0,&bytes)&&
            c3_pda_entry(&a,10,44,&bytes)&&c3_pda_entry(&a,11,1,&bytes)&&c3_pda_entry(&a,12,28,&bytes);
    }
    fclose(f);if(!ok){free(data);return 0;}free(m->asset_memory);m->asset_memory=data;m->assets=a;m->dirty=1;return 1;
}
void c3_ingame_set_assets(C3IngameMenu *m,const C3PdaAssets *a)
{if(m&&a){free(m->asset_memory);m->asset_memory=NULL;m->assets=*a;m->dirty=1;}}
static void text(C3IngameMenu *m,const C3GameSummary *s,unsigned id,const char *fallback,char *out,size_t cap)
{if(!c3_pda_text(&m->assets,id,s->name,s->partner,out,cap))snprintf(out,cap,"%s",fallback);}
static void species_name(C3IngameMenu *m,const C3GameSummary *s,unsigned id,char *out,size_t cap)
{C3PdaSpecies p;if(c3_pda_species(&m->assets,id,&p))text(m,s,p.name,"POKEMON",out,cap);else snprintf(out,cap,id==197?"UMBREON":id==196?"ESPEON":"POKEMON %u",id);}
static int list(C3IngameMenu *m,const C3GameState *state)
{
    m->count=0;
    if(m->page==C3_GAME_PARTY){for(unsigned i=0;i<6;++i){uint8_t p[312];if(!c3_game_state_pokemon(state,i,p))return 0;
    if(c3_pda_be16(p))m->order[m->count++]=i;}}
    else if(m->page==C3_GAME_ITEMS){for(unsigned i=0;i<pocket_size[m->pocket];++i){C3GameItem item;if(!c3_game_state_item(state,m->pocket,i,&item))return 0;
    if(item.id&&item.quantity)m->order[m->count++]=i;}}
    else if(m->page==C3_GAME_MAIL){
        unsigned tone;if(!c3_game_state_mail_count(state,&m->count)||!c3_game_state_mail_settings(state,&m->sort,&tone))return 0;
        for(unsigned i=0;i<m->count;++i){C3GameMail mail;if(!c3_game_state_mail(state,i,&mail))return 0;
        m->order[i]=m->sort==0?m->count-1u-i:i;}
        if(m->sort>=2){C3GameSummary s;if(!c3_game_state_summary(state,&s))return 0;
            for(unsigned i=1;i<m->count;++i){unsigned key=m->order[i],j=i;C3GameMail a;char first[256],second[256];size_t n;
                c3_game_state_mail(state,key,&a);const uint8_t *e=c3_pda_entry(&m->assets,3,a.id,&n);if(!e)return 0;
                text(m,&s,c3_pda_be32(e+(m->sort==2?12:16)),"",first,sizeof first);
                while(j){C3GameMail b;c3_game_state_mail(state,m->order[j-1],&b);e=c3_pda_entry(&m->assets,3,b.id,&n);if(!e)return 0;
                    text(m,&s,c3_pda_be32(e+(m->sort==2?12:16)),"",second,sizeof second);
                    if(strcmp(first,second)>=0)break;
                    m->order[j]=m->order[j-1];--j;}m->order[j]=key;
            }
        }
    }else if(m->page==C3_GAME_MEMO){
        unsigned total;if(!c3_game_state_memo_count(state,&total))return 0;
        for(unsigned i=0;i<total;++i){C3GameMemo memo;C3PdaSpecies p;
            if(!c3_game_state_memo(state,i,&memo)||!c3_pda_species(&m->assets,memo.species,&p))return 0;
            if((m->sort>=2||m->filter_types[0]||m->filter_types[1]||m->filter_color)&&(memo.raw&0x8000u))continue;
            if(m->filter_color&&(p.color<1||p.color>10||p.color!=m->filter_color))continue;
            int match=1;for(unsigned t=0;t<2;++t)if(m->filter_types[t]&&p.types[0]+1u!=m->filter_types[t]&&p.types[1]+1u!=m->filter_types[t])match=0;
            if(match)m->order[m->count++]=i;
        }
        for(unsigned i=1;i<m->count;++i){unsigned key=m->order[i],j=i;C3GameMemo a;C3PdaSpecies sa;
            c3_game_state_memo(state,key,&a);if(!c3_pda_species(&m->assets,a.species,&sa))return 0;
            while(j){C3GameMemo b;C3PdaSpecies sb;c3_game_state_memo(state,m->order[j-1],&b);if(!c3_pda_species(&m->assets,b.species,&sb))return 0;
                int comparison=0;
                if(m->sort==1){C3GameSummary s;char na[96],nb[96];c3_game_state_summary(state,&s);species_name(m,&s,a.species,na,sizeof na);species_name(m,&s,b.species,nb,sizeof nb);comparison=strcmp(na,nb);}
                else {unsigned av=m->sort==2?sa.height:m->sort==3?sa.weight:sa.dex,bv=m->sort==2?sb.height:m->sort==3?sb.weight:sb.dex;comparison=av<bv?-1:av>bv?1:0;}
                if(comparison>=0)break;
                m->order[j]=m->order[j-1];--j;
            }m->order[j]=key;
        }
    }else if(m->page==C3_GAME_SNAG){m->count=48;for(unsigned i=0;i<48;++i){C3PdaSnag s;if(!c3_pda_snag(&m->assets,state,i,&s))return 0;
    m->order[i]=i;}}
    if(m->count&&m->selection>=m->count)m->selection=m->count-1u;
    return 1;
}
static void open_list(C3IngameMenu *m,C3GameState *s,C3IngamePage p,unsigned selection)
{page(m,p,selection);if(!list(m,s))c3_ingame_error(m,"The saved list or original PDA data could not be read.");}
static void inventory_clear_choices(C3IngameMenu *m)
{
    m->inventory.move=m->inventory.replacement=m->inventory.evolution=-1;
    m->inventory.replacement_count=0;
    memset(m->inventory.replacements,0,sizeof m->inventory.replacements);
}
static void inventory_done(C3IngameMenu *m,C3GameState *state)
{
    size_t bytes;
    if(m->inventory.action==C3_INVENTORY_USE&&!c3_pda_entry(&m->assets,14,0,&bytes)){
        snprintf(m->error,sizeof m->error,"Update boot/pda.c3pd to use the original item effects.");
        page(m,C3_GAME_ITEM_RESULT,0);return;
    }
    C3InventoryCode code=c3_inventory_apply(state,&m->assets,&m->inventory,&m->inventory_result);
    if(code==C3_INVENTORY_NEED_MOVE||code==C3_INVENTORY_NEED_REPLACEMENT||code==C3_INVENTORY_NEED_EVOLUTION){
        m->inventory_choice=(unsigned)code;page(m,C3_GAME_ITEM_CHOICE,0);return;
    }
    static const char *const messages[]={"Done.","It won't have any effect.","That selection is unavailable.","That item can't be used here.","There is no room in the BAG."};
    snprintf(m->error,sizeof m->error,"%s",code<=C3_INVENTORY_FULL?messages[code]:"The saved selection changed. Please choose again.");
    page(m,C3_GAME_ITEM_RESULT,0);
}
static void inventory_start(C3IngameMenu *m,int party)
{
    m->inventory_origin=party?C3_GAME_PARTY:C3_GAME_ITEMS;m->inventory_selection=m->selection;
    m->inventory=(C3InventoryRequest){.pocket=m->pocket,.slot=m->detail,.party=party?m->detail:0,.quantity=1,.move=-1,.replacement=-1,.evolution=-1};
    page(m,party?C3_GAME_PARTY_ACTIONS:C3_GAME_ITEM_ACTIONS,0);
}
static uint16_t navigation(C3IngameMenu *m,const C3N3dsInput *input,float dt)
{
    uint16_t held=input->held&15u;
    if(input->stick_x>36)held|=C3_PAD_RIGHT;
    if(input->stick_x < -36)held|=C3_PAD_LEFT;
    if(input->stick_y>36)held|=C3_PAD_UP;
    if(input->stick_y < -36)held|=C3_PAD_DOWN;
    if((held&3u)==3u)held&=(uint16_t)~3u;
    if((held&12u)==12u)held&=(uint16_t)~12u;
    if(held!=m->direction){m->direction=held;m->held_seconds=m->repeat_seconds=0;return held;}
    if(!held||!isfinite(dt)||dt<0)return 0;
    if(dt>.25f)dt=.25f;
    m->held_seconds+=dt;
    if(m->held_seconds<.35f)return 0;
    m->repeat_seconds+=dt;if(m->repeat_seconds<.12f)return 0;
    m->repeat_seconds=0;return held;
}
static void move(C3IngameMenu *m,uint16_t keys,unsigned count,unsigned step)
{
    if(!count)return;
    if(keys&C3_PAD_UP)m->selection=m->selection>=step?m->selection-step:0;
    if(keys&C3_PAD_DOWN)m->selection=m->selection+step<count?m->selection+step:count-1u;
}
static C3IngameEvent letter(C3IngameMenu *m,C3GameState *state,unsigned selection)
{
    C3IngameEvent e={0};C3GameMail mail;
    if(selection>=m->count||!c3_game_state_mail(state,m->order[selection],&mail))return e;
    size_t n;if(!c3_pda_entry(&m->assets,3,mail.id,&n)){c3_ingame_error(m,"The original text for this saved mail is unavailable.");return e;}
    m->selection=selection;m->detail=mail.id;
    if(!mail.read){
        if(!c3_pda_mail_action(&m->assets,state,mail.id,&e.mail)){c3_ingame_error(m,"This mail's saved read effects could not be prepared.");return (C3IngameEvent){0};}
        e.kind=C3_INGAME_READ_MAIL;
    }
    page(m,C3_GAME_LETTER,selection);return e;
}
int c3_ingame_preview_identity(const C3IngameMenu *m,const C3GameState *state,
                              uint16_t *species,uint32_t *pid,uint32_t *trainer)
{
    if(!m||!state||!species||!pid||!trainer)return 0;
    if(m->page==C3_GAME_POKEMON){uint8_t p[312];
        if(!c3_game_state_pokemon(state,m->detail,p)||!c3_pda_be16(p)||p[0xcb])return 0;
        *species=c3_pda_be16(p);*pid=c3_pda_be32(p+4);*trainer=c3_pda_be32(p+0x14);return 1;
    }
    if(m->page==C3_GAME_MEMO_ENTRY){C3GameMemo memo;
        if(!c3_game_state_memo(state,m->detail,&memo)||(memo.raw&0x8000u))return 0;
        *species=memo.species;*pid=memo.personality;*trainer=memo.trainer_id;return 1;
    }
    if(m->page==C3_GAME_SNAG_ENTRY){C3PdaSnag snag;C3GameSummary summary;
        if(!c3_pda_snag(&m->assets,state,m->detail,&snag)||!snag.seen||!c3_game_state_summary(state,&summary))return 0;
        for(unsigned i=0;i<6;++i){uint8_t p[312];if(c3_game_state_pokemon(state,i,p)&&c3_pda_be16(p+0xd8)==snag.id){
            *species=snag.species;*pid=c3_pda_be32(p+4);*trainer=c3_pda_be32(p+0x14);return 1;}}
        const uint8_t *b=c3_game_state_body(state,NULL);if(!b||snag.slot>96)return 0;
        *species=snag.species;*pid=c3_pda_be32(b+0x1b014+snag.slot*12u+8);*trainer=summary.trainer_id;return 1;
    }
    return 0;
}
C3IngameEvent c3_ingame_update(C3IngameMenu *m,C3GameState *state,const C3N3dsInput *input,float dt)
{
    C3IngameEvent e={0};if(!m||!m->active||!state||!input)return e;
    uint16_t pressed=input->pressed,keys=navigation(m,input,dt);if(keys||pressed)m->dirty=1;
    int accept=!!(pressed&C3_PAD_A),back=!!(pressed&C3_PAD_B);
    if((pressed&C3_PAD_Y)&&(m->page==C3_GAME_POKEMON||m->page==C3_GAME_MEMO_ENTRY||m->page==C3_GAME_SNAG_ENTRY)){
        e.kind=C3_INGAME_PREVIEW;return e;
    }
    if(m->page==C3_GAME_HUB){
        move(m,keys,8,2);if((keys&C3_PAD_LEFT)&&(m->selection&1u))--m->selection;
        if((keys&C3_PAD_RIGHT)&&!(m->selection&1u))++m->selection;
        m->hub_selection=m->selection;
        if(back||(pressed&(C3_PAD_X|C3_PAD_START))||(accept&&m->selection==7))e.kind=C3_INGAME_RESUME;
        else if(accept){switch(m->selection){
            case 0:open_list(m,state,C3_GAME_PARTY,0);break;
            case 1:m->pocket=0;open_list(m,state,C3_GAME_ITEMS,0);break;
            case 2:if(!m->assets.data)c3_ingame_error(m,"Prepare boot/pda.c3pd from the original game data to open the P*DA.");else page(m,C3_GAME_PDA,m->pda_selection);break;
            case 3:page(m,C3_GAME_TRAINER,0);break;
            case 4:{C3GameSummary s;c3_game_state_summary(state,&s);m->option_mono=s.sound_mono;m->option_rumble=s.rumble_disabled;page(m,C3_GAME_OPTIONS,0);break;}
            case 5:e.kind=C3_INGAME_SAVE;break;
            case 6:e.kind=C3_INGAME_LEAVE;break;
        }}
    }else if(m->page==C3_GAME_PDA){
        move(m,keys,4,1);m->pda_selection=m->selection;
        if(back||(accept&&m->selection==3))page(m,C3_GAME_HUB,m->hub_selection);
        else if(accept){
            if(m->selection==0)open_list(m,state,C3_GAME_SNAG,0);
            else if(m->selection==1){uint32_t unlocked=0;
                if(!c3_game_state_get_flag(state,0x3f0,&unlocked))c3_ingame_error(m,"The saved Strategy Memo unlock could not be read.");
                else if(!unlocked)c3_ingame_error(m,"The Strategy Memo is not available yet.");
                else{m->sort=0;m->filter_types[0]=m->filter_types[1]=m->filter_color=0;open_list(m,state,C3_GAME_MEMO,0);}}
            else open_list(m,state,C3_GAME_MAIL,0);
        }
    }else if(m->page==C3_GAME_PARTY||m->page==C3_GAME_ITEMS||m->page==C3_GAME_MAIL||m->page==C3_GAME_MEMO||m->page==C3_GAME_SNAG){
        move(m,keys,m->count,1);
        if(pressed&C3_PAD_L){if(m->page==C3_GAME_ITEMS){m->pocket=(m->pocket+5u)%6u;open_list(m,state,C3_GAME_ITEMS,0);}else m->selection=m->selection>=5?m->selection-5u:0;}
        if(pressed&C3_PAD_R){if(m->page==C3_GAME_ITEMS){m->pocket=(m->pocket+1u)%6u;open_list(m,state,C3_GAME_ITEMS,0);}else if(m->count)m->selection=m->selection+5u<m->count?m->selection+5u:m->count-1u;}
        if(back)page(m,m->page==C3_GAME_MAIL||m->page==C3_GAME_MEMO||m->page==C3_GAME_SNAG?C3_GAME_PDA:C3_GAME_HUB,
            m->page==C3_GAME_MAIL?2:m->page==C3_GAME_MEMO?1:m->page==C3_GAME_SNAG?0:m->hub_selection);
        else if((m->page==C3_GAME_PARTY||m->page==C3_GAME_ITEMS)&&(pressed&C3_PAD_X)&&m->count){m->detail=m->order[m->selection];inventory_start(m,m->page==C3_GAME_PARTY);}
        else if(m->page==C3_GAME_MAIL&&(pressed&C3_PAD_X)){m->sort=(m->sort+1u)%4u;c3_game_state_mail_preferences(state,(int)m->sort,-1);open_list(m,state,C3_GAME_MAIL,0);}
        else if(m->page==C3_GAME_MAIL&&(pressed&C3_PAD_Y)){unsigned sort,tone;c3_game_state_mail_settings(state,&sort,&tone);page(m,C3_GAME_TONES,tone);e.kind=C3_INGAME_PLAY_TONE;e.tone=tone;}
        else if(m->page==C3_GAME_MEMO&&(pressed&C3_PAD_X)){m->sort=(m->sort+1u)%4u;open_list(m,state,C3_GAME_MEMO,0);}
        else if(m->page==C3_GAME_MEMO&&(pressed&C3_PAD_Y)){
            m->saved_filters[0]=m->filter_types[0];m->saved_filters[1]=m->filter_types[1];m->saved_filters[2]=m->filter_color;
            page(m,C3_GAME_MEMO_FILTER,0);
        }
        else if(accept&&m->count){
            m->detail=m->order[m->selection];m->tab=0;
            if(m->page==C3_GAME_MAIL)return letter(m,state,m->selection);
            page(m,m->page==C3_GAME_PARTY?C3_GAME_POKEMON:m->page==C3_GAME_ITEMS?C3_GAME_ITEM:m->page==C3_GAME_MEMO?C3_GAME_MEMO_ENTRY:C3_GAME_SNAG_ENTRY,m->selection);
        }
    }else if(m->page==C3_GAME_OPTIONS){
        move(m,keys,3,1);
        if(keys&(C3_PAD_LEFT|C3_PAD_RIGHT)){if(m->selection==0)m->option_mono^=1u;else if(m->selection==1)m->option_rumble^=1u;}
        if(back)page(m,C3_GAME_HUB,m->hub_selection);
        else if(accept&&m->selection==2){if(c3_game_state_set_options(state,(uint8_t)m->option_mono,(uint8_t)m->option_rumble)){e.kind=C3_INGAME_OPTIONS;page(m,C3_GAME_HUB,m->hub_selection);}else c3_ingame_error(m,"Unable to apply Options.");}
    }else if(m->page==C3_GAME_TONES){
        unsigned previous=m->selection;move(m,keys,10,1);
        if(back){open_list(m,state,C3_GAME_MAIL,0);e.kind=C3_INGAME_STOP_TONE;}
        else if(accept){c3_game_state_mail_preferences(state,-1,(int)m->selection);open_list(m,state,C3_GAME_MAIL,0);e.kind=C3_INGAME_STOP_TONE;}
        else if(previous!=m->selection||(pressed&C3_PAD_Y)){e.kind=C3_INGAME_PLAY_TONE;e.tone=m->selection;}
    }else if(m->page==C3_GAME_MEMO_FILTER){
        move(m,keys,5,1);
        if(keys&(C3_PAD_LEFT|C3_PAD_RIGHT)){
            unsigned *v=m->selection<2?m->filter_types+m->selection:&m->filter_color;
            unsigned count=m->selection<2?19u:11u;
            if(m->selection<3)*v=(*v+((keys&C3_PAD_LEFT)?count-1u:1u))%count;
        }
        if(back){m->filter_types[0]=m->saved_filters[0];m->filter_types[1]=m->saved_filters[1];m->filter_color=m->saved_filters[2];open_list(m,state,C3_GAME_MEMO,0);}
        else if(accept&&m->selection>=3){
            if(m->selection==4)m->filter_types[0]=m->filter_types[1]=m->filter_color=0;
            open_list(m,state,C3_GAME_MEMO,0);
        }
    }else if(m->page==C3_GAME_LETTER){
        if(back)open_list(m,state,C3_GAME_MAIL,m->selection);
        else if(pressed&C3_PAD_L){if(m->selection)return letter(m,state,m->selection-1u);}
        else if(pressed&C3_PAD_R){if(m->selection+1u<m->count)return letter(m,state,m->selection+1u);}
        else if(pressed&C3_PAD_X){size_t n;const uint8_t *d=c3_pda_entry(&m->assets,3,m->detail,&n);
            if(d&&c3_pda_be32(d+24)&&c3_pda_entry(&m->assets,6,100u+c3_pda_be32(d+24),&n))page(m,C3_GAME_PICTURE,m->selection);}
        else if(accept){if(!c3_ui_pager_advance(&m->pager,&pressed))open_list(m,state,C3_GAME_MAIL,m->selection);}
    }else if(m->page==C3_GAME_PICTURE){if(back||accept)page(m,C3_GAME_LETTER,m->selection);}
    else if(m->page==C3_GAME_POKEMON){
        if(back)open_list(m,state,C3_GAME_PARTY,m->selection);
        else if(pressed&(C3_PAD_L|C3_PAD_R)){m->tab=(m->tab+((pressed&C3_PAD_L)?2u:1u))%3u;m->pager=(C3UiPager){0};}
        else if(pressed&C3_PAD_X)inventory_start(m,1);
        else if(accept)c3_ui_pager_advance(&m->pager,&pressed);
    }else if(m->page==C3_GAME_MEMO_ENTRY||m->page==C3_GAME_SNAG_ENTRY||m->page==C3_GAME_ITEM){
        if(back)open_list(m,state,m->page==C3_GAME_MEMO_ENTRY?C3_GAME_MEMO:m->page==C3_GAME_SNAG_ENTRY?C3_GAME_SNAG:C3_GAME_ITEMS,m->selection);
        else if(m->page==C3_GAME_ITEM&&((pressed&C3_PAD_X)||(accept&&!m->pager.more)))inventory_start(m,0);
        else if(accept)c3_ui_pager_advance(&m->pager,&pressed);
    }else if(m->page==C3_GAME_TRAINER){if(back)page(m,C3_GAME_HUB,m->hub_selection);}
    else if(m->page==C3_GAME_ITEM_ACTIONS||m->page==C3_GAME_PARTY_ACTIONS){
        int party=m->page==C3_GAME_PARTY_ACTIONS;move(m,keys,party?4u:5u,1);
        if(back||(accept&&m->selection==(party?3u:4u)))open_list(m,state,(C3IngamePage)m->inventory_origin,m->inventory_selection);
        else if(accept){
            inventory_clear_choices(m);
            if(party&&m->selection==0){page(m,C3_GAME_POKEMON,m->inventory_selection);}
            else if(party&&m->selection==1){m->inventory.action=C3_INVENTORY_TAKE;inventory_done(m,state);}
            else if(party){m->inventory.pocket=6;m->inventory.action=C3_INVENTORY_REORDER;page(m,C3_GAME_ITEM_TARGET,0);}
            else {m->inventory.action=m->selection==0?C3_INVENTORY_USE:m->selection==1?C3_INVENTORY_GIVE:m->selection==2?C3_INVENTORY_TOSS:C3_INVENTORY_REORDER;
                page(m,m->inventory.action==C3_INVENTORY_TOSS?C3_GAME_ITEM_QUANTITY:C3_GAME_ITEM_TARGET,0);}
        }
    }else if(m->page==C3_GAME_ITEM_TARGET){
        unsigned count=6;if(m->inventory.action==C3_INVENTORY_REORDER&&m->inventory_origin==C3_GAME_ITEMS)count=c3_inventory_capacity(m->pocket);
        move(m,keys,count,1);
        if(back)page(m,m->inventory_origin==C3_GAME_PARTY?C3_GAME_PARTY_ACTIONS:C3_GAME_ITEM_ACTIONS,0);
        else if(accept){inventory_clear_choices(m);m->inventory.party=m->inventory.action==C3_INVENTORY_REORDER?m->inventory.party:m->selection;m->inventory.destination=m->selection;inventory_done(m,state);}
    }else if(m->page==C3_GAME_ITEM_QUANTITY){
        C3GameItem item;if(c3_game_state_item(state,m->inventory.pocket,m->inventory.slot,&item)){
            if((keys&(C3_PAD_UP|C3_PAD_RIGHT))&&m->inventory.quantity<item.quantity)++m->inventory.quantity;
            if((keys&(C3_PAD_DOWN|C3_PAD_LEFT))&&m->inventory.quantity>1)--m->inventory.quantity;
        }
        if(back)page(m,C3_GAME_ITEM_ACTIONS,2);else if(accept)page(m,C3_GAME_ITEM_CONFIRM,0);
    }else if(m->page==C3_GAME_ITEM_CONFIRM){
        move(m,keys,2,1);if(back||(accept&&!m->selection))page(m,C3_GAME_ITEM_ACTIONS,2);
        else if(accept)inventory_done(m,state);
    }else if(m->page==C3_GAME_ITEM_CHOICE){
        unsigned count=m->inventory_result.choice_count;move(m,keys,count+1u,1);
        if(back||(accept&&m->selection==count&&m->inventory_choice!=C3_INVENTORY_NEED_EVOLUTION)){inventory_clear_choices(m);page(m,C3_GAME_ITEM_ACTIONS,0);}
        else if(accept){
            if(m->inventory_choice==C3_INVENTORY_NEED_MOVE)m->inventory.move=(int)m->inventory_result.choices[m->selection];
            else if(m->inventory_choice==C3_INVENTORY_NEED_REPLACEMENT){
                unsigned index=m->inventory_result.learning_index;
                if(index<20){m->inventory.replacements[index]=(int)m->inventory_result.choices[m->selection];m->inventory.replacement_count=index+1u;}
                m->inventory.replacement=-1;
            }
            else m->inventory.evolution=m->selection==count?0:(int)m->inventory_result.choices[m->selection];
            inventory_done(m,state);
        }
    }else if(m->page==C3_GAME_ITEM_RESULT){
        if(back||accept)open_list(m,state,(C3IngamePage)m->inventory_origin,m->inventory_selection);
    }
    else if(m->page==C3_GAME_NOTICE){if(back||accept){
        if(m->parent==C3_GAME_POKEMON||m->parent==C3_GAME_MEMO_ENTRY||m->parent==C3_GAME_SNAG_ENTRY||m->parent==C3_GAME_ITEM){page(m,m->parent,m->notice_selection);return e;}
        int pda=m->parent==C3_GAME_PDA||m->parent==C3_GAME_MAIL||m->parent==C3_GAME_LETTER||m->parent==C3_GAME_MEMO||m->parent==C3_GAME_SNAG;
        page(m,pda?C3_GAME_PDA:C3_GAME_HUB,pda?m->pda_selection:m->hub_selection);
    }}
    return e;
}

static void image(C3IngameMenu *m,C3BottomUi *ui,unsigned id,int x,int y,int w,int h)
{
    size_t n;const uint8_t *p=c3_pda_entry(&m->assets,6,id,&n);if(!p||n<8)return;
    c3_ui_rgba(ui,p+8,le32(p),le32(p+4),x,y,w,h);
}
static void header(C3IngameMenu *m,C3BottomUi *ui,const char *label,const char *sub)
{
    image(m,ui,0,0,0,320,240);image(m,ui,1,0,0,320,48);
    if(!m->assets.data)c3_ui_panel(ui,5,5,310,43);
    c3_ui_text(ui,34,22,252,1,label,0x77ef74u);
    c3_ui_text(ui,34,49,252,1,sub?sub:"",C3_UI_MUTED);
}
static void body(C3IngameMenu *m,C3BottomUi *ui,const char *text_)
{
    c3_ui_message(ui,&m->pager,text_,63,8);
}
static void decoded_name(const uint8_t *p,char *out,size_t cap)
{
    unsigned i=0;for(;i<10&&i+1<cap;++i){unsigned c=c3_pda_be16(p+i*2);if(!c)break;
    out[i]=(c>=32&&c<=126)?(char)c:'?';}out[i]=0;
}
void c3_ingame_draw_to(C3IngameMenu *m,const C3GameState *state,C3BottomUi *ui)
{
    if(!m||!state||!ui)return;
    C3GameSummary s;if(!c3_game_state_summary(state,&s))return;
    int previous_inset=ui->content_inset;ui->content_inset=16;
    char label[256],value[128],name[96],sub[128];size_t n;
    snprintf(sub,sizeof sub,"%s   $%lu   %lu:%02lu",s.name,(unsigned long)s.money,(unsigned long)(s.play_seconds/3600u),(unsigned long)(s.play_seconds/60u%60u));
    header(m,ui,"P*DA",sub);
    switch(m->page){
    case C3_GAME_HUB:{
        const char *labels[]={"POKEMON","ITEMS","P*DA",s.name,"OPTIONS","SAVE","MAIN MENU","RESUME"};
        for(unsigned i=0;i<8;++i){int x=32+(int)(i%2u)*135,y=65+(int)(i/2u)*36;
            c3_ui_rect(ui,x,y,121,29,i==m->selection?0x175134u:0x072819u);
            c3_ui_rect(ui,x,y,2,29,i==m->selection?0x66ff55u:0x235d35u);
            if(i==m->selection)c3_ui_sprite(ui,C3_UI_HAND,x+3,y+5,21,16);
            c3_ui_text(ui,x+(i==m->selection?26:10),y+6,i==m->selection?89:105,1,labels[i],i==m->selection?C3_UI_WHITE:0x8dcbaau);
        }c3_ui_footer(ui,"D-pad: choose   A: open   B: resume");break;
    }
    case C3_GAME_PDA:{
        static const char *const labels[]={"SNAG LIST","STRATEGY MEMO","MAIL","CANCEL"};uint32_t unlocked=0;c3_game_state_get_flag(state,0x3f0,&unlocked);
        for(unsigned i=0;i<4;++i){snprintf(value,sizeof value,"%s",i==1&&!unlocked?"LOCKED":"");
            c3_ui_row_value(ui,68+(int)i*32,labels[i],value,m->selection==i,i==1&&!unlocked);}
        c3_ui_footer(ui,"A: open   B: in-game menu");break;
    }
    case C3_GAME_ITEM_ACTIONS:case C3_GAME_PARTY_ACTIONS:{
        static const char *const item_actions[]={"USE","GIVE","TOSS","SWITCH","CANCEL"};
        static const char *const party_actions[]={"SUMMARY","TAKE ITEM","SWITCH","CANCEL"};
        int party=m->page==C3_GAME_PARTY_ACTIONS;header(m,ui,party?"POKEMON":"ITEMS","Choose an action.");
        for(unsigned i=0;i<(party?4u:5u);++i)c3_ui_row(ui,65+(int)i*25,party?party_actions[i]:item_actions[i],i==m->selection,0);
        c3_ui_footer(ui,"A: choose   B: cancel");break;
    }
    case C3_GAME_ITEM_TARGET:{
        int bag=m->inventory.action==C3_INVENTORY_REORDER&&m->inventory_origin==C3_GAME_ITEMS;
        header(m,ui,bag?"SWITCH ITEMS":"POKEMON",bag?"Choose a destination slot.":"Choose a POKEMON.");
        unsigned count=bag?c3_inventory_capacity(m->pocket):6u,first=m->selection/6u*6u;
        for(unsigned i=first;i<count&&i<first+6u;++i){
            value[0]=0;
            if(bag){C3GameItem item;
                if(c3_game_state_item(state,m->pocket,i,&item)&&item.id){
                    size_t bytes;const uint8_t *d=c3_pda_entry(&m->assets,7,item.id,&bytes);
                    snprintf(label,sizeof label,"ITEM %u",item.id);if(d&&bytes==40)text(m,&s,c3_pda_be32(d+16),"ITEM",label,sizeof label);
                    snprintf(value,sizeof value,"x%u",item.quantity);
                }else snprintf(label,sizeof label,"---");
            }else {uint8_t p[312];if(c3_game_state_pokemon(state,i,p)&&c3_pda_be16(p)){
                decoded_name(p+0x2e,label,sizeof label);if(!label[0])species_name(m,&s,c3_pda_be16(p),label,sizeof label);
                snprintf(value,sizeof value,"%u/%u HP",c3_pda_be16(p+0x8a),c3_pda_be16(p+0x8c));
            }else snprintf(label,sizeof label,"---");}
            c3_ui_row_value(ui,63+(int)(i-first)*24,label,value,i==m->selection,0);
        }c3_ui_footer(ui,"A: choose   B: cancel");break;
    }
    case C3_GAME_ITEM_QUANTITY:
        header(m,ui,"TOSS","How many will you toss?");snprintf(label,sizeof label,"Quantity: %u",m->inventory.quantity);c3_ui_row(ui,90,label,1,0);c3_ui_footer(ui,"Up / Down: quantity   A: next   B: cancel");break;
    case C3_GAME_ITEM_CONFIRM:
        header(m,ui,"TOSS","Is it OK to throw away these items?");c3_ui_row(ui,85,"NO",m->selection==0,0);c3_ui_row(ui,115,"YES",m->selection==1,0);c3_ui_footer(ui,"A: choose   B: cancel");break;
    case C3_GAME_ITEM_CHOICE:{
        header(m,ui,m->inventory_choice==C3_INVENTORY_NEED_EVOLUTION?"EVOLUTION":"MOVES",m->inventory_choice==C3_INVENTORY_NEED_REPLACEMENT?"Choose a move to forget.":"Choose an option.");
        unsigned count=m->inventory_result.choice_count,first=m->selection/6u*6u;
        for(unsigned i=first;i<=count&&i<first+6u;++i){
            value[0]=0;
            snprintf(label,sizeof label,"%s",m->inventory_choice==C3_INVENTORY_NEED_EVOLUTION?"Don't evolve":"CANCEL");if(i<count){unsigned id=m->inventory_result.choices[i];
                if(m->inventory_choice==C3_INVENTORY_NEED_EVOLUTION)species_name(m,&s,id,label,sizeof label);
                else {uint8_t p[312];unsigned slot=id;
                    if(c3_pda_be16(m->inventory_result.pokemon))memcpy(p,m->inventory_result.pokemon,sizeof p);
                    else if(!c3_game_state_pokemon(state,m->inventory.party,p))memset(p,0,sizeof p);
                    if(slot<4)id=c3_pda_be16(p+0x78+slot*4);
                    size_t bytes;const uint8_t *d=c3_pda_entry(&m->assets,10,id,&bytes);snprintf(label,sizeof label,"MOVE %u",id);if(d&&bytes>=36)text(m,&s,c3_pda_be32(d+32),"MOVE",label,sizeof label);
                    if(slot<4&&d&&bytes==56)snprintf(value,sizeof value,"%u/%u PP",p[0x7a+slot*4],d[1]*(5u+p[0x7b+slot*4])/5u);
                    if(m->inventory_result.choices[i]==4)snprintf(label,sizeof label,"Don't learn this move");}}
            c3_ui_row_value(ui,63+(int)(i-first)*24,label,value,i==m->selection,0);
        }c3_ui_footer(ui,"A: choose   B: cancel");break;
    }
    case C3_GAME_ITEM_RESULT:header(m,ui,"ITEMS","");body(m,ui,m->error);c3_ui_footer(ui,"A / B: return");break;
    case C3_GAME_PARTY:
        header(m,ui,"POKEMON",sub);
        for(unsigned i=0;i<m->count;++i){uint8_t p[312];c3_game_state_pokemon(state,m->order[i],p);
            decoded_name(p+0x2e,name,sizeof name);if(!name[0])species_name(m,&s,c3_pda_be16(p),name,sizeof name);
            snprintf(label,sizeof label,"%s Lv.%u",name,p[0x60]);snprintf(value,sizeof value,"%u/%u HP",c3_pda_be16(p+0x8a),c3_pda_be16(p+0x8c));
            int y=62+(int)i*25;c3_ui_row_value(ui,y,label,value,i==m->selection,0);
            uint32_t progress,range;if(c3_pda_experience(&m->assets,p,&progress,&range)){
                c3_ui_text(ui,61,y+10,32,1,"EXP",C3_UI_GOLD);c3_ui_experience(ui,96,y+17,187,progress,range);
            }
        }c3_ui_footer(ui,"A: summary   X: actions   B: back");break;
    case C3_GAME_ITEMS:{
        header(m,ui,pockets[m->pocket],"L / R: change pocket");unsigned first=m->selection/6u*6u;
        for(unsigned row=0;row<6&&first+row<m->count;++row){unsigned i=first+row;C3GameItem item;c3_game_state_item(state,m->pocket,m->order[i],&item);
            const uint8_t *d=c3_pda_entry(&m->assets,7,item.id,&n);snprintf(label,sizeof label,"ITEM %u",item.id);
            if(d)text(m,&s,c3_pda_be32(d+16),"ITEM",label,sizeof label);
            snprintf(value,sizeof value,"x%u",item.quantity);c3_ui_row_value(ui,63+(int)row*24,label,value,i==m->selection,0);}
        if(!m->count)c3_ui_text(ui,40,86,240,2,"There are no items in this pocket.",C3_UI_WHITE);
        c3_ui_footer(ui,"A: description   X: actions   B: back");break;
    }
    case C3_GAME_ITEM:{
        C3GameItem item;c3_game_state_item(state,m->pocket,m->detail,&item);const uint8_t *d=c3_pda_entry(&m->assets,7,item.id,&n);
        snprintf(label,sizeof label,"ITEM %u",item.id);if(d)text(m,&s,c3_pda_be32(d+16),"ITEM",label,sizeof label);
        header(m,ui,label,pockets[m->pocket]);
        text(m,&s,d?c3_pda_be32(d+20):0,"Original item description is unavailable.",m->text,sizeof m->text);body(m,ui,m->text);
        c3_ui_footer(ui,m->pager.more?"A: more   B: back":"A / X: actions   B: item list");break;
    }
    case C3_GAME_POKEMON:{
        uint8_t p[312];c3_game_state_pokemon(state,m->detail,p);decoded_name(p+0x2e,name,sizeof name);if(!name[0])species_name(m,&s,c3_pda_be16(p),name,sizeof name);
        snprintf(label,sizeof label,"%s  Lv.%u",name,p[0x60]);header(m,ui,label,m->tab==0?"SUMMARY":m->tab==1?"STATS":"MOVES");
        if(m->tab==0){
            species_name(m,&s,c3_pda_be16(p),label,sizeof label);snprintf(m->text,sizeof m->text,"%s\nEXP  %lu\nHP  %u / %u\nOriginal Trainer: ",label,(unsigned long)c3_pda_be32(p+0x5c),c3_pda_be16(p+0x8a),c3_pda_be16(p+0x8c));
            decoded_name(p+0x18,label,sizeof label);strncat(m->text,label,sizeof m->text-strlen(m->text)-1u);
            C3PdaSpecies species;if(c3_pda_species(&m->assets,c3_pda_be16(p),&species)){
                unsigned slot=p[0xcc]?1u:0u;const uint8_t *ability=c3_pda_entry(&m->assets,12,species.abilities[slot],&n);
                if(ability){text(m,&s,c3_pda_be32(ability+4),"--",label,sizeof label);strncat(m->text,"\nAbility: ",sizeof m->text-strlen(m->text)-1u);strncat(m->text,label,sizeof m->text-strlen(m->text)-1u);}
            }
            body(m,ui,m->text);
            uint32_t progress,range;if(c3_pda_experience(&m->assets,p,&progress,&range)){
                c3_ui_text(ui,36,185,32,1,"EXP",C3_UI_GOLD);c3_ui_experience(ui,71,192,212,progress,range);
            }
        }else if(m->tab==1){
            const char *labels[]={"HP","ATTACK","DEFENSE","SP. ATK","SP. DEF","SPEED"};
            for(unsigned i=0;i<6;++i){snprintf(value,sizeof value,"%u",c3_pda_be16(p+0x8c+i*2));c3_ui_row_value(ui,63+(int)i*24,labels[i],value,0,0);}
        }else for(unsigned i=0;i<4;++i){unsigned id=c3_pda_be16(p+0x78+i*4);const uint8_t *d=c3_pda_entry(&m->assets,10,id,&n);
            if(!id)snprintf(label,sizeof label,"--");else if(d)text(m,&s,c3_pda_be32(d+32),"MOVE",label,sizeof label);else snprintf(label,sizeof label,"MOVE %u",id);
            unsigned ups=p[0x7b+i*4];if(ups>3)ups=3;
            snprintf(value,sizeof value,"%u/%u PP",p[0x7a+i*4],d?(unsigned)d[1]*(5u+ups)/5u:0);
            c3_ui_row_value(ui,70+(int)i*32,label,id?value:"",0,0);
        }c3_ui_footer(ui,m->preview?"C-stick: rotate   B/Y: close":m->pager.more?"A: more   Y: model   B: party":"L/R: pages   X: actions   Y: model");break;
    }
    case C3_GAME_TRAINER:{
        header(m,ui,s.name,"TRAINER");const uint8_t *b=c3_game_state_body(state,NULL);
        snprintf(m->text,sizeof m->text,"TRAINER ID  %05u\nMoney  %lu\nPOKE COUPONS  %lu\nTime  %lu:%02lu\nParty  %u\nPartner  %s",(unsigned)(s.trainer_id&65535u),(unsigned long)s.money,(unsigned long)c3_pda_be32(b+0x70+0xa88),(unsigned long)(s.play_seconds/3600u),(unsigned long)(s.play_seconds/60u%60u),s.party_count,s.partner);
        body(m,ui,m->text);c3_ui_footer(ui,"B: in-game menu");break;
    }
    case C3_GAME_OPTIONS:
        header(m,ui,"OPTIONS","Changes apply with END; Save keeps them.");
        snprintf(label,sizeof label,"SOUND  <%s>",m->option_mono?"MONO":"STEREO");c3_ui_row(ui,72,label,m->selection==0,0);
        snprintf(label,sizeof label,"RUMBLE <%s>",m->option_rumble?"OFF":"ON");c3_ui_row(ui,109,label,m->selection==1,0);c3_ui_row(ui,146,"END",m->selection==2,0);
        c3_ui_text(ui,38,185,244,1,"3DS has no Controller Rumble motor.",C3_UI_MUTED);c3_ui_footer(ui,"D-pad: change   A: END   B: cancel");break;
    case C3_GAME_MAIL:{
        unsigned tone,sort;if(!c3_game_state_mail_settings(state,&sort,&tone)){body(m,ui,"The saved mail settings are invalid.");c3_ui_footer(ui,"B: back");break;}
        snprintf(sub,sizeof sub,"%u letters   %s",m->count,sorts[sort]);header(m,ui,"MAIL",sub);
        unsigned first=m->selection/5u*5u;
        for(unsigned row=0;row<5&&first+row<m->count;++row){unsigned i=first+row;C3GameMail mail;c3_game_state_mail(state,m->order[i],&mail);const uint8_t *d=c3_pda_entry(&m->assets,3,mail.id,&n);
            int y=65+(int)row*29;c3_ui_rect(ui,32,y,256,28,i==m->selection?0x12502cu:0x052415u);
            image(m,ui,mail.read?4:3,37,y+6,20,16);text(m,&s,d?c3_pda_be32(d+16):0,"Unknown saved mail",label,sizeof label);
            c3_ui_text(ui,64,y,217,1,label,mail.read?C3_UI_WHITE:C3_UI_GOLD);
            text(m,&s,d?c3_pda_be32(d+12):0,"Unknown sender",label,sizeof label);c3_ui_text(ui,64,y+13,217,1,label,C3_UI_MUTED);
        }
        if(!m->count)c3_ui_text(ui,40,88,240,2,"No mail has been received.",C3_UI_WHITE);
        c3_ui_footer(ui,"A: read   X: sort   Y: tone   B: back");break;
    }
    case C3_GAME_LETTER:{
        const uint8_t *d=c3_pda_entry(&m->assets,3,m->detail,&n);text(m,&s,d?c3_pda_be32(d+16):0,"MAIL",label,sizeof label);
        text(m,&s,d?c3_pda_be32(d+12):0,"",sub,sizeof sub);header(m,ui,label,sub);
        text(m,&s,d?c3_pda_be32(d+20):0,"This original letter could not be decoded.",m->text,sizeof m->text);body(m,ui,m->text);
        c3_ui_footer(ui,m->pager.more?"A: more   B: inbox":d&&c3_pda_be32(d+24)?"X: picture   L/R: letter   B: inbox":"L/R: letter   B: inbox");break;
    }
    case C3_GAME_PICTURE:{
        const uint8_t *d=c3_pda_entry(&m->assets,3,m->detail,&n);header(m,ui,"MAIL PICTURE","");
        if(d)image(m,ui,100u+c3_pda_be32(d+24),53,60,214,160);
        c3_ui_footer(ui,"A / B: return to letter");
        break;
    }
    case C3_GAME_TONES:{
        unsigned sort,tone;c3_game_state_mail_settings(state,&sort,&tone);header(m,ui,"ALERT TONE","Select the saved mail alert tone.");
        unsigned first=m->selection/6u*6u;for(unsigned r=0;r<6&&first+r<10;++r){unsigned i=first+r;c3_ui_row_value(ui,65+(int)r*24,tones[i],i==tone?"SET":"",i==m->selection,0);}
        c3_ui_footer(ui,"A: set tone   B: inbox");break;
    }
    case C3_GAME_MEMO:{
        const char *sort_names[]={"No.","Name","Height","Weight"};snprintf(sub,sizeof sub,"%u entries   Sort: %s",m->count,sort_names[m->sort]);header(m,ui,"STRATEGY MEMO",sub);
        unsigned first=m->selection/6u*6u;
        for(unsigned row=0;row<6&&first+row<m->count;++row){unsigned i=first+row;C3GameMemo memo;c3_game_state_memo(state,m->order[i],&memo);C3PdaSpecies p;c3_pda_species(&m->assets,memo.species,&p);
            species_name(m,&s,memo.species,name,sizeof name);snprintf(label,sizeof label,"%03u %s",p.dex,name);int caught=0;c3_pda_memo_caught(&m->assets,state,memo.species,&caught);
            c3_ui_row_value(ui,65+(int)row*24,label,(memo.raw&0x8000u)?"NEW":caught?"CAUGHT":"",i==m->selection,0);}
        if(!m->count)c3_ui_text(ui,40,88,240,2,m->filter_types[0]||m->filter_types[1]||m->filter_color||m->sort>=2?"No registered Pokemon match this search.":"No Pokemon are recorded.",C3_UI_WHITE);
        c3_ui_footer(ui,"A: details   X: sort   Y: search   B: back");break;
    }
    case C3_GAME_MEMO_FILTER:
        header(m,ui,"STRATEGY MEMO","SEARCH - registered entries only");
        for(unsigned i=0;i<2;++i){snprintf(label,sizeof label,"TYPE %u",i+1u);unsigned type=m->filter_types[i];c3_ui_row_value(ui,65+(int)i*29,label,type?types[type-1u]:"ALL",m->selection==i,0);}
        c3_ui_row_value(ui,123,"COLOR",m->filter_color?colors[m->filter_color-1u]:"ALL",m->selection==2,0);
        c3_ui_row(ui,152,"SEARCH",m->selection==3,0);c3_ui_row(ui,181,"RESET",m->selection==4,0);
        c3_ui_footer(ui,"D-pad: change   A: search   B: cancel");break;
    case C3_GAME_MEMO_ENTRY:{
        C3GameMemo memo;C3PdaSpecies p;if(!c3_game_state_memo(state,m->detail,&memo)||!c3_pda_species(&m->assets,memo.species,&p))break;
        species_name(m,&s,memo.species,name,sizeof name);snprintf(sub,sizeof sub,"No. %03u",p.dex);header(m,ui,name,sub);
        if(memo.raw&0x8000u)snprintf(m->text,sizeof m->text,"Newly encountered Pokemon.\n\nThe Strategy Memo data has not yet been updated.");
        else{
            text(m,&s,p.category,"POKEMON",label,sizeof label);int caught=0;c3_pda_memo_caught(&m->assets,state,memo.species,&caught);
            char type_label[64];snprintf(type_label,sizeof type_label,p.types[0]==p.types[1]?"%s":"%s / %s",types[p.types[0]<18?p.types[0]:9],types[p.types[1]<18?p.types[1]:9]);
            snprintf(m->text,sizeof m->text,"%s%s\n%s\nHT %u'%02u\"   WT %u.%u lb\n%s\n\n",label,strstr(label,"POKEMON")?"":" POKEMON",type_label,p.height/100u,p.height%100u,p.weight/10u,p.weight%10u,caught?"Caught":"Seen");
            char ability_name[96];
            const uint8_t *ability=c3_pda_entry(&m->assets,12,p.abilities[0],&n);
            if(ability){
                text(m,&s,c3_pda_be32(ability+4),"--",ability_name,sizeof ability_name);strncat(m->text,"Ability: ",sizeof m->text-strlen(m->text)-1u);strncat(m->text,ability_name,sizeof m->text-strlen(m->text)-1u);
                if(p.abilities[1]&&p.abilities[1]!=p.abilities[0]){
                    ability=c3_pda_entry(&m->assets,12,p.abilities[1],&n);
                    if(ability){text(m,&s,c3_pda_be32(ability+4),"--",ability_name,sizeof ability_name);strncat(m->text," / ",sizeof m->text-strlen(m->text)-1u);strncat(m->text,ability_name,sizeof m->text-strlen(m->text)-1u);}
                }
                strncat(m->text,"\n\n",sizeof m->text-strlen(m->text)-1u);
            }
            size_t used=strlen(m->text);
            if(p.description&&!c3_pda_text(&m->assets,p.description,s.name,s.partner,m->text+used,sizeof m->text-used))
                snprintf(m->text+used,sizeof m->text-used,"No further recorded information.");
        }body(m,ui,m->text);c3_ui_footer(ui,m->preview?"C-stick: rotate   B/Y: close":m->pager.more?"A: more   Y: model   B: memo":"Y: model   B: Strategy Memo");break;
    }
    case C3_GAME_SNAG:{
        unsigned seen=0,snagged=0,purified=0;for(unsigned i=0;i<48;++i){C3PdaSnag p;if(c3_pda_snag(&m->assets,state,i,&p)){seen+=p.seen;snagged+=p.snagged;purified+=p.purified;}}
        snprintf(sub,sizeof sub,"Snagged %u / 48   Purified %u",snagged,purified);header(m,ui,"SNAG LIST",sub);unsigned first=m->selection/6u*6u;
        for(unsigned row=0;row<6&&first+row<48;++row){unsigned i=first+row;C3PdaSnag p;if(!c3_pda_snag(&m->assets,state,i,&p))continue;
            if(p.seen||p.snagged||p.purified||p.hint)species_name(m,&s,p.species,name,sizeof name);else snprintf(name,sizeof name,"-----");
            snprintf(label,sizeof label,"%02u %s",i+1u,name);c3_ui_row_value(ui,65+(int)row*24,label,p.purified?"PURIFIED":p.snagged?"SNAGGED":p.seen?"SEEN":p.hint?"INFO":"---",i==m->selection,0);
        }(void)seen;c3_ui_footer(ui,"A: details   L / R: page   B: back");break;
    }
    case C3_GAME_SNAG_ENTRY:{
        C3PdaSnag p;if(!c3_pda_snag(&m->assets,state,m->detail,&p))break;
        if(p.seen||p.snagged||p.purified||p.hint)species_name(m,&s,p.species,name,sizeof name);else snprintf(name,sizeof name,"-----");
        snprintf(sub,sizeof sub,"SNAG LIST  %02u / 48",m->detail+1u);header(m,ui,name,sub);
        if(!(p.seen||p.snagged||p.purified||p.hint))snprintf(m->text,sizeof m->text,"No information has been received.");
        else{
            const uint8_t *b=c3_game_state_body(state,NULL);decoded_name(b+0x1b014+p.slot*24u+0x490,label,sizeof label);
            snprintf(m->text,sizeof m->text,"%s\nLevel %u\n%s\n%s\n%s",name,p.level,p.purified?"Purified":p.snagged?"Snagged":"Not yet snagged",p.seen?"Encountered in battle.":"Information received by mail.",label[0]?label:"Trainer unknown.");
            strncat(m->text,"\n\n",sizeof m->text-strlen(m->text)-1u);size_t used=strlen(m->text);
            if(!c3_pda_snag_text(&m->assets,state,&p,m->text+used,sizeof m->text-used))snprintf(m->text+used,sizeof m->text-used,"Recorded location details are unavailable.");
        }body(m,ui,m->text);c3_ui_footer(ui,m->preview?"C-stick: rotate   B/Y: close":m->pager.more?"A: more   Y: model   B: Snag List":"Y: model   B: Snag List");break;
    }
    case C3_GAME_NOTICE:
        header(m,ui,"P*DA","");body(m,ui,m->error);c3_ui_footer(ui,"A / B: back");break;
    }
    ui->content_inset=previous_inset;
    m->dirty=0;
}
