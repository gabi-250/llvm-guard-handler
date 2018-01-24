#ifndef STMAP_H
#define STMAP_H

#include <stdint.h>

typedef enum {
    REGISTER    = 0x1,
    DIRECT      = 0x2,
    INDIRECT    = 0x3,
    CONSTANT    = 0x4,
    CONST_INDEX = 0x5
} location_type;

typedef struct Location {
    uint8_t  kind; // Register | Direct | Indirect | Constant | ConstantIndex
    uint8_t  reserved;
    uint16_t size;
    uint16_t dwarf_reg_num;
    uint16_t reserved2;
    int32_t  offset;
} location_t;

typedef struct LiveOut {
    uint16_t dwarf_reg_num;
    uint8_t  reserved;
    uint8_t  size;
} liveout_t;

typedef struct StackMapRecord {
    uint64_t   patchpoint_id;
    uint32_t   instr_offset;
    uint16_t   reserved;
    uint16_t   num_locations;
    location_t *locations;
    uint16_t   num_liveouts;
    liveout_t  *liveouts;
} stack_map_record_t;

typedef struct StackSizeRecord {
    uint64_t fun_addr;
    uint64_t stack_size;
    uint64_t record_count;
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

stack_map_t* create_stack_map(uint8_t*);
int get_record(stack_map_t *stack_map, uint64_t patchpoint_id);
stack_size_record_t* get_stack_size_record(stack_map_t *sm, uint64_t sm_rec_idx);
void free_stack_map(stack_map_t *);
void print_stack_size_records(stack_map_t *);
void print_locations(stack_map_t *sm, void *frame_addr, uint64_t *regs);
void print_rec(stack_map_t *sm, stack_map_record_t rec, void *frame_addr,
               uint64_t *regs);
void print_liveouts(stack_map_t *rec, uint64_t *regs);
void print_constants(stack_map_t *rec);

#endif // STMAP_H
