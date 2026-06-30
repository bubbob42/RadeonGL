#include <exec/types.h>
#include <dos/dos.h>
#include <proto/dos.h>

#define BALL_RADIUS 7
#define NUM_RINGS   4

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
    int dx, dy, width, distSq, dist, ring, oct, checker;
    char line[2 * BALL_RADIUS + 2];

    for (dy = -BALL_RADIUS; dy <= BALL_RADIUS; dy++)
    {
        width = 0;

        for (dx = -BALL_RADIUS; dx <= BALL_RADIUS; dx++)
        {
            distSq = dx * dx + dy * dy;

            if (distSq > BALL_RADIUS * BALL_RADIUS)
            {
                line[width++] = ' ';
                continue;
            }

            dist = Isqrt(distSq);
            ring = (dist * NUM_RINGS) / (BALL_RADIUS + 1);
            oct  = Octant(dx, dy);
            checker = (ring + oct) & 1;

            line[width++] = checker ? '#' : '.';
        }

        line[width] = '\0';

        Printf((CONST_STRPTR)"%s\n", (CONST_STRPTR)line);
    }

    return RETURN_OK;
}
