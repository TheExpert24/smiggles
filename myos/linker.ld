ENTRY(_start)
SECTIONS
{
    /* Load kernel at 1 MiB (traditional protected-mode kernel base). */
    . = 0x00100000;

    /* Multiboot2 header must be in the first 32 KiB and 8-byte aligned. */
    .multiboot ALIGN(8) : {
        *(.multiboot)
    }

    .text ALIGN(4096) : {
        *(.text)
    }

    .rodata ALIGN(4096) : { *(.rodata) }

    .data ALIGN(4096) : { *(.data) }
    .dma ALIGN(131072) (NOLOAD) : {
        *(.dma)
    }
    .bss ALIGN(4096) : {
        __bss_start = .;
        *(.bss)
        *(COMMON)

        . = ALIGN(16);
        stack_bottom = .;
        . = . + 0x10000; /* 64 KiB stack */
        stack_top = .;

        __bss_end = .;
    }
}