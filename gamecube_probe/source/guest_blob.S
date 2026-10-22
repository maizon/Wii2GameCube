/*
 * Embed exactly one project-built guest DOL.
 * The loader verifies DOL header and copies TEXT bytes into managed GC MEM1.
 * This section is DATA and must never be invoked as code directly.
 */
.section .rodata.gcwclguest,"a",@progbits
.balign 32
.global gcwcl_guest_start
gcwcl_guest_start:
.incbin "../guest_probe/guest.dol"
.global gcwcl_guest_end
gcwcl_guest_end:
.balign 32
