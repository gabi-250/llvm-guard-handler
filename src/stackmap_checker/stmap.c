#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <err.h>
#include "stmap.h"

stack_map_t* stmap_create(uint8_t *start_addr)
{
    stack_map_t *sm = (stack_map_t *)malloc(sizeof(stack_map_t));
    char *addr = (char *)start_addr;
    size_t header_size =
            sizeof(uint8_t) * 2 + sizeof(uint16_t) + 3 * sizeof(uint32_t);
    memcpy(sm, (void *)addr, header_size);
    addr += header_size;
    sm->stk_size_records = (stack_size_record_t *)calloc(
            sm->num_func,
            sizeof(stack_size_record_t));
    memcpy(sm->stk_size_records, addr,
           sm->num_func * sizeof(stack_size_record_t));
    addr += sm->num_func * sizeof(stack_size_record_t);
    sm->constants = (uint64_t *)calloc(sm->num_const, sizeof(uint64_t));
    memcpy(sm->constants, addr, sm->num_const * sizeof(uint64_t));
    addr += sizeof(uint64_t) * sm->num_const;
    sm->stk_map_records = (stack_map_record_t *)calloc(
            sm->num_rec, sizeof(stack_map_record_t));
    size_t rec_header_size = sizeof(uint64_t) + sizeof(uint32_t)
        + 2 * sizeof(uint16_t);
    for (size_t i = 0; i < sm->num_rec; ++i) {
        stack_map_record_t *rec = sm->stk_map_records + i;
        // copy the first 4 fields
        memcpy(rec, addr, rec_header_size);
        addr += rec_header_size;
        rec->locations =
            (location_t *)calloc(rec->num_locations, sizeof(location_t));
        memcpy(rec->locations, addr, rec->num_locations * sizeof(location_t));
        addr += rec->num_locations * sizeof(location_t);
        // padding to align on 8-byte boundary
        printf("Num loc %lu sizeof loc %d\n", rec->num_locations,
                sizeof(location_t));
        if ((rec->num_locations * sizeof(location_t)) % 8) {
            addr += sizeof(uint32_t);
        }
        addr += sizeof(uint16_t); // padding
        memcpy(&rec->num_liveouts, addr, sizeof(uint16_t));
        addr += sizeof(uint16_t);
        rec->liveouts = (liveout_t *)calloc(rec->num_liveouts,
                                            sizeof(liveout_t));
        memcpy(rec->liveouts, addr, sizeof(liveout_t) * rec->num_liveouts);
        addr += sizeof(liveout_t) * rec->num_liveouts;
        // padding to align on 8-byte boundary
        if ((2 * sizeof(uint16_t) + sizeof(liveout_t) * rec->num_liveouts) % 8) {
            addr += sizeof(uint32_t);
        }
    }
    return sm;
}

int stmap_get_map_record(stack_map_t *sm, uint64_t patchpoint_id)
{
    for (size_t i = 0; i < sm->num_rec; ++i) {
        if (sm->stk_map_records[i].patchpoint_id == patchpoint_id) {
            return i;
        }
    }
    return -1;
}

uint64_t stmap_get_location_value(stack_map_t *sm, location_t loc,
        uint64_t *regs, void *frame_addr)
{
    uint64_t *dest_addr;
    uint64_t addr, value;
    switch (loc.kind) {
        case REGISTER:
            return regs[loc.dwarf_reg_num];
        case DIRECT:
            dest_addr = (uint64_t *)malloc(sizeof(uint64_t));
            addr = (uint64_t)frame_addr + loc.offset;
            memcpy(dest_addr, (void *)addr, loc.size);
            value = *dest_addr;
            free(dest_addr);
            return value;
        case INDIRECT:
            dest_addr = (uint64_t *)malloc(sizeof(uint64_t));
            addr = (uint64_t)frame_addr + loc.offset;
            memcpy(dest_addr, (void *)*(uint64_t *)addr, loc.size);
            value = *dest_addr;
            free(dest_addr);
            return value;
        case CONST_INDEX:
            return sm->constants[loc.offset];
        case CONSTANT:
            return loc.offset;
        default:
            errx(1, "Unknown location - %u.\nExiting.\n", loc.kind);
    }
    return 0;
}

int stmap_get_size_record(stack_map_t *sm, uint64_t sm_rec_idx)
{
    size_t record_count = 1;
    // Each function contains a number of stackmap calls. This works out which
    // function the specified stack map record is associated with. This
    // approach works because LLVM preserves the order of locations, records
    // and functions.
    for (size_t i = 0; i < sm->num_func; ++i) {
        stack_size_record_t rec = sm->stk_size_records[i];
        if (sm_rec_idx + 1 >= record_count &&
                sm_rec_idx + 1 < record_count + rec.record_count) {
            return i;
        } else {
            record_count += rec.record_count;
        }
    }
    return -1;
}

int stmap_get_last_record(stack_map_t *sm, int target_size_rec_idx)
{
    int map_idx = -1;
    uint64_t max_addr = 0;
    for (size_t i = 0; i < sm->num_rec; ++i) {
        stack_map_record_t rec = sm->stk_map_records[i];
        int size_rec_idx = stmap_get_size_record(sm, i);
        if (size_rec_idx != target_size_rec_idx) {
            continue;
        }
        if (size_rec_idx == -1) {
            return -1;
        }
        uint64_t addr =
            sm->stk_size_records[size_rec_idx].fun_addr + rec.instr_offset;
        if (addr > max_addr) {
            max_addr = addr;
            map_idx = i;
        }
    }
    return map_idx;
}

int stmap_first_rec_after_addr(stack_map_t *sm, uint64_t addr)
{
    for (size_t i = 0; i < sm->num_rec; ++i) {
        stack_map_record_t rec = sm->stk_map_records[i];
        int size_rec_idx = stmap_get_size_record(sm, i);
        if (size_rec_idx == -1) {
            errx(1, "No stack map after call!. Exiting.\n");
        }
        stack_size_record_t size_rec = sm->stk_size_records[size_rec_idx];
        int last_rec_idx = stmap_get_last_record(sm, size_rec_idx);
        uint64_t last_addr = size_rec.fun_addr +
            sm->stk_map_records[last_rec_idx].instr_offset;
        if (addr > last_addr) {
            continue;
        }
        if (size_rec.fun_addr + rec.instr_offset < addr) {
            return i;
        }
    }
    return -1;
}

void stmap_print_map_record(stack_map_t *sm, uint32_t rec_idx,
        uint64_t *regs, void *frame_addr) {
    stack_map_record_t rec = sm->stk_map_records[rec_idx];
    for (size_t i = 0; i < rec.num_locations; ++i) {
        location_type type = rec.locations[i].kind;
        uint64_t loc_value = stmap_get_location_value(sm, rec.locations[i],
                                                      regs, frame_addr);
        if (type == REGISTER) {
            uint16_t reg_num = rec.locations[i].dwarf_reg_num;
            printf("\t[REGISTER %hu] Loc %zu, value is %lu\n", reg_num, i,
                   loc_value);
        } else if (type == DIRECT) {
            uint64_t addr = (uint64_t)frame_addr + rec.locations[i].offset;
            printf("\t[DIRECT] Loc value is %lu @ %p\n", loc_value,
                   (void *)addr);
        } else if (type == INDIRECT) {
            uint64_t addr = (uint64_t)frame_addr + rec.locations[i].offset;
            printf("\t[INDIRECT] Loc %zu @ [%p]\n", i, (void *)addr);
        } else if (type == CONSTANT) {
            printf("\t[CONSTANT] Loc %zu, value is %lu\n", i, loc_value);
        } else if (type == CONST_INDEX) {
            printf("\t[CONSTANT INDEX] Loc %zu, value is %lu\n", i,
                   loc_value);
        }
    }
}

void stmap_print_liveouts(stack_map_t *stack_map, uint64_t *regs)
{
    for (size_t i = 0; i < stack_map->num_rec; ++i) {
        stack_map_record_t rec = stack_map->stk_map_records[i];
        if (rec.num_liveouts) {
            printf("Record %lu:\n", rec.patchpoint_id);
        }
        for (size_t j = 0; j < rec.num_liveouts; ++j) {
            liveout_t liveout = rec.liveouts[j];
            uint16_t reg_num = liveout.dwarf_reg_num;
            printf("\t[REGISTER %hu] Liveout %zu, value is %p\n", reg_num, j,
                   (void *)regs[reg_num]);
        }
    }
}

void stmap_print_size_records(stack_map_t *sm)
{
    for (size_t i = 0; i < sm->num_func; ++i) {
        stack_size_record_t rec = sm->stk_size_records[i];
        printf("Stack size record %lu: addr: %p, stack size %lu, num recs %lu\n",
               i, (void *)rec.fun_addr, rec.stack_size, rec.record_count);
    }
}

void stmap_free(stack_map_t *sm)
{
    free(sm->stk_size_records);
    free(sm->constants);
    for (size_t i = 0; i < sm->num_rec; ++i) {
        stack_map_record_t *rec = sm->stk_map_records + i;
        free(rec->locations);
        free(rec->liveouts);
    }
    free(sm->stk_map_records);
    free(sm);
}
