/*
 * VoltLog -- the voltage and temperature sensors of i2csensors.library (the
 * ICY board on the A4000D) read about ten times a second, each round one
 * line on the SERIAL port through exec's RawPutChar:
 *
 *     v <ms since start> <name>=<value> <name>=<value> ...
 *
 * Volts in mV, temperatures in tenths of a degree. Nothing goes to the
 * console while it logs (a console window draws through the RTG board, the
 * very thing under test); the sensor list is printed once at the start.
 *
 *   VoltLog [INTERVAL=ms] [SECONDS=n] [PRI=n] [LIST]
 *
 * Start it with Run before a test; CTRL-C or SECONDS ends it. Why
 * (2026-11-11): GLBench Pelt freezes the A4000D from 6 shells on, the
 * freeze point moving with the pixel load, and the ICY reads 4.8 V on the
 * Zorro 5 V line: does it sag under 3D load, right before the freeze?
 *
 * i2csensors.library is Henryk Richter's (V1.1+); its offsets and the
 * 16.16 values as Warden reads them (Warden src/i2csens.h). The ICY's
 * i2c.library polls a transfer in the CALLING task against a half-second
 * timeout: at a low priority under load reads fail or return garbage
 * (MEASURED on the A4000T), hence PRI (default 5).
 */

#include <exec/types.h>
#include <exec/tasks.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>

static const char ver[] = "$VER: VoltLog 0.1 (11.11.2026)";

#define TEMPLATE "INTERVAL/N,SECONDS/N,PRI/N,LIST/S"
#define GI2C_BASE    (0x80000000UL + 0x012C0000UL)
#define GI2C_VOLTAGE (GI2C_BASE + 0x10)
#define GI2C_TEMP    (GI2C_BASE + 0x12)
#define MAX_SENS 24

static struct Library *I2CSensorsBase = NULL;

#ifdef __SASC
LONG i2c_SensorNum(LONG type);
LONG i2c_ReadSensor(LONG type, LONG index, BYTE **unit, BYTE **name);
void VL_RawChar(ULONG c);
#pragma libcall I2CSensorsBase i2c_SensorNum  1e 001
#pragma libcall I2CSensorsBase i2c_ReadSensor 24 a91004
#pragma libcall SysBase VL_RawChar 204 001
#else
#include <inline/macros.h>
#define i2c_SensorNum(t) \
    LP1(0x1e, LONG, i2c_SensorNum, LONG, (t), d0, , I2CSensorsBase)
#define i2c_ReadSensor(t, i, u, n) \
    LP4(0x24, LONG, i2c_ReadSensor, LONG, (t), d0, LONG, (i), d1, \
        BYTE **, (u), a1, BYTE **, (n), a2, , I2CSensorsBase)
#define VL_RawChar(c) LP1NR(0x204, VL_RawChar, ULONG, (c), d0, , SysBase)
#endif

struct Sens
{
    ULONG type;
    LONG index;
    char name[16];
};

static struct Sens sens[MAX_SENS];
static ULONG nsens = 0;

static void RawStr(const char *s)
{
    while (*s != 0)
        VL_RawChar((ULONG) (UBYTE) *s++);
}

static void RawDec(LONG v)
{
    char buf[12];
    int i;
    ULONG u;

    if (v < 0)
    {
        VL_RawChar('-');
        u = (ULONG) -v;
    }
    else
        u = (ULONG) v;
    i = 0;
    do
    {
        buf[i++] = (char) ('0' + u % 10);
        u /= 10;
    } while (u != 0 && i < 11);
    while (i > 0)
        VL_RawChar((ULONG) (UBYTE) buf[--i]);
}

/* a 16.16 value scaled: volts to mV (scale 1000), degrees to tenths (10) */
static LONG Scaled(LONG v, LONG scale)
{
    LONG whole;
    ULONG frac;

    whole = v >> 16;
    frac = (ULONG) v & 0xFFFFUL;
    return whole * scale + (LONG) ((frac * (ULONG) scale) >> 16);
}

static BOOL ReadOne(ULONG type, LONG index, LONG *value, BYTE **name)
{
    BYTE *unit;

    unit = NULL;
    *name = NULL;
    *value = i2c_ReadSensor((LONG) type, index, &unit, name);
    if (unit == NULL && *name == NULL)
        *value = i2c_ReadSensor((LONG) type, index, &unit, name);  /* once more, as Warden */
    return (BOOL) (unit != NULL || *name != NULL);
}

static void Collect(ULONG type)
{
    LONG n;
    LONG i;
    LONG v;
    BYTE *name;
    ULONG k;

    n = i2c_SensorNum((LONG) type);
    for (i = 0; i < n && nsens < MAX_SENS; i++)
    {
        if (!ReadOne(type, i, &v, &name))
            continue;
        sens[nsens].type = type;
        sens[nsens].index = i;
        for (k = 0; name != NULL && name[k] != 0 && k < sizeof(sens[0].name) - 1; k++)
            sens[nsens].name[k] = (name[k] == ' ' || name[k] == '=') ? '_' : name[k];
        sens[nsens].name[k] = 0;
        Printf("  %-15s %s %ld\n", (ULONG) sens[nsens].name,
               (ULONG) (type == GI2C_VOLTAGE ? "mV  " : "dC/10"),
               Scaled(v, type == GI2C_VOLTAGE ? 1000 : 10));
        nsens++;
    }
}

int main(void)
{
    struct RDArgs *rda;
    LONG args[4];
    ULONG interval;
    ULONG seconds;
    LONG pri;
    LONG oldpri;
    struct Task *me;
    ULONG ticks;
    ULONG t;
    ULONG i;
    LONG v;
    BYTE *name;
    int rc;

    args[0] = args[1] = args[2] = args[3] = 0;
    rda = ReadArgs(TEMPLATE, args, NULL);
    if (rda == NULL)
    {
        PrintFault(IoErr(), "VoltLog");
        return RETURN_FAIL;
    }
    interval = args[0] ? (ULONG) *(LONG *) args[0] : 100;
    seconds = args[1] ? (ULONG) *(LONG *) args[1] : 600;
    pri = args[2] ? *(LONG *) args[2] : 5;
    rc = RETURN_FAIL;
    I2CSensorsBase = OpenLibrary("i2csensors.library", 1);
    if (I2CSensorsBase == NULL)
    {
        Printf("VoltLog: no i2csensors.library\n");
        goto done;
    }
    Printf("VoltLog: sensors (i2csensors %ld.%ld)\n", (LONG) I2CSensorsBase->lib_Version,
           (LONG) I2CSensorsBase->lib_Revision);
    Collect(GI2C_VOLTAGE);
    Collect(GI2C_TEMP);
    if (nsens == 0)
    {
        Printf("VoltLog: no voltage or temperature sensor\n");
        goto done;
    }
    rc = RETURN_OK;
    if (args[3])
        goto done;

    ticks = (interval + 19) / 20;     /* Delay counts 50 Hz ticks */
    if (ticks == 0)
        ticks = 1;
    Printf("VoltLog: every %lu ms for %lu s to the serial port (CTRL-C ends it)\n",
           ticks * 20, seconds);
    Flush(Output());
    me = FindTask(NULL);
    oldpri = SetTaskPri(me, pri);
    RawStr("\nv start interval_ms=");
    RawDec((LONG) (ticks * 20));
    RawStr("\n");
    t = 0;
    while (t < seconds * 1000)
    {
        RawStr("v ");
        RawDec((LONG) t);
        for (i = 0; i < nsens; i++)
        {
            VL_RawChar(' ');
            RawStr(sens[i].name);
            VL_RawChar('=');
            if (ReadOne(sens[i].type, sens[i].index, &v, &name))
                RawDec(Scaled(v, sens[i].type == GI2C_VOLTAGE ? 1000 : 10));
            else
                VL_RawChar('?');
        }
        VL_RawChar('\n');
        if (SetSignal(0, 0) & SIGBREAKF_CTRL_C)
            break;
        Delay(ticks);
        t += ticks * 20;
    }
    RawStr("v end\n");
    SetTaskPri(me, oldpri);

done:
    if (I2CSensorsBase != NULL)
        CloseLibrary(I2CSensorsBase);
    FreeArgs(rda);
    return rc;
}
