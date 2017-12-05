#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <llvm-c/ExecutionEngine.h>
#include <llvm-c/Object.h>
#include <llvm-c/Core.h>
#include <llvm-c/BitReader.h>
#include <llvm-c/BitWriter.h>
#include "stmap.h"

size_t calculate_size(size_t size) {
    size_t page_size = getpagesize();
    return size < page_size ? page_size : ceil(size / page_size) * page_size;
}

uint8_t* code_section_cb (void *opaque, uintptr_t size,
      unsigned align, unsigned section_id, const char *section_name) {
   uint8_t *start = (uint8_t *)mmap(NULL,  calculate_size(size),
                                     PROT_WRITE | PROT_READ | PROT_EXEC,
                                     MAP_ANON | MAP_PRIVATE, -1, 0);
   if (start == (uint8_t*)-1) {
      //error
      return NULL;
   }

   if (!strcmp(section_name, ".llvm_stackmaps")) {
      stack_map_t **ptr = (stack_map_t **) opaque;
      *ptr = (stack_map_t *)start;
      printf("%s at %p\n", section_name, (void *)start);
   }

   return start;
}

uint8_t* data_section_cb(void *opaque, uintptr_t size, unsigned align,
      unsigned section_id, const char *section_name, LLVMBool read_only) {
   return code_section_cb(opaque, size, align, section_id, section_name);
}

void destroy_cb(void *opaque) {

}

LLVMBool finalize_cb(void *opaque, char **error) {
   return 0;
}

void __guard_failure(uint64_t sm_id) {
   // XXX
   printf("guard failure @ %lu!\n", sm_id);
}

LLVMModuleRef create_module(char *filename) {
   LLVMMemoryBufferRef buf;
   char *error = NULL;
   if (LLVMCreateMemoryBufferWithContentsOfFile(filename, &buf, &error)) {
      printf("%s\n", error);
      return NULL;
   }
   LLVMDisposeMessage(error);
   LLVMModuleRef mod;
   if (LLVMParseBitcode(buf, &mod, &error)) {
      printf("%s\n", error);
      return NULL;
   }
   LLVMDisposeMessage(error);
   LLVMDisposeMemoryBuffer(buf);
   return mod;
}

void inspect_stackmap(stack_map_t *stack_map) {
   printf("Version: %u\n", stack_map->version);
   printf("Num func: %u\n", stack_map->num_func);
   printf("Num rec: %u\n", stack_map->num_rec);
   printf("Num const: %u\n", stack_map->num_const);
   printf("Records:\n");
   for (size_t i = 0; i < stack_map->num_func; ++i) {
      stack_size_record_t *rec = stack_map->stack_size_records + i;
      printf("Fun addr: %p\n", (void *)rec->fun_addr);
      printf("Stack size: %lu\n", rec->stack_size);
      printf("Record count: %lu\n", rec->record_count);
   }

   for (size_t i = 0; i < stack_map->num_rec; ++i) {
      printf("* Record num: %zu\n", i);
      stack_map_record_t *rec = stack_map->sm_records + i;
      printf("\tPatchpoint id: %lu\n", rec->patchpoint_id);
      printf("\tInstruction offset: %u\n", rec->instruction_offset);
      printf("\tNum locations: %u\n", rec->num_locations);
      for (size_t j = 0; j < rec->num_locations; ++j) {
         printf("\t\tLocation %zu\n", j);
         location_t *loc = rec->locations + j;
         printf("\t\tKind %u\n", loc->kind);
         printf("\t\tSize %u\n", loc->size);
         printf("\t\tOffset %u\n", loc->offset);
         printf("\t\tReg num: %u\n", loc->dwarf_reg_num);
      }
      for (int j = 0; j < rec->num_liveouts; ++j) {
         printf("\t\tLiveout: %d\n", j);
         liveout_t *liveout = rec->liveouts + j;
         printf("\t\tReg num: %u\n", liveout->dwarf_reg_num);
         printf("\t\tSize: %u\n", liveout->size);
      }
   }
}

int main(int argc, char **argv) {
   char *error = NULL;
   if (argc < 2) {
      printf("Please provide an input file\n");
      return 1;
   }
   LLVMModuleRef mod = create_module(argv[1]);

   LLVMLinkInMCJIT();
   LLVMInitializeNativeTarget();
   LLVMInitializeNativeAsmPrinter();
   uint8_t *stack_map_addr = NULL;
   LLVMMCJITMemoryManagerRef mm_ref = LLVMCreateSimpleMCJITMemoryManager(
            &stack_map_addr,
            code_section_cb,
            data_section_cb,
            finalize_cb,
            destroy_cb
         );
   LLVMExecutionEngineRef ee;
   struct LLVMMCJITCompilerOptions options;
   LLVMInitializeMCJITCompilerOptions(&options, sizeof(options));
   options.MCJMM = mm_ref;
   LLVMCreateMCJITCompilerForModule(&ee, mod, &options, sizeof(options),
                                    &error);
   LLVMValueRef gf_handler = LLVMGetNamedFunction(mod, "__guard_failure");
   if (!gf_handler) {
      printf("Could not find \'__guard_failure\'\n");
      return 1;
   }
   LLVMAddGlobalMapping(ee, gf_handler, (void *)__guard_failure);
   LLVMValueRef fun = LLVMGetNamedFunction(mod, "main");
   printf("Executing main:\n\n");
   int ret_val = LLVMRunFunctionAsMain(ee, fun, argc,
                                       (const char * const *)argv, NULL);
   if (ret_val) {
      printf("main failed to exit successfully\n");
      return ret_val;
   }
   printf("\nStack maps:\n");
   stack_map_t *stack_map = create_stack_map(stack_map_addr);
   inspect_stackmap(stack_map);
   LLVMDisposeExecutionEngine(ee);
   LLVMDisposeMessage(error);
   free_stack_map(stack_map);
   return 0;
}
