ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = 1M;

    kernel_start = .;

    .multiboot ALIGN(4K) : {
        *(.multiboot)
    } :text

    .text ALIGN(4K) : {
        *(.text*)
    } :text

    .rodata ALIGN(4K) : {
        *(.rodata*)
    } :text

    .data ALIGN(4K) : {
        *(.data*)
        *(.ldata*)
    } :data

    .bss ALIGN(4K) (NOLOAD) : {
        *(COMMON)
        *(.bss*)
        *(.lbss*)
    } :data

    kernel_end = ALIGN(4K);
}
