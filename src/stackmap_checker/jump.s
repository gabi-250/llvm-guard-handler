.global restore_and_jmp

# Restores the register state stored in array r and jumps to addr
restore_and_jmp:
    mov    %rbp, %rsp
    pop    %rbp
    pop    %rax # pop the return address of __guard_failure
    jmp    *addr
