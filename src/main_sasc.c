#include <exec/types.h>
#include <dos/dos.h>
#include <proto/dos.h>

#define BALL_RADIUS 10
#define NUM_RINGS   4

/* Character cells are roughly twice as tall as wide, so the horizontal
 * axis is sampled at ASPECT times the resolution of the vertical axis.
 * Without this the ball prints as an oval instead of a circle. */
#define ASPECT      2

static int Isqrt(int n)
{
    int r = 0;
    while ((r + 1) * (r + 1) <= n) r++;
    return r;
}

static int Octant(int dx, int dy)
{
    int ax = (dx < 0) ? -dx : dx;
    int ay = (dy < 0) ? -dy : dy;

    if (dx >= 0 && dy >= 0) return (ax >= ay) ? 0 : 1;
    if (dx < 0  && dy >= 0) return (ay >= ax) ? 2 : 3;
    if (dx < 0  && dy < 0)  return (ax >= ay) ? 4 : 5;
    return (ay >= ax) ? 6 : 7;
}

int main(void)
{
    int dx, dy, sdy, width, distSq, dist, ring, oct, checker, hSpan, limit;
    char line[2 * ASPECT * BALL_RADIUS + 2];

    hSpan = ASPECT * BALL_RADIUS;
    limit = hSpan * hSpan;

    for (dy = -BALL_RADIUS; dy <= BALL_RADIUS; dy++)
    {
        width = 0;
        sdy = ASPECT * dy;

        for (dx = -hSpan; dx <= hSpan; dx++)
        {
            distSq = dx * dx + sdy * sdy;

            if (distSq > limit)
            {
                line[width++] = ' ';
                continue;
            }

            dist = Isqrt(distSq);
            ring = (dist * NUM_RINGS) / (hSpan + 1);
            oct  = Octant(dx, sdy);
            checker = (ring + oct) & 1;

            line[width++] = checker ? '#' : '.';
        }

        line[width] = '\0';

        Printf((CONST_STRPTR)"%s\n", (CONST_STRPTR)line);
    }

    return RETURN_OK;
}
