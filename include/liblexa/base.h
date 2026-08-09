#ifndef LIBLEXA_BASE_H
#define LIBLEXA_BASE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Keep language/compiler-specific arithmetic in this header. The opt-out
 * exists for testing the portable path; production selects the best available
 * implementation without changing callers.
 */
#if !defined(LXA_PORTABLE_CHECKED_ARITHMETIC)
# if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
#  if defined(__has_include)
#   if __has_include(<stdckdint.h>)
#    include <stdckdint.h>
#    define LXA_USE_CKD_ARITHMETIC 1
#   endif
#  endif
# endif
# if !defined(LXA_USE_CKD_ARITHMETIC)
#  if defined(__has_builtin)
#   if __has_builtin(__builtin_add_overflow) && __has_builtin(__builtin_mul_overflow)
#    define LXA_USE_BUILTIN_ARITHMETIC 1
#   endif
#  elif defined(__GNUC__) && __GNUC__ >= 5
#   define LXA_USE_BUILTIN_ARITHMETIC 1
#  endif
# endif
#endif

/* Return nonzero on overflow; callers commit *out only on success. */
static inline int
lxa_checked_add_size(size_t a, size_t b, size_t *out)
{
#if defined(LXA_USE_CKD_ARITHMETIC)
    return ckd_add(out, a, b);
#elif defined(LXA_USE_BUILTIN_ARITHMETIC)
    return __builtin_add_overflow(a, b, out);
#else
    if (a > SIZE_MAX - b) {
        return 1;
    }
    *out = a + b;
    return 0;
#endif
}

static inline int
lxa_checked_mul_size(size_t a, size_t b, size_t *out)
{
#if defined(LXA_USE_CKD_ARITHMETIC)
    return ckd_mul(out, a, b);
#elif defined(LXA_USE_BUILTIN_ARITHMETIC)
    return __builtin_mul_overflow(a, b, out);
#else
    if (b != 0 && a > SIZE_MAX / b) {
        return 1;
    }
    *out = a * b;
    return 0;
#endif
}

/* Canonical allocation and region lifetime contract. */


#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LXA_OK = 0,
    LXA_ERROR_ARGUMENT,
    LXA_ERROR_OVERFLOW,
    LXA_ERROR_NO_MEMORY,
    LXA_ERROR_LIMIT,
    LXA_ERROR_ENTROPY,
    LXA_ERROR_UNSUPPORTED,
    LXA_ERROR_INTERNAL
} lxa_status_t;

/*
 * Core operations return OK only after publishing a complete result.
 * ARGUMENT rejects an invalid handle, pointer, or index; OVERFLOW rejects
 * size arithmetic before allocation; NO_MEMORY leaves earlier owned state
 * usable; LIMIT means an explicit resource budget was reached. A caller
 * propagates the first error and does not treat a partial result as committed.
 * Output defaults and rollback guarantees are stated
 * by each operation. Parser-specific continuation, malformed input and
 * output-full results belong to those modules.
 * ENTROPY means the OS or caller could not provide a new hash key; callers
 * must abort secure initialization rather than substitute a predictable key.
 * UNSUPPORTED rejects a deliberate implementation boundary; it is not an
 * internal failure or a recoverable grammar diagnostic. INTERNAL means an
 * implementation invariant failed under valid API use. Discard the failed
 * operation's output; do not retry by weakening resource limits.
 * Mutable core objects are confined to one caller at a time. Allocator
 * callbacks must not reenter the object whose allocation is in progress;
 * observers may only read a container during its iteration callback when
 * that container's contract allows it. Stop readers before invalidating
 * borrowed views or resetting a region.
 */

/*
 * A region copies this descriptor at creation. Its context remains owned by
 * the caller and must outlive the region and every allocation made through it.
 * All three callbacks are required. resize(context, p, n) preserves p when
 * it returns NULL; release(context, NULL) is not called. Callbacks return
 * storage suitable for any fundamental C type (as malloc does).
 */
typedef struct {
    void *context;
    void *(*allocate)(void *context, size_t size);
    void *(*resize)(void *context, void *ptr, size_t size);
    void (*release)(void *context, void *ptr);
} lxa_allocator_t;

/* A value backed by the C runtime; no process-wide allocator is mutable. */
lxa_allocator_t lxa_allocator_default(void);

/* Standalone storage is released with the same allocator descriptor.
 * lxa_free requires a valid descriptor when ptr is non-NULL.
 */
lxa_status_t lxa_alloc(const lxa_allocator_t *allocator, size_t size,
                       void **out);
lxa_status_t lxa_calloc(const lxa_allocator_t *allocator, size_t count,
                        size_t size, void **out);
/* On failure *ptr still owns the original allocation. NULL *ptr is allowed. */
lxa_status_t lxa_resize(const lxa_allocator_t *allocator, void **ptr,
                        size_t size);
lxa_status_t lxa_resize_array(const lxa_allocator_t *allocator, void **ptr,
                              size_t count, size_t item_size);
void lxa_free(const lxa_allocator_t *allocator, void *ptr);

typedef struct lxa_arena lxa_arena_t;
typedef struct lxa_pool lxa_pool_t;

/*
 * All create/alloc functions set a non-NULL output to NULL before doing any
 * work. A NULL handle/output, missing callback, or zero size is an argument
 * error; unrepresentable sizes are overflow; exhausted storage is NO_MEMORY.
 * On failure, earlier allocations remain valid. No user-visible allocation
 * is published until its entire backing storage is ready.
 */
lxa_status_t lxa_arena_create(const lxa_allocator_t *allocator,
                              size_t minimum_chunk_size, lxa_arena_t **out);
lxa_status_t lxa_arena_alloc(lxa_arena_t *arena, size_t size, void **out);

/*
 * Arena allocations cannot be released individually. reset invalidates all
 * of them, including borrowed pointers; destroy also releases backing
 * storage. The arena owns its allocations, never its allocator context.
 * Callers must stop all readers before reset or destroy. reset may retain
 * backing capacity; it never invokes client destructors.
 */
void lxa_arena_reset(lxa_arena_t *arena);
void lxa_arena_destroy(lxa_arena_t *arena);

lxa_status_t lxa_pool_create(const lxa_allocator_t *allocator,
                             lxa_pool_t **out);
lxa_status_t lxa_pool_alloc(lxa_pool_t *pool, size_t size, void **out);

/*
 * Pool allocations may be returned individually. free(NULL) is a no-op;
 * other pointers must be live allocations from this pool, not interior
 * pointers. free invalidates only that allocation. reset invalidates *all*
 * pool allocations, including ones not attached to a tree. destroy also
 * releases the pool's storage. Neither operation invokes client destructors.
 * The caller must stop readers and release externally owned resources first.
 */
void lxa_pool_free(lxa_pool_t *pool, void *ptr);
void lxa_pool_reset(lxa_pool_t *pool);
void lxa_pool_destroy(lxa_pool_t *pool);

/*
 * An allocation can move between pools without moving its address. Both
 * pools must use identical allocator callbacks AND context: the destination
 * must be able to release the block after the source pool is destroyed.
 * Invalid handles or incompatible allocators return ARGUMENT unchanged.
 * ptr must be a live allocation from source (as for lxa_pool_free).
 * Moving to the same pool succeeds without changing anything.
 * No allocation, callback, or client destructor is involved.
 */
lxa_status_t lxa_pool_transfer(lxa_pool_t *destination, lxa_pool_t *source,
                               void *ptr);
/* Preflight a batch of transfers before changing any ownership. */
int lxa_pool_compatible(const lxa_pool_t *destination,
                        const lxa_pool_t *source);

/*
 * Regions are confined to one thread at a time. A caller may use distinct
 * regions concurrently if its allocator context/callbacks permit it.
 * Regions do not read mutable process-wide allocation settings.
 */
#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Document-local indexes use UINT32_MAX only for the absent-link sentinel.
 * Counts may reach UINT32_MAX; a valid index may not.
 */
typedef uint32_t lxa_id_t;
#define LXA_ID_NONE UINT32_MAX
typedef uint64_t lxa_handle_t;
#define LXA_HANDLE_NONE UINT64_C(0)

/* A handle packs a document-local slot and a nonzero generation. It does
 * not carry document identity or reset epoch; the owning document validates
 * those separately. Slot generation must never wrap: retire the slot when
 * its generation would advance from UINT32_MAX to zero.
 */
static inline lxa_status_t
lxa_handle_pack(lxa_id_t slot, uint32_t generation, lxa_handle_t *out)
{
    if (out == NULL) {
        return LXA_ERROR_ARGUMENT;
    }
    *out = LXA_HANDLE_NONE;
    if (slot == LXA_ID_NONE || generation == 0) {
        return LXA_ERROR_ARGUMENT;
    }
    *out = ((uint64_t) generation << 32) | slot;
    return LXA_OK;
}

static inline lxa_status_t
lxa_handle_unpack(lxa_handle_t handle, lxa_id_t *slot, uint32_t *generation)
{
    lxa_id_t index;
    uint32_t gen;
    if (slot == NULL || generation == NULL) {
        return LXA_ERROR_ARGUMENT;
    }
    *slot = 0;
    *generation = 0;
    index = (lxa_id_t) handle;
    gen = (uint32_t) (handle >> 32);
    if (index == LXA_ID_NONE || gen == 0) {
        return LXA_ERROR_ARGUMENT;
    }
    *slot = index;
    *generation = gen;
    return LXA_OK;
}

/*
 * The output must be non-NULL. On error, a valid output is always zeroed;
 * callers must check the status before using it as an allocation size.
 * Zero is a valid arithmetic result, though region allocation rejects it.
 */
static inline lxa_status_t
lxa_size_add(size_t a, size_t b, size_t *out)
{
    size_t sum;
    if (out == NULL) {
        return LXA_ERROR_ARGUMENT;
    }
    *out = 0;
    if (lxa_checked_add_size(a, b, &sum)) {
        return LXA_ERROR_OVERFLOW;
    }
    *out = sum;
    return LXA_OK;
}

static inline lxa_status_t
lxa_size_mul(size_t a, size_t b, size_t *out)
{
    size_t product;
    if (out == NULL) {
        return LXA_ERROR_ARGUMENT;
    }
    *out = 0;
    if (lxa_checked_mul_size(a, b, &product)) {
        return LXA_ERROR_OVERFLOW;
    }
    *out = product;
    return LXA_OK;
}

/* Counts may use all 32 bits; indexes must leave LXA_ID_NONE available.
 * Callers check the status even though failure clears *out.
 */
static inline lxa_status_t
lxa_size_to_u32(size_t value, uint32_t *out)
{
    if (out == NULL) {
        return LXA_ERROR_ARGUMENT;
    }
    *out = 0;
    if (value > UINT32_MAX) {
        return LXA_ERROR_OVERFLOW;
    }
    *out = (uint32_t) value;
    return LXA_OK;
}

static inline lxa_status_t
lxa_size_to_id(size_t index, lxa_id_t *out)
{
    lxa_status_t status = lxa_size_to_u32(index, out);
    if (status == LXA_OK && *out == LXA_ID_NONE) {
        *out = 0;
        return LXA_ERROR_OVERFLOW;
    }
    return status;
}

/* Align a byte count upward; alignment must be a nonzero power of two. */
static inline lxa_status_t
lxa_size_align(size_t size, size_t alignment, size_t *out)
{
    size_t mask;
    lxa_status_t status;
    if (out == NULL) {
        return LXA_ERROR_ARGUMENT;
    }
    *out = 0;
    if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
        return LXA_ERROR_ARGUMENT;
    }
    mask = alignment - 1;
    status = lxa_size_add(size, mask, out);
    if (status != LXA_OK) {
        return status;
    }
    *out &= ~mask;
    return LXA_OK;
}

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    size_t max_input_bytes;
    size_t max_memory_bytes;
    uint32_t max_nodes;
    uint32_t max_attributes;
    uint32_t max_attributes_per_element;
    size_t max_string_bytes;
    size_t max_token_bytes;
    size_t max_name_bytes;
    uint32_t max_tree_depth;
    uint32_t max_open_elements;
    uint32_t max_active_formatting; /* HTML entries, including scope markers */
    uint32_t max_css_nesting;
    uint32_t max_tokens;
    uint64_t initial_work_credit;
    uint64_t work_per_input_byte;
    uint64_t max_work;
} lxa_limits_t;

typedef enum {
    LXA_BUDGET_INPUT,
    LXA_BUDGET_MEMORY,
    LXA_BUDGET_NODES,
    LXA_BUDGET_ATTRIBUTES,
    LXA_BUDGET_STRINGS,
    LXA_BUDGET_TREE_DEPTH,
    LXA_BUDGET_OPEN_ELEMENTS,
    LXA_BUDGET_ACTIVE_FORMATTING,
    LXA_BUDGET_CSS_NESTING,
    LXA_BUDGET_TOKENS,
    LXA_BUDGET_WORK,
    /* Per-object checks; these are not aggregate charges. */
    LXA_BUDGET_ATTRS_PER_ELEMENT,
    LXA_BUDGET_TOKEN_BYTES,
    LXA_BUDGET_NAME_BYTES,
    LXA_BUDGET_COUNT
} lxa_budget_kind_t;
/* A limit reason is the resource that rejected the operation. COUNT means
 * no limit failed (success, invalid arguments or arithmetic overflow).
 */
typedef lxa_budget_kind_t lxa_limit_reason_t;

typedef struct {
    lxa_limits_t limits;
    uint64_t used[LXA_BUDGET_COUNT];
    uint64_t work_credit;
} lxa_budget_t;

/* The default is bounded; unbounded is explicit opt-in. Arithmetic/ID
 * representability still applies to an unbounded profile.
 * These limits apply only when a parser opts into this contract.
 * Individual existing parsers retain their own bounds until migrated.
 * Callers may provide tighter or wider limits, including zero.
 */
lxa_limits_t lxa_limits_default(void);
lxa_limits_t lxa_limits_unbounded(void);
lxa_status_t lxa_budget_init(lxa_budget_t *budget,
                             const lxa_limits_t *limits);
/* Reset only after releasing all retained storage and ending its readers. */
void lxa_budget_reset(lxa_budget_t *budget);
/* Start a new owned parse session after its readers have ended. Reset only
 * cumulative INPUT and WORK; retained capacity and other live charges remain
 * attached to their allocations until freed.
 * Never reset a caller-owned budget while other consumers still use it.
 */
void lxa_budget_reset_session(lxa_budget_t *budget);
/* reason may be NULL. On LIMIT it identifies the failed category; otherwise
 * it is LXA_BUDGET_COUNT. INVALID category/arguments return ARGUMENT.
 */
/* check is a preflight: it never changes accounting. A budgeted resize
 * temporarily reserves the MEMORY capacity delta before calling its
 * allocator; failure cancels the reservation, success commits it. Allocator
 * callbacks must not access or mutate that budget while the reservation is
 * in flight. No other callback or concurrent mutation may intervene between
 * a check and charge. All retained allocations, including scratch
 * and buffers, charge MEMORY for their actual capacity. A failed charge
 * leaves all counters unchanged.
 */
lxa_status_t lxa_budget_check(const lxa_budget_t *budget,
                              lxa_budget_kind_t kind, uint64_t amount,
                              lxa_budget_kind_t *reason);
lxa_status_t lxa_budget_charge(lxa_budget_t *budget,
                               lxa_budget_kind_t kind, uint64_t amount,
                               lxa_budget_kind_t *reason);
/* Input is accepted once per byte, regardless of chunk boundaries. The
 * operation atomically checks the input limit and earns work credit:
 * initial_work_credit + accepted_bytes * work_per_input_byte.
 * A failed call changes neither counter. Only call for accepted bytes.
 */
lxa_status_t lxa_budget_accept_input(lxa_budget_t *budget, size_t bytes,
                                     lxa_budget_kind_t *reason);
/* Enforce a single object's size, without charging aggregate capacity. */
lxa_status_t lxa_budget_check_item(const lxa_budget_t *budget,
                                   lxa_budget_kind_t kind, uint64_t size,
                                   lxa_budget_kind_t *reason);
/* Only live stack/depth charges can be released. Input and work are
 * cumulative; nodes, strings, attributes and tokens charge retained storage
 * capacity, so removing a live item does not release their charges. MEMORY
 * is released on backing free or when a failed resize cancels its reservation.
 */
lxa_status_t lxa_budget_release(lxa_budget_t *budget,
                                lxa_budget_kind_t kind, uint64_t amount);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Borrowed binary views: NULL data is valid only with zero length. Neither
 * view owns storage or implies a trailing NUL. The owner must outlive use.
 */
typedef struct {
    const uint8_t *data;
    size_t length;
} lxa_span_t;

typedef struct {
    uint8_t *data;
    size_t length;
} lxa_mut_span_t;

/* A string is a byte span with its encoding interpreted by the caller.
 * In particular, core does not validate UTF-8 or append a terminator.
 */
typedef lxa_span_t lxa_string_t;

/*
 * Incremental callers borrow each input chunk only while feed is active.
 * A parser copies an incomplete suffix into its own lxa_buffer_t before
 * returning from feed; it must not save a pointer into the caller's chunk.
 * The parser owns its cursor, absolute offset, EOF and error state because
 * those transitions depend on its grammar. Reset clears pending bytes and
 * those parser-owned fields together. A complete token can refer directly
 * to the borrowed chunk only while its synchronous consumer is running.
 * This is the shared input boundary; a second owning chunk queue would
 * duplicate the pending state of the encoding stream or tokenizer.
 */

/*
 * The caller owns the buffer object; the buffer owns its backing allocation
 * and a copy of the allocator descriptor. Keep the allocator context alive
 * until destroy. init accepts a complete descriptor and performs no
 * allocation. Call destroy before reinitializing an initialized buffer.
 * destroy releases storage but not the allocator context; clear retains
 * capacity, and truncate rejects lengths greater than the current length.
 */
typedef struct {
    uint8_t *data;
    size_t length;
    size_t capacity;
    lxa_allocator_t allocator;
    lxa_budget_t *budget;
} lxa_buffer_t;

lxa_status_t lxa_buffer_init(lxa_buffer_t *buffer,
                             const lxa_allocator_t *allocator);
/* Bind before any allocation. The budget outlives the buffer and cannot
 * reset while its backing allocation exists. All growth paths charge the
 * actual capacity delta against MEMORY; destroy releases that charge.
 */
lxa_status_t lxa_buffer_bind_budget(lxa_buffer_t *buffer,
                                    lxa_budget_t *budget);
void lxa_buffer_destroy(lxa_buffer_t *buffer);
void lxa_buffer_clear(lxa_buffer_t *buffer);
lxa_status_t lxa_buffer_reserve(lxa_buffer_t *buffer, size_t minimum_capacity);
/* Reserve exactly the requested capacity when growing; never shrink storage.
 * On failure data, capacity, and length remain unchanged.
 */
lxa_status_t lxa_buffer_reserve_exact(lxa_buffer_t *buffer, size_t capacity);
lxa_status_t lxa_buffer_append(lxa_buffer_t *buffer,
                               lxa_span_t bytes);
/* Append a range of this buffer's current logical bytes. */
lxa_status_t lxa_buffer_append_self(lxa_buffer_t *buffer, size_t offset,
                                    size_t length);
lxa_status_t lxa_buffer_truncate(lxa_buffer_t *buffer, size_t length);
/* Ensure data[length] == 0 without increasing length; binary data may have
 * embedded NULs, so the caller must still use length for parsing.
 */
lxa_status_t lxa_buffer_terminate(lxa_buffer_t *buffer);

/*
 * Length is always <= capacity. On failure, length, bytes, and allocation
 * ownership are unchanged. A failed reserve may relocate no storage.
 * A successful reserve or append may relocate storage and invalidates all
 * borrowed views/pointers; clear/truncate only change the logical length.
 * append requires an external non-overlapping span; use append_self for
 * overlapping input. append_self checks offset and length before growth,
 * including overflow. Zero-length appends accept a NULL source. All
 * operations are single-thread confined.
 */
#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* The callback supplies all 16 bytes or returns an error. Its context and
 * quality are caller-owned. NULL uses the platform's OS entropy source.
 */
typedef lxa_status_t (*lxa_entropy_callback_t)(void *context,
                                                uint8_t key[16]);

/* Initialize a new per-document/hash-domain key. Failed initialization
 * clears the key; no clock, address or fixed-key fallback exists.
 */
lxa_status_t lxa_entropy_key(uint8_t key[16],
                              lxa_entropy_callback_t callback, void *context);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* SipHash-2-4 for document-owned dynamic identifier tables. The key is
 * initialized once with lxa_entropy_key and retained for the hash domain.
 * Data is a bounded borrowed span (NULL is valid only for zero length).
 * On invalid arguments *out is zero.
 */
lxa_status_t lxa_siphash24(const uint8_t key[16], const uint8_t *data,
                           size_t length, uint64_t *out);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Fixed-size elements in contiguous owned storage. The element size is set
 * at init, must be nonzero, and cannot change while initialized. Choose
 * sizeof(T) for typed access: that preserves T's array alignment/stride.
 * For other sizes, element pointers are only byte views.
 *
 * The caller owns the vector object; its allocator context must outlive
 * destroy. Elements are copied bytes, not owned pointed-to objects. clear,
 * erase, and destroy do not invoke destructors or release pointed-to data.
 * The bytes field is implementation state, not an alternate mutation API.
 */
typedef struct {
    lxa_buffer_t bytes;
    size_t element_size;
} lxa_vector_t;

lxa_status_t lxa_vector_init(lxa_vector_t *vector,
                             const lxa_allocator_t *allocator,
                             size_t element_size);
/* Bind the underlying buffer before first reserve/push. */
lxa_status_t lxa_vector_bind_budget(lxa_vector_t *vector,
                                    lxa_budget_t *budget);
void lxa_vector_destroy(lxa_vector_t *vector);
void lxa_vector_clear(lxa_vector_t *vector);
size_t lxa_vector_length(const lxa_vector_t *vector);
lxa_status_t lxa_vector_reserve(lxa_vector_t *vector, size_t element_count);
/* value points to one external, non-overlapping element of element_size
 * bytes. For self-insertion, copy it out before calling insert.
 */
lxa_status_t lxa_vector_insert(lxa_vector_t *vector, size_t index,
                               const void *value);
lxa_status_t lxa_vector_push(lxa_vector_t *vector, const void *value);
/* On failure out is set to NULL when non-NULL. The result is borrowed. */
lxa_status_t lxa_vector_at(lxa_vector_t *vector, size_t index, void **out);
/* Erase count elements beginning at index; count=0 accepts index==length. */
lxa_status_t lxa_vector_erase(lxa_vector_t *vector, size_t index,
                              size_t count);

/*
 * Index and byte-count arithmetic are checked before any mutation. An
 * invalid index or allocation failure leaves length, elements, and
 * ownership unchanged. insert/erase invalidate element views at or after
 * the index; successful reserve/push may relocate and invalidate all views.
 * clear/destroy invalidate all views. Concurrent operations on one vector
 * are not supported.
 */
#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct lxa_table lxa_table_t;

/*
 * A table owns copies of its binary keys, including zero-length keys.
 * NULL data with nonzero length is invalid. Values are borrowed opaque
 * pointers; NULL is a valid stored value. Key comparisons are byte-exact;
 * case folding and Unicode normalization belong to the caller.
 * The table copies its allocator descriptor; its context must outlive it.
 */
lxa_status_t lxa_table_create(const lxa_allocator_t *allocator,
                              lxa_table_t **out);
/* budget is borrowed and must outlive the table. NULL uses a private bounded
 * default. A NULL entropy callback uses OS entropy and never falls back to
 * a predictable key. Budget WORK charges each hashed block, compared
 * eight-byte key block (including a partial tail), and probed slot;
 * MEMORY charges table, slot capacity and allocated key bytes. The caller
 * charges any separate document-level string/atom quotas.
 */
lxa_status_t lxa_table_create_with_budget(const lxa_allocator_t *allocator,
                                           lxa_budget_t *budget,
                                           lxa_entropy_callback_t entropy,
                                           void *entropy_context,
                                           lxa_table_t **out);
lxa_budget_kind_t lxa_table_limit_reason(const lxa_table_t *table);
void lxa_table_destroy(lxa_table_t *table);
/*
 * Reset scans the slot capacity, releases each live key, then zeroes the
 * slots. The cost is 2 * capacity + length WORK units. Use clear_cost to
 * preflight a larger reset before invalidating other state. clear_checked
 * charges the full cost before touching the table; on failure it leaves
 * keys, values and capacity unchanged. No concurrent users or budget
 * mutations may intervene between a caller's preflight and the reset.
 * clear is the unconditional cleanup path (no WORK charge), for teardown
 * even after a work limit has been reached.
 */
lxa_status_t lxa_table_clear_cost(const lxa_table_t *table, size_t *work);
lxa_status_t lxa_table_clear_checked(lxa_table_t *table);
void lxa_table_clear(lxa_table_t *table);
size_t lxa_table_length(const lxa_table_t *table);

/*
 * Output pointers, if supplied, are initialized to false/NULL on failure.
 * find/delete distinguish missing keys from present keys with NULL values.
 * set replaces a present value without taking ownership of it, returning
 * its old value through previous when requested. On allocation or size
 * failure the existing keys and values stay unchanged. Failed insertions may
 * retain newly reserved slot capacity; WORK spent during attempted operations
 * is never refunded. find is logically const but charges the table's budget.
 */
lxa_status_t lxa_table_find(const lxa_table_t *table, lxa_span_t key,
                            bool *found, void **value);
lxa_status_t lxa_table_set(lxa_table_t *table, lxa_span_t key, void *value,
                           bool *replaced, void **previous);
lxa_status_t lxa_table_delete(lxa_table_t *table, lxa_span_t key,
                              bool *found, void **previous);

/*
 * The callback receives borrowed key bytes and value pointers. It may
 * stop iteration by returning a non-OK status. Neither the callback nor
 * another thread may mutate the table during iteration. No iteration
 * order is guaranteed; any mutation invalidates borrowed key views.
 */
typedef lxa_status_t (*lxa_table_visit_f)(void *context, lxa_span_t key,
                                          void *value);
lxa_status_t lxa_table_each(const lxa_table_t *table,
                            lxa_table_visit_f visit, void *context);

#ifdef __cplusplus
}
#endif

#endif /* LIBLEXA_BASE_H */
