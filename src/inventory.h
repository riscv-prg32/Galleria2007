/*
 * Backpack: a fixed array of item-instance indices (not booleans), so the
 * same physical object can be dropped, recovered and traded between players.
 */
#ifndef G2007_INVENTORY_H
#define G2007_INVENTORY_H

#include <stdint.h>
#include "config.h"

#define INV_EMPTY 0xFFu

typedef struct {
    uint8_t slot[BACKPACK_SLOTS];   /* item instance index or INV_EMPTY */
} Backpack;

void inv_init(Backpack *pack);
int inv_count(const Backpack *pack);
int inv_full(const Backpack *pack);
int inv_add(Backpack *pack, uint8_t item);           /* slot or -1 when full */
uint8_t inv_remove(Backpack *pack, int slot);        /* item or INV_EMPTY    */
int inv_find_item(const Backpack *pack, uint8_t item);
int inv_find_type(const Backpack *pack, uint8_t type); /* via g_world items  */

/* Swap flow for a full backpack: drop the item in `slot` at (x, y) and take
 * `target` into the freed slot. Fails without changing anything when the
 * drop position is invalid or the target is no longer available. */
int inv_swap(Backpack *pack, int slot, int target, uint16_t tag,
             int32_t x, int32_t y, int sector);

/* Remove instances that the shared world no longer assigns to `tag`
 * (e.g. a simultaneous pickup won by another player). Returns how many. */
int inv_reconcile(Backpack *pack, uint16_t tag);

#endif
