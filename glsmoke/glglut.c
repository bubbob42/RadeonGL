/*
 * GLGlut -- minigl.library 29.1's GLUT on the card (RadeonGL, 2026-11-08).
 *
 * A window from glutCreateWindow: glutGet's answers, reshape before the
 * first frame, display only on glutPostRedisplay, the five solid shapes
 * and the normals GLUT 3.7 leaves after each (programs read them), a shape
 * recorded in a display list, the game mode queries. Ends the way GLUT
 * programs do, with exit() from a callback; the library closes the window.
 * GAME/S: the same frame in game mode on a 640 x 480 screen, left with
 * glutLeaveGameMode, so glutMainLoop returns.
 *
 *   GLGlut [GAME/S]
 *
 * One PASS/FAIL line a check; return code 5 if any failed.
 */

#include <exec/types.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/minigl.h>
#include <stdlib.h>

#define W 160

static LONG passed, failed, frame, reshapes, shape_w, shape_h;
static BOOL game;
static struct RDArgs *rd;

static void Count(BOOL ok, const char *what)
{
    if (ok)
        passed++;
    else
        failed++;
    Printf("%s %s\n", (LONG) (ok ? "PASS" : "FAIL"), (LONG) what);
}

static void WantInt(const char *what, LONG got, LONG want)
{
    BOOL ok;

    ok = (BOOL) (got == want);
    if (ok)
        passed++;
    else
        failed++;
    Printf("%s %-50s got %ld, want %ld\n", (LONG) (ok ? "PASS" : "FAIL"), (LONG) what, got, want);
}

static LONG Milli(float f)
{
    return (LONG) (f * 1000.0f + (f < 0.0f ? -0.5f : 0.5f));
}

/* the current normal against GLUT 3.7's value, in 1/1000 */
static void WantNormal(const char *what, float x, float y, float z)
{
    GLfloat n[4];
    BOOL ok;

    n[0] = n[1] = n[2] = -9.0f;
    glGetFloatv(GL_CURRENT_NORMAL, n);
    ok = (BOOL) (Milli(n[0]) - Milli(x) <= 2 && Milli(x) - Milli(n[0]) <= 2
                 && Milli(n[1]) - Milli(y) <= 2 && Milli(y) - Milli(n[1]) <= 2
                 && Milli(n[2]) - Milli(z) <= 2 && Milli(z) - Milli(n[2]) <= 2);
    if (ok)
        passed++;
    else
        failed++;
    Printf("%s %-50s got %ld %ld %ld /1000\n", (LONG) (ok ? "PASS" : "FAIL"), (LONG) what,
           Milli(n[0]), Milli(n[1]), Milli(n[2]));
}

/* the middle pixel: red, green, blue each above 200 or below 50 as asked */
static void WantMiddle(const char *what, int r, int g, int b)
{
    GLubyte px[4];
    BOOL ok;

    px[0] = px[1] = px[2] = 77;
    glReadPixels(shape_w / 2, shape_h / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    ok = (BOOL) ((r ? px[0] > 200 : px[0] < 50) && (g ? px[1] > 200 : px[1] < 50)
                 && (b ? px[2] > 200 : px[2] < 50));
    if (ok)
        passed++;
    else
        failed++;
    Printf("%s %-50s got %ld %ld %ld\n", (LONG) (ok ? "PASS" : "FAIL"), (LONG) what, (LONG) px[0],
           (LONG) px[1], (LONG) px[2]);
}

static void Reshape(int w, int h)
{
    reshapes++;
    shape_w = w;
    shape_h = h;
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-1.0, 1.0, -1.0, 1.0, -5.0, 5.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

static void Finish(void)
{
    Printf("GLGlut: %ld passed, %ld failed\n", passed, failed);
}

static void Display(void)
{
    static GLuint list;
    LONG t;

    frame++;
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_LIGHTING);
    glDisable(GL_CULL_FACE);
    if (frame == 1)
    {
        WantInt("reshape before the first frame", reshapes, 1);
        WantInt("reshape's width", shape_w, game ? 640 : W);
        glColor3f(1.0f, 0.0f, 0.0f);
        glutSolidCube(1.0);
        WantMiddle("glutSolidCube: red in the middle", 1, 0, 0);
        WantNormal("glutSolidCube leaves (-1, 0, 0)", -1.0f, 0.0f, 0.0f);
        glutSolidTorus(0.1, 0.5, 8, 16);
        WantNormal("glutSolidTorus (16 rings) leaves GLUT's normal", 0.9239f, 0.3827f, 0.0f);
        glutSolidDodecahedron();
        WantNormal("glutSolidDodecahedron leaves GLUT's normal", -0.525731f, 0.850651f, 0.0f);
        glutSolidSphere(0.4, 8, 8);
        glutSolidCone(0.4, 0.8, 8, 4);
        WantInt("the shapes: no GL error", (LONG) glGetError(), 0);
        glutSwapBuffers();
        glutPostRedisplay();
        return;
    }
    if (frame == 2)
    {
        /* a shape in a display list: recorded, drawn at the call */
        list = glGenLists(1);
        glNewList(list, GL_COMPILE);
        glColor3f(0.0f, 1.0f, 0.0f);
        glutSolidCube(1.0);
        glEndList();
        WantMiddle("glutSolidCube compiled: nothing drawn yet", 0, 0, 0);
        glCallList(list);
        WantMiddle("glCallList: the recorded green cube", 0, 1, 0);
        t = glutGet(GLUT_ELAPSED_TIME);
        Count((BOOL) (t >= 0 && t < 600000), "glutGet(GLUT_ELAPSED_TIME): milliseconds since glutInit");
        glutSwapBuffers();
        glutPostRedisplay();
        return;
    }
    if (game)
    {
        WantInt("glutGameModeGet(GLUT_GAME_MODE_ACTIVE) in game mode",
                glutGameModeGet(GLUT_GAME_MODE_ACTIVE), 1);
        WantInt("game mode: the screen's width", glutGameModeGet(GLUT_GAME_MODE_WIDTH), 640);
        glutSwapBuffers();
        glutLeaveGameMode();        /* the loop ends: glutMainLoop returns in main */
        return;
    }
    glutSwapBuffers();
    Finish();
    if (rd != NULL)
        FreeArgs(rd);
    exit(failed ? RETURN_WARN : RETURN_OK);     /* GLUT's way out; the library closes the window */
}

static void Idle(void)
{
    /* nothing: the frames post their own redisplay */
}

int main(int argc, char **argv)
{
    LONG args[1];
    int win;

    args[0] = 0;
    rd = ReadArgs((CONST_STRPTR) "GAME/S", args, NULL);
    game = (BOOL) (rd != NULL && args[0]);
    if (!MiniGLOpen())
    {
        Printf("GLGlut: minigl.library 29.1 did not open\n");
        return RETURN_FAIL;
    }
    atexit(MiniGLClose);
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_RGB | GLUT_DOUBLE | GLUT_DEPTH);
    WantInt("glutGet(GLUT_DISPLAY_MODE_POSSIBLE)", glutGet(GLUT_DISPLAY_MODE_POSSIBLE), 1);
    glutInitWindowSize(W, W);
    glutInitWindowPosition(60, 60);
    WantInt("glutGet(GLUT_INIT_WINDOW_X)", glutGet(GLUT_INIT_WINDOW_X), 60);
    glutGameModeString("640x480:16");
    WantInt("glutGameModeGet(GLUT_GAME_MODE_WIDTH) before", glutGameModeGet(GLUT_GAME_MODE_WIDTH), 640);
    WantInt("glutGameModeGet(GLUT_GAME_MODE_POSSIBLE)", glutGameModeGet(GLUT_GAME_MODE_POSSIBLE), 1);
    if (game)
        win = glutEnterGameMode();
    else
        win = glutCreateWindow("GLGlut");
    WantInt(game ? "glutEnterGameMode" : "glutCreateWindow", (LONG) win, 1);
    if (!win)
    {
        Finish();
        return RETURN_FAIL;
    }
    if (!game)
    {
        WantInt("glutGet(GLUT_WINDOW_WIDTH)", glutGet(GLUT_WINDOW_WIDTH), W);
        WantInt("glutGet(GLUT_WINDOW_HEIGHT)", glutGet(GLUT_WINDOW_HEIGHT), W);
        WantInt("glutGet(GLUT_WINDOW_RGBA)", glutGet(GLUT_WINDOW_RGBA), 1);
        WantInt("glutEnterGameMode with a window open: refused", glutEnterGameMode(), 0);
        WantInt("glutGet(unknown): -1", glutGet(12345), -1);
    }
    glutReshapeFunc(Reshape);
    glutDisplayFunc(Display);
    glutIdleFunc(Idle);
    glutMainLoop();
    /* game mode only: glutLeaveGameMode ended the loop */
    WantInt("glutMainLoop returned after glutLeaveGameMode: frames", frame, 3);
    Finish();
    if (rd != NULL)
        FreeArgs(rd);
    return failed ? RETURN_WARN : RETURN_OK;
}
