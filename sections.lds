ENTRY(_start)
MEMORY {
    FLASH (rx) : ORIGIN = 0x30000000, LENGTH = 16M
    SRAM (rwx) : ORIGIN = 0x00001000, LENGTH = 120K
}
SECTIONS {
    .init : { KEEP(*(.init)) . = ALIGN(4); } > FLASH
    .fixture : { *(.fixture*) . = ALIGN(4); } > FLASH
    .ram : {
        __ram_start = .;
        *(.text*) *(.rodata*) *(.srodata*) *(.data*) *(.sdata*)
        . = ALIGN(4); __ram_end = .;
    } > SRAM AT> FLASH
    __ram_load = LOADADDR(.ram);
    .bss (NOLOAD) : {
        __bss_start = .; *(.bss*) *(.sbss*) *(COMMON)
        . = ALIGN(4); __bss_end = .;
    } > SRAM
    ASSERT(__bss_end <= 0x1e000, "SRAM overflow: reserve 8 KiB for stack")
    /DISCARD/ : { *(.comment) *(.eh_frame*) *(.riscv.attributes) }
}
