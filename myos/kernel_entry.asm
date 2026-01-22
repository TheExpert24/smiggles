[BITS 32]
[GLOBAL _start]
[EXTERN kernel_main]

_start:
    call kernel_main
    cli
    hlt
