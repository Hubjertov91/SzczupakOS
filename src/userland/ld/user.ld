OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = 0x0000008000400000;
    
    .text : {
        *(.text*)
    } :text
    
    .rodata : {
        *(.rodata*)
    } :text
    
    .data : {
        *(.data*)
    } :data
    
    .bss (NOLOAD) : {
        *(.bss*)
    } :data
}
