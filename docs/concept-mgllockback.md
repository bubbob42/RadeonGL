# mglLockBack (scheduled item 4, 2026-11-25)

The user, 2026-11-25 01:20: "Schedule 1-4 for tomorrow". Item 4: a P96
lock of the back buffer with the 3D engine idle, its address and pitch to
the program, released at MGLUnlockDisplay / MGLSwitchDisplay (V30
context.c); refusals stay allowed.

## What the others do

- **29.1:** refuses (no lock it could hand out), size filled in. RadeonGL
  did the same until now.
- **V30** (PiStorm3D gl/src/context.c:4050-4153): collects the frame in
  flight, LockBitMapTags on the back bitmap, keeps the lock until
  MGLUnlockDisplay (MGLSwitchDisplay calls that first, so a forgotten
  unlock ends at the frame); refuses a second lock and a double-buffered
  screen whose back address it cannot know. pixel_format 0 ("RGBA8").
- **MiniGL 1.x** (mglQ3 context.c:1315): pixel_format is the Warp3D
  context's W3D_FMT_ flag.

## RadeonGL

All in rglread.c (linked after the hot code), next to glReadPixels, which
already finds the frame and locks it:

1. Card_WaitIdle (everything drawn finished; its fence wait outside the
   lock is the guard against a hung engine, card.c PreSettle).
2. The frame being drawn: the target, a bitmap context's bitmap, or
   triple buffering's third (FrameIndex/FrameThird/FrameBase, now shared
   with glReadPixels, whose behaviour is unchanged).
3. p96LockBitMap, on the board only; the lock is stored in the card's
   own slot for that bitmap (`b->lock`), so RadeonGL's flushes and reads
   leave the bitmap alone while the program holds it, and Card_Close's
   UnlockAll gives it back in any case.
4. info: width, height, depth, **pixel_format = Warp3D's flag for the
   bytes as they lie** (R5G6B5PC, R5G5B5PC or B8G8R8A8: the R200's
   little-endian formats, the aperture does not swap), base address of the
   top row, pitch in bytes.

Released by MGLUnlockDisplay, MGLSwitchDisplay, mglResizeContext and
MGLDeleteContext: their Hook_ entries moved to rglread.c and give the lock
back first; the bodies stay in rgl.c under Rgl_ names, so **rgl.o keeps its
11508 bytes and every hot object its address** (map compared: everything up
to rglread.o unchanged).

Refused (size filled in, address NULL): the null card, a lost context,
inside glBegin, a second lock, a bitmap off the board.

## Caveats

- Reads of card memory are slow behind the Mediator (~4.4 MB/s, BusBench);
  writes are faster. That is the program's choice.
- A program that draws with GL while it holds the lock breaks the
  contract: the flush then cannot lock the bitmap, fails, and its
  UnlockAll takes the program's lock away. UnlockBack checks that the slot
  still holds its own lock before giving it back.
- While the lock is held, other contexts' flushes may wait for P96 (the
  lock idles the board). Hold it briefly, as CyberGraphX asks.

## Tests

- GLCompat 0.5, LockCases (on the card; not run yet, Hamiller down):
  granted, size, depth, Warp3D format, pitch; a second lock refused; a
  green square written through the pointer in the named format and read
  back from the bitmap; a forgotten unlock ended by mglSwitchDisplay.
- Host: nothing new (the card side has no host model); the host tests pass.

## Open questions (for the user)

1. pixel_format as Warp3D's flag (MiniGL 1.x) rather than V30's 0: agreed?
2. Window contexts: the back buffer is RadeonGL's off-screen target, so
   the lock is granted there too (V30 refuses only a double-buffered
   screen). Fine?
