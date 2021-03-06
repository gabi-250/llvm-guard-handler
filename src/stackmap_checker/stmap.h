#ifndef STMAP_H
#define STMAP_H

#define UNW_LOCAL_ONLY
#include <libunwind.h>
#include <stdint.h>

#define PATCHPOINT_CALL_SIZE 13

/**
 * This module provides an interface to the stack map section.
 *
 * More details about how the stackmap section should be parsed can be found
 * here: http://llvm.org/docs/StackMaps.html
 */

// The type of a location recorded in the stack map.
typedef enum {
    REGISTER    = 0x1,
    DIRECT      = 0x2,
    INDIRECT    = 0x3,
    CONSTANT    = 0x4,
    CONST_INDEX = 0x5
} location_type;

// A live location recorded in the stackmap.
typedef struct Location {
    uint8_t  kind;   // Register | Direct | Indirect | Constant | ConstantIndex
    uint8_t  reserved;
    uint16_t size;
    uint16_t dwarf_reg_num;
    uint16_t reserved2;
    int32_t  offset;
} location_t;

// A register which is 'live out', and, therefore, should be restored.
typedef struct LiveOut {
    uint16_t dwarf_reg_num;
    uint8_t  reserved;
    uint8_t  size;
} liveout_t;

// A record associated with a stackmap/patchpoint call.
typedef struct StackMapRecord {
    uint64_t   patchpoint_id;
    uint32_t   instr_offset;
    uint16_t   reserved;
    uint16_t   num_locations;
    location_t *locations;
    uint16_t   num_liveouts;
    liveout_t  *liveouts;
    uint64_t index;
} stack_map_record_t;

// A record which describes a function which contains a `stackmap` call.
typedef struct StackSizeRecord {
    uint64_t fun_addr;
    uint64_t stack_size;
    uint64_t record_count;
    uint64_t index;
} stack_size_record_t;

typedef struct StackMap {
    // Header
    uint8_t  version;
    uint8_t  reserved;
    uint16_t reserved2;

    uint32_t num_func;
    uint32_t num_const;
    uint32_t num_rec;

    stack_size_record_t *stk_size_records;
    uint64_t *constants;
    stack_map_record_t *stk_map_records;
} stack_map_t;

// Identifies an address using a stack map record and a stack size record. This
// is the address of a stackmap/patchpoint call.
typedef struct StackMapPosition {
    uint32_t stk_size_record_index;
    uint32_t stk_map_record_index;
} stack_map_pos_t;

/*
 * Populate a StackMap with the information at the given address.
 *
 * The address needs to be the address of the .llvm_stackmaps section.
 */
stack_map_t* stmap_create(uint8_t *start_addr);

/*
 * Free the StackMap.
 */
void stmap_free(stack_map_t *sm);

/*
 * Return a stack map record which corresponds to the specified patchpoint.
 *
 * Important: as functions may be inlined, there may be multiple records with
 * the same ID. This does not happen for '__unopt_' functions, as all
 * optimizations are disabled for them.
 */
stack_map_record_t* stmap_get_map_record(stack_map_t *sm, uint64_t patchpoint_id);

/*
 * Return the stack map record which corresponds to the patchpoint call with the
 * specified ID. The returned record will correspond to a patchpoint call
 * located at an address greater than or equal to `addr`, or NULL if there
 * is no such record.
 */
stack_map_record_t* stmap_get_map_record_after_addr(stack_map_t *sm,
                                                    uint64_t patchpoint_id,
                                                    uint64_t addr);

/*
 * Return the stack map record which corresponds to the patchpoint call with the
 * specified ID. The returned record will correspond to a patchpoint call in
 * the function which starts at `fun_addr`, or NULL if there is no such record.
 */
stack_map_record_t* stmap_get_map_record_in_func(stack_map_t *sm,
                                                 uint64_t patchpoint_id,
                                                 uint64_t fun_addr);

/*
 * Return the stack size record associated with the specified stack map record.
 *
 * Each function which contains a call to llvm.experimental.stackmaps or to
 * llvm.experimental.patchpoint generates a stack size record in the stack map.
 * This is used to associate each stackmap/patchpoint call with the function it
 * belongs to.
 */
stack_size_record_t* stmap_get_size_record(stack_map_t *sm, uint64_t sm_rec_idx);

/*
 * Return the stack size record which corresponds to the function that starts at
 * the specified address.
 */
stack_size_record_t* stmap_get_size_record_in_func(stack_map_t *sm,
                                                   uint64_t addr);

/*
 * Compute the value of the specified location.
 */
void* stmap_get_location_value(stack_map_t *sm, location_t loc, uint64_t *regs,
                               void *frame_addr, uint64_t loc_size);

/*
 * Return the stack map/size record pair which describes the return address in an
 * unoptimized function which corresponds to `return_addr`.
 *
 * `return_addr` is expected to be the return address of an optimized function.
 */
stack_map_pos_t* stmap_get_unopt_return_addr(stack_map_t *sm, uint64_t return_addr);

/*
 * Return the first stack map record located at an address greater than `addr`.
 */
stack_map_record_t* stmap_first_rec_after_addr(stack_map_t *sm, uint64_t addr);

/*
 * Exit if the specified register is not an x86-64 general purpose register.
 */
void assert_valid_reg_num(unw_regnum_t reg);

 /*
 * Return the last stack map record associated with the specified stack size
 * record.
 */
stack_map_record_t* stmap_get_last_record(stack_map_t *sm,
                                          stack_size_record_t target_size_rec);

#endif // STMAP_H
