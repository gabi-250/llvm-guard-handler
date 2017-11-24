#include <stdlib.h>
#include "stmap.h"

stack_map_t* create_stack_map(uint8_t *start_addr) {
   stack_map_t *sm = (stack_map_t *)malloc(sizeof(stack_map_t));
   char *addr = (char *)start_addr;
   // XXX use memcpy
   sm->version = *(uint8_t *)addr;
   addr += 1;
   // skip the reserved bits
   sm->reserved = *(uint8_t *)addr;
   addr += 1;
   sm->reserved2 = *(uint16_t *)addr;
   addr += 2;
   sm->num_func = *(uint32_t *)addr;
   addr += 4;
   sm->num_const= *(uint32_t *)addr;
   addr += 4;
   sm->num_rec = *(uint32_t *)addr;
   addr += 4;
   sm->stack_size_records =
      (stack_size_record_t *)malloc(sizeof(stack_size_record_t) * sm->num_func);
   for (size_t i = 0; i < sm->num_func; ++i) {
      stack_size_record_t *rec = sm->stack_size_records + i;
      rec->fun_addr = *(uint64_t *)addr;
      addr += 8;
      rec->stack_size = *(uint64_t *)addr;
      addr += 8;
      rec->record_count = *(uint64_t *)addr;
      addr += 8;
   }
   sm->constants = (uint64_t *)malloc(sizeof(uint64_t) * sm->num_const);
   for (size_t i = 0; i < sm->num_const; ++i) {
      uint64_t *constant = sm->constants + i;
      *constant = *(uint64_t *)addr;
      addr += 8;
   }

   sm->sm_records =
      (stack_map_record_t *)malloc(sizeof(stack_map_record_t) * sm->num_rec);
   for (size_t i = 0; i < sm->num_rec; ++i) {
      stack_map_record_t *rec = sm->sm_records + i;
      rec->patchpoint_id = *(uint64_t *)addr;
      addr += 8;
      rec->instruction_offset = *(uint32_t *)addr;
      addr += 4;
      rec->reserved = *(uint16_t *)addr;
      addr += 2;
      rec->num_locations = *(uint16_t *)addr;
      addr += 2;
      rec->locations =
         (location_t *)malloc(sizeof(location_t) * rec->num_locations);
      for (size_t j = 0; j < rec->num_locations; ++j) {
         location_t *loc = rec->locations + j;
         loc->kind = *(uint8_t *)addr;
         addr += 1;
         loc->reserved = *(uint8_t *)addr;
         addr += 1;
         loc->size = *(uint16_t *)addr;
         addr += 2;
         loc->dward_reg_num = *(uint16_t *)addr;
         addr += 2;
         loc->reserved2 = *(uint16_t *)addr;
         addr += 2;
         loc->offset = *(uint32_t *)addr;
         addr += 4;
      }
      if ((rec->num_locations * sizeof(location_t)) % 8 != 0) {
         addr += 4; // padding
      }
      addr += 2; // padding
      rec->num_liveouts = *(uint16_t *)addr;
      addr += 2;
      rec->liveouts = (liveout_t *)malloc(sizeof(liveout_t) * rec->num_liveouts);
      for (int j = 0; j < rec->num_liveouts; ++j) {
         liveout_t *liveout = rec->liveouts + j;
         liveout->dwarf_reg_num = *(uint16_t *)addr;
         addr += 2;
         liveout->reserved = *(uint8_t *)addr;
         addr += 1;
         liveout->size = *(uint8_t *)addr;
         addr += 1;
      }
      if ((rec->num_liveouts * sizeof(liveout_t) + 4) % 8 != 0) {
         addr += 4; // padding
      }
   }
   return sm;
}

void free_stack_map(stack_map_t *sm) {
   // XXX
   //
   free(sm->stack_size_records);
   free(sm->constants);
   for (size_t i = 0; i < sm->num_rec; ++i) {
      stack_map_record_t *rec = sm->sm_records + i;
      free(rec->locations);
   }
   free(sm->sm_records);
   free(sm);
}
