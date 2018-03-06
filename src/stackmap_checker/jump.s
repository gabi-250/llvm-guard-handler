.global jmp_to_addr
.global restore_inlined

# Jumps to addr, cleaning up after __guard_failure
jmp_to_addr:
    mov    r,      %rax
    mov    r+0x8,  %rcx
    mov    r+0x10, %rdx
    mov    r+0x18, %rbx
    mov    r+0x30, %rsi
    mov    r+0x38, %rdi
    mov    r+0x40, %r8
    mov    r+0x48, %r9
    mov    r+0x50, %r10
    mov    r+0x58, %r11
    mov    r+0x60, %r12
    mov    r+0x68, %r13
    mov    r+0x70, %r14
    mov    r+0x78, %r15
    mov    %rbp,   %rsp
    pop    %rbp
    add    $0x8,   %rsp # pop the return address of __guard_failure
    jmp    *addr

restore_inlined:
    mov    restored_stack_size, %rsi
    mov    restored_start_addr, %rdi
    mov    %rbp, %rsp
    pop    %rbp
    add    $0x8,   %rsp # pop the return address of __guard_failure
    mov    %rdi, %rbp
    mov    %rbp, %rsp
    sub    %rsi, %rsp
    mov    r,      %rax
    mov    r+0x8,  %rcx
    mov    r+0x10, %rdx
    mov    r+0x18, %rbx
    mov    r+0x30, %rsi
    mov    r+0x38, %rdi
    mov    r+0x40, %r8
    mov    r+0x48, %r9
    mov    r+0x50, %r10
    mov    r+0x58, %r11
    mov    r+0x60, %r12
    mov    r+0x68, %r13
    mov    r+0x70, %r14
    mov    r+0x78, %r15
    jmp    *addr
