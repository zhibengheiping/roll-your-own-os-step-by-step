ENTRY(EfiMain)

SECTIONS {
    . = SIZEOF_HEADERS;

    .text : {
        *(.text)
    }

    . = ALIGN(0x1000);

    .rodata : {
        *(.rodata)
    }

    . = ALIGN(0x1000);

    .data : {
        *(.data)
    }

    . = ALIGN(0x1000);

    .bss : {
        *(.bss)
    }

    /DISCARD/ : {
        *(.eh_frame)
        *(.comment)
        *(.note.*)
    }
}
