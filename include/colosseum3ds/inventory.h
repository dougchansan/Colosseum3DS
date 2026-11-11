#ifndef C3_INVENTORY_H
#define C3_INVENTORY_H
#include "colosseum3ds/pda.h"
typedef enum { C3_INVENTORY_USE, C3_INVENTORY_GIVE, C3_INVENTORY_TAKE, C3_INVENTORY_TOSS, C3_INVENTORY_REORDER } C3InventoryAction;
typedef enum { C3_INVENTORY_OK, C3_INVENTORY_NO_EFFECT, C3_INVENTORY_INVALID, C3_INVENTORY_FORBIDDEN, C3_INVENTORY_FULL, C3_INVENTORY_NEED_MOVE, C3_INVENTORY_NEED_REPLACEMENT, C3_INVENTORY_NEED_EVOLUTION, C3_INVENTORY_CONFLICT } C3InventoryCode;
typedef struct { C3InventoryAction action; unsigned pocket,slot,party,quantity,destination; int move,replacement,evolution; unsigned replacement_count; int replacements[20]; } C3InventoryRequest;
typedef struct { C3InventoryCode code; uint16_t item,choices[20]; unsigned choice_count,learning_index; uint16_t learned_move,evolved_species; uint8_t pokemon[312]; } C3InventoryResult;
typedef struct { uint16_t id; uint8_t kind,important,param; uint32_t name,description,field_function; } C3InventoryItem;
unsigned c3_inventory_capacity(unsigned pocket);
int c3_inventory_item(const C3PdaAssets *assets,unsigned id,C3InventoryItem *item);
/* Choices never change the save. Set move/replacement/evolution to -1 before
 * initial use. An OK result changes owned memory only; explicit Save persists. */
/* A gift from a field script (scriptGetItem): adds `count` of the item to
 * its pocket through the same slot rules as the menu. Returns 0 when all
 * were added, `count` when they do not fit (nothing is added) and -1 for an
 * unknown item or an invalid request. Owned memory only; Save persists. */
int c3_inventory_receive(C3GameState *state,const C3PdaAssets *assets,unsigned id,unsigned count);
C3InventoryCode c3_inventory_apply(C3GameState *state,const C3PdaAssets *assets,const C3InventoryRequest *request,C3InventoryResult *result);
#endif
