|
| MMU hack for Amiga 68k
| Written by Frank Wille <frank@phoenix.owl.de>
|
| GAS-syntax port of sys_amiga68k.s, for the m68k-amigaos-gcc
| feasibility investigation -- not used by the real vbcc/vasm build.
|
| INTENTIONAL DEVIATION, not a byte-verified translation: the real
| file's _MMUHackOn/_MMUHackOff use MC68040-specific privileged MMU
| instructions (cpusha, movec to/from DTT0/DTT1) to set up transparent
| translation registers. Per direct instruction, these are stubbed to
| a no-op here instead of translated faithfully, because (a) the
| PiStorm/Emu68 target this project actually runs on is not confirmed
| to support these 68040-specific MMU instructions correctly, and
| (b) the real functions are only ever called when glquake is built
| with a specific opt-in flag this project doesn't use. Symbol names
| are preserved so anything that references them still links.

	.text

	.globl	_MMUHackOn
	.globl	_MMUHackOff

_MMUHackOn:
	rts

_MMUHackOff:
	rts
