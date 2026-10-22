/* Wii-target scene DOL embedded as raw bytes. */
.section .rodata.gcwclscene,"a",@progbits
.balign 32
.global gcwcl_scene_start
gcwcl_scene_start:
.incbin "../guest_scene/scene.dol"
.global gcwcl_scene_end
gcwcl_scene_end:
.balign 32
