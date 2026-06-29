#include <exec/types.h>
#include <dos/dos.h>
#include <proto/dos.h>

#define BALL_RADIUS 7

int main(void)
{
    int dx, dy, width;
    char line[2 * BALL_RADIUS + 2];

    for (dy = -BALL_RADIUS; dy <= BALL_RADIUS; dy++)
    {
        width = 0;

        for (dx = -BALL_RADIUS; dx <= BALL_RADIUS; dx++)
            line[width++] = (dx * dx + dy * dy <= BALL_RADIUS * BALL_RADIUS) ? 'A' : ' ';

        line[width] = '\0';

        Printf("%s\n", line);
    }

    return RETURN_OK;
}