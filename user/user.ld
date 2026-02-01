ENTRY(_start)

SECTIONS
{
    . = 0x400000;
    
    .text : {
        *(.text*)
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
}