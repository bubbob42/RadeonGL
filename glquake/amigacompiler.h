/* compiler dependant definitions */

#ifdef __SASC
#   define SAVEDS       __saveds
#   define ASM          __asm
#   define REG(x,y)     register __##x y
#elif defined(__GNUC__)
#   define SAVEDS       __saveds
#   define ASM
#   define REG(x,y)     y __asm(#x)
#elif defined(_DCC)
#   define SAVEDS       __geta4
#   define ASM
#   define REG(x,y)     __##x y
#elif defined(__STORM__)
#   define SAVEDS       __saveds
#   define ASM
#   define REG(x,y)     register __##x y
#elif defined(__VBCC__)
#   define SAVEDS       __saveds
#   define ASM
#   define REG(x,y)     __reg(#x) y
#else
#   error   add #defines for your compiler...
#endif
