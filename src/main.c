#include <exec/types.h>
#include <dos/dos.h>
#include <proto/dos.h>

#define BALL_RADIUS 10

/* Character cells are roughly twice as tall as wide, so the horizontal
 * axis is sampled at ASPECT times the resolution of the vertical axis.
 * Without this the ball prints as an oval instead of a circle. */
#define ASPECT      2

/* The checker pattern is a true latitude/longitude sphere texture,
 * like the real Boing Ball, rather than rings and pie slices. Band
 * edges sit at +/-18 and +/-54 degrees, giving 5 bands per axis.
 * Trig constants are fixed-point (Q12) so no floating point or
 * math library is required. */
#define SCALE 4096
#define SIN18 1266
#define SIN54 3314
#define TAN18 1331
#define TAN54 5638

/* Yaw the checker texture on the sphere by ROT_DEG degrees so the ball
 * looks turned to the right, while the silhouette (dx,dy circle) stays
 * fixed -- the same technique the real Boing Ball uses. Fixed-point
 * (Q12) cos/sin of ROT_DEG, no floating point required. */
#define ROT_DEG 25
#define ROT_COS 3712
#define ROT_SIN 1731

static int Isqrt(int n)
{
    int r = 0;
    while ((r + 1) * (r + 1) <= n) r++;
    return r;
}

static int Classify(long v, long t1, long t2)
{
    if (v < -t2) return 0;
    if (v < -t1) return 1;
    if (v < t1)  return 2;
    if (v < t2)  return 3;
    return 4;
}

int main(void)
{
    int dx, dy, sdy, width, distSq, rz, hSpan, limit, latBand, lonBand, checker;
    long latT1, latT2, lonV, lonT1, lonT2, rxRot, rzRot;
    char line[2 * ASPECT * BALL_RADIUS + 2];

    hSpan = ASPECT * BALL_RADIUS;
    limit = hSpan * hSpan;
    latT1 = ((long) SIN18 * hSpan) / SCALE;
    latT2 = ((long) SIN54 * hSpan) / SCALE;

    for (dy = -BALL_RADIUS; dy <= BALL_RADIUS; dy++)
    {
        width = 0;
        sdy = ASPECT * dy;
        latBand = Classify((long) sdy, latT1, latT2);

        for (dx = -hSpan; dx <= hSpan; dx++)
        {
            distSq = dx * dx + sdy * sdy;

            if (distSq > limit)
            {
                line[width++] = ' ';
                continue;
            }

            rz = Isqrt(limit - distSq);

            rxRot = ((long) dx * ROT_COS + (long) rz * ROT_SIN) / SCALE;
            rzRot = ((long) rz * ROT_COS - (long) dx * ROT_SIN) / SCALE;
            if (rzRot < 0) rzRot = -rzRot;

            lonV  = rxRot * SCALE;
            lonT1 = (long) TAN18 * rzRot;
            lonT2 = (long) TAN54 * rzRot;
            lonBand = Classify(lonV, lonT1, lonT2);

            checker = (latBand + lonBand) & 1;

            line[width++] = checker ? '#' : '.';
        }

        line[width] = '\0';

        Printf("%s\n", line);
    }

    return RETURN_OK;
}