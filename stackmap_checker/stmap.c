#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "stmap.h"

stack_map_t* create_stack_map(uint8_t *start_addr)
{
    stack_map_t *sm = (stack_map_t *)malloc(sizeof(stack_map_t));
    char *addr = (char *)start_addr;
    size_t header_size = sizeof(uint8_t) * 2 + sizeof(uint16_t) + 3 * sizeof(uint32_t);
    memcpy(sm, (void *)addr, header_size);
    addr += header_size;
    sm->stk_size_records =
        (stack_size_record_t *)malloc(sizeof(stack_size_record_t) * sm->num_func);
    for (size_t i = 0; i < sm->num_func; ++i) {
        stack_size_record_t *rec = sm->stk_size_records + i;
        memcpy(rec, (void *)addr, sizeof(stack_size_record_t));
        addr += sizeof(stack_size_record_t);
    }
    sm->constants = (uint64_t *)malloc(sizeof(uint64_t) * sm->num_const);
    for (size_t i = 0; i < sm->num_const; ++i) {
        uint64_t *constant = sm->constants + i;
        *constant = *(uint64_t *)addr;
        addr += sizeof(uint64_t);
    }
    sm->stk_map_records =
        (stack_map_record_t *)malloc(sizeof(stack_map_record_t) * sm->num_rec);
    size_t record_size = sizeof(uint64_t) + sizeof(uint32_t) + 2 * sizeof(uint16_t);
    for (size_t i = 0; i < sm->num_rec; ++i) {
        stack_map_record_t *rec = sm->stk_map_records + i;
        // copy the first 4 fields
        memcpy(rec, addr, record_size);
        addr += record_size;
        rec->locations =
            (location_t *)malloc(sizeof(location_t) * rec->num_locations);
        size_t loc_size =
            2 * sizeof(uint8_t) + 3 * sizeof(uint16_t) + sizeof(int32_t);
        for (size_t j = 0; j < rec->num_locations; ++j) {
            location_t *loc = rec->locations + j;
            memcpy(loc, addr, loc_size);
            addr += loc_size;
        }
        if ((rec->num_locations * sizeof(location_t)) % 8 != 0) {
            addr += 4; // padding
        }
        addr += 2; // padding
        rec->num_liveouts = *(uint16_t *)addr;
        addr += 2;
        rec->liveouts = (liveout_t *)malloc(sizeof(liveout_t) * rec->num_liveouts);
        size_t liveout_size = sizeof(uint16_t) + 2 * sizeof(uint8_t);
        for (int j = 0; j < rec->num_liveouts; ++j) {
            liveout_t *liveout = rec->liveouts + j;
            memcpy(liveout, addr, liveout_size);
            addr += liveout_size;
        }
        if ((rec->num_liveouts * sizeof(liveout_t) + 4) % 8 != 0) {
            addr += 4; // padding
        }
    }
    return sm;
}

stack_map_record_t* get_record(stack_map_t *stack_map, uint64_t patchpoint_id) {
    for (size_t i = 0; i < stack_map->num_rec; ++i) {
        if (stack_map->stk_map_records[i].patchpoint_id == patchpoint_id) {
            return &stack_map->stk_map_records[i];
        }
    }
    return NULL;
}

void print_rec(stack_map_t *sm, stack_map_record_t rec,
        void *frame_addr, uint64_t *regs) {
    for (size_t j = 0; j < rec.num_locations; ++j) {
        location_type type = rec.locations[j].kind;
        if (type == REGISTER) {
            uint16_t reg_num = rec.locations[j].dwarf_reg_num;
            printf("\t[REGISTER %hu] Loc %zu, value is %lu\n", reg_num, j,
                   regs[reg_num]);
        } else if (type == DIRECT) {
            uint64_t addr = (uint64_t)frame_addr + rec.locations[j].offset;
            printf("\t[DIRECT] Loc value is %d @ %p\n",
                    *(int *)addr, (void *)addr);
        } else if (type == INDIRECT) {
            // XXX
            uint64_t addr = regs[rec.locations[j].dwarf_reg_num] +
                rec.locations[j].offset;
            printf("\t[INDIRECT] Loc %zu, value is %lu @ %p\n", j,
                    *(uint64_t *)addr, (void *)addr);
        } else if (type == CONSTANT) {
            printf("\t[CONSTANT] Loc %zu, value is %d\n", j,
                    rec.locations[j].offset);
        } else if (type == CONST_INDEX) {
            int32_t offset = rec.locations[j].offset;
            printf("\t[CONSTANT INDEX] Loc %zu, value is %lu\n", j,
                    sm->constants[offset]);
        } else {
            printf("Unknown location type %d\n", type);
            exit(1);
        }
    }
}
void print_locations(stack_map_t *stack_map, void *frame_addr, uint64_t *regs)
{
    for (size_t i = 0; i < stack_map->num_rec; ++i) {
        stack_map_record_t rec = stack_map->stk_map_records[i];
        if (rec.num_locations) {
            printf("Record %zu:\n", i);
        }
        for (size_t j = 0; j < rec.num_locations; ++j) {
            location_type type = rec.locations[j].kind;
            if (type == REGISTER) {
                uint16_t reg_num = rec.locations[j].dwarf_reg_num;
                printf("\t[REGISTER %hu] Loc %zu, value is %lu\n", reg_num, j,
                       regs[reg_num]);
            } else if (type == DIRECT) {
                uint64_t addr = (uint64_t)frame_addr + rec.locations[j].offset;
                printf("\t[DIRECT] Loc value is %p @ %d\n",
                        (void *)addr, *(int *)addr);
            } else if (type == INDIRECT) {
                // XXX
                uint64_t addr = regs[rec.locations[j].dwarf_reg_num] +
                    rec.locations[j].offset;
                printf("\t[INDIRECT] Loc %zu, value is %lu @ %p\n", j,
                        *(uint64_t *)addr, (void *)addr);
            } else if (type == CONSTANT) {
                printf("\t[CONSTANT] Loc %zu, value is %d\n", j,
                        rec.locations[j].offset);
            } else if (type == CONST_INDEX) {
                int32_t offset = rec.locations[j].offset;
                printf("\t[CONSTANT INDEX] Loc %zu, value is %lu\n", j,
                        stack_map->constants[offset]);
            } else {
                printf("Unknown location type %d\n", type);
                exit(1);
            }
        }
    }
}

void print_constants(stack_map_t *stack_map)
{
    for (size_t i = 0; i < stack_map->num_const; ++i) {
        uint64_t constant = stack_map->constants[i];
        printf("[CONSTANT] Constant %zu, value is %lu\n", i,
                constant);
    }
}

void print_liveouts(stack_map_t *stack_map, uint64_t *regs)
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

void free_stack_map(stack_map_t *sm)
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
