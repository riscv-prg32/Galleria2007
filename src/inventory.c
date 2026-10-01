/*
 * Backpack implementation. Slots stay compact (no holes) so the menu order
 * equals the pickup order.
 */
#include "inventory.h"
#include "world.h"

void inv_init(Backpack *pack) {
    for (int i = 0; i < BACKPACK_SLOTS; ++i) pack->slot[i] = INV_EMPTY;
}

int inv_count(const Backpack *pack) {
    int n = 0;
    for (int i = 0; i < BACKPACK_SLOTS; ++i) n += pack->slot[i] != INV_EMPTY;
    return n;
}

int inv_full(const Backpack *pack) { return inv_count(pack) >= BACKPACK_SLOTS; }

int inv_add(Backpack *pack, uint8_t item) {
    for (int i = 0; i < BACKPACK_SLOTS; ++i) {
        if (pack->slot[i] == INV_EMPTY) {
            pack->slot[i] = item;
            return i;
        }
    }
    return -1;
}

uint8_t inv_remove(Backpack *pack, int slot) {
    if (slot < 0 || slot >= BACKPACK_SLOTS) return INV_EMPTY;
    uint8_t item = pack->slot[slot];
    for (int i = slot; i + 1 < BACKPACK_SLOTS; ++i) pack->slot[i] = pack->slot[i + 1];
    pack->slot[BACKPACK_SLOTS - 1] = INV_EMPTY;
    return item;
}

int inv_find_item(const Backpack *pack, uint8_t item) {
    for (int i = 0; i < BACKPACK_SLOTS; ++i) {
        if (pack->slot[i] == item) return i;
    }
    return -1;
}

int inv_find_type(const Backpack *pack, uint8_t type) {
    for (int i = 0; i < BACKPACK_SLOTS; ++i) {
        uint8_t it = pack->slot[i];
        if (it != INV_EMPTY && g_world.items[it].type == type) return i;
    }
    return -1;
}

int inv_swap(Backpack *pack, int slot, int target, uint16_t tag,
             int32_t x, int32_t y, int sector) {
    if (slot < 0 || slot >= BACKPACK_SLOTS || pack->slot[slot] == INV_EMPTY) return 0;
    if (target < 0 || target >= g_world.item_count ||
        g_world.items[target].state != IS_WORLD) return 0;
    uint8_t old = pack->slot[slot];
    if (!world_item_drop(old, x, y, sector)) return 0;
    inv_remove(pack, slot);
    if (!world_item_take(target, tag)) return 0;   /* old item stays on the floor */
    inv_add(pack, (uint8_t)target);
    return 1;
}

int inv_reconcile(Backpack *pack, uint16_t tag) {
    int removed = 0;
    for (int i = 0; i < BACKPACK_SLOTS;) {
        uint8_t it = pack->slot[i];
        if (it != INV_EMPTY &&
            (g_world.items[it].state != IS_CARRIED || g_world.items[it].holder != tag)) {
            inv_remove(pack, i);
            ++removed;
        } else {
            ++i;
        }
    }
    return removed;
}
