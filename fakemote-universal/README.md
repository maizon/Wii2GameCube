# Maizon FakeMote Universal — experimental Wii Starlet build

This is an isolated source/build experiment. It does not modify the default branch, install IOS, or change Priiloader. Binary output is **FAKEMOTE.app**, a cIOS module, **not** an installable WAD.

The four `packed/part*.b64` files contain an XZ source archive (SHA256 e12b9707bb463d01829fc145bcd468ddb5f78ebf8d33309fd6546686436e000f). The standalone build workflow verifies the archive, clones upstream Fakemote and Nintendont, generates supported Nintendont HID mappings and compiles an experimental Starlet module.

The generic driver only handles a subset of Nintendont INIs: interrupt endpoint 0x81, Polltype=1, MultiIn=0/1. Hardware testing on a Wii is still necessary, and Wii Menu compatibility has **not** been confirmed. The 0810:0001 Twin USB receiver is not yet proven compatible.

**Do not change the Priiloader Wii Menu IOS to this untested module.** A broken System Menu IOS choice may require recovery, and deleting the experimental cIOS is not itself a recovery plan.

Based on original Fakemote, embedded-game-controller, and Nintendont. Respect the original upstream licenses.
