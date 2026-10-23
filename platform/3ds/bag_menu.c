#include "bag_menu.h"
#include <stdio.h>
#include <string.h>
#ifndef C3_BAG_HOST_TEST
#include <3ds.h>
#endif

static void notice(C3BagMenu *m,C3BagPage back,const char *text)
{
    m->page=C3_BAG_NOTICE;m->return_page=back;
    snprintf(m->notice,sizeof m->notice,"%s",text);
}
static int refresh(C3BagMenu *m,const C3GameState *state,const C3ItemCatalog *catalog)
{
    char error[C3_GAME_ERROR_SIZE]={0};
    if(!c3_game_bag_view(state,catalog,m->pocket,&m->view,error) ||
       !c3_game_bag_party(state,m->party,error)){
        notice(m,C3_BAG_CLOSED,error);return 0;
    }
    if(m->selected>=m->view.capacity || !m->view.slots[m->selected].id){
        m->selected=0;
        for(unsigned i=0;i<m->view.capacity;++i)if(m->view.slots[i].id){m->selected=i;break;}
    }
    return 1;
}
void c3_bag_menu_init(C3BagMenu *m,const C3GameState *state,const C3ItemCatalog *catalog)
{
    if(!m)return;
    memset(m,0,sizeof *m);m->pocket=2;m->page=C3_BAG_LIST;
    (void)refresh(m,state,catalog);
}
static void move_slot(C3BagMenu *m,int direction)
{
    unsigned n=m->view.capacity;
    for(unsigned i=0;i<n;++i){
        m->selected=(m->selected+n+(direction>0?1u:n-1u))%n;
        if(m->view.slots[m->selected].id)return;
    }
}
static void move_target(C3BagMenu *m,int direction)
{
    for(unsigned i=0;i<6;++i){
        m->target=(m->target+6u+(direction>0?1u:5u))%6u;
        if(m->party[m->target].species)return;
    }
}
C3BagEvent c3_bag_menu_step(C3BagMenu *m,C3GameState *state,
                            const C3ItemCatalog *catalog,uint16_t pressed,
                            C3ItemUseResult *used)
{
    if(!m || m->page==C3_BAG_CLOSED)return C3_BAG_EVENT_CLOSED;
    if(!pressed)return C3_BAG_EVENT_NONE;
    if(pressed&(C3_PAD_START|C3_PAD_X)){m->page=C3_BAG_CLOSED;return C3_BAG_EVENT_CLOSED;}
    if(pressed&C3_PAD_B){
        if(m->page==C3_BAG_LIST)m->page=C3_BAG_CLOSED;
        else if(m->page==C3_BAG_TARGET)m->page=C3_BAG_LIST;
        else if(m->page==C3_BAG_CONFIRM)m->page=C3_BAG_TARGET;
        else if(m->page==C3_BAG_NOTICE)m->page=m->return_page;
        return m->page==C3_BAG_CLOSED?C3_BAG_EVENT_CLOSED:C3_BAG_EVENT_NONE;
    }
    if(m->page==C3_BAG_NOTICE){
        if(pressed&C3_PAD_A)m->page=m->return_page;
        return m->page==C3_BAG_CLOSED?C3_BAG_EVENT_CLOSED:C3_BAG_EVENT_NONE;
    }
    if(m->page==C3_BAG_LIST){
        if(pressed&(C3_PAD_LEFT|C3_PAD_L)){m->pocket=(m->pocket+4u)%6u+1u;m->selected=0;}
        else if(pressed&(C3_PAD_RIGHT|C3_PAD_R)){m->pocket=m->pocket%6u+1u;m->selected=0;}
        if(!refresh(m,state,catalog))return C3_BAG_EVENT_NONE;
        if(pressed&C3_PAD_UP)move_slot(m,-1);
        else if(pressed&C3_PAD_DOWN)move_slot(m,1);
        if(pressed&C3_PAD_A){
            C3ItemInfo info;
            m->item=m->view.slots[m->selected].id;
            if(!m->item){notice(m,C3_BAG_LIST,"This pocket is empty.");return C3_BAG_EVENT_NONE;}
            if(!c3_item_info(catalog,m->item,&info)||!info.field_supported){
                notice(m,C3_BAG_LIST,"This item's field action is not implemented.\nYour inventory is unchanged.");
                return C3_BAG_EVENT_NONE;
            }
            m->target=0;
            if(!m->party[m->target].species)move_target(m,1);
            m->page=C3_BAG_TARGET;
        }
    }else if(m->page==C3_BAG_TARGET){
        if(pressed&C3_PAD_UP)move_target(m,-1);
        else if(pressed&C3_PAD_DOWN)move_target(m,1);
        if(pressed&C3_PAD_A){
            char error[C3_GAME_ERROR_SIZE]={0};
            C3ItemUseStatus result=c3_game_item_use(state,catalog,m->pocket,m->selected,
                m->item,m->target,1,&m->proposed,error);
            if(result==C3_ITEM_APPLIED){m->page=C3_BAG_CONFIRM;m->yes=0;}
            else notice(m,C3_BAG_TARGET,error);
        }
    }else if(m->page==C3_BAG_CONFIRM){
        /* Default NO; direction+confirm in one packet cannot accept. */
        if(pressed&(C3_PAD_UP|C3_PAD_DOWN|C3_PAD_LEFT|C3_PAD_RIGHT)){
            m->yes=(uint8_t)!m->yes;return C3_BAG_EVENT_NONE;
        }
        if(pressed&C3_PAD_A){
            if(!m->yes){m->page=C3_BAG_TARGET;return C3_BAG_EVENT_NONE;}
            C3ItemUseResult current;char error[C3_GAME_ERROR_SIZE]={0};
            C3ItemUseStatus result=c3_game_item_use(state,catalog,m->pocket,m->selected,
                m->item,m->target,1,&current,error);
            if(result!=C3_ITEM_APPLIED){notice(m,C3_BAG_LIST,error);return C3_BAG_EVENT_NONE;}
            if(current.quantity_before!=m->proposed.quantity_before ||
               current.hp_before!=m->proposed.hp_before ||
               current.condition_before!=m->proposed.condition_before ||
               current.hp_after!=m->proposed.hp_after || current.condition_after!=m->proposed.condition_after){
                m->proposed=current;m->yes=0;
                notice(m,C3_BAG_CONFIRM,"The target or quantity changed.\nReview the updated effect before using.");
                return C3_BAG_EVENT_NONE;
            }
            result=c3_game_item_use(state,catalog,m->pocket,m->selected,m->item,
                                    m->target,0,&current,error);
            if(result!=C3_ITEM_APPLIED){notice(m,C3_BAG_LIST,error);return C3_BAG_EVENT_NONE;}
            if(used)*used=current;
            if(!refresh(m,state,catalog))return C3_BAG_EVENT_USED;
            snprintf(m->notice,sizeof m->notice,
                "Item used.\nHP %u -> %u%s\nQuantity %u -> %u\n\nSave from the field menu to keep\nthese changes after closing.",
                current.hp_before,current.hp_after,current.cured?"\nCondition cured.":"",
                current.quantity_before,current.quantity_after);
            m->page=C3_BAG_NOTICE;m->return_page=C3_BAG_LIST;return C3_BAG_EVENT_USED;
        }
    }
    return C3_BAG_EVENT_NONE;
}
static const char *condition_name(unsigned condition)
{
    switch(condition){case 0:return "OK";case 3:return "POISON";case 4:return "TOXIC";
    case 5:return "PARALYSIS";case 6:return "BURN";case 7:return "FROZEN";
    case 8:return "SLEEP";default:return "OTHER";}
}
void c3_bag_menu_draw(const C3BagMenu *m,const C3ItemCatalog *catalog)
{
    if(!m)return;
#ifndef C3_BAG_HOST_TEST
    consoleClear();
#endif
    printf("BAG | %s\n\n",c3_bag_pocket_name(m->pocket));
    if(m->page==C3_BAG_NOTICE){
        printf("%s\n\nA / B: continue\n",m->notice);return;
    }
    if(m->page==C3_BAG_LIST){
        unsigned ordinal=0,total=0;
        for(unsigned i=0;i<m->view.capacity;++i)if(m->view.slots[i].id){
            if(i==m->selected)ordinal=total;
            ++total;
        }
        unsigned first=(ordinal/8u)*8u,shown=0;
        if(!total)printf("(EMPTY)\n");
        for(unsigned i=0;i<m->view.capacity;++i)if(m->view.slots[i].id){
            unsigned position=shown++;if(position<first||position>=first+8)continue;
            C3ItemInfo info;
            if(!c3_item_info(catalog,m->view.slots[i].id,&info))continue;
            printf("%s %-17.17s x%3u%s\n",m->selected==i?">":" ",info.name,
                   m->view.slots[i].quantity,info.field_supported?"":" *");
        }
        printf("\n%u/%u entries\nL/R or Left/Right: pocket\nUp/Down: item    A: use\nB/X: back\n\n* Field action not implemented.\n",total?ordinal+1:0,total);
    }else if(m->page==C3_BAG_TARGET){
        C3ItemInfo info;
        if(c3_item_info(catalog,m->item,&info))printf("Use %s on:\n\n",info.name);
        for(unsigned i=0;i<6;++i)if(m->party[i].species){
            const C3BagPokemon *p=&m->party[i];
            printf("%s %-12.12s Lv.%u\n  HP %u/%u  %s%s\n",i==m->target?">":" ",p->name,p->level,
                p->hp,p->maximum_hp,p->egg?"EGG":p->hyper?"HYPER":condition_name(p->condition),p->hp?"":" / FAINTED");
        }
        printf("\nA: preview use   B: back\n");
    }else if(m->page==C3_BAG_CONFIRM){
        C3ItemInfo info;
        if(c3_item_info(catalog,m->item,&info))printf("Use one %s?\n\n",info.name);
        printf("%s\nHP %u -> %u\n",m->party[m->target].name,m->proposed.hp_before,m->proposed.hp_after);
        if(m->proposed.cured)printf("Cure %s\n",condition_name(m->proposed.condition_before));
        printf("Remaining: %u\n\n%s YES\n%s NO\n\nA: confirm   B: cancel\n",m->proposed.quantity_after,m->yes?">":" ",m->yes?" ":">");
    }
}
