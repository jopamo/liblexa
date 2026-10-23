#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "liblexa/base.h"
#include "base/budget_internal.h"

typedef struct {
    uint8_t *key;
    size_t length;
    uint64_t hash;
    void *value;
    bool live;
    bool used;
} slot_t;

struct lxa_table {
    lxa_allocator_t allocator;
    lxa_budget_t owned_budget;
    lxa_budget_t *budget;
    lxa_budget_kind_t reason;
    uint8_t key[16];
    slot_t *slots;
    size_t capacity, count, used;
};

static lxa_status_t
charge(lxa_table_t *table, lxa_budget_kind_t kind, size_t count)
{
    return lxa_budget_charge_impl(table->budget, kind, count, &table->reason);
}

static void
release_memory(lxa_table_t *table, size_t bytes)
{
    lxa_status_t status = lxa_budget_release(table->budget,
                                              LXA_BUDGET_MEMORY, bytes);
    assert(status == LXA_OK);
    (void) status;
}

static lxa_status_t
hash_key(lxa_table_t *table, lxa_span_t key, uint64_t *hash)
{
    lxa_status_t status;
    status = lxa_budget_check_item(table->budget, LXA_BUDGET_NAME_BYTES,
                                   key.length, &table->reason);
    if (status != LXA_OK) {
        return status;
    }
    status = charge(table, LXA_BUDGET_WORK, key.length / 8 + 1);
    return status == LXA_OK ? lxa_siphash24(table->key, key.data,
                                             key.length, hash) : status;
}

/* At least one empty slot exists at the chosen load factor. A deleted slot
 * may be reused, but lookup continues past it to find a later matching key.
 */
static lxa_status_t
locate(lxa_table_t *table, lxa_span_t key, uint64_t hash,
       size_t *index, bool *found)
{
    size_t step, candidate = SIZE_MAX;
    *found = false;
    *index = SIZE_MAX;
    if (table->capacity == 0) {
        return LXA_OK;
    }
    for (step = 0; step < table->capacity; ++step) {
        size_t position = ((size_t) hash + step) & (table->capacity - 1);
        slot_t *slot = &table->slots[position];
        lxa_status_t status = charge(table, LXA_BUDGET_WORK, 1);
        if (status != LXA_OK) {
            return status;
        }
        if (!slot->used) {
            *index = candidate == SIZE_MAX ? position : candidate;
            return LXA_OK;
        }
        if (!slot->live) {
            if (candidate == SIZE_MAX) {
                candidate = position;
            }
        }
        else if (slot->hash == hash && slot->length == key.length) {
            if (key.length != 0) {
                status = charge(table, LXA_BUDGET_WORK,
                                 key.length / 8 + (key.length % 8 != 0));
                if (status != LXA_OK) {
                    return status;
                }
                if (memcmp(slot->key, key.data, key.length) != 0) {
                    continue;
                }
            }
            *found = true;
            *index = position;
            return LXA_OK;
        }
    }
    *index = candidate;
    return LXA_OK;
}

static lxa_status_t
prepare_slots(lxa_table_t *table, size_t capacity, slot_t **out,
              size_t *bytes)
{
    lxa_status_t status = lxa_size_mul(capacity, sizeof(slot_t), bytes);
    if (status != LXA_OK) {
        return status;
    }
    status = charge(table, LXA_BUDGET_MEMORY, *bytes);
    if (status != LXA_OK) {
        return status;
    }
    status = lxa_calloc(&table->allocator, capacity, sizeof(slot_t),
                        (void **) out);
    if (status == LXA_OK)
        status = lxa_budget_check_impl(table->budget, LXA_BUDGET_MEMORY,
                                       0, &table->reason);
    if (status == LXA_OK)
        status = lxa_budget_check_impl(table->budget, LXA_BUDGET_WORK,
                                       0, &table->reason);
    if (status != LXA_OK) {
        lxa_free(&table->allocator, *out);
        *out = NULL;
        release_memory(table, *bytes);
    }
    return status;
}

/* Rebuild privately. No old slot or key moves until all work is charged. */
static lxa_status_t
grow(lxa_table_t *table, size_t capacity)
{
    slot_t *next = NULL;
    size_t bytes, i, old_bytes;
    lxa_status_t status = prepare_slots(table, capacity, &next, &bytes);
    if (status != LXA_OK) {
        return status;
    }
    for (i = 0; i < table->capacity; ++i) {
        size_t step;
        status = charge(table, LXA_BUDGET_WORK, 1);
        if (status != LXA_OK) {
            lxa_free(&table->allocator, next);
            release_memory(table, bytes);
            return status;
        }
        if (!table->slots[i].live) {
            continue;
        }
        for (step = 0; step < capacity; ++step) {
            size_t position = ((size_t) table->slots[i].hash + step)
                              & (capacity - 1);
            status = charge(table, LXA_BUDGET_WORK, 1);
            if (status != LXA_OK) {
                lxa_free(&table->allocator, next);
                release_memory(table, bytes);
                return status;
            }
            if (!next[position].used) {
                next[position] = table->slots[i];
                break;
            }
        }
    }
    old_bytes = table->capacity * sizeof(slot_t);
    lxa_free(&table->allocator, table->slots);
    release_memory(table, old_bytes);
    table->slots = next;
    table->capacity = capacity;
    table->used = table->count;
    return LXA_OK;
}

lxa_status_t
lxa_table_create_with_budget(const lxa_allocator_t *allocator,
                             lxa_budget_t *budget,
                             lxa_entropy_callback_t entropy,
                             void *entropy_context, lxa_table_t **out)
{
    lxa_table_t *table;
    lxa_budget_t local = {0};
    uint8_t key[16];
    lxa_status_t status;
    if (out == NULL) {
        return LXA_ERROR_ARGUMENT;
    }
    *out = NULL;
    if (allocator == NULL || allocator->allocate == NULL
        || allocator->resize == NULL || allocator->release == NULL) {
        return LXA_ERROR_ARGUMENT;
    }
    status = lxa_entropy_key(key, entropy, entropy_context);
    if (status != LXA_OK) {
        return status;
    }
    if (budget == NULL) {
        status = lxa_budget_init(&local, NULL);
        if (status != LXA_OK) {
            return status;
        }
        budget = &local;
    }
    status = lxa_budget_charge_impl(budget, LXA_BUDGET_MEMORY, sizeof(*table), NULL);
    if (status != LXA_OK) {
        return status;
    }
    status = lxa_calloc(allocator, 1, sizeof(*table), (void **) &table);
    if (status != LXA_OK) {
        lxa_budget_release(budget, LXA_BUDGET_MEMORY, sizeof(*table));
        return status;
    }
    status = lxa_budget_check_impl(budget, LXA_BUDGET_MEMORY, 0, NULL);
    if (status != LXA_OK) {
        lxa_free(allocator, table);
        lxa_budget_release(budget, LXA_BUDGET_MEMORY, sizeof(*table));
        return status;
    }
    table->allocator = *allocator;
    table->owned_budget = local;
    table->budget = budget == &local ? &table->owned_budget : budget;
    table->reason = LXA_BUDGET_COUNT;
    memcpy(table->key, key, sizeof(key));
    *out = table;
    return LXA_OK;
}

lxa_status_t
lxa_table_create(const lxa_allocator_t *allocator, lxa_table_t **out)
{
    return lxa_table_create_with_budget(allocator, NULL, NULL, NULL, out);
}

lxa_budget_kind_t
lxa_table_limit_reason(const lxa_table_t *table)
{
    return table == NULL ? LXA_BUDGET_COUNT : table->reason;
}

lxa_status_t
lxa_table_clear_cost(const lxa_table_t *table, size_t *work)
{
    size_t scan;
    lxa_status_t status;
    if (table == NULL || work == NULL) {
        return LXA_ERROR_ARGUMENT;
    }
    status = lxa_size_mul(table->capacity, 2, &scan);
    return status == LXA_OK ? lxa_size_add(scan, table->count, work) : status;
}

lxa_status_t
lxa_table_clear_checked(lxa_table_t *table)
{
    size_t work;
    lxa_status_t status;
    if (table == NULL) {
        return LXA_ERROR_ARGUMENT;
    }
    table->reason = LXA_BUDGET_COUNT;
    status = lxa_table_clear_cost(table, &work);
    if (status != LXA_OK) {
        return status;
    }
    status = charge(table, LXA_BUDGET_WORK, work);
    if (status == LXA_OK) {
        lxa_table_clear(table);
    }
    return status;
}

void
lxa_table_clear(lxa_table_t *table)
{
    size_t i;
    if (table == NULL) {
        return;
    }
    for (i = 0; i < table->capacity; ++i) {
        if (table->slots[i].live) {
            size_t bytes = table->slots[i].length != 0
                           ? table->slots[i].length : 1;
            lxa_free(&table->allocator, table->slots[i].key);
            release_memory(table, bytes);
        }
    }
    if (table->capacity != 0) {
        memset(table->slots, 0, table->capacity * sizeof(slot_t));
    }
    table->count = table->used = 0;
}

void
lxa_table_destroy(lxa_table_t *table)
{
    lxa_allocator_t allocator;
    if (table == NULL) {
        return;
    }
    allocator = table->allocator;
    lxa_table_clear(table);
    lxa_free(&allocator, table->slots);
    release_memory(table, table->capacity * sizeof(slot_t));
    release_memory(table, sizeof(*table));
    lxa_free(&allocator, table);
}

size_t
lxa_table_length(const lxa_table_t *table)
{
    return table == NULL ? 0 : table->count;
}

lxa_status_t
lxa_table_find(const lxa_table_t *table, lxa_span_t key,
               bool *found, void **value)
{
    size_t index;
    uint64_t hash;
    bool exists;
    lxa_status_t status;
    lxa_table_t *mutable = (lxa_table_t *) table;
    if (found != NULL) {
        *found = false;
    }
    if (value != NULL) {
        *value = NULL;
    }
    if (table == NULL || (key.length != 0 && key.data == NULL)) {
        return LXA_ERROR_ARGUMENT;
    }
    mutable->reason = LXA_BUDGET_COUNT;
    status = hash_key(mutable, key, &hash);
    if (status == LXA_OK) {
        status = locate(mutable, key, hash, &index, &exists);
    }
    if (status == LXA_OK && exists) {
        if (found != NULL) {
            *found = true;
        }
        if (value != NULL) {
            *value = table->slots[index].value;
        }
    }
    return status;
}

lxa_status_t
lxa_table_set(lxa_table_t *table, lxa_span_t key, void *value,
              bool *replaced, void **previous)
{
    size_t index, bytes, capacity;
    uint64_t hash;
    bool exists;
    uint8_t *copy = NULL;
    lxa_status_t status;
    if (replaced != NULL) {
        *replaced = false;
    }
    if (previous != NULL) {
        *previous = NULL;
    }
    if (table == NULL || (key.length != 0 && key.data == NULL)) {
        return LXA_ERROR_ARGUMENT;
    }
    table->reason = LXA_BUDGET_COUNT;
    status = hash_key(table, key, &hash);
    if (status == LXA_OK) {
        status = locate(table, key, hash, &index, &exists);
    }
    if (status != LXA_OK) {
        return status;
    }
    if (exists) {
        if (replaced != NULL) {
            *replaced = true;
        }
        if (previous != NULL) {
            *previous = table->slots[index].value;
        }
        table->slots[index].value = value;
        return LXA_OK;
    }
    bytes = key.length != 0 ? key.length : 1;
    status = charge(table, LXA_BUDGET_MEMORY, bytes);
    if (status != LXA_OK) {
        return status;
    }
    status = lxa_alloc(&table->allocator, bytes, (void **) &copy);
    if (status != LXA_OK) {
        release_memory(table, bytes);
        return status;
    }
    if (key.length != 0) {
        memcpy(copy, key.data, key.length);
    }
    if (table->capacity == 0 || table->used >= table->capacity / 2) {
        capacity = table->capacity == 0 ? 16 : table->capacity;
        if (table->count >= capacity / 2) {
            if (capacity > SIZE_MAX / 2) {
                status = LXA_ERROR_OVERFLOW;
                goto fail;
            }
            capacity *= 2;
        }
        status = grow(table, capacity);
        if (status != LXA_OK) {
            goto fail;
        }
        status = locate(table, key, hash, &index, &exists);
        if (status != LXA_OK) {
            goto fail;
        }
    }
    status = lxa_budget_check_item(table->budget, LXA_BUDGET_NAME_BYTES,
                                   key.length, &table->reason);
    if (status == LXA_OK)
        status = lxa_budget_check_impl(table->budget, LXA_BUDGET_WORK,
                                       0, &table->reason);
    if (status == LXA_OK)
        status = lxa_budget_check_impl(table->budget, LXA_BUDGET_MEMORY,
                                       0, &table->reason);
    if (status != LXA_OK) {
        goto fail;
    }
    table->used += !table->slots[index].used;
    table->slots[index] = (slot_t) {copy, key.length, hash, value, true, true};
    ++table->count;
    return LXA_OK;
fail:
    lxa_free(&table->allocator, copy);
    release_memory(table, bytes);
    return status;
}

lxa_status_t
lxa_table_delete(lxa_table_t *table, lxa_span_t key,
                 bool *found, void **previous)
{
    size_t index, bytes;
    uint64_t hash;
    bool exists;
    lxa_status_t status;
    if (found != NULL) {
        *found = false;
    }
    if (previous != NULL) {
        *previous = NULL;
    }
    if (table == NULL || (key.length != 0 && key.data == NULL)) {
        return LXA_ERROR_ARGUMENT;
    }
    table->reason = LXA_BUDGET_COUNT;
    status = hash_key(table, key, &hash);
    if (status == LXA_OK) {
        status = locate(table, key, hash, &index, &exists);
    }
    if (status != LXA_OK || !exists) {
        return status;
    }
    if (found != NULL) {
        *found = true;
    }
    if (previous != NULL) {
        *previous = table->slots[index].value;
    }
    bytes = table->slots[index].length != 0 ? table->slots[index].length : 1;
    lxa_free(&table->allocator, table->slots[index].key);
    release_memory(table, bytes);
    table->slots[index].key = NULL;
    table->slots[index].live = false;
    --table->count;
    return LXA_OK;
}

lxa_status_t
lxa_table_each(const lxa_table_t *table, lxa_table_visit_f visit, void *context)
{
    size_t i;
    lxa_table_t *mutable = (lxa_table_t *) table;
    if (table == NULL || visit == NULL) {
        return LXA_ERROR_ARGUMENT;
    }
    mutable->reason = LXA_BUDGET_COUNT;
    for (i = 0; i < table->capacity; ++i) {
        slot_t *slot = &table->slots[i];
        lxa_status_t status = charge(mutable, LXA_BUDGET_WORK, 1);
        if (status != LXA_OK) {
            return status;
        }
        if (slot->live) {
            lxa_span_t key = {slot->key, slot->length};
            status = visit(context, key, slot->value);
            if (status != LXA_OK) {
                return status;
            }
        }
    }
    return LXA_OK;
}
