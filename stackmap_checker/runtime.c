#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <execinfo.h>
#include <llvm-c/ExecutionEngine.h>
#include <llvm-c/Object.h>
#include <llvm-c/Core.h>
#include <llvm-c/BitReader.h>
#include <llvm-c/BitWriter.h>
#include "stmap.h"

static uint8_t *stack_map_addr = NULL;

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
   volatile uint64_t r[16];
   asm("mov %%rax,%0\n"
       "mov %%rcx,%1\n"
       "mov %%rdx,%2\n"
       "mov %%rbx,%3\n"
       "mov %%rsp,%4\n"
       "mov %%rbp,%5\n"
       "mov %%rsi,%6\n"
       "mov %%rdi,%7\n" : "=r"(r[0]), "=r"(r[1]), "=r"(r[2]), "=r"(r[3]),
                          "=r"(r[4]), "=r"(r[5]), "=r"(r[6]), "=r"(r[7]) : : );
   asm("mov %%r8,%0\n"
       "mov %%r9,%1\n"
       "mov %%r10,%2\n"
       "mov %%r11,%3\n"
       "mov %%r12,%4\n"
       "mov %%r13,%5\n"
       "mov %%r14,%6\n"
       "mov %%r15,%7\n" : "=r"(r[8]), "=r"(r[9]), "=r"(r[10]), "=r"(r[11]),
                          "=r"(r[12]), "=r"(r[13]), "=r"(r[14]), "=r"(r[15]) : : );
   stack_map_t *stack_map = create_stack_map(stack_map_addr);
   stack_map_record_t rec = stack_map->sm_records[sm_id];
   void *bp = __builtin_frame_address(1);
   printf("Locations:\n");
   for (size_t j = 0; j < rec.num_locations; ++j) {
      location_type type = rec.locations[j].kind - 1;
      if (type == REGISTER) {
         int reg_num = rec.locations[j].dwarf_reg_num;
         printf("[REGISTER %d] Loc %zu, value is %lu\n", reg_num, j, r[reg_num]);
      } else if (type == DIRECT) {
         int *addr = bp + stack_map->sm_records[sm_id].locations[j].offset;
         printf("[DIRECT] Loc %zu, value is %d @ %p\n", j,
               *addr, (void *)addr);
      }
   }
   printf("Liveouts:\n");
   for (size_t j = 0; j < rec.num_liveouts; ++j) {
      liveout_t liveout = rec.liveouts[j];
      int reg_num = liveout.dwarf_reg_num;
      printf("[REGISTER %d] Liveout %zu, value is %lu\n", reg_num, j,
             *(int *)r[reg_num]);
   }
   free_stack_map(stack_map);
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
         printf("\t\tOffset %d\n", loc->offset);
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
   LLVMDisposeExecutionEngine(ee);
   LLVMDisposeMessage(error);
   return 0;
}
