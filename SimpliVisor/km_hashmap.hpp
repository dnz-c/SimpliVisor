#include <ntddk.h>

typedef volatile LONG HV_SPINLOCK;

__forceinline void HvAcquireSpinLock(HV_SPINLOCK* Lock)
{
    while (_InterlockedCompareExchange(Lock, 1, 0) != 0)
    {
        while (*Lock != 0)
        {
            _mm_pause();
        }
    }
}

__forceinline void HvReleaseSpinLock(HV_SPINLOCK* Lock)
{
    _InterlockedExchange(Lock, 0);
}

typedef enum
{
    SLOT_EMPTY = 0,
    SLOT_ACTIVE,
    SLOT_REMOVED
} SLOT_STATE;

typedef struct _EPT_HOOK
{
    UINT64 cr3;
    UINT64 shadow_pfn;
} EPT_HOOK, * PEPT_HOOK;

typedef struct _HASH_ENTRY
{
    UINT64 key;
    EPT_HOOK value;
    SLOT_STATE state;
} HASH_ENTRY;

typedef struct _LINEAR_128b_HASH_MAP
{
    UINT32 entry_cap;
    UINT32 count;
    HASH_ENTRY* buffer;
    HV_SPINLOCK lock;
} LINEAR_128b_HASH_MAP, * PLINEAR_128b_HASH_MAP;

static PLINEAR_128b_HASH_MAP init_hash_map(UINT32 entries)
{
    PLINEAR_128b_HASH_MAP map = (PLINEAR_128b_HASH_MAP) ExAllocatePool(NonPagedPool, sizeof(LINEAR_128b_HASH_MAP));
    if (!map) return NULL;
    RtlSecureZeroMemory(map, sizeof(LINEAR_128b_HASH_MAP));

    map->entry_cap = entries;
    map->count = 0;
    map->buffer = (HASH_ENTRY*) ExAllocatePool(NonPagedPool, entries * sizeof(HASH_ENTRY));

    if (!map->buffer)
    {
        ExFreePool(map);
        return NULL;
    }

    RtlSecureZeroMemory(map->buffer, entries * sizeof(HASH_ENTRY));

    return map;
}

static bool insert_in_hashmap(PLINEAR_128b_HASH_MAP map, UINT64 key, EPT_HOOK value)
{
    if (map->count >= map->entry_cap) return FALSE;

    HvAcquireSpinLock(&map->lock);

    UINT32 index = key % map->entry_cap;

    for (UINT32 i = 0; i < map->entry_cap; i++) {
        UINT32 slot = (index + i) % map->entry_cap;
        HASH_ENTRY* entry = &map->buffer[slot];

        if (entry->state != SLOT_ACTIVE || entry->key == key)
        {
            if (entry->state != SLOT_ACTIVE) map->count++;

            entry->key = key;
            entry->value = value;
            entry->state = SLOT_ACTIVE;
            HvReleaseSpinLock(&map->lock);
            return TRUE;
        }
    }
    HvReleaseSpinLock(&map->lock);
    return FALSE;
}

static bool get_in_hashmap(PLINEAR_128b_HASH_MAP map, UINT64 key, PEPT_HOOK out_value)
{
    UINT32 index = key % map->entry_cap;

    for (UINT32 i = 0; i < map->entry_cap; i++)
    {
        UINT32 slot = (index + i) % map->entry_cap;
        HASH_ENTRY* entry = &map->buffer[slot];

        if (entry->state == SLOT_EMPTY) return FALSE;

        if (entry->state == SLOT_ACTIVE && entry->key == key)
        {
            if (out_value) *out_value = entry->value;
            return TRUE;
        }
    }
    return FALSE;
}

static bool remove_from_hashmap(PLINEAR_128b_HASH_MAP map, UINT64 key)
{
    UINT32 index = key % map->entry_cap;

    HvAcquireSpinLock(&map->lock);

    for (UINT32 i = 0; i < map->entry_cap; i++) {
        UINT32 slot = (index + i) % map->entry_cap;
        HASH_ENTRY* entry = &map->buffer[slot];

        if (entry->state == SLOT_EMPTY) return FALSE;

        if (entry->state == SLOT_ACTIVE && entry->key == key)
        {
            entry->state = SLOT_REMOVED;
            map->count--;
            HvReleaseSpinLock(&map->lock);
            return TRUE;
        }
    }
    HvReleaseSpinLock(&map->lock);
    return FALSE;
}

static void free_hash_map(PLINEAR_128b_HASH_MAP map)
{
    if (map)
    {
        ExFreePool(map->buffer);
        ExFreePool(map);
    }
}