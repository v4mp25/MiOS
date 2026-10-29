ENTRY(_start)

SECTIONS
{
    . = 1M;
__kernel_start = .;
    .text : {
        *(.multiboot)
        *(.text*)
    }

    .rodata : {
        *(.rodata*)
    }

    .data : {
        *(.data*)
    }

    .bss : {
        *(COMMON)
        *(.bss*)
    }
    . = ALIGN(4096);
__kernel_end = .;
}
