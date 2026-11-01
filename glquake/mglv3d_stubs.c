/*
 * MiniGLV3D port note: "C fallback" build variant only -- provides empty
 * stub implementations for the handful of glquake symbols that were
 * originally hand-written 68k assembly (in_amigamouse68k.s,
 * in_amigamousewin68k.s, snd_int68k.s, sys_amiga68k.s) with no working
 * portable C alternative anywhere in the source tree (the one C attempt,
 * in_amigamousePPC.c, is an incomplete/broken partial translation --
 * confirmed unusable, has a dangling `ie = &` mid-statement).
 *
 * This file exists ONLY so this build variant links at all without an
 * assembler -- it deliberately trades away real functionality for build
 * simplicity:
 *   - InputIntCode/InputIntCodeWin: mouse movement/button input via the
 *     Amiga input.device interrupt handler does nothing -- no mouse
 *     input from this path in this variant.
 *   - AudioIntCode: the Paula chip audio-playback interrupt handler does
 *     nothing -- no digital sound output in this variant.
 *   - MMUHackOn/MMUHackOff: a real, working no-op either way -- this is
 *     a genuinely optional performance hack, off by default, only
 *     engaged behind glquake's own explicit "-hack" command-line flag
 *     (sys_amiga.c) and already skipped entirely on PPC builds.
 *
 * The real-assembly build variant assembles the actual .s files instead
 * and does not compile this file at all -- see STATUS.md's Phase L
 * "full glquake build/link" section for both variants' exact composition.
 */

void InputIntCode(void)
{
}

void InputIntCodeWin(void)
{
}

void AudioIntCode(void)
{
}

void MMUHackOn(void)
{
}

void MMUHackOff(void)
{
}
