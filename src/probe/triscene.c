/*
 * triscene.c -- the scene the Tri probe draws (triscene.h).
 *
 * GL coordinates (glOrtho 0..256, y up); the check pixels are in memory
 * order, y_mem = 255 - y_gl. Every exact check sits well inside or well
 * outside a triangle, away from edges where fill rules could differ.
 */

#include "triscene.h"

const struct TriPixel TriScene_Pixels[TRI_NPIXELS] =
{
    {   0,   0, TRI_BACKGROUND, 1, "top-left corner, background" },
    { 255, 255, TRI_BACKGROUND, 1, "bottom-right corner, background" },
    {  40, 215, 0xff0000UL,     1, "inside the red triangle" },
    { 100, 155, TRI_BACKGROUND, 1, "beside the red triangle's long edge" },
    {  40,  95, 0x00ff00UL,     1, "inside the green triangle" },
    { 200, 215, TRI_BACKGROUND, 1, "bottom right, background" },
    { 188,  85, TRI_BACKGROUND, 0, "middle of the shaded triangle (not background)" },
    { 150, 230, TRI_BACKGROUND, 1, "between the triangles, background" }
};

static void Solid(struct Core *c, ULONG r, ULONG g, ULONG b,
                  float x0, float y0, float x1, float y1, float x2, float y2)
{
    Core_Color4ub(c, r, g, b, 255);
    Core_Vertex3f(c, x0, y0, 0.0f);
    Core_Vertex3f(c, x1, y1, 0.0f);
    Core_Vertex3f(c, x2, y2, 0.0f);
}

void TriScene_Draw(struct Core *c)
{
    Core_Viewport(c, 0, 0, TRI_W, TRI_H);
    Core_MatrixMode(c, GL_PROJECTION);
    Core_LoadIdentity(c);
    Core_Ortho(c, 0.0, (double) TRI_W, 0.0, (double) TRI_H, -1.0, 1.0);
    Core_MatrixMode(c, GL_MODELVIEW);
    Core_LoadIdentity(c);
    Core_ClearColor(c, 0.0f, 0.0f, 0.2509804f, 1.0f);     /* 64 / 255 */
    Core_Clear(c, GL_COLOR_BUFFER_BIT);

    Core_Begin(c, GL_TRIANGLES);
    Solid(c, 255, 0, 0, 16.0f, 16.0f, 120.0f, 16.0f, 16.0f, 120.0f);
    Solid(c, 0, 255, 0, 16.0f, 136.0f, 120.0f, 136.0f, 16.0f, 240.0f);
    Core_Color4ub(c, 255, 0, 0, 255);
    Core_Vertex3f(c, 136.0f, 136.0f, 0.0f);
    Core_Color4ub(c, 0, 255, 0, 255);
    Core_Vertex3f(c, 240.0f, 136.0f, 0.0f);
    Core_Color4ub(c, 0, 0, 255, 255);
    Core_Vertex3f(c, 188.0f, 240.0f, 0.0f);
    Core_End(c);
}
