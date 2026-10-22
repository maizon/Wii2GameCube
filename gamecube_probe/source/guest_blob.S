/* Exactly the code built with wii_rules and extracted from .text.guest. */
.section .rodata.gcwclguest,"a",@progbits
.balign 32
.global gcwcl_guest_start
gcwcl_guest_start:
.incbin "../guest_probe/guest.bin"
.global gcwcl_guest_end
gcwcl_guest_end:
.balign 32
