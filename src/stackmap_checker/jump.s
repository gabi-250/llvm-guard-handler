.global jmp_to_addr

# Jumps to addr, cleaning up after __guard_failure
jmp_to_addr:
    mov    %rbp, %rsp
    pop    %rbp
    pop    %rax # pop the return address of __guard_failure
    jmp    *addr
