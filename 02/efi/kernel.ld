ENTRY(EfiMain)

SECTIONS {
    . = SIZEOF_HEADERS;
    .text : {
        ASSERT((0x1f1 > SIZEOF_HEADERS), "error: header too large");
        FILL(0);
        . += 0x1f1 - SIZEOF_HEADERS;
        *(.setup)
        *(.rodata .rodata.*)
        . = ALIGN(0x1000);
        *(.text)
        . = ALIGN(0x1000);
        *(.got .got.*)
        *(.data)
        *(.bss)
    }

    /DISCARD/ : {
        *(.eh_frame)
        *(.comment)
        *(.note.*)
    }
}
