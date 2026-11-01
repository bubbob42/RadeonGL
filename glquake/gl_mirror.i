
#line 7 "math.h"
 typedef float float_t ;
 typedef double double_t ;



















 extern const char __pInf_d [ ] ;
 extern const char __pInf_s [ ] ;
 extern const char __qNaN_s [ ] ;























 double acos ( double ) ;
 double acosh ( double ) ;
 double asin ( double ) ;
 double asinh ( double ) ;
 double atan ( double ) ;
 double atanh ( double ) ;
 double atan2 ( double , double ) ;
 double ceil ( double ) ;
 double copysign ( double , double ) ;
 double cos ( double ) ;
 double cosh ( double ) ;
 double exp ( double ) ;
 double exp2 ( double ) ;
 double exp10 ( double ) ;
 double expm1 ( double ) ;
 double fabs ( double ) ;
 double fdim ( double , double ) ;
 double floor ( double ) ;
 double fma ( double , double , double ) ;
 double fmax ( double , double ) ;
 double fmin ( double , double ) ;
 double fmod ( double , double ) ;
 double frexp ( double , int * ) ;
 double hypot ( double , double ) ;
 double ldexp ( double , int ) ;
 double log ( double ) ;
 double log10 ( double ) ;
 double log1p ( double ) ;
 double log2 ( double ) ;
 double logb ( double ) ;
 double modf ( double , double * ) ;
 double nan ( const char * ) ;
 double nearbyint ( double ) ;
 double pow ( double , double ) ;
 double remainder ( double , double ) ;
 double rint ( double ) ;
 double round ( double ) ;
 double scalbln ( double , long ) ;
 double scalbn ( double , int ) ;
 double sin ( double ) ;
 double sinh ( double ) ;
 double sqrt ( double ) ;
 double tan ( double ) ;
 double tanh ( double ) ;
 double trunc ( double ) ;
 float acosf ( float ) ;
 float acoshf ( float ) ;
 float asinf ( float ) ;
 float asinhf ( float ) ;
 float atanf ( float ) ;
 float atanhf ( float ) ;
 float atan2f ( float , float ) ;
 float ceilf ( float ) ;
 float copysignf ( float , float ) ;
 float cosf ( float ) ;
 float coshf ( float ) ;
 float expf ( float ) ;
 float exp2f ( float ) ;
 float exp10f ( float ) ;
 float expm1f ( float ) ;
 float fabsf ( float ) ;
 float fdimf ( float , float ) ;
 float floorf ( float ) ;
 float fmaf ( float , float , float ) ;
 float fmaxf ( float , float ) ;
 float fminf ( float , float ) ;
 float fmodf ( float , float ) ;
 float frexpf ( float value , int * ) ;
 float hypotf ( float , float ) ;
 float ldexpf ( float , int ) ;
 float logf ( float ) ;
 float log10f ( float ) ;
 float log1pf ( float ) ;
 float log2f ( float ) ;
 float logbf ( float ) ;
 float modff ( float , float * ) ;
 float nanf ( const char * ) ;
 float nearbyintf ( float ) ;
 float powf ( float , float ) ;
 float remainderf ( float , float ) ;
 float rintf ( float ) ;
 float roundf ( float ) ;
 float scalblnf ( float , long ) ;
 float scalbnf ( float , int ) ;
 float sinf ( float ) ;
 float sinhf ( float ) ;
 float sqrtf ( float ) ;
 float tanf ( float ) ;
 float tanhf ( float ) ;
 float truncf ( float ) ;
 long double acosl ( long double ) ;
 long double acoshl ( long double ) ;
 long double asinl ( long double ) ;
 long double asinhl ( long double ) ;
 long double atanl ( long double ) ;
 long double atanhl ( long double ) ;
 long double atan2l ( long double , long double ) ;
 long double ceill ( long double ) ;
 long double copysignl ( long double , long double ) ;
 long double cosl ( long double ) ;
 long double coshl ( long double ) ;
 long double expl ( long double ) ;
 long double exp2l ( long double ) ;
 long double exp10l ( long double ) ;
 long double expm1l ( long double ) ;
 long double fabsl ( long double ) ;
 long double fdiml ( long double , long double ) ;
 long double floorl ( long double ) ;
 long double fmal ( long double , long double , long double ) ;
 long double fmaxl ( long double , long double ) ;
 long double fminl ( long double , long double ) ;
 long double fmodl ( long double , long double ) ;
 long double frexpl ( long double value , int * ) ;
 long double hypotl ( long double , long double ) ;
 long double ldexpl ( long double , int ) ;
 long double logl ( long double ) ;
 long double log10l ( long double ) ;
 long double log1pl ( long double ) ;
 long double log2l ( long double ) ;
 long double logbl ( long double ) ;
 long double modfl ( long double , long double * ) ;
 long double nanl ( const char * ) ;
 long double nearbyintl ( long double ) ;
 long double powl ( long double , long double ) ;
 long double remainderl ( long double , long double ) ;
 long double rintl ( long double ) ;
 long double roundl ( long double ) ;
 long double scalblnl ( long double , long ) ;
 long double scalbnl ( long double , int ) ;
 long double sinl ( long double ) ;
 long double sinhl ( long double ) ;
 long double sqrtl ( long double ) ;
 long double tanl ( long double ) ;
 long double tanhl ( long double ) ;
 long double truncl ( long double ) ;
 int ilogb ( double ) ;
 int ilogbf ( float ) ;
 int ilogbl ( long double ) ;
 long lrint ( double ) ;
 long lrintf ( float ) ;
 long lrintl ( long double ) ;
 long lround ( double ) ;
 long lroundf ( float ) ;
 long lroundl ( long double ) ;


 long long llround ( double ) ;
 long long llroundf ( float ) ;
 long long llroundl ( long double ) ;
#line 6 "math_881.h"
 int fpclassify ( __reg ( "fp0" ) double ) =
 "\tinline\n"
 "\tftst.x\tfp0\n"
 "\tmoveq\t#7,d1\n"
 "\tfmove.l\tfpsr,d0\n"
 "\trol.l\t#8,d0\n"
 "\tand.l\td1,d0\n"
 "\teinline" ;
 int isfinite ( __reg ( "fp0" ) double ) =
 "\tinline\n"
 "\tftst.x\tfp0\n"
 "\tfmove.l\tfpsr,d0\n"
 "\tand.l\t#$03000000,d0\n"
 "\tseq\td0\n"
 "\textb.l\td0\n"
 "\teinline" ;
 int isnormal ( __reg ( "fp0" ) double ) =
 "\tinline\n"
 "\tftst.x\tfp0\n"
 "\tfmove.l\tfpsr,d0\n"
 "\tand.l\t#$07000000,d0\n"
 "\tseq\td0\n"
 "\textb.l\td0\n"
 "\teinline" ;
 int isnan ( __reg ( "fp0" ) double ) =
 "\tinline\n"
 "\tftst.x\tfp0\n"
 "\tmoveq\t#1,d0\n"
 "\tfbun\t.skip\n"
 "\tmoveq\t#0,d0\n"
 ".skip\n"
 "\teinline" ;
 int isinf ( __reg ( "fp0" ) double ) =
 "\tinline\n"
 "\tftst.x\tfp0\n"
 "\tmoveq\t#1,d0\n"
 "\tfmove.l\tfpsr,d1\n"
 "\trol.l\t#7,d1\n"
 "\tand.l\td1,d0\n"
 "\teinline" ;
 int signbit ( __reg ( "fp0" ) double ) =
 "\tinline\n"
 "\tfmove.s\tfp0,d0\n"
 "\tmoveq\t#31,d1\n"
 "\tasr.l\td1,d0\n"
 "\teinline" ;
 int isgreater ( __reg ( "fp0" ) double , __reg ( "fp1" ) double ) =
 "\tinline\n"
 "\tfcmp.x\tfp1,fp0\n"
 "\tmoveq\t#1,d0\n"
 "\tfbogt\t.skip\n"
 "\tmoveq\t#0,d0\n"
 ".skip\n"
 "\teinline" ;
 int isgreaterequal ( __reg ( "fp0" ) double , __reg ( "fp1" ) double ) =
 "\tinline\n"
 "\tfcmp.x\tfp1,fp0\n"
 "\tmoveq\t#0,d0\n"
 "\tfbult\t.skip\n"
 "\tmoveq\t#1,d0\n"
 ".skip\n"
 "\teinline" ;
 int isless ( __reg ( "fp0" ) double , __reg ( "fp1" ) double ) =
 "\tinline\n"
 "\tfcmp.x\tfp1,fp0\n"
 "\tmoveq\t#1,d0\n"
 "\tfbolt\t.skip\n"
 "\tmoveq\t#0,d0\n"
 ".skip\n"
 "\teinline" ;
 int islessequal ( __reg ( "fp0" ) double , __reg ( "fp1" ) double ) =
 "\tinline\n"
 "\tfcmp.x\tfp1,fp0\n"
 "\tmoveq\t#0,d0\n"
 "\tfbugt\t.skip\n"
 "\tmoveq\t#1,d0\n"
 ".skip\n"
 "\teinline" ;
 int islessgreater ( __reg ( "fp0" ) double , __reg ( "fp1" ) double ) =
 "\tinline\n"
 "\tfcmp.x\tfp1,fp0\n"
 "\tmoveq\t#0,d0\n"
 "\tfbueq\t.skip\n"
 "\tmoveq\t#1,d0\n"
 ".skip\n"
 "\teinline" ;
 int isunordered ( __reg ( "fp0" ) double , __reg ( "fp1" ) double ) =
 "\tinline\n"
 "\tfcmp.x\tfp1,fp0\n"
 "\tmoveq\t#1,d0\n"
 "\tfbun\t.skip\n"
 "\tmoveq\t#0,d0\n"
 ".skip\n"
 "\teinline" ;



 __attr ( "__fp0ret;" ) double __asm_acos ( __reg ( "fp0" ) double ) =
 "\tinline\n"
 "\tfacos.x\tfp0\n"
 "\teinline" ;
 __attr ( "__fp0ret;" ) double __asm_asin ( __reg ( "fp0" ) double ) =
 "\tinline\n"
 "\tfasin.x\tfp0\n"
 "\teinline" ;
 __attr ( "__fp0ret;" ) double __asm_atan ( __reg ( "fp0" ) double ) =
 "\tinline\n"
 "\tfatan.x\tfp0\n"
 "\teinline" ;
 __attr ( "__fp0ret;" ) double __asm_atanh ( __reg ( "fp0" ) double ) =
 "\tinline\n"
 "\tfatanh.x\tfp0\n"
 "\teinline" ;
 __attr ( "__fp0ret;" ) double __asm_ceil ( __reg ( "fp0" ) double ) =
 "\tinline\n"
 "\tfmove.x\tfp0,fp1\n"
 "\tfintrz.x\tfp0\n"
 "\tfcmp.x\tfp1,fp0\n"
 "\tfboge\t.skip\n"
 "\tfadd.s\t#$3f800000,fp0\n"
 ".skip\n"
 "\teinline" ;
 __attr ( "__fp0ret;" ) double __asm_copysign ( __reg ( "fp0" ) double , __reg ( "fp1" ) double ) =
 "\tinline\n"
 "\tfmove.s\tfp1,d0\n"
 "\tfabs.x\tfp0\n"
 "\ttst.l\td0\n"
 "\tbpl\t.skip\n"
 "\tfneg.x\tfp0\n"
 ".skip\n"
 "\teinline" ;
 __attr ( "__fp0ret;" ) double __asm_cos ( __reg ( "fp0" ) double ) =
 "\tinline\n"
 "\tfcos.x\tfp0\n"
 "\teinline" ;
 __attr ( "__fp0ret;" ) double __asm_cosh ( __reg ( "fp0" ) double ) =
 "\tinline\n"
 "\tfcosh.x\tfp0\n"
 "\teinline" ;
 __attr ( "__fp0ret;" ) double __asm_exp ( __reg ( "fp0" ) double ) =
 "\tinline\n"
 "\tfetox.x\tfp0\n"
 "\teinline" ;
 __attr ( "__fp0ret;" ) double __asm_exp2 ( __reg ( "fp0" ) double ) =
 "\tinline\n"
 "\tftwotox.x\tfp0\n"
 "\teinline" ;
 __attr ( "__fp0ret;" ) double __asm_exp10 ( __reg ( "fp0" ) double ) =
 "\tinline\n"
 "\tftentox.x\tfp0\n"
 "\teinline" ;
 __attr ( "__fp0ret;" ) double __asm_expm1 ( __reg ( "fp0" ) double ) =
 "\tinline\n"
 "\tfetoxm1.x\tfp0\n"
 "\teinline" ;
 __attr ( "__fp0ret;" ) double __asm_fabs ( __reg ( "fp0" ) double ) =
 "\tinline\n"
 "\tfabs.x\tfp0\n"
 "\teinline" ;
 __attr ( "__fp0ret;" ) double __asm_fdim ( __reg ( "fp0" ) double , __reg ( "fp1" ) double ) =
 "\tinline\n"
 "\tfsub.x\tfp1,fp0\n"
 "\tfabs.x\tfp0\n"
 "\teinline" ;
 __attr ( "__fp0ret;" ) double __asm_floor ( __reg ( "fp0" ) double ) =
 "\tinline\n"
 "\tfmove.x\tfp0,fp1\n"
 "\tfintrz.x\tfp0\n"
 "\tfcmp.x\tfp1,fp0\n"
 "\tfbole\t.skip\n"
 "\tfsub.s\t#$3f800000,fp0\n"
 ".skip\n"
 "\teinline" ;
 __attr ( "__fp0ret;" ) double __asm_fma ( __reg ( "fp0" ) double , __reg ( "fp1" ) double , __reg ( "fp2" ) double ) =
 "\tinline\n"
 "\tfmul.x\tfp1,fp0\n"
 "\tfadd.x\tfp2,fp0\n"
 "\teinline" ;
 __attr ( "__fp0ret;" ) double __asm_fmax ( __reg ( "fp0" ) double , __reg ( "fp1" ) double ) =
 "\tinline\n"
 "\tfcmp.x\tfp1,fp0\n"
 "\tfboge\t.skip\n"
 "\tftst.x\tfp1\n"
 "\tfbun\t.skip\n"
 "\tfmove.x\tfp1,fp0\n"
 ".skip\n"
 "\teinline" ;
 __attr ( "__fp0ret;" ) double __asm_fmin ( __reg ( "fp0" ) double , __reg ( "fp1" ) double ) =
 "\tinline\n"
 "\tfcmp.x\tfp1,fp0\n"
 "\tfbole\t.skip\n"
 "\tftst.x\tfp1\n"
 "\tfbun\t.skip\n"
 "\tfmove.x\tfp1,fp0\n"
 ".skip\n"
 "\teinline" ;
 __attr ( "__fp0ret;" ) double __asm_fmod ( __reg ( "fp0" ) double , __reg ( "fp1" ) double ) =
 "\tinline\n"
 "\tfmod.x\tfp1,fp0\n"
 "\teinline" ;
 __attr ( "__fp0ret;" ) double __asm_hypot ( __reg ( "fp0" ) double , __reg ( "fp1" ) double ) =
 "\tinline\n"
 "\tfmul.x\tfp0,fp0\n"
 "\tfmul.x\tfp1,fp1\n"
 "\tfadd.x\tfp1,fp0\n"
 "\tfsqrt.x\tfp0\n"
 "\teinline" ;
 __attr ( "__fp0ret;" ) double __asm_ldexp ( __reg ( "fp0" ) double , __reg ( "d0" ) int ) =
 "\tinline\n"
 "\tfscale.l\td0,fp0\n"
 "\teinline" ;
 __attr ( "__fp0ret;" ) double __asm_log ( __reg ( "fp0" ) double ) =
 "\tinline\n"
 "\tflogn.x\tfp0\n"
 "\teinline" ;
 __attr ( "__fp0ret;" ) double __asm_log10 ( __reg ( "fp0" ) double ) =
 "\tinline\n"
 "\tflog10.x\tfp0\n"
 "\teinline" ;
 __attr ( "__fp0ret;" ) double __asm_log2 ( __reg ( "fp0" ) double ) =
 "\tinline\n"
 "\tflog2.x\tfp0\n"
 "\teinline" ;
 __attr ( "__fp0ret;" ) double __asm_logb ( __reg ( "fp0" ) double ) =
 "\tinline\n"
 "\tfgetexp.x\tfp0\n"
 "\teinline" ;
 __attr ( "__fp0ret;" ) double __asm_lognp1 ( __reg ( "fp0" ) double ) =
 "\tinline\n"
 "\tflognp1.x\tfp0\n"
 "\teinline" ;
 long __asm_lround ( __reg ( "fp0" ) double ) =
 "\tinline\n"
 "\tfmove.s\tfp0,d0\n"
 "\tand.l\t#$80000000,d0\n"
 "\tor.l\t#$3f000000,d0\n"
 "\tfadd.s\td0,fp0\n"
 "\tfintrz.x\tfp0\n"
 "\tfmove.l\tfp0,d0\n"
 "\teinline" ;
 __attr ( "__fp0ret;" ) double __asm_nan ( __reg ( "a0" ) const char * ) =
 "\tinline\n"
 "\tfmove.s\t#$7fc00000,fp0\n"
 "\teinline" ;
 __attr ( "__fp0ret;" ) double __asm_nearbyint ( __reg ( "fp0" ) double ) =
 "\tinline\n"
 "\tfint.x\tfp0\n"
 "\teinline" ;
 __attr ( "__fp0ret;" ) double __asm_remainder ( __reg ( "fp0" ) double , __reg ( "fp1" ) double ) =
 "\tinline\n"
 "\tfrem.x\tfp1,fp0\n"
 "\teinline" ;
 __attr ( "__fp0ret;" ) double __asm_round ( __reg ( "fp0" ) double ) =
 "\tinline\n"
 "\tfmove.s\tfp0,d0\n"
 "\tand.l\t#$80000000,d0\n"
 "\tor.l\t#$3f000000,d0\n"
 "\tfadd.s\td0,fp0\n"
 "\tfintrz.x\tfp0\n"
 "\teinline" ;
 __attr ( "__fp0ret;" ) double __asm_sin ( __reg ( "fp0" ) double ) =
 "\tinline\n"
 "\tfsin.x\tfp0\n"
 "\teinline" ;
 __attr ( "__fp0ret;" ) double __asm_sinh ( __reg ( "fp0" ) double ) =
 "\tinline\n"
 "\tfsinh.x\tfp0\n"
 "\teinline" ;
 __attr ( "__fp0ret;" ) double __asm_sqrt ( __reg ( "fp0" ) double ) =
 "\tinline\n"
 "\tfsqrt.x\tfp0\n"
 "\teinline" ;
 __attr ( "__fp0ret;" ) double __asm_tan ( __reg ( "fp0" ) double ) =
 "\tinline\n"
 "\tftan.x\tfp0\n"
 "\teinline" ;
 __attr ( "__fp0ret;" ) double __asm_tanh ( __reg ( "fp0" ) double ) =
 "\tinline\n"
 "\tftanh.x\tfp0\n"
 "\teinline" ;
 __attr ( "__fp0ret;" ) double __asm_trunc ( __reg ( "fp0" ) double ) =
 "\tinline\n"
 "\tfintrz.x\tfp0\n"
 "\teinline" ;
#line 6 "string.h"
 typedef unsigned long size_t ;





 void * memcpy ( void * , const void * , size_t ) ;
 void * memmove ( void * , const void * , size_t ) ;
 char * strcpy ( char * , const char * ) ;
 char * strncpy ( char * , const char * , size_t ) ;
 char * strcat ( char * , const char * ) ;
 char * strncat ( char * , const char * , size_t ) ;
 int memcmp ( const void * , const void * , size_t ) ;
 int strcmp ( const char * , const char * ) ;
 int strncmp ( const char * , const char * , size_t ) ;
 void * memchr ( const void * , int , size_t ) ;
 char * strchr ( const char * , int ) ;
 size_t strcspn ( const char * , const char * ) ;
 char * strpbrk ( const char * , const char * ) ;
 char * strrchr ( const char * , int ) ;
 size_t strspn ( const char * , const char * ) ;
 char * strstr ( const char * , const char * ) ;
 void * memset ( void * , int , size_t ) ;
 size_t strlen ( const char * ) ;
 char * strtok ( char * , const char * ) ;
 char * strerror ( int ) ;
 int strcoll ( const char * , const char * ) ;
 size_t strxfrm ( char * , const char * , size_t ) ;






















































































































 void * __asm_memcpy ( __reg ( "a0" ) void * , __reg ( "a1" ) const void * ,
 __reg ( "d2" ) size_t ) =
 "\tinline\n"
 "\tmove.l\ta0,d0\n"
 "\tsubq.l\t#4,d2\n"
 "\tbcs\t.l3\n"
 "\tmove.l\td0,d1\n"
 "\tlsr.l\t#1,d1\n"
 "\tbcc\t.l1\n"
 "\tmove.b\t(a1)+,(a0)+\n"
 "\tsubq.l\t#1,d2\n"
 "\tbcs\t.l3\n"
 "\taddq.l\t#1,d1\n"
 ".l1\n"
 "\tlsr.l\t#1,d1\n"
 "\tbcc\t.l2\n"
 "\tmove.w\t(a1)+,(a0)+\n"
 "\tsubq.l\t#2,d2\n"
 "\tbcs\t.l3\n"
 ".l2\n"
 "\tmove.l\t(a1)+,(a0)+\n"
 "\tsubq.l\t#4,d2\n"
 "\tbcc\t.l2\n"
 ".l3\n"
 "\taddq.l\t#3,d2\n"
 "\tbcc\t.l5\n"
 ".l4\n"
 "\tmove.b\t(a1)+,(a0)+\n"
 "\tdbf\td2,.l4\n"
 ".l5\n"
 "\teinline" ;
 void * __asm_memcpy_desc ( __reg ( "a0" ) void * , __reg ( "a1" ) const void * ,
 __reg ( "d2" ) size_t ) =
 "\tinline\n"
 "\tsubq.l\t#4,d2\n"
 "\tbcs\t.l3\n"
 "\tmove.l\ta0,d0\n"
 "\tlsr.l\t#1,d0\n"
 "\tbcc\t.l1\n"
 "\tmove.b\t-(a1),-(a0)\n"
 "\tsubq.l\t#1,d2\n"
 "\tbcs\t.l3\n"
 ".l1\n"
 "\tlsr.l\t#1,d0\n"
 "\tbcc\t.l2\n"
 "\tmove.w\t-(a1),-(a0)\n"
 "\tsubq.l\t#2,d2\n"
 "\tbcs\t.l3\n"
 ".l2\n"
 "\tmove.l\t-(a1),-(a0)\n"
 "\tsubq.l\t#4,d2\n"
 "\tbcc\t.l2\n"
 ".l3\n"
 "\taddq.l\t#3,d2\n"
 "\tbcc\t.l5\n"
 ".l4\n"
 "\tmove.b\t-(a1),-(a0)\n"
 "\tdbf\td2,.l4\n"
 ".l5\n"
 "\tmove.l\ta0,d0\n"
 "\teinline" ;
 void * __asm_memset ( __reg ( "a0" ) void * , __reg ( "d0" ) int , __reg ( "d2" ) size_t ) =
 "\tinline\n"
 "\tmove.l\ta0,a1\n"
 "\tsubq.l\t#8,d2\n"
 "\tbcs\t.l3\n"
 "\tmove.l\td0,d1\n"
 "\tlsl.l\t#8,d0\n"
 "\tmove.b\td1,d0\n"
 "\taddq.l\t#4,d2\n"
 "\tmove.l\td0,d1\n"
 "\tswap\td0\n"
 "\tmove.w\td1,d0\n"
 "\tmove.l\ta1,d1\n"
 "\tlsr.l\t#1,d1\n"
 "\tbcc\t.l1\n"
 "\tmove.b\td0,(a0)+\n"
 "\taddq.l\t#1,d1\n"
 "\tsubq.l\t#1,d2\n"
 ".l1\n"
 "\tlsr.l\t#1,d1\n"
 "\tbcc\t.l2\n"
 "\tmove.w\td0,(a0)+\n"
 "\tsubq.l\t#2,d2\n"
 ".l2\n"
 "\tmove.l\td0,(a0)+\n"
 "\tsubq.l\t#4,d2\n"
 "\tbcc\t.l2\n"
 "\taddq.l\t#3,d2\n"
 "\tbcs\t.l4\n"
 "\tbra\t.l5\n"
 ".l3\n"
 "\taddq.l\t#7,d2\n"
 "\tbcc\t.l5\n"
 ".l4\n"
 "\tmove.b\td0,(a0)+\n"
 "\tdbf\td2,.l4\n"
 ".l5\n"
 "\tmove.l\ta1,d0\n"
 "\teinline" ;



 __vattr ( "regused(" "d0/a0" ")" ) size_t __asm_strlen ( __reg ( "a0" ) const char * ) =
 "\tinline\n"
 "\tmove.l\ta0,d0\n"
 ".l1\n"
 "\ttst.b\t(a0)+\n"
 "\tbne\t.l1\n"
 "\tsub.l\ta0,d0\n"
 "\tnot.l\td0\n"
 "\teinline" ;
 __vattr ( "regused(" "d0/a0/a1" ")" ) char * __asm_strcpy ( __reg ( "a0" ) char * , __reg ( "a1" ) const char * ) =
 "\tinline\n"
 "\tmove.l\ta0,d0\n"
 ".l1\n"
 "\tmove.b\t(a1)+,(a0)+\n"
 "\tbne\t.l1\n"
 "\teinline" ;
 char * __asm_strncpy ( __reg ( "a0" ) char * , __reg ( "a1" ) const char * ,
 __reg ( "d1" ) size_t ) =
 "\tinline\n"
 "\tmove.l\ta0,d0\n"
 "\tbra\t.l2\n"
 ".l1\n"
 "\tmove.b\t(a1),(a0)+\n"
 "\tbeq\t.l2\n"
 "\taddq.l\t#1,a1\n"
 ".l2\n"
 "\tsubq.l\t#1,d1\n"
 "\tbpl\t.l1\n"
 "\teinline" ;
 int __asm_strcmp ( __reg ( "a0" ) const char * , __reg ( "a1" ) const char * ) =
 "\tinline\n"
 "\tmoveq\t#0,d0\n"
 "\tmoveq\t#0,d1\n"
 ".l1\n"
 "\tmove.b\t(a0)+,d0\n"
 "\tmove.b\t(a1)+,d1\n"
 "\tbeq\t.l2\n"
 "\tsub.l\td1,d0\n"
 "\tbeq\t.l1\n"
 "\tmoveq\t#0,d1\n"
 ".l2\n"
 "\tsub.l\td1,d0\n"
 "\teinline" ;
 int __asm_strncmp ( __reg ( "a0" ) const char * , __reg ( "a1" ) const char * ,
 __reg ( "d2" ) size_t ) =
 "\tinline\n"
 "\tmoveq\t#0,d0\n"
 "\tmoveq\t#0,d1\n"
 ".l1\n"
 "\tsubq.l\t#1,d2\n"
 "\tbmi\t.l3\n"
 "\tmove.b\t(a0)+,d0\n"
 "\tmove.b\t(a1)+,d1\n"
 "\tbeq\t.l2\n"
 "\tsub.l\td1,d0\n"
 "\tbeq\t.l1\n"
 "\tmoveq\t#0,d1\n"
 ".l2\n"
 "\tsub.l\td1,d0\n"
 ".l3\n"
 "\teinline" ;
 __vattr ( "regused(" "d0/a0/a1" ")" ) char * __asm_strcat ( __reg ( "a0" ) char * , __reg ( "a1" ) const char * ) =
 "\tinline\n"
 "\tmove.l\ta0,d0\n"
 ".l1\n"
 "\ttst.b\t(a0)+\n"
 "\tbne\t.l1\n"
 "\tsubq.l\t#1,a0\n"
 ".l2\n"
 "\tmove.b\t(a1)+,(a0)+\n"
 "\tbne\t.l2\n"
 "\teinline" ;
 char * __asm_strncat ( __reg ( "a0" ) char * , __reg ( "a1" ) const char * ,
 __reg ( "d1" ) size_t ) =
 "\tinline\n"
 "\tmove.l\ta0,d0\n"
 "\ttst.l\td1\n"
 "\tbeq\t.l3\n"
 ".l1\n"
 "\ttst.b\t(a0)+\n"
 "\tbne\t.l1\n"
 "\tsubq.l\t#1,a0\n"
 ".l2\n"
 "\tmove.b\t(a1)+,(a0)+\n"
 "\tbeq\t.l3\n"
 "\tsubq.l\t#1,d1\n"
 "\tbne\t.l2\n"
 "\tclr.b\t(a0)\n"
 ".l3\n"
 "\teinline" ;
 __vattr ( "regused(" "d0/d1/a0" ")" ) char * __asm_strrchr ( __reg ( "a0" ) const char * , __reg ( "d1" ) int ) =
 "\tinline\n"
 "\tmoveq\t#0,d0\n"
 ".l1\n"
 "\tcmp.b\t(a0),d1\n"
 "\tbne\t.l2\n"
 "\tmove.l\ta0,d0\n"
 ".l2\n"
 "\ttst.b\t(a0)+\n"
 "\tbne\t.l1\n"
 "\teinline" ;
#line 4 "stdarg.h"
 typedef unsigned char * va_list ;
#line 35 "stdio.h"
 typedef struct _iobuf
 {
 char * filehandle ;
 char * pointer ;
 char * base ;
 struct _iobuf * next ;
 struct _iobuf * prev ;
 int count ;
 int flags ;
 int bufsize ;
 } FILE ;


 extern FILE * stdin , * stdout , * stderr ;

 int _fillbuf ( FILE * ) , _putbuf ( int , FILE * ) , _flushbuf ( FILE * ) ;
 void _ttyflush ( void ) ;














 typedef long fpos_t ;









 FILE * fopen ( const char * , const char * ) ;
 FILE * freopen ( const char * , const char * , FILE * ) ;
 int fflush ( FILE * ) ;
 int fclose ( FILE * ) ;
 int rename ( const char * , const char * ) ;
 int remove ( const char * ) ;
 FILE * tmpfile ( void ) ;
 char * tmpnam ( char * ) ;
 int setvbuf ( FILE * , char * , int , size_t ) ;
 void setbuf ( FILE * , char * ) ;
 int fprintf ( FILE * , const char * , ... ) ;
 int printf ( const char * , ... ) ;
 int sprintf ( char * , const char * , ... ) ;
 int snprintf ( char * , size_t , const char * , ... ) ;
 int __v0fprintf ( FILE * , const char * ) ;
 int __v0printf ( const char * ) ;
 int __v0sprintf ( char * , const char * ) ;
 int vprintf ( const char * , va_list ) ;
 int vfprintf ( FILE * , const char * , va_list ) ;
 int vsprintf ( char * , const char * , va_list ) ;
 int vsnprintf ( char * , size_t , const char * , va_list ) ;
 int fscanf ( FILE * , const char * , ... ) ;
 int scanf ( const char * , ... ) ;
 int sscanf ( const char * , const char * , ... ) ;
 int vscanf ( const char * , va_list ) ;
 int vfscanf ( FILE * , const char * , va_list ) ;
 int vsscanf ( const char * , const char * , va_list ) ;
 char * fgets ( char * , int , FILE * ) ;
 int fputs ( const char * , FILE * ) ;
 char * gets ( char * ) ;
 int puts ( const char * ) ;
 int ungetc ( int , FILE * ) ;
 size_t fread ( void * , size_t , size_t , FILE * ) ;
 size_t fwrite ( void * , size_t , size_t , FILE * ) ;
 int fseek ( FILE * , long , int ) ;
 void rewind ( FILE * ) ;
 long ftell ( FILE * ) ;
 int fgetpos ( FILE * , fpos_t * ) ;
 int fsetpos ( FILE * , const fpos_t * ) ;
 void perror ( const char * ) ;
 int fgetc ( FILE * ) ;
 int fputc ( int , FILE * ) ;
 int getchar ( void ) ;
 int putchar ( int ) ;


















#pragma printflike printf
#pragma printflike fprintf
#pragma printflike sprintf
#pragma printflike snprintf
#pragma scanflike scanf
#pragma scanflike fscanf
#pragma scanflike sscanf
#line 11 "stdlib.h"
 typedef char wchar_t ;













 void exit ( int ) ;

 void _Exit ( int ) ;

 void * malloc ( size_t ) ;
 void * calloc ( size_t , size_t ) ;
 void * realloc ( void * , size_t ) ;
 void free ( void * ) ;
 int system ( const char * ) ;
 int rand ( void ) ;
 void srand ( unsigned int ) ;
 double atof ( const char * ) ;
 int atoi ( const char * ) ;
 long atol ( const char * ) ;

 long long atoll ( const char * ) ;

 double strtod ( const char * , char * * ) ;
 long strtol ( const char * , char * * , int ) ;

 signed long long strtoll ( const char * , char * * , int ) ;

 unsigned long strtoul ( const char * , char * * , int ) ;

 unsigned long long strtoull ( const char * , char * * , int ) ;

 void abort ( void ) ;
 int atexit ( void ( * ) ( void ) ) ;
 char * getenv ( const char * ) ;
 void * bsearch ( const void * , const void * , size_t , size_t , int ( * ) ( const void * , const void * ) ) ;
 void qsort ( void * , size_t , size_t , int ( * ) ( const void * , const void * ) ) ;

 typedef struct {
 int quot , rem ;
 } div_t ;

 typedef struct {
 long quot , rem ;
 } ldiv_t ;


 typedef struct {
 long long quot , rem ;
 } lldiv_t ;


 div_t div ( int , int ) ;
 ldiv_t ldiv ( long , long ) ;

 lldiv_t lldiv ( long long , long long ) ;


 int abs ( int ) ;
 long labs ( long ) ;

 long long llabs ( long long ) ;



 __vattr ( "regused(" "d0" ")" ) int __asm_abs ( __reg ( "d0" ) int ) =
 "\tinline\n"
 "\ttst.l\td0\n"
 "\tbpl\t.skip\n"
 "\tneg.l\td0\n"
 ".skip\n"
 "\teinline" ;
 __vattr ( "regused(" "d0" ")" ) long __asm_labs ( __reg ( "d0" ) long ) =
 "\tinline\n"
 "\ttst.l\td0\n"
 "\tbpl\t.skip\n"
 "\tneg.l\td0\n"
 ".skip\n"
 "\teinline" ;



 __vattr ( "regused(" "d0/d1" ")" ) long long __asm_llabs ( __reg ( "d0/d1" ) long long ) =
 "\tinline\n"
 "\ttst.l\td0\n"
 "\tbpl\t.skip\n"
 "\tneg.l\td1\n"
 "\tnegx.l\td0\n"
 ".skip\n"
 "\teinline" ;




 __vattr ( "regused(" "d0/d1" ")" ) div_t __asm_div ( __reg ( "d0" ) int , __reg ( "d1" ) int ) =
 "\tinline\n"
 "\tdivsl.l\td1,d1:d0\n"
 "\teinline" ;
 __vattr ( "regused(" "d0/d1" ")" ) ldiv_t __asm_ldiv ( __reg ( "d0" ) long , __reg ( "d1" ) long ) =
 "\tinline\n"
 "\tdivsl.l\td1,d1:d0\n"
 "\teinline" ;









 extern size_t _nalloc ;

 struct __exitfuncs {
 struct __exitfuncs * next ;
 void ( * func ) ( void ) ;
 } ;
#line 6 "setjmp.h"
 typedef unsigned char jmp_buf [ 13 * 4 + 6 * 12 ] ;




 int setjmp ( jmp_buf ) ;
 void longjmp ( jmp_buf , int ) ;
#line 23 "common.h"
 typedef unsigned char byte ;






 typedef enum { false , true } qboolean ;



 typedef struct sizebuf_s
 {
 qboolean allowoverflow ;
 qboolean overflowed ;
 byte * data ;
 int maxsize ;
 int cursize ;

 } sizebuf_t ;

 void SZ_Alloc ( sizebuf_t * buf , int startsize ) ;
 void SZ_Free ( sizebuf_t * buf ) ;
 void SZ_Clear ( sizebuf_t * buf ) ;
 void * SZ_GetSpace ( sizebuf_t * buf , int length ) ;
 void SZ_Write ( sizebuf_t * buf , void * data , int length ) ;
 void SZ_Print ( sizebuf_t * buf , char * data ) ;



 typedef struct link_s
 {
 struct link_s * prev , * next ;

 } link_t ;


 void ClearLink ( link_t * l ) ;
 void RemoveLink ( link_t * l ) ;
 void InsertLinkBefore ( link_t * l , link_t * before ) ;
 void InsertLinkAfter ( link_t * l , link_t * after ) ;


























 extern qboolean bigendien ;

 extern short ( * BigShort ) ( short l ) ;
 extern short ( * LittleShort ) ( short l ) ;
 extern int ( * BigLong ) ( int l ) ;
 extern int ( * LittleLong ) ( int l ) ;
 extern float ( * BigFloat ) ( float l ) ;




 extern float ( * LittleFloat ) ( float l ) ;




 void MSG_WriteChar ( sizebuf_t * sb , int c ) ;
 void MSG_WriteByte ( sizebuf_t * sb , int c ) ;
 void MSG_WriteShort ( sizebuf_t * sb , int c ) ;
 void MSG_WriteLong ( sizebuf_t * sb , int c ) ;
 void MSG_WriteFloat ( sizebuf_t * sb , float f ) ;
 void MSG_WriteString ( sizebuf_t * sb , char * s ) ;
 void MSG_WriteCoord ( sizebuf_t * sb , float f ) ;
 void MSG_WriteAngle ( sizebuf_t * sb , float f ) ;
 void MSG_WritePreciseAngle ( sizebuf_t * sb , float f ) ;

 extern int msg_readcount ;
 extern qboolean msg_badread ;

 void MSG_BeginReading ( void ) ;
 int MSG_ReadChar ( void ) ;
 int MSG_ReadByte ( void ) ;
 int MSG_PeekByte ( void ) ;
 int MSG_ReadShort ( void ) ;
 int MSG_ReadLong ( void ) ;
 float MSG_ReadFloat ( void ) ;
 char * MSG_ReadString ( void ) ;

 float MSG_ReadCoord ( void ) ;
 float MSG_ReadAngle ( void ) ;
 float MSG_ReadPreciseAngle ( void ) ;































































 void Q_memcpy ( void * dest , void * src , int count ) ;
 int Q_memcmp ( void * m1 , void * m2 , int count ) ;
 void Q_memset ( void * dest , int fill , int count ) ;
 void Q_strcpy ( char * dest , char * src ) ;
 void Q_strncpy ( char * dest , char * src , int count ) ;
 int Q_strlen ( char * str ) ;
 char * Q_strrchr ( char * s , char c ) ;
 void Q_strcat ( char * dest , char * src ) ;
 int Q_strcmp ( char * s1 , char * s2 ) ;
 int Q_strncmp ( char * s1 , char * s2 , int count ) ;
 int Q_atoi ( char * str ) ;
 float Q_atof ( char * str ) ;



 int Q_strcasecmp ( char * s1 , char * s2 ) ;
 int Q_strncasecmp ( char * s1 , char * s2 , int n ) ;



 extern char com_token [ 1024 ] ;
 extern qboolean com_eof ;

 char * COM_Parse ( char * data ) ;


 extern int com_argc ;
 extern char * * com_argv ;

 int COM_CheckParm ( char * parm ) ;
 void COM_Init ( char * path ) ;
 void COM_InitArgv ( int argc , char * * argv ) ;

 char * COM_SkipPath ( char * pathname ) ;
 void COM_StripExtension ( char * in , char * out ) ;
 void COM_FileBase ( char * in , char * out , size_t outside ) ;
 void COM_DefaultExtension ( char * path , char * extension ) ;

 char * va ( char * format , ... ) ;





 extern int com_filesize ;
 struct cache_user_s ;

 extern char com_gamedir [ 128 ] ;

 void COM_WriteFile ( char * filename , void * data , int len ) ;
 int COM_OpenFile ( char * filename , int * hndl ) ;
 int COM_FOpenFile ( char * filename , FILE * * file ) ;
 void COM_CloseFile ( int h ) ;

 byte * COM_LoadStackFile ( char * path , void * buffer , int bufsize ) ;
 byte * COM_LoadTempFile ( char * path ) ;
 byte * COM_LoadHunkFile ( char * path ) ;
 void COM_LoadCacheFile ( char * path , struct cache_user_s * cu ) ;


 extern struct cvar_s registered ;

 extern qboolean standard_quake , rogue , hipnotic ;
#line 62 "bspfile.h"
 typedef struct
 {
 int fileofs , filelen ;

 } lump_t ;



















 typedef struct
 {
 float mins [ 3 ] , maxs [ 3 ] ;
 float origin [ 3 ] ;
 int headnode [ 4 ] ;
 int visleafs ;
 int firstface , numfaces ;

 } dmodel_t ;

 typedef struct
 {
 int version ;
 lump_t lumps [ 15 ] ;

 } dheader_t ;

 typedef struct
 {
 int nummiptex ;
 int dataofs [ 4 ] ;

 } dmiptexlump_t ;



 typedef struct miptex_s
 {
 char name [ 16 ] ;
 unsigned width , height ;
 unsigned offsets [ 4 ] ;

 } miptex_t ;


 typedef struct
 {
 float point [ 3 ] ;

 } dvertex_t ;












 typedef struct
 {
 float normal [ 3 ] ;
 float dist ;
 int type ;

 } dplane_t ;





















 typedef struct
 {
 int planenum ;
 short children [ 2 ] ;
 short mins [ 3 ] ;
 short maxs [ 3 ] ;
 unsigned short firstface ;
 unsigned short numfaces ;

 } dsnode_t ;


 typedef struct
 {
 int planenum ;
 int children [ 2 ] ;
 float mins [ 3 ] ;
 float maxs [ 3 ] ;
 unsigned int firstface ;
 unsigned int numfaces ;

 } dl2node_t ;

 typedef struct
 {
 int planenum ;
 short children [ 2 ] ;

 } dsclipnode_t ;


 typedef struct
 {
 int planenum ;
 int children [ 2 ] ;

 } dlclipnode_t ;

 typedef struct texinfo_s
 {
 float vecs [ 2 ] [ 4 ] ;
 int miptex ;
 int flags ;

 } texinfo_t ;






 typedef struct
 {
 unsigned short v [ 2 ] ;

 } dsedge_t ;


 typedef struct
 {
 unsigned int v [ 2 ] ;

 } dledge_t ;



 typedef struct
 {
 short planenum ;
 short side ;

 int firstedge ;
 short numedges ;
 short texinfo ;


 byte styles [ 4 ] ;
 int lightofs ;

 } dsface_t ;


 typedef struct
 {
 int planenum ;
 int side ;

 int firstedge ;
 int numedges ;
 int texinfo ;


 byte styles [ 4 ] ;
 int lightofs ;

 } dlface_t ;











 typedef struct
 {
 int contents ;
 int visofs ;

 short mins [ 3 ] ;
 short maxs [ 3 ] ;

 unsigned short firstmarksurface ;
 unsigned short nummarksurfaces ;

 byte ambient_level [ 4 ] ;

 } dsleaf_t ;


 typedef struct
 {
 int contents ;
 int visofs ;

 float mins [ 3 ] ;
 float maxs [ 3 ] ;

 unsigned int firstmarksurface ;
 unsigned int nummarksurfaces ;

 byte ambient_level [ 4 ] ;

 } dl2leaf_t ;
#line 26 "vid.h"
 typedef byte pixel_t ;

 typedef struct vrect_s
 {
 int x , y , width , height ;
 struct vrect_s * pnext ;
 } vrect_t ;

 typedef struct
 {
 pixel_t * buffer ;
 pixel_t * colormap ;
 unsigned short * colormap16 ;
 int fullbright ;
 unsigned rowbytes ;
 unsigned width ;
 unsigned height ;
 float aspect ;
 int numpages ;
 int recalc_refdef ;
 pixel_t * conbuffer ;
 int conrowbytes ;
 unsigned conwidth ;
 unsigned conheight ;
 int maxwarpwidth ;
 int maxwarpheight ;
 pixel_t * direct ;

 } viddef_t ;

 extern viddef_t vid ;
 extern unsigned short d_8to16table [ 256 ] ;
 extern unsigned d_8to24table [ 256 ] ;
 extern void ( * vid_menudrawfn ) ( void ) ;
 extern void ( * vid_menukeyfn ) ( int key ) ;

 void VID_SetPalette ( unsigned char * palette ) ;


 void VID_ShiftPalette ( unsigned char * palette ) ;


 void VID_Init ( unsigned char * palette ) ;




 void VID_Shutdown ( void ) ;


 void VID_Update ( vrect_t * rects ) ;


 int VID_SetMode ( int modenum , unsigned char * palette ) ;



 void VID_HandlePause ( qboolean pause ) ;
#line 29 "sys.h"
 int Sys_FileOpenRead ( char * path , int * hndl ) ;
 int Sys_FileOpenWrite ( char * path ) ;
 void Sys_FileClose ( int handle ) ;
 void Sys_FileSeek ( int handle , int position ) ;
 int Sys_FileRead ( int handle , void * dest , int count ) ;
 int Sys_FileWrite ( int handle , void * data , int count ) ;
 int Sys_FileTime ( char * path ) ;
 void Sys_mkdir ( char * path ) ;




 void Sys_MakeCodeWriteable ( unsigned long startaddr , unsigned long length ) ;










































 void Sys_DebugLog ( char * file , char * fmt , ... ) ;

 void Sys_Error ( char * error , ... ) ;


 void Sys_Printf ( char * fmt , ... ) ;


 void Sys_Quit ( void ) ;

 double Sys_FloatTime ( void ) ;

 char * Sys_ConsoleInput ( void ) ;

 void Sys_Sleep ( void ) ;



 void Sys_SendKeyEvents ( void ) ;


 void Sys_LowFPPrecision ( void ) ;
 void Sys_HighFPPrecision ( void ) ;
 void Sys_SetFPCW ( void ) ;
#line 86 "zone.h"
 void Memory_Init ( void * buf , int size ) ;

 void Z_Free ( void * ptr ) ;
 void * Z_Malloc ( int size ) ;
 void * Z_TagMalloc ( int size , int tag ) ;

 void Z_DumpHeap ( void ) ;
 void Z_CheckHeap ( void ) ;
 int Z_FreeMemory ( void ) ;

 void * Hunk_Alloc ( int size ) ;
 void * Hunk_AllocName ( int size , char * name ) ;

 void * Hunk_HighAllocName ( int size , char * name ) ;

 int Hunk_LowMark ( void ) ;
 void Hunk_FreeToLowMark ( int mark ) ;

 int Hunk_HighMark ( void ) ;
 void Hunk_FreeToHighMark ( int mark ) ;

 void * Hunk_TempAlloc ( int size ) ;

 void Hunk_Check ( void ) ;

 typedef struct cache_user_s
 {
 void * data ;
 } cache_user_t ;

 void Cache_Flush ( void ) ;

 void * Cache_Check ( cache_user_t * c ) ;



 void Cache_Free ( cache_user_t * c ) ;

 void * Cache_Alloc ( cache_user_t * c , int size , char * name ) ;



 void Cache_Report ( void ) ;
#line 22 "mathlib.h"
 typedef float vec_t ;
 typedef vec_t vec3_t [ 3 ] ;
 typedef vec_t vec5_t [ 5 ] ;

 typedef int fixed4_t ;
 typedef int fixed8_t ;
 typedef int fixed16_t ;







 struct mplane_s ;

 extern vec3_t vec3_origin ;
 extern int nanmask ;

















 void VectorMA ( vec3_t veca , float scale , vec3_t vecb , vec3_t vecc ) ;

 vec_t _DotProduct ( vec3_t v1 , vec3_t v2 ) ;
 void _VectorSubtract ( vec3_t veca , vec3_t vecb , vec3_t out ) ;
 void _VectorAdd ( vec3_t veca , vec3_t vecb , vec3_t out ) ;
 void _VectorCopy ( vec3_t in , vec3_t out ) ;

 int VectorCompare ( vec3_t v1 , vec3_t v2 ) ;
 vec_t Length ( vec3_t v ) ;
 void CrossProduct ( vec3_t v1 , vec3_t v2 , vec3_t cross ) ;



 float VectorNormalize ( vec3_t v ) ;
 void VectorInverse ( vec3_t v ) ;
 void VectorScale ( vec3_t in , vec_t scale , vec3_t out ) ;
 int Q_log2 ( int val ) ;

 void R_ConcatRotations ( float in1 [ 3 ] [ 3 ] , float in2 [ 3 ] [ 3 ] , float out [ 3 ] [ 3 ] ) ;
 void R_ConcatTransforms ( float in1 [ 3 ] [ 4 ] , float in2 [ 3 ] [ 4 ] , float out [ 3 ] [ 4 ] ) ;

 void FloorDivMod ( double numer , double denom , int * quotient , int * rem ) ;
 fixed16_t Invert24To16 ( fixed16_t val ) ;
 int GreatestCommonDivisor ( int i1 , int i2 ) ;

 void AngleVectors ( vec3_t angles , vec3_t forward , vec3_t right , vec3_t up ) ;





 float anglemod ( float a ) ;


 void VectorVectors ( const vec3_t forward , vec3_t right , vec3_t up ) ;


 void BoxOnPlaneSideClassify ( struct mplane_s * p ) ;
#line 232 "quakedef.h"
 typedef struct
 {
 vec3_t origin ;
 vec3_t angles ;
 unsigned short modelindex ;
 unsigned short frame ;
 unsigned char colormap ;
 unsigned char skin ;
 unsigned char alpha ;
 int effects ;

 } entity_state_t ;
#line 39 "wad.h"
 typedef struct
 {
 int width , height ;
 byte data [ 4 ] ;
 } qpic_t ;



 typedef struct
 {
 char identification [ 4 ] ;
 int numlumps ;
 int infotableofs ;
 } wadinfo_t ;

 typedef struct
 {
 int filepos ;
 int disksize ;
 int size ;
 char type ;
 char compression ;
 char pad1 , pad2 ;
 char name [ 16 ] ;
 } lumpinfo_t ;

 extern int wad_numlumps ;
 extern lumpinfo_t * wad_lumps ;
 extern byte * wad_base ;

 void W_LoadWadFile ( char * filename ) ;
 void W_CleanupName ( char * in , char * out ) ;
 lumpinfo_t * W_GetLumpinfo ( char * name ) ;
 void * W_GetLumpName ( char * name ) ;
 void * W_GetLumpNum ( int num ) ;

 void SwapPic ( qpic_t * pic ) ;
#line 24 "draw.h"
 extern qpic_t * draw_disc ;

 void Draw_Init ( void ) ;
 void Draw_Character ( int x , int y , int num ) ;
 void Draw_DebugChar ( char num ) ;
 void Draw_Pic ( int x , int y , qpic_t * pic ) ;
 void Draw_TransPic ( int x , int y , qpic_t * pic ) ;
 void Draw_TransPicTranslate ( int x , int y , qpic_t * pic , byte * translation ) ;
 void Draw_ConsoleBackground ( int lines ) ;
 void Draw_BeginDisc ( void ) ;
 void Draw_EndDisc ( void ) ;
 void Draw_TileClear ( int x , int y , int w , int h ) ;
 void Draw_Fill ( int x , int y , int w , int h , int c ) ;
 void Draw_FadeScreen ( void ) ;
 void Draw_String ( int x , int y , char * str ) ;
 qpic_t * Draw_PicFromWad ( char * name ) ;
 qpic_t * Draw_CachePic ( char * path ) ;
#line 65 "cvar.h"
 typedef struct cvar_s
 {
 char * name ;
 char * string ;
 qboolean archive ;
 int server ;
 float value ;
 struct cvar_s * next ;

 } cvar_t ;

 void Cvar_RegisterVariable ( cvar_t * variable ) ;



 void Cvar_Set ( char * var_name , char * value ) ;


 void Cvar_SetValue ( char * var_name , float value ) ;


 float Cvar_VariableValue ( char * var_name ) ;


 char * Cvar_VariableString ( char * var_name ) ;


 char * Cvar_CompleteVariable ( char * partial ) ;



 qboolean Cvar_Command ( void ) ;




 void Cvar_WriteVariables ( FILE * f ) ;



 cvar_t * Cvar_FindVar ( char * var_name ) ;

 extern cvar_t * cvar_vars ;
#line 22 "screen.h"
 void SCR_Init ( void ) ;

 void SCR_UpdateScreen ( void ) ;


 void SCR_SizeUp ( void ) ;
 void SCR_SizeDown ( void ) ;
 void SCR_BringDownConsole ( void ) ;
 void SCR_CenterPrint ( char * str ) ;

 void SCR_BeginLoadingPlaque ( void ) ;
 void SCR_EndLoadingPlaque ( void ) ;

 int SCR_ModalMessage ( char * text ) ;

 extern float scr_con_current ;
 extern float scr_conlines ;

 extern int scr_fullupdate ;
 extern int sb_lines ;

 extern int clearnotify ;
 extern qboolean scr_disabled_for_loading ;
 extern qboolean scr_skipupdate ;

 extern cvar_t scr_viewsize ;

 extern cvar_t scr_viewsize ;


 extern int scr_copytop ;
 extern int scr_copyeverything ;

 extern qboolean block_drawing ;

 void SCR_UpdateWholeScreen ( void ) ;
#line 22 "net.h"
 struct qsockaddr
 {
 short sa_family ;
 unsigned char sa_data [ 14 ] ;
 } ;
































































































 typedef struct qsocket_s
 {
 struct qsocket_s * next ;
 double connecttime ;
 double lastMessageTime ;
 double lastSendTime ;

 qboolean disconnected ;
 qboolean canSend ;
 qboolean sendNext ;

 int driver ;
 int landriver ;
 int socket ;
 void * driverdata ;

 unsigned int ackSequence ;
 unsigned int sendSequence ;
 unsigned int unreliableSendSequence ;
 int sendMessageLength ;
 byte sendMessage [ 32000 ] ;

 unsigned int receiveSequence ;
 unsigned int unreliableReceiveSequence ;
 int receiveMessageLength ;
 byte receiveMessage [ 32000 ] ;

 struct qsockaddr addr ;
 char address [ 64 ] ;


 int mod ;

 } qsocket_t ;

 extern qsocket_t * net_activeSockets ;
 extern qsocket_t * net_freeSockets ;
 extern int net_numsockets ;

 typedef struct
 {
 char * name ;
 qboolean initialized ;
 int controlSock ;
 int ( * Init ) ( void ) ;
 void ( * Shutdown ) ( void ) ;
 void ( * Listen ) ( qboolean state ) ;
 int ( * OpenSocket ) ( int port ) ;
 int ( * CloseSocket ) ( int socket ) ;
 int ( * Connect ) ( int socket , struct qsockaddr * addr ) ;
 int ( * CheckNewConnections ) ( void ) ;
 int ( * Read ) ( int socket , byte * buf , int len , struct qsockaddr * addr ) ;
 int ( * Write ) ( int socket , byte * buf , int len , struct qsockaddr * addr ) ;
 int ( * Broadcast ) ( int socket , byte * buf , int len ) ;
 char * ( * AddrToString ) ( struct qsockaddr * addr ) ;
 int ( * StringToAddr ) ( char * string , struct qsockaddr * addr ) ;
 int ( * GetSocketAddr ) ( int socket , struct qsockaddr * addr ) ;
 int ( * GetNameFromAddr ) ( struct qsockaddr * addr , char * name ) ;
 int ( * GetAddrFromName ) ( char * name , struct qsockaddr * addr ) ;
 int ( * AddrCompare ) ( struct qsockaddr * addr1 , struct qsockaddr * addr2 ) ;
 int ( * GetSocketPort ) ( struct qsockaddr * addr ) ;
 int ( * SetSocketPort ) ( struct qsockaddr * addr , int port ) ;

 } net_landriver_t ;


 extern int net_numlandrivers ;
 extern net_landriver_t net_landrivers [ 8 ] ;

 typedef struct
 {
 char * name ;
 qboolean initialized ;
 int ( * Init ) ( void ) ;
 void ( * Listen ) ( qboolean state ) ;
 void ( * SearchForHosts ) ( qboolean xmit ) ;
 qsocket_t * ( * Connect ) ( char * host ) ;
 qsocket_t * ( * CheckNewConnections ) ( void ) ;
 int ( * QGetMessage ) ( qsocket_t * sock ) ;
 int ( * QSendMessage ) ( qsocket_t * sock , sizebuf_t * data ) ;
 int ( * SendUnreliableMessage ) ( qsocket_t * sock , sizebuf_t * data ) ;
 qboolean ( * CanSendMessage ) ( qsocket_t * sock ) ;
 qboolean ( * CanSendUnreliableMessage ) ( qsocket_t * sock ) ;
 void ( * Close ) ( qsocket_t * sock ) ;
 void ( * Shutdown ) ( void ) ;
 int controlSock ;

 } net_driver_t ;

 extern int net_numdrivers ;
 extern net_driver_t net_drivers [ 8 ] ;

 extern int DEFAULTnet_hostport ;
 extern int net_hostport ;

 extern int net_driverlevel ;
 extern cvar_t hostname ;
 extern char playername [ ] ;
 extern int playercolor ;

 extern int messagesSent ;
 extern int messagesReceived ;
 extern int unreliableMessagesSent ;
 extern int unreliableMessagesReceived ;

 qsocket_t * NET_NewQSocket ( void ) ;
 void NET_FreeQSocket ( qsocket_t * ) ;
 double SetNetTime ( void ) ;




 typedef struct
 {
 char name [ 16 ] ;
 char map [ 16 ] ;
 char cname [ 32 ] ;
 int users ;
 int maxusers ;
 int driver ;
 int ldriver ;
 struct qsockaddr addr ;

 } hostcache_t ;

 extern int hostCacheCount ;
 extern hostcache_t hostcache [ 8 ] ;



 extern unsigned long htonl ( unsigned long hostlong ) ;


 extern unsigned short htons ( unsigned short hostshort ) ;


 extern unsigned long ntohl ( unsigned long netlong ) ;


 extern unsigned short ntohs ( unsigned short netshort ) ;













 extern double net_time ;
 extern sizebuf_t net_message ;
 extern int net_activeconnections ;

 void NET_Init ( void ) ;
 void NET_Shutdown ( void ) ;

 struct qsocket_s * NET_CheckNewConnections ( void ) ;


 struct qsocket_s * NET_Connect ( char * host ) ;


 qboolean NET_CanSendMessage ( qsocket_t * sock ) ;



 int NET_GetMessage ( struct qsocket_s * sock ) ;






 int NET_SendMessage ( struct qsocket_s * sock , sizebuf_t * data ) ;
 int NET_SendUnreliableMessage ( struct qsocket_s * sock , sizebuf_t * data ) ;





 int NET_SendToAll ( sizebuf_t * data , int blocktime ) ;



 void NET_Close ( struct qsocket_s * sock ) ;








 void NET_Poll ( void ) ;


 typedef struct _PollProcedure
 {
 struct _PollProcedure * next ;
 double nextTime ;
 void ( * procedure ) ( ) ;
 void * arg ;

 } PollProcedure ;

 void SchedulePollProcedure ( PollProcedure * pp , double timeOffset ) ;

 extern qboolean serialAvailable ;
 extern qboolean ipxAvailable ;
 extern qboolean tcpipAvailable ;
 extern char my_ipx_address [ 64 ] ;
 extern char my_tcpip_address [ 64 ] ;
 extern void ( * GetComPortConfig ) ( int portNumber , int * port , int * irq , int * baud , qboolean * useModem ) ;
 extern void ( * SetComPortConfig ) ( int portNumber , int port , int irq , int baud , qboolean useModem ) ;
 extern void ( * GetModemConfig ) ( int portNumber , char * dialType , char * clear , char * init , char * hangup ) ;
 extern void ( * SetModemConfig ) ( int portNumber , char * dialType , char * clear , char * init , char * hangup ) ;

 extern qboolean slistInProgress ;
 extern qboolean slistSilent ;
 extern qboolean slistLocal ;

 void NET_Slist_f ( void ) ;
#line 38 "cmd.h"
 void Cbuf_Init ( void ) ;


 void Cbuf_AddText ( char * text ) ;



 void Cbuf_InsertText ( char * text ) ;




 void Cbuf_Execute ( void ) ;


















 typedef void ( * xcommand_t ) ( void ) ;

 typedef enum
 {
 src_client ,

 src_command

 } cmd_source_t ;

 extern cmd_source_t cmd_source ;

 void Cmd_Init ( void ) ;

 void Cmd_AddCommand ( char * cmd_name , xcommand_t function ) ;




 qboolean Cmd_Exists ( char * cmd_name ) ;


 char * Cmd_CompleteCommand ( char * partial ) ;



 int Cmd_Argc ( void ) ;
 char * Cmd_Argv ( int arg ) ;
 char * Cmd_Args ( void ) ;




 int Cmd_CheckParm ( char * parm ) ;



 void Cmd_TokenizeString ( char * text ) ;



 void Cmd_ExecuteString ( char * text , cmd_source_t src ) ;



 void Cmd_ForwardToServer ( void ) ;




 void Cmd_Print ( char * text ) ;
#line 26 "sbar.h"
 extern int sb_lines ;

 void Sbar_Init ( void ) ;

 void Sbar_Changed ( void ) ;


 void Sbar_Draw ( void ) ;


 void Sbar_IntermissionOverlay ( void ) ;


 void Sbar_FinaleOverlay ( void ) ;
#line 29 "sound.h"
 typedef struct
 {
 int left ;
 int right ;

 } portable_samplepair_t ;

 typedef struct sfx_s
 {
 char name [ 64 ] ;
 cache_user_t cache ;

 } sfx_t ;


 typedef struct
 {
 int length ;
 int loopstart ;
 int speed ;
 int width ;
 int stereo ;
 byte data [ 1 ] ;

 } sfxcache_t ;

 typedef struct
 {
 qboolean gamealive ;
 qboolean soundalive ;
 qboolean splitbuffer ;
 int channels ;
 int samples ;
 int submission_chunk ;
 int samplepos ;
 int samplebits ;
 int speed ;
 unsigned char * buffer ;

 } dma_t ;


 typedef struct
 {
 sfx_t * sfx ;
 int leftvol ;
 int rightvol ;
 int end ;
 int pos ;
 int looping ;
 int entnum ;
 int entchannel ;
 vec3_t origin ;
 vec_t dist_mult ;
 int master_vol ;

 } channel_t ;

 typedef struct
 {
 int rate ;
 int width ;
 int channels ;
 int loopstart ;
 int samples ;
 int dataofs ;

 } wavinfo_t ;

 void S_Init ( void ) ;
 void S_Startup ( void ) ;
 void S_Shutdown ( void ) ;
 void S_StartSound ( int entnum , int entchannel , sfx_t * sfx , vec3_t origin , float fvol , float attenuation ) ;
 void S_StaticSound ( sfx_t * sfx , vec3_t origin , float vol , float attenuation ) ;
 void S_StopSound ( int entnum , int entchannel ) ;
 void S_StopAllSounds ( qboolean clear ) ;
 void S_ClearBuffer ( void ) ;
 void S_Update ( vec3_t origin , vec3_t v_forward , vec3_t v_right , vec3_t v_up ) ;
 void S_ExtraUpdate ( void ) ;

 sfx_t * S_PrecacheSound ( char * sample ) ;
 void S_TouchSound ( char * sample ) ;
 void S_ClearPrecache ( void ) ;
 void S_BeginPrecaching ( void ) ;
 void S_EndPrecaching ( void ) ;
 void S_PaintChannels ( int endtime ) ;
 void S_InitPaintChannels ( void ) ;


 channel_t * SND_PickChannel ( int entnum , int entchannel ) ;


 void SND_Spatialize ( channel_t * ch ) ;


 qboolean SNDDMA_Init ( void ) ;


 int SNDDMA_GetDMAPos ( void ) ;


 void SNDDMA_Shutdown ( void ) ;








 extern channel_t channels [ 512 ] ;





 extern int total_channels ;







 extern qboolean fakedma ;
 extern int fakedma_updates ;
 extern int paintedtime ;
 extern vec3_t listener_origin ;
 extern vec3_t listener_forward ;
 extern vec3_t listener_right ;
 extern vec3_t listener_up ;
 extern volatile dma_t * shm ;
 extern volatile dma_t sn ;
 extern vec_t sound_nominal_clip_dist ;

 extern cvar_t loadas8bit ;
 extern cvar_t bgmvolume ;
 extern cvar_t volume ;

 extern qboolean snd_initialized ;

 extern int snd_blocked ;

 void S_LocalSound ( char * s ) ;
 sfxcache_t * S_LoadSound ( sfx_t * s ) ;

 wavinfo_t GetWavinfo ( char * name , byte * wav , int wavlength ) ;

 void SND_InitScaletable ( void ) ;
 void SNDDMA_Submit ( void ) ;

 void S_AmbientOff ( void ) ;
 void S_AmbientOn ( void ) ;
#line 30 "render.h"
 typedef struct efrag_s
 {

 struct efrag_s * leafnext ;
 struct entity_s * entity ;


 } efrag_t ;









 typedef struct entity_s
 {
 qboolean forcelink ;

 int update_type ;

 entity_state_t baseline ;

 double msgtime ;
 vec3_t msg_origins [ 2 ] ;
 vec3_t origin ;
 vec3_t msg_angles [ 2 ] ;
 vec3_t angles ;
 struct model_s * model ;
 struct efrag_s * efrag ;
 int frame ;
 float syncbase ;
 byte * colormap ;
 int effects ;
 int skinnum ;
 int visframe ;


 int dlightframe ;
 int dlightbits ;


 int trivial_accept ;
 struct mnode_s * topnode ;




 float frame_start_time ;
 float frame_interval ;
 int pose1 ;
 int pose2 ;


 float translate_start_time ;
 vec3_t origin1 ;
 vec3_t origin2 ;
 float rotate_start_time ;
 vec3_t angles1 ;
 vec3_t angles2 ;

 byte alpha ;















 } entity_t ;


 typedef struct
 {
 vrect_t vrect ;





















 vec3_t vieworg ;
 vec3_t viewangles ;

 float fov_x , fov_y ;





 } refdef_t ;





 extern int reinit_surfcache ;


 extern refdef_t r_refdef ;
 extern vec3_t r_origin , vpn , vright , vup ;

 extern struct texture_s * r_notexture_mip ;


 void R_Init ( void ) ;
 void R_InitTextures ( void ) ;
 void R_InitEfrags ( void ) ;
 void R_RenderView ( void ) ;
 void R_ViewChanged ( vrect_t * pvrect , int lineadj , float aspect ) ;


 void R_InitSky ( miptex_t * mt ) ;

 void R_CheckEfrags ( void ) ;
 void R_AddEfrags ( entity_t * ent ) ;
 void R_RemoveEfrags ( entity_t * ent ) ;

 void R_NewMap ( void ) ;


 void R_ParseParticleEffect ( void ) ;
 void R_RunParticleEffect ( vec3_t org , vec3_t dir , int color , int count ) ;
 void R_RocketTrail ( vec3_t start , vec3_t end , int type ) ;





 void R_EntityParticles ( entity_t * ent ) ;
 void R_BlobExplosion ( vec3_t org ) ;
 void R_ParticleExplosion ( vec3_t org ) ;
 void R_ParticleExplosion2 ( vec3_t org , int colorStart , int colorLength ) ;
 void R_LavaSplash ( vec3_t org ) ;
 void R_TeleportSplash ( vec3_t org ) ;

 void R_PushDlights ( void ) ;






 extern int reinit_surfcache ;
 extern qboolean r_cache_thrash ;

 int D_SurfaceCacheForRes ( int width , int height ) ;
 void D_FlushCaches ( void ) ;
 void D_DeleteSurfaceCache ( void ) ;
 void D_InitCaches ( void * buffer , int size ) ;
 void R_SetVrect ( vrect_t * pvrect , vrect_t * pvrectin , int lineadj ) ;
#line 23 "client.h"
 typedef struct
 {
 vec3_t viewangles ;


 float forwardmove ;
 float sidemove ;
 float upmove ;





 } usercmd_t ;

 typedef struct
 {
 int length ;
 char map [ 64 ] ;
 char average ;
 char peak ;

 } lightstyle_t ;

 typedef struct
 {
 char name [ 32 ] ;
 float entertime ;
 int frags ;
 int colors ;
 int ping ;
 byte translations [ ( 1 << 6 ) * 256 ] ;

 } scoreboard_t ;


 typedef struct
 {
 int colors ;
 int frags ;

 } teamscore_t ;

 typedef struct
 {
 int destcolor [ 3 ] ;
 int percent ;

 } cshift_t ;


















 typedef struct
 {
 vec3_t origin ;
 float radius ;
 float die ;
 float decay ;
 float minlight ;
 int key ;






 vec3_t color ;


 } dlight_t ;




 typedef struct
 {
 int entity ;
 struct model_s * model ;
 float endtime ;
 vec3_t start , end ;

 } beam_t ;







 typedef enum
 {
 ca_dedicated ,
 ca_disconnected ,
 ca_connected

 } cactive_t ;






 typedef struct
 {
 cactive_t state ;



 char mapstring [ 64 ] ;
 char spawnparms [ 2048 ] ;


 int demonum ;
 char demos [ 8 ] [ 16 ] ;




 qboolean demorecording ;
 qboolean demoplayback ;
 qboolean timedemo ;

 int forcetrack ;
 FILE * demofile ;
 int td_lastframe ;
 int td_startframe ;
 float td_starttime ;



 int signon ;
 struct qsocket_s * netcon ;
 sizebuf_t message ;

 } client_static_t ;

 extern client_static_t cls ;






 typedef struct
 {
 int movemessages ;




 usercmd_t cmd ;



 int stats [ 32 ] ;
 int items ;
 float item_gettime [ 32 ] ;
 float faceanimtime ;

 cshift_t cshifts [ 4 ] ;
 cshift_t prev_cshifts [ 4 ] ;






 vec3_t mviewangles [ 2 ] ;
 vec3_t viewangles ;

 vec3_t mvelocity [ 2 ] ;
 vec3_t velocity ;

 vec3_t punchangle ;



 float idealpitch ;
 float pitchvel ;
 qboolean nodrift ;
 float driftmove ;
 double laststop ;

 float viewheight ;
 float crouch ;

 qboolean paused ;
 qboolean onground ;
 qboolean inwater ;

 int intermission ;
 int completed_time ;

 double mtime [ 2 ] ;
 double time ;



 double oldtime ;


 float last_received_message ;





 struct model_s * model_precache [ 2048 ] ;
 struct sfx_s * sound_precache [ 2048 ] ;

 char levelname [ 128 ] ;
 int viewentity ;
 int maxclients ;
 int gametype ;



 struct model_s * worldmodel ;
 struct efrag_s * free_efrags ;

 int num_efrags ;

 int num_entities ;
 int num_statics ;
 entity_t viewent ;

 int cdtrack , looptrack ;



 scoreboard_t * scores ;
 teamscore_t * teamscores ;
 qboolean teamgame ;
 int minutes ;
 int seconds ;
 double last_match_time ;
 double last_ping_time ;
 qboolean sbar_ping ;
 double match_pause_time ;
 vec3_t lerpangles ;

 unsigned protocol ;








 } client_state_t ;






 extern cvar_t cl_name ;
 extern cvar_t cl_color ;

 extern cvar_t cl_upspeed ;
 extern cvar_t cl_forwardspeed ;
 extern cvar_t cl_backspeed ;
 extern cvar_t cl_sidespeed ;

 extern cvar_t cl_movespeedkey ;

 extern cvar_t cl_yawspeed ;
 extern cvar_t cl_pitchspeed ;

 extern cvar_t cl_anglespeedkey ;

 extern cvar_t cl_autofire ;

 extern cvar_t cl_shownet ;
 extern cvar_t cl_nolerp ;

 extern cvar_t cl_pitchdriftspeed ;
 extern cvar_t lookspring ;
 extern cvar_t lookstrafe ;
 extern cvar_t sensitivity ;

 extern cvar_t m_pitch ;
 extern cvar_t m_yaw ;
 extern cvar_t m_forward ;
 extern cvar_t m_side ;






 extern client_state_t cl ;





 extern entity_t cl_static_entities [ 4096 ] ;
 extern lightstyle_t cl_lightstyle [ 64 ] ;
 extern dlight_t cl_dlights [ 64 ] ;
 extern entity_t cl_temp_entities [ 256 ] ;
 extern beam_t cl_beams [ 32 ] ;
 extern int cl_numvisedicts ;
 extern entity_t * cl_visedicts [ 4096 ] ;

 extern entity_t * cl_entities ;
 extern int cl_max_edicts ;







 dlight_t * CL_AllocDlight ( int key ) ;
 void CL_DecayLights ( void ) ;

 void CL_Init ( void ) ;

 void CL_EstablishConnection ( char * host ) ;
 void CL_Signon1 ( void ) ;
 void CL_Signon2 ( void ) ;
 void CL_Signon3 ( void ) ;
 void CL_Signon4 ( void ) ;

 void CL_Disconnect ( void ) ;
 void CL_Disconnect_f ( void ) ;
 void CL_NextDemo ( void ) ;






 typedef struct
 {
 int down [ 2 ] ;
 int state ;

 } kbutton_t ;

 extern kbutton_t in_mlook , in_klook ;
 extern kbutton_t in_strafe ;
 extern kbutton_t in_speed ;
 extern kbutton_t in_attack ;


 void CL_InitInput ( void ) ;
 void CL_SendCmd ( void ) ;
 void CL_SendMove ( usercmd_t * cmd ) ;
 void CL_SendLagMove ( void ) ;

 void CL_ParseTEnt ( void ) ;
 void CL_UpdateTEnts ( void ) ;

 void CL_ClearState ( void ) ;


 int CL_ReadFromServer ( void ) ;
 void CL_WriteToServer ( usercmd_t * cmd ) ;
 void CL_BaseMove ( usercmd_t * cmd ) ;


 float CL_KeyState ( kbutton_t * key ) ;
 char * Key_KeynumToString ( int keynum ) ;





 void CL_StopPlayback ( void ) ;
 int CL_GetMessage ( void ) ;

 void CL_Stop_f ( void ) ;
 void CL_Record_f ( void ) ;
 void CL_PlayDemo_f ( void ) ;
 void CL_TimeDemo_f ( void ) ;





 void CL_ParseServerMessage ( void ) ;
 void CL_NewTranslation ( int slot ) ;





 void V_StartPitchDrift ( void ) ;
 void V_StopPitchDrift ( void ) ;

 void V_RenderView ( void ) ;
 void V_UpdatePalette ( void ) ;
 void V_Register ( void ) ;
 void V_ParseDamage ( void ) ;
 void V_SetContentsColor ( int contents ) ;






 void CL_InitTEnts ( void ) ;
 void CL_SignonReply ( void ) ;
#line 23 "pr_comp.h"
 typedef int func_t ;
 typedef int string_t ;

 typedef enum { ev_void = 0 , ev_string , ev_float , ev_vector , ev_entity , ev_field , ev_function , ev_pointer } etype_t ;















 enum
 {
 OP_DONE ,
 OP_MUL_F ,
 OP_MUL_V ,
 OP_MUL_FV ,
 OP_MUL_VF ,
 OP_DIV_F ,
 OP_ADD_F ,
 OP_ADD_V ,
 OP_SUB_F ,
 OP_SUB_V ,

 OP_EQ_F ,
 OP_EQ_V ,
 OP_EQ_S ,
 OP_EQ_E ,
 OP_EQ_FNC ,

 OP_NE_F ,
 OP_NE_V ,
 OP_NE_S ,
 OP_NE_E ,
 OP_NE_FNC ,

 OP_LE ,
 OP_GE ,
 OP_LT ,
 OP_GT ,

 OP_LOAD_F ,
 OP_LOAD_V ,
 OP_LOAD_S ,
 OP_LOAD_ENT ,
 OP_LOAD_FLD ,
 OP_LOAD_FNC ,

 OP_ADDRESS ,

 OP_STORE_F ,
 OP_STORE_V ,
 OP_STORE_S ,
 OP_STORE_ENT ,
 OP_STORE_FLD ,
 OP_STORE_FNC ,

 OP_STOREP_F ,
 OP_STOREP_V ,
 OP_STOREP_S ,
 OP_STOREP_ENT ,
 OP_STOREP_FLD ,
 OP_STOREP_FNC ,

 OP_RETURN ,
 OP_NOT_F ,
 OP_NOT_V ,
 OP_NOT_S ,
 OP_NOT_ENT ,
 OP_NOT_FNC ,
 OP_IF ,
 OP_IFNOT ,
 OP_CALL0 ,
 OP_CALL1 ,
 OP_CALL2 ,
 OP_CALL3 ,
 OP_CALL4 ,
 OP_CALL5 ,
 OP_CALL6 ,
 OP_CALL7 ,
 OP_CALL8 ,
 OP_STATE ,
 OP_GOTO ,
 OP_AND ,
 OP_OR ,

 OP_BITAND ,
 OP_BITOR
 } ;


 typedef struct statement_s
 {
 unsigned short op ;
 short a , b , c ;

 } dstatement_t ;

 typedef struct
 {
 unsigned short type ;

 unsigned short ofs ;
 int s_name ;

 } ddef_t ;





 typedef struct
 {
 int first_statement ;
 int parm_start ;
 int locals ;

 int profile ;

 int s_name ;
 int s_file ;

 int numparms ;
 byte parm_size [ 8 ] ;

 } dfunction_t ;




 typedef struct
 {
 int version ;
 int crc ;

 int ofs_statements ;
 int numstatements ;

 int ofs_globaldefs ;
 int numglobaldefs ;

 int ofs_fielddefs ;
 int numfielddefs ;

 int ofs_functions ;
 int numfunctions ;

 int ofs_strings ;
 int numstrings ;

 int ofs_globals ;
 int numglobals ;

 int entityfields ;

 } dprograms_t ;
#line 4 "progdefs.q1"
 typedef struct
 { int pad [ 28 ] ;
 int self ;
 int other ;
 int world ;
 float time ;
 float frametime ;
 float force_retouch ;
 string_t mapname ;
 float deathmatch ;
 float coop ;
 float teamplay ;
 float serverflags ;
 float total_secrets ;
 float total_monsters ;
 float found_secrets ;
 float killed_monsters ;
 float parm1 ;
 float parm2 ;
 float parm3 ;
 float parm4 ;
 float parm5 ;
 float parm6 ;
 float parm7 ;
 float parm8 ;
 float parm9 ;
 float parm10 ;
 float parm11 ;
 float parm12 ;
 float parm13 ;
 float parm14 ;
 float parm15 ;
 float parm16 ;
 vec3_t v_forward ;
 vec3_t v_up ;
 vec3_t v_right ;
 float trace_allsolid ;
 float trace_startsolid ;
 float trace_fraction ;
 vec3_t trace_endpos ;
 vec3_t trace_plane_normal ;
 float trace_plane_dist ;
 int trace_ent ;
 float trace_inopen ;
 float trace_inwater ;
 int msg_entity ;
 func_t main ;
 func_t StartFrame ;
 func_t PlayerPreThink ;
 func_t PlayerPostThink ;
 func_t ClientKill ;
 func_t ClientConnect ;
 func_t PutClientInServer ;
 func_t ClientDisconnect ;
 func_t SetNewParms ;
 func_t SetChangeParms ;
 } globalvars_t ;

 typedef struct
 {
 float modelindex ;
 vec3_t absmin ;
 vec3_t absmax ;
 float ltime ;
 float movetype ;
 float solid ;
 vec3_t origin ;
 vec3_t oldorigin ;
 vec3_t velocity ;
 vec3_t angles ;
 vec3_t avelocity ;
 vec3_t punchangle ;
 string_t classname ;
 string_t model ;
 float frame ;
 float skin ;
 float effects ;
 vec3_t mins ;
 vec3_t maxs ;
 vec3_t size ;
 func_t touch ;
 func_t use ;
 func_t think ;
 func_t blocked ;
 float nextthink ;
 int groundentity ;
 float health ;
 float frags ;
 float weapon ;
 string_t weaponmodel ;
 float weaponframe ;
 float currentammo ;
 float ammo_shells ;
 float ammo_nails ;
 float ammo_rockets ;
 float ammo_cells ;
 float items ;
 float takedamage ;
 int chain ;
 float deadflag ;
 vec3_t view_ofs ;
 float button0 ;
 float button1 ;
 float button2 ;
 float impulse ;
 float fixangle ;
 vec3_t v_angle ;
 float idealpitch ;
 string_t netname ;
 int enemy ;
 float flags ;
 float colormap ;
 float team ;
 float max_health ;
 float teleport_time ;
 float armortype ;
 float armorvalue ;
 float waterlevel ;
 float watertype ;
 float ideal_yaw ;
 float yaw_speed ;
 int aiment ;
 int goalentity ;
 float spawnflags ;
 string_t target ;
 string_t targetname ;
 float dmg_take ;
 float dmg_save ;
 int dmg_inflictor ;
 int owner ;
 vec3_t movedir ;
 string_t message ;
 float sounds ;
 string_t noise ;
 string_t noise1 ;
 string_t noise2 ;
 string_t noise3 ;
 } entvars_t ;
#line 24 "progs.h"
 typedef union eval_s
 {
 string_t string ;
 float _float ;
 float vector [ 3 ] ;
 func_t function ;
 int _int ;
 int edict ;

 } eval_t ;



 typedef struct edict_s
 {
 qboolean free ;
 link_t area ;

 int num_leafs ;
 short leafnums [ 32 ] ;

 entity_state_t baseline ;
 unsigned char alpha ;
 qboolean sendinterval ;

 float freetime ;
 entvars_t v ;


 } edict_t ;







 extern int eval_gravity ;
 extern int eval_button3 ;
 extern int eval_button4 ;
 extern int eval_button5 ;
 extern int eval_button6 ;
 extern int eval_button7 ;
 extern int eval_button8 ;
 extern int eval_glow_size ;
 extern int eval_glow_trail ;
 extern int eval_glow_color ;
 extern int eval_items2 ;
 extern int eval_scale ;
 extern int eval_alpha ;
 extern int eval_renderamt ;
 extern int eval_rendermode ;
 extern int eval_fullbright ;
 extern int eval_ammo_shells1 ;
 extern int eval_ammo_nails1 ;
 extern int eval_ammo_lava_nails ;
 extern int eval_ammo_rockets1 ;
 extern int eval_ammo_multi_rockets ;
 extern int eval_ammo_cells1 ;
 extern int eval_ammo_plasma ;
 extern int eval_idealpitch ;
 extern int eval_pitch_speed ;
 extern int eval_viewmodelforclient ;
 extern int eval_nodrawtoclient ;
 extern int eval_exteriormodeltoclient ;
 extern int eval_drawonlytoclient ;
 extern int eval_colormod ;
 extern int eval_ping ;
 extern int eval_movement ;
 extern int eval_pmodel ;
 extern int eval_punchvector ;



 extern dprograms_t * progs ;
 extern dfunction_t * pr_functions ;
 extern char * pr_strings ;
 extern ddef_t * pr_globaldefs ;
 extern ddef_t * pr_fielddefs ;
 extern dstatement_t * pr_statements ;
 extern globalvars_t * pr_global_struct ;
 extern float * pr_globals ;
 extern int pr_edict_size ;



 void PR_Init ( void ) ;

 void PR_ExecuteProgram ( func_t fnum ) ;
 void PR_LoadProgs ( void ) ;

 void PR_Profile_f ( void ) ;

 edict_t * ED_Alloc ( void ) ;
 void ED_Free ( edict_t * ed ) ;

 char * ED_NewString ( char * string ) ;


 void ED_Print ( edict_t * ed ) ;
 void ED_Write ( FILE * f , edict_t * ed ) ;
 char * ED_ParseEdict ( char * data , edict_t * ent ) ;

 void ED_WriteGlobals ( FILE * f ) ;
 void ED_ParseGlobals ( char * data ) ;

 void ED_LoadFromFile ( char * data ) ;




 edict_t * EDICT_NUM ( int n ) ;
 int NUM_FOR_EDICT ( edict_t * e ) ;




















 extern int type_size [ 8 ] ;

 typedef void ( * builtin_t ) ( void ) ;
 extern builtin_t * pr_builtins ;
 extern int pr_numbuiltins ;

 extern int pr_argc ;

 extern qboolean pr_trace ;
 extern dfunction_t * pr_xfunction ;
 extern int pr_xstatement ;

 extern unsigned short pr_crc ;

 void PR_RunError ( char * error , ... ) ;

 void ED_PrintEdicts ( void ) ;
 void ED_PrintNum ( int ent ) ;

 eval_t * GetEdictFieldValue ( edict_t * ed , char * field ) ;
#line 22 "server.h"
 typedef struct
 {
 int maxclients ;
 int maxclientslimit ;
 struct client_s * clients ;
 int serverflags ;
 qboolean changelevel_issued ;

 } server_static_t ;



 typedef enum { ss_loading , ss_active } server_state_t ;

 typedef struct
 {
 qboolean active ;

 qboolean paused ;
 qboolean loadgame ;

 double time ;

 int lastcheck ;
 double lastchecktime ;

 char name [ 64 ] ;





 char modelname [ 64 ] ;
 struct model_s * worldmodel ;
 char * model_precache [ 2048 ] ;
 struct model_s * models [ 2048 ] ;
 char * sound_precache [ 2048 ] ;
 char * lightstyles [ 64 ] ;
 int num_edicts ;
 int max_edicts ;
 edict_t * edicts ;


 server_state_t state ;

 sizebuf_t datagram ;
 byte datagram_buf [ 32000 ] ;

 sizebuf_t reliable_datagram ;
 byte reliable_datagram_buf [ 32000 ] ;

 sizebuf_t signon ;
 byte signon_buf [ 32000 - 2 ] ;

 unsigned protocol ;

 } server_t ;





 typedef struct client_s
 {
 qboolean active ;
 qboolean spawned ;
 qboolean dropasap ;
 qboolean privileged ;
 qboolean sendsignon ;

 double last_message ;


 struct qsocket_s * netconnection ;

 usercmd_t cmd ;
 vec3_t wishdir ;

 sizebuf_t message ;

 byte msgbuf [ 32000 ] ;
 edict_t * edict ;
 char name [ 32 ] ;
 int colors ;

 float ping_times [ 16 ] ;
 int num_pings ;


 float spawn_parms [ 16 ] ;


 int old_frags ;

 } client_t ;


























































































 extern cvar_t teamplay ;
 extern cvar_t skill ;
 extern cvar_t deathmatch ;
 extern cvar_t coop ;
 extern cvar_t fraglimit ;
 extern cvar_t timelimit ;

 extern server_static_t svs ;
 extern server_t sv ;

 extern client_t * host_client ;

 extern jmp_buf host_abortserver ;

 extern double host_time ;

 extern edict_t * sv_player ;



 void SV_Init ( void ) ;

 void SV_StartParticle ( vec3_t org , vec3_t dir , int color , int count ) ;
 void SV_StartSound ( edict_t * entity , int channel , char * sample , int volume , float attenuation ) ;

 void SV_DropClient ( qboolean crash ) ;

 void SV_SendClientMessages ( void ) ;
 void SV_ClearDatagram ( void ) ;

 int SV_ModelIndex ( char * name ) ;

 void SV_SetIdealPitch ( void ) ;

 void SV_AddUpdates ( void ) ;

 void SV_ClientThink ( void ) ;
 void SV_AddClientToServer ( struct qsocket_s * ret ) ;

 void SV_ClientPrintf ( char * fmt , ... ) ;
 void SV_BroadcastPrintf ( char * fmt , ... ) ;

 void SV_Physics ( void ) ;

 qboolean SV_CheckBottom ( edict_t * ent ) ;
 qboolean SV_movestep ( edict_t * ent , vec3_t move , qboolean relink ) ;

 void SV_WriteClientdataToMessage ( edict_t * ent , sizebuf_t * msg ) ;

 void SV_MoveToGoal ( void ) ;

 void SV_CheckForNewClients ( void ) ;
 void SV_RunClients ( void ) ;
 void SV_SaveSpawnparms ( ) ;




 void SV_SpawnServer ( char * server ) ;
#line 52 "modelgen.h"
 typedef enum { ST_SYNC = 0 , ST_RAND } synctype_t ;


 typedef enum { ALIAS_SINGLE = 0 , ALIAS_GROUP } aliasframetype_t ;

 typedef enum { ALIAS_SKIN_SINGLE = 0 , ALIAS_SKIN_GROUP } aliasskintype_t ;

 typedef struct {
 int ident ;
 int version ;
 vec3_t scale ;
 vec3_t scale_origin ;
 float boundingradius ;
 vec3_t eyeposition ;
 int numskins ;
 int skinwidth ;
 int skinheight ;
 int numverts ;
 int numtris ;
 int numframes ;
 synctype_t synctype ;
 int flags ;
 float size ;
 } mdl_t ;



 typedef struct {
 int onseam ;
 int s ;
 int t ;
 } stvert_t ;

 typedef struct dtriangle_s {
 int facesfront ;
 int vertindex [ 3 ] ;
 } dtriangle_t ;






 typedef struct {
 byte v [ 3 ] ;
 byte lightnormalindex ;
 } trivertx_t ;

 typedef struct {
 trivertx_t bboxmin ;
 trivertx_t bboxmax ;
 char name [ 16 ] ;
 } daliasframe_t ;

 typedef struct {
 int numframes ;
 trivertx_t bboxmin ;
 trivertx_t bboxmax ;
 } daliasgroup_t ;

 typedef struct {
 int numskins ;
 } daliasskingroup_t ;

 typedef struct {
 float interval ;
 } daliasinterval_t ;

 typedef struct {
 float interval ;
 } daliasskininterval_t ;

 typedef struct {
 aliasframetype_t type ;
 } daliasframetype_t ;

 typedef struct {
 aliasskintype_t type ;
 } daliasskintype_t ;
#line 70 "spritegn.h"
 typedef struct {
 int ident ;
 int version ;
 int type ;
 float boundingradius ;
 int width ;
 int height ;
 int numframes ;
 float beamlength ;
 synctype_t synctype ;
 } dsprite_t ;







 typedef struct {
 int origin [ 2 ] ;
 int width ;
 int height ;
 } dspriteframe_t ;

 typedef struct {
 int numframes ;
 } dspritegroup_t ;

 typedef struct {
 float interval ;
 } dspriteinterval_t ;

 typedef enum { SPR_SINGLE = 0 , SPR_GROUP } spriteframetype_t ;

 typedef struct {
 spriteframetype_t type ;
 } dspriteframetype_t ;
#line 55 "gl_model.h"
 typedef struct
 {
 vec3_t position ;

 } mvertex_t ;










 struct mplane_s ;
 typedef int ( * BOPSfunc ) ( vec3_t emins , vec3_t emaxs , struct mplane_s * p ) ;



 typedef struct mplane_s
 {
 vec3_t normal ;
 float dist ;
 byte type ;
 byte signbits ;
 byte pad [ 2 ] ;


 BOPSfunc BoxOnPlaneSideFunc ;


 } mplane_t ;

 typedef struct texture_s
 {
 char name [ 16 ] ;
 unsigned width , height ;
 int gl_texturenum ;
 struct msurface_s * texturechain ;
 int anim_total ;
 int anim_min , anim_max ;
 struct texture_s * anim_next ;
 struct texture_s * alternate_anims ;
 unsigned offsets [ 4 ] ;

 } texture_t ;


















 typedef struct
 {
 unsigned int v [ 2 ] ;
 unsigned int cachededgeoffset ;

 } medge_t ;

 typedef struct
 {
 float vecs [ 2 ] [ 4 ] ;
 float mipadjust ;
 texture_t * texture ;
 int flags ;

 } mtexinfo_t ;



 typedef struct glpoly_s
 {
 struct glpoly_s * next ;
 struct glpoly_s * chain ;
 int numverts ;
 int flags ;
 float verts [ 4 ] [ 7 ] ;

 } glpoly_t ;

 typedef struct msurface_s
 {
 int visframe ;


 float mins [ 3 ] ;
 float maxs [ 3 ] ;

 mplane_t * plane ;
 int flags ;

 int firstedge ;
 int numedges ;

 short texturemins [ 2 ] ;
 short extents [ 2 ] ;

 int light_s , light_t ;

 glpoly_t * polys ;
 struct msurface_s * texturechain ;

 mtexinfo_t * texinfo ;


 int dlightframe , lightframe ;

 unsigned int dlightbits [ ( 64 + 31 ) >> 5 ] ;

 int lightmaptexturenum ;
 byte styles [ 4 ] ;
 int cached_light [ 4 ] ;
 qboolean cached_dlight ;
 byte * samples ;

 } msurface_t ;

 typedef struct mnode_s
 {

 int contents ;
 int visframe ;


 vec3_t mins ;
 vec3_t maxs ;

 struct mnode_s * parent ;


 mplane_t * plane ;
 struct mnode_s * children [ 2 ] ;

 unsigned int firstsurface ;
 unsigned int numsurfaces ;

 } mnode_t ;


 typedef struct mleaf_s
 {

 int contents ;
 int visframe ;


 vec3_t mins ;
 vec3_t maxs ;

 struct mnode_s * parent ;


 byte * compressed_vis ;
 efrag_t * efrags ;

 msurface_t * * firstmarksurface ;
 int nummarksurfaces ;
 int key ;
 byte ambient_sound_level [ 4 ] ;

 } mleaf_t ;



 typedef struct mclipnode_s
 {
 int planenum ;
 int children [ 2 ] ;

 } mclipnode_t ;



 typedef struct
 {
 mclipnode_t * clipnodes ;
 mplane_t * planes ;
 int firstclipnode ;
 int lastclipnode ;
 vec3_t clip_mins ;
 vec3_t clip_maxs ;
 int available ;

 } hull_t ;











 typedef struct mspriteframe_s
 {
 int width ;
 int height ;
 float up , down , left , right ;
 float smax , tmax ;
 int gl_texturenum ;

 } mspriteframe_t ;

 typedef struct
 {
 int numframes ;
 float * intervals ;
 mspriteframe_t * frames [ 1 ] ;

 } mspritegroup_t ;

 typedef struct
 {
 spriteframetype_t type ;
 mspriteframe_t * frameptr ;

 } mspriteframedesc_t ;

 typedef struct
 {
 int type ;
 int maxwidth ;
 int maxheight ;
 int numframes ;
 float beamlength ;
 void * cachespot ;
 mspriteframedesc_t frames [ 1 ] ;

 } msprite_t ;











 typedef struct
 {
 int firstpose ;
 int numposes ;
 float interval ;
 trivertx_t bboxmin ;
 trivertx_t bboxmax ;
 int frame ;
 char name [ 16 ] ;

 } maliasframedesc_t ;

 typedef struct
 {
 trivertx_t bboxmin ;
 trivertx_t bboxmax ;
 int frame ;

 } maliasgroupframedesc_t ;

 typedef struct
 {
 int numframes ;
 int intervals ;
 maliasgroupframedesc_t frames [ 1 ] ;

 } maliasgroup_t ;



 typedef struct mtriangle_s
 {
 int facesfront ;
 int vertindex [ 3 ] ;

 } mtriangle_t ;




 typedef struct
 {
 int ident ;
 int version ;
 vec3_t scale ;
 vec3_t scale_origin ;
 float boundingradius ;
 vec3_t eyeposition ;
 int numskins ;
 int skinwidth ;
 int skinheight ;
 int numverts ;
 int numtris ;
 int numframes ;
 synctype_t synctype ;
 int flags ;
 float size ;

 int numposes ;
 int poseverts ;
 int posedata ;
 int commands ;
 int gl_texturenum [ 32 ] [ 4 ] ;
 int texels [ 32 ] ;
 maliasframedesc_t frames [ 1 ] ;








 } aliashdr_t ;




 extern aliashdr_t * pheader ;
 extern stvert_t stverts [ 2000 ] ;
 extern mtriangle_t triangles [ 2048 ] ;
 extern trivertx_t * poseverts [ 256 ] ;







 typedef enum { mod_brush , mod_sprite , mod_alias } modtype_t ;



 typedef enum { MOD_NORMAL , MOD_PLAYER , MOD_EYES , MOD_FLAME , MOD_FLAME2 ,
 MOD_THUNDERBOLT , MOD_THUNDERBOLT2 , MOD_SPIKE ,
 MOD_LIGHT , MOD_QUADDAMA , MOD_INVULNER } modhint_t ;











 typedef struct model_s
 {
 char name [ 64 ] ;
 qboolean needload ;

 modtype_t type ;
 int numframes ;
 synctype_t synctype ;

 int flags ;




 vec3_t mins , maxs ;
 vec3_t ymins , ymaxs ;
 vec3_t rmins , rmaxs ;
 float radius ;




 qboolean clipbox ;
 vec3_t clipmins , clipmaxs ;




 int firstmodelsurface , nummodelsurfaces ;

 int numsubmodels ;
 dmodel_t * submodels ;

 int numplanes ;
 mplane_t * planes ;

 int numleafs ;
 mleaf_t * leafs ;

 int numvertexes ;
 mvertex_t * vertexes ;

 int numedges ;
 medge_t * edges ;

 int numnodes ;
 mnode_t * nodes ;

 int numtexinfo ;
 mtexinfo_t * texinfo ;

 int numsurfaces ;
 msurface_t * surfaces ;

 int numsurfedges ;
 int * surfedges ;

 int numclipnodes ;
 mclipnode_t * clipnodes ;

 int nummarksurfaces ;
 msurface_t * * marksurfaces ;

 hull_t hulls [ 4 ] ;

 int numtextures ;
 texture_t * * textures ;

 byte * visdata ;
 byte * lightdata ;
 char * entities ;

 int bspversion ;

 modhint_t modhint ;

 qboolean isworldmodel ;






 cache_user_t cache ;

 } model_t ;



 void Mod_Init ( void ) ;
 void Mod_ClearAll ( void ) ;
 model_t * Mod_ForName ( char * name , qboolean crash ) ;
 void * Mod_Extradata ( model_t * mod ) ;
 void Mod_TouchModel ( char * name ) ;

 mleaf_t * Mod_PointInLeaf ( float * p , model_t * model ) ;
 byte * Mod_LeafPVS ( mleaf_t * leaf , model_t * model ) ;
#line 22 "input.h"
 void IN_Init ( void ) ;

 void IN_Shutdown ( void ) ;

 void IN_Commands ( void ) ;


 void IN_Move ( usercmd_t * cmd ) ;


 void IN_ClearStates ( void ) ;
#line 22 "world.h"
 typedef struct
 {
 vec3_t normal ;
 float dist ;
 } plane_t ;

 typedef struct
 {
 qboolean allsolid ;
 qboolean startsolid ;
 qboolean inopen , inwater ;
 float fraction ;
 vec3_t endpos ;
 plane_t plane ;
 edict_t * ent ;
 } trace_t ;







 void SV_ClearWorld ( void ) ;


 void SV_UnlinkEdict ( edict_t * ent ) ;




 void SV_LinkEdict ( edict_t * ent , qboolean touch_triggers ) ;





 int SV_PointContents ( vec3_t p ) ;
 int SV_TruePointContents ( vec3_t p ) ;




 edict_t * SV_TestEntityPosition ( edict_t * ent ) ;

 trace_t SV_Move ( vec3_t start , vec3_t mins , vec3_t maxs , vec3_t end , int type , edict_t * passedict ) ;
#line 120 "keys.h"
 typedef enum { key_game , key_console , key_message , key_menu } keydest_t ;

 extern keydest_t key_dest ;
 extern char * keybindings [ 256 ] ;
 extern int key_repeats [ 256 ] ;
 extern int key_count ;
 extern int key_lastpress ;


 void Key_Event ( int key , qboolean down , int rawkey ) ;
 void Key_Init ( void ) ;
 void Key_WriteBindings ( FILE * f ) ;
 void Key_SetBinding ( int keynum , char * binding ) ;
 void Key_ClearStates ( void ) ;
#line 24 "console.h"
 extern int con_totallines ;
 extern int con_backscroll ;
 extern qboolean con_forcedup ;
 extern qboolean con_initialized ;
 extern byte * con_chars ;
 extern int con_notifylines ;

 void Con_DrawCharacter ( int cx , int line , int num ) ;

 void Con_CheckResize ( void ) ;
 void Con_Init ( void ) ;
 void Con_DrawConsole ( int lines , qboolean drawinput ) ;
 void Con_Print ( char * txt ) ;
 void Con_Printf ( char * fmt , ... ) ;
 void Con_DPrintf ( char * fmt , ... ) ;
 void Con_SafePrintf ( char * fmt , ... ) ;
 void Con_Clear_f ( void ) ;
 void Con_DrawNotify ( void ) ;
 void Con_ClearNotify ( void ) ;
 void Con_ToggleConsole_f ( void ) ;

 void Con_NotifyBox ( char * text ) ;
#line 22 "view.h"
 extern cvar_t v_gamma ;

 extern byte gammatable [ 256 ] ;
 extern byte ramps [ 3 ] [ 256 ] ;
 extern float v_blend [ 4 ] ;

 extern cvar_t lcd_x ;


 void V_Init ( void ) ;
 void V_RenderView ( void ) ;
 float V_CalcRoll ( vec3_t angles , vec3_t velocity ) ;
 void V_UpdatePalette ( void ) ;
#line 28 "menu.h"
 extern int m_activenet ;




 void M_Init ( void ) ;
 void M_Keydown ( int key ) ;
 void M_Draw ( void ) ;
 void M_ToggleMenu_f ( void ) ;
#line 22 "crc.h"
 void CRC_Init ( unsigned short * crcvalue ) ;
 void CRC_ProcessByte ( unsigned short * crcvalue , byte data ) ;
 unsigned short CRC_Value ( unsigned short crcvalue ) ;
#line 21 "cdaudio.h"
 int CDAudio_Init ( void ) ;
 void CDAudio_Play ( byte track , qboolean looping ) ;
 void CDAudio_Stop ( void ) ;
 void CDAudio_Pause ( void ) ;
 void CDAudio_Resume ( void ) ;
 void CDAudio_Shutdown ( void ) ;
 void CDAudio_Update ( void ) ;
#line 41 "mgl\gl.h"
 typedef void GLvoid ;

 typedef char GLbyte ;
 typedef unsigned char GLubyte ;
 typedef short GLshort ;
 typedef unsigned short GLushort ;
 typedef int GLint ;
 typedef unsigned int GLuint ;
 typedef unsigned int GLboolean ;
 typedef long GLsizei ;
 typedef unsigned long GLbitfield ;
 typedef float GLfloat ;
 typedef double GLdouble ;
 typedef float GLclampf ;
 typedef double GLclampd ;

 typedef unsigned int GLenum ;








 enum
 {
 GL_BASE = 0 ,
 GL_NO_ERROR = 0 ,
 GL_ALPHA ,
 GL_ALPHA8 ,
 GL_ALPHA_BITS ,
 GL_ALPHA_SCALE ,
 GL_ALPHA_TEST ,
 GL_ALPHA_TEST_FUNC ,
 GL_ALPHA_TEST_REF ,
 GL_ALWAYS ,
 GL_AUX_BUFFERS ,
 GL_BACK ,
 GL_BACK_LEFT ,
 GL_BACK_RIGHT ,
 GL_BLEND ,
 GL_BLEND_DST ,
 GL_BLEND_SRC ,
 GL_BLUE ,
 GL_BLUE_BITS ,
 GL_BYTE ,
 GL_C3F_V3F ,
 GL_C4UB_V2F ,
 GL_C4UB_V3F ,
 GL_CCW ,
 GL_COLOR_ARRAY ,
 GL_COLOR_ARRAY_POINTER ,
 GL_COLOR_ARRAY_SIZE ,
 GL_COLOR_ARRAY_STRIDE ,
 GL_COLOR_ARRAY_TYPE ,
 GL_COLOR_CLEAR_VALUE ,
 GL_COLOR_INDEX ,
 GL_CULL_FACE ,
 GL_CULL_FACE_MODE ,
 GL_CURRENT_COLOR ,
 GL_CURRENT_INDEX ,
 GL_CURRENT_TEXTURE_COORDS ,
 GL_CW ,
 GL_DECAL ,
 GL_DEPTH_BITS ,
 GL_DEPTH_CLEAR_VALUE ,
 GL_DEPTH_COMPONENT ,
 GL_DEPTH_FUNC ,
 GL_DEPTH_RANGE ,
 GL_DEPTH_SCALE ,
 GL_DEPTH_TEST ,
 GL_DEPTH_WRITEMASK ,
 GL_DITHER ,
 GL_DONT_CARE ,
 GL_DOUBLE ,
 GL_DOUBLEBUFFER ,
 GL_DRAW_BUFFER ,
 GL_DST_ALPHA ,
 GL_DST_COLOR ,
 GL_EDGE_FLAG ,
 GL_EDGE_FLAG_ARRAY ,
 GL_EDGE_FLAG_ARRAY_POINTER ,
 GL_EDGE_FLAG_ARRAY_STRIDE ,
 GL_EQUAL ,
 GL_EXTENSIONS ,
 GL_FASTEST ,
 GL_FLAT ,
 GL_FLOAT ,
 GL_FOG ,
 GL_FOG_COLOR ,
 GL_FOG_DENSITY ,
 GL_FOG_END ,
 GL_FOG_HINT ,
 GL_FOG_INDEX ,
 GL_FOG_MODE ,
 GL_FOG_START ,
 GL_FRONT_AND_BACK ,
 GL_FRONT ,
 GL_FRONT_FACE ,
 GL_FRONT_LEFT ,
 GL_FRONT_RIGHT ,
 GL_GEQUAL ,
 GL_GREATER ,
 GL_GREEN ,
 GL_GREEN_BITS ,
 GL_INDEX_ARRAY ,
 GL_INDEX_ARRAY_POINTER ,
 GL_INDEX_ARRAY_STRIDE ,
 GL_INDEX_ARRAY_TYPE ,
 GL_INDEX_BITS ,
 GL_INDEX_CLEAR_VALUE ,
 GL_INDEX_MODE ,
 GL_INT ,
 GL_INTENSITY ,
 GL_INTENSITY8 ,
 GL_INVALID_ENUM ,
 GL_INVALID_OPERATION ,
 GL_INVALID_VALUE ,
 GL_LEFT ,
 GL_LEQUAL ,
 GL_LESS ,
 GL_LINEAR ,
 GL_LINEAR_MIPMAP_LINEAR ,
 GL_LINEAR_MIPMAP_NEAREST ,
 GL_LINES ,
 GL_LINE_LOOP ,
 GL_LINE_STRIP ,
 GL_LUMINANCE ,
 GL_LUMINANCE8 ,
 GL_LUMINANCE8_ALPHA8 ,
 GL_LUMINANCE_ALPHA ,
 GL_MATRIX_MODE ,
 GL_MAX_TEXTURE_SIZE ,
 GL_MAX_VIEWPORT_DIMS ,
 GL_MODELVIEW ,
 GL_MODELVIEW_MATRIX ,
 GL_MODELVIEW_STACK_DEPTH ,
 GL_MODULATE ,
 GL_NEVER ,
 GL_NEAREST ,
 GL_NEAREST_MIPMAP_NEAREST ,
 GL_NEAREST_MIPMAP_LINEAR ,
 GL_NICEST ,
 GL_NONE ,
 GL_NOTEQUAL ,
 GL_ONE ,
 GL_ONE_MINUS_DST_ALPHA ,
 GL_ONE_MINUS_DST_COLOR ,
 GL_ONE_MINUS_SRC_ALPHA ,
 GL_ONE_MINUS_SRC_COLOR ,
 GL_OUT_OF_MEMORY ,
 GL_PACK_ALIGNMENT ,
 GL_PACK_LSB_FIRST ,
 GL_PACK_ROW_LENGTH ,
 GL_PACK_SKIP_PIXELS ,
 GL_PACK_SKIP_ROWS ,
 GL_PACK_SWAP_BYTES ,
 GL_PERSPECTIVE_CORRECTION_HINT ,
 GL_POINTS ,
 GL_POLYGON_MODE ,
 GL_POLYGON_OFFSET ,
 GL_POLYGON_OFFSET_FACTOR ,
 GL_POLYGON_OFFSET_FILL ,
 GL_POLYGON_OFFSET_LINE ,
 GL_POLYGON_OFFSET_POINT ,
 GL_POLYGON_OFFSET_UNITS ,
 GL_POLYGON ,
 GL_PROJECTION ,
 GL_PROJECTION_MATRIX ,
 GL_PROJECTION_STACK_DEPTH ,
 GL_QUADS ,
 GL_QUAD_STRIP ,
 GL_READ_BUFFER ,
 GL_RED ,
 GL_RED_BITS ,
 GL_RENDERER ,
 GL_REPLACE ,
 GL_REPEAT ,
 GL_RGB ,
 GL_RGB5 ,
 GL_RGB5_A1 ,
 GL_RGB8 ,
 GL_RGBA ,
 GL_RGBA8 ,
 GL_RGBA_MODE ,
 GL_RIGHT ,
 GL_SCISSOR_BOX ,
 GL_SCISSOR_TEST ,
 GL_SHADE_MODEL ,
 GL_SHORT ,
 GL_SMOOTH ,
 GL_SRC_ALPHA ,
 GL_SRC_ALPHA_SATURATE ,
 GL_SRC_COLOR ,
 GL_STACK_OVERFLOW ,
 GL_STACK_UNDERFLOW ,
 GL_STEREO ,
 GL_T2F_C3F_V3F ,
 GL_T2F_C4UB_V3F ,
 GL_T2F_V3F ,
 GL_TABLE_TOO_LARGE ,
 GL_TEXTURE_2D ,
 GL_TEXTURE_2D_BINDING ,
 GL_TEXTURE_COORD_ARRAY ,
 GL_TEXTURE_COORD_ARRAY_SIZE ,
 GL_TEXTURE_COORD_ARRAY_STRIDE ,
 GL_TEXTURE_COORD_ARRAY_TYPE ,
 GL_TEXTURE_DOOR_ARRAY_POINTER ,
 GL_TEXTURE_ENV ,
 GL_TEXTURE_ENV_COLOR ,
 GL_TEXTURE_ENV_MODE ,
 GL_TEXTURE_MAG_FILTER ,
 GL_TEXTURE_MIN_FILTER ,
 GL_TEXTURE_PRIORITY ,
 GL_TEXTURE_WRAP_S ,
 GL_TEXTURE_WRAP_T ,
 GL_TEXTURE_GEN_S ,
 GL_TEXTURE_GEN_T ,
 GL_TRIANGLES ,
 GL_TRIANGLE_FAN ,
 GL_TRIANGLE_STRIP ,
 GL_UNPACK_ALIGNMENT ,
 GL_UNPACK_LSB_FIRST ,
 GL_UNPACK_ROW_LENGTH ,
 GL_UNPACK_SKIP_PIXELS ,
 GL_UNPACK_SKIP_ROWS ,
 GL_UNPACK_SWAP_BYTES ,
 GL_UNSIGNED_BYTE ,
 GL_UNSIGNED_INT ,
 GL_UNSIGNED_SHORT ,
 GL_V2F ,
 GL_V3F ,
 GL_VENDOR ,
 GL_VERSION ,
 GL_VERTEX_ARRAY ,
 GL_VERTEX_ARRAY_POINTER ,
 GL_VERTEX_ARRAY_SIZE ,
 GL_VERTEX_ARRAY_STRIDE ,
 GL_VERTEX_ARRAY_TYPE ,
 GL_VIEWPORT ,
 GL_ZERO ,
 GL_POINT_SMOOTH ,
 GL_CLAMP ,
 GL_EXP ,
 GL_EXP2 ,
 GL_TEXTURE_GEN_MODE ,
 GL_SPHERE_MAP ,
 GL_T ,
 GL_S ,
 GL_FILL ,
 GL_TEXTURE0_ARB ,
 GL_TEXTURE1_ARB ,
 GL_TEXTURE2_ARB ,
 GL_TEXTURE3_ARB ,
 GL_MAX_TEXTURE_UNITS_ARB ,
 MGL_LOCK_AUTOMATIC ,
 MGL_LOCK_MANUAL ,
 MGL_LOCK_SMART ,
 MGL_FLATFAN ,
 MGL_FLATSTRIP ,
 MGL_PERSPECTIVE_MAPPING ,
 MGL_W_ONE_HINT ,
 MGL_Z_OFFSET ,
 GL_COLOR_TABLE ,
 MGL_UBYTE_BGRA ,
 MGL_UBYTE_ARGB ,
 MGL_UNSIGNED_SHORT_5_6_5 ,
 MGL_UNSIGNED_SHORT_4_4_4_4 ,
 MGL_FIXPOINTTRANS_HINT ,
 MGL_ARRAY_TRANSFORMATIONS ,
 } ;














 typedef struct MGLColor_t
 {
 GLfloat r , g , b , a ;
 } MGLColor ;


 typedef struct MGLNormal_t
 {
 GLfloat x , y , z , w ;
 } MGLNormal ;
#line 57 "exec\types.h"
 typedef void * APTR ;

 typedef long LONG ;
 typedef unsigned long ULONG ;
 typedef unsigned long LONGBITS ;
 typedef short WORD ;
 typedef unsigned short UWORD ;
 typedef unsigned short WORDBITS ;

 typedef signed char BYTE ;



 typedef unsigned char UBYTE ;
 typedef unsigned char BYTEBITS ;
 typedef unsigned short RPTR ;




 typedef unsigned char * STRPTR ;



 typedef const void * CONST_APTR ;



 typedef const unsigned char * CONST_STRPTR ;



 typedef short SHORT ;
 typedef unsigned short USHORT ;
 typedef short COUNT ;
 typedef unsigned short UCOUNT ;
 typedef ULONG CPTR ;



 typedef float FLOAT ;
 typedef double DOUBLE ;
 typedef short BOOL ;
 typedef unsigned char TEXT ;
#line 17 "v3d_types.h"
 typedef unsigned char v3d_u8 ;
 typedef unsigned short v3d_u16 ;
 typedef unsigned int v3d_u32 ;
 typedef int v3d_i32 ;













 typedef unsigned long uint32_t ;
 typedef unsigned long uintptr_t ;
 typedef signed int v3d_int ;
 typedef unsigned long long v3d_u64 ;
 typedef unsigned long v3d_uint ;
 typedef float v3d_float ;
 typedef unsigned int v3d_address ;
 typedef unsigned int v3d_uintptr ;
 typedef unsigned short v3d_f187 ;
 typedef unsigned int v3d_u14_8 ;
 typedef unsigned short v3d_u4_8 ;
 typedef signed short v3d_s8_8 ;
 typedef v3d_u64 v3d_qpu_instruction ;








 ULONG LE32 ( __reg ( "d0" ) ULONG ) = "\trol.w\t#8,d0\n\tswap\td0\n\trol.w\t#8,d0\n" ;
 UWORD LE16 ( __reg ( "d0" ) UWORD ) = "\trol.w\t#8,d0\n" ;
 void NOP ( void ) = "\tnop\n" ;




 typedef struct v3d_mem {
 unsigned size ;
 unsigned handle ;
 unsigned busaddr ;
 void * hostptr ;
 } v3d_mem ;


 enum {
 MEM_FLAG_DISCARDABLE = 1 << 0 ,
 MEM_FLAG_NORMAL = 0 << 2 ,
 MEM_FLAG_DIRECT = 1 << 2 ,
 MEM_FLAG_COHERENT = 2 << 2 ,
 MEM_FLAG_L1_NONALLOCATING = ( MEM_FLAG_DIRECT | MEM_FLAG_COHERENT ) ,
 MEM_FLAG_ZERO = 1 << 4 ,
 MEM_FLAG_NO_INIT = 1 << 5 ,
 MEM_FLAG_HINT_PERMALOCK = 1 << 6
 } ;








 typedef struct v3d_static_buffer {
 v3d_u8 * start ;
 int used ;
 int capacity ;











 v3d_u8 overflowed ;
 } v3d_static_buffer ;
#line 61 "..\..\..\backend\include\v3d_vertex.h"
 typedef struct V3DVertex {
 float x , y , z , w ;
 v3d_u8 r , g , b , a ;
 float u0 , v0 ;
 float u1 , v1 ;
 } V3DVertex ;






















 typedef struct V3DAttribDesc {
 v3d_u32 offset ;
 v3d_u32 stride ;
 v3d_u32 maximum_index ;
 v3d_u8 read_as_int_uint ;
 v3d_u8 normalized_int_type ;
 v3d_u8 signed_int_type ;
 v3d_u8 vec_size ;
 v3d_u8 type ;
 v3d_u8 values_read_by_vertex_shader ;
 v3d_u8 values_read_by_coordinate_shader ;
 v3d_u16 instance_divisor ;
 } V3DAttribDesc ;
#line 61 "mgl\vertexbuffer.h"
 struct MGLVertex_t {

 V3DVertex v ;

 MGLColor color ;

 ULONG outcode ;

 int xi , yi , zi ;

 float bx , by , bz , bw ;
 GLfloat q ;

 GLuint normal ;
 } ;

 typedef struct MGLVertex_t MGLVertex ;

 enum {
 MGL_CLIP_NEGW = 1 << 0 ,
 MGL_CLIP_TOP = 1 << 1 ,
 MGL_CLIP_BOTTOM = 1 << 2 ,
 MGL_CLIP_LEFT = 1 << 3 ,
 MGL_CLIP_RIGHT = 1 << 4 ,
 MGL_CLIP_FRONT = 1 << 5 ,
 MGL_CLIP_BACK = 1 << 6
 } ;





 typedef struct PolyIndex_s
 {
 ULONG numverts ;
 ULONG first ;
 } PolyIndex ;
#line 18 "mgl\matrix.h"
 typedef struct Matrix_t
 {
 float v [ 16 ] ;
 int flags ;
 struct Matrix_t * Inverse ;
 int _pad_ [ 2 ] ;
 } Matrix ;
#line 20 "..\..\..\backend\include\v3d_device.h"
 struct v3d_device_info {
 unsigned char ver ;
 unsigned char rev ;
 int vpm_size ;
 int qpu_count ;
 char has_accumulators ;
 } ;

 typedef struct V3DDevice {
 struct ExecBase * sysbase ;
 struct DOSBase * dosbase ;
 v3d_u32 BytesAllocated ;
 struct v3d_device_info deviceInfo ;
 } V3DDevice ;

 int v3d_init ( V3DDevice * device ) ;
 void v3d_free ( V3DDevice * device ) ;
 int v3d_mem_alloc ( V3DDevice * device , v3d_mem * m , unsigned size ) ;
 void v3d_mem_free ( V3DDevice * device , v3d_mem * m ) ;
 int v3d_qpu_active ( V3DDevice * device ) ;
#line 14 "v3d_texture.h"
 typedef struct V3DContext V3DContext ;


























 enum {
 V3D_TEXFILTER_NEAREST = 0 ,
 V3D_TEXFILTER_LINEAR ,
 V3D_TEXFILTER_NEAREST_MIPMAP ,
 V3D_TEXFILTER_LINEAR_MIPMAP
 } ;

 enum {
 V3D_TEXWRAP_REPEAT = 0 ,
 V3D_TEXWRAP_CLAMP ,
 V3D_TEXWRAP_BORDER
 } ;









 enum {
 V3D_TEXENV_MODULATE = 0 ,
 V3D_TEXENV_DECAL ,
 V3D_TEXENV_REPLACE
 } ;








 enum {
 V3D_SRCFMT_RGBA8 = 0 ,
 V3D_SRCFMT_ARGB8 ,
 V3D_SRCFMT_RGB8 ,
 V3D_SRCFMT_ARGB4 ,
 V3D_SRCFMT_ARGB1555 ,
 V3D_SRCFMT_RGB565 ,
 V3D_SRCFMT_LA8
 } ;

 typedef struct V3DTexture {
 struct V3DTexture * next ;

 v3d_mem texture_mem ;

 v3d_u16 width , height ;
 v3d_u8 format ;
 v3d_u8 mipmap_count ;

 v3d_u8 min_filter , mag_filter ;
 v3d_u8 wrap_s , wrap_t ;
 v3d_u8 border_r , border_g , border_b , border_a ;

 v3d_u8 texenv_mode ;
 v3d_u8 env_r , env_g , env_b , env_a ;

 v3d_u8 resident ;
 v3d_u8 dirty ;













 v3d_u8 tiling_format ;
 } V3DTexture ;









 int v3d_texture_alloc ( V3DDevice * device , V3DTexture * tex , v3d_u16 width , v3d_u16 height ) ;


 void v3d_texture_free ( V3DDevice * device , V3DTexture * tex ) ;













 void v3d_texture_upload_rgba8 ( V3DDevice * device , V3DTexture * tex , const void * src , v3d_u32 srcStride ) ;




















 void v3d_texture_upload_rgba8_subimage ( V3DDevice * device , V3DTexture * tex , const void * src , v3d_u32 srcStride ,
 int xoffset , int yoffset , int width , int height ) ;












 void v3d_texture_convert_row ( v3d_u32 * dst , const v3d_u8 * src , v3d_u32 count , v3d_u8 srcFormat ) ;















 void v3d_texture_emit_state ( V3DDevice * device , V3DContext * context , V3DTexture * tex ,
 ULONG * outTextureShaderStateAddress , ULONG * outTextureSamplerStateAddress ) ;
#line 43 "v3d_frame.h"
 typedef struct V3DFrame {
 v3d_u32 tilesX , tilesY ;

 v3d_mem spill_blocks [ 8 ] ;
 int spill_count ;
 } V3DFrame ;


 void v3d_frame_compute_pool_sizes ( v3d_u16 width , v3d_u16 height ,
 v3d_u32 * zbuffer_size ,
 v3d_u32 * tile_state_size ,
 v3d_u32 * tile_alloc_size ,
 v3d_u32 * tilesX ,
 v3d_u32 * tilesY ) ;


 void v3d_frame_begin ( V3DFrame * frame , v3d_u32 tilesX , v3d_u32 tilesY ) ;


 void v3d_frame_end ( V3DDevice * device , V3DFrame * frame ) ;






 v3d_mem * v3d_frame_add_spill_block ( V3DDevice * device , V3DFrame * frame ) ;














 void v3d_frame_defer_free ( V3DFrame * frame , v3d_mem * mem ) ;
#line 119 "..\..\..\backend\include\v3d_context.h"
 enum {
 V3D_FOG_LINEAR = 0 ,
 V3D_FOG_EXP ,
 V3D_FOG_EXP2
 } ;












 enum {
 V3D_ALPHAFUNC_NEVER = 0 ,
 V3D_ALPHAFUNC_LESS ,
 V3D_ALPHAFUNC_EQUAL ,
 V3D_ALPHAFUNC_LEQUAL ,
 V3D_ALPHAFUNC_GREATER ,
 V3D_ALPHAFUNC_NOTEQUAL ,
 V3D_ALPHAFUNC_GEQUAL ,
 V3D_ALPHAFUNC_ALWAYS
 } ;























 enum {
 V3D_Z_NEVER = 0 ,
 V3D_Z_LESS ,
 V3D_Z_EQUAL ,
 V3D_Z_LEQUAL ,
 V3D_Z_GREATER ,
 V3D_Z_NOTEQUAL ,
 V3D_Z_GEQUAL ,
 V3D_Z_ALWAYS
 } ;






 struct V3DContext {

 void * framebuffer ;
 v3d_u16 width , height ;
 v3d_i32 y_offset ;


 v3d_mem zbuffer_mem ;
 v3d_mem tile_state_mem ;
 v3d_mem tile_alloc_mem ;











 v3d_mem binning_mem , render_mem , tile_list_mem , state_mem ;
 v3d_static_buffer binning_buf ;
 v3d_static_buffer render_buf ;
 v3d_static_buffer tile_list_buf ;
 v3d_static_buffer state_buf ;
 v3d_static_buffer * current_buf ;


 V3DFrame frame ;


 v3d_u32 fixed_color ;
 v3d_u32 zmode ;
 v3d_u32 blend_srcmode , blend_dstmode ;
 v3d_u8 alpha_test_enable ;
 v3d_u8 alpha_func ;
 float alpha_ref ;
 v3d_u8 color_mask_r , color_mask_g , color_mask_b , color_mask_a ;


 v3d_u16 scissor_x , scissor_y , scissor_w , scissor_h ;
 v3d_u8 scissor_enable ;


 v3d_u8 fog_enable ;
 v3d_u8 fog_mode ;
 float fog_start , fog_end , fog_density ;
 v3d_u8 fog_r , fog_g , fog_b , fog_a ;


 V3DTexture * bound_texture [ 2 ] ;


 V3DTexture * texture_list ;
















 v3d_u8 frame_active ;
 v3d_u8 pending_clear_color ;
 v3d_u8 pending_clear_depth ;
















 v3d_u8 draw_state_configured ;










 v3d_mem shader_code_mem ;
 v3d_u8 shaders_ready ;
 } ;








 int v3d_context_init ( V3DContext * context , V3DDevice * device , v3d_u16 width , v3d_u16 height ) ;


 void v3d_context_free ( V3DDevice * device , V3DContext * context ) ;

























 void v3d_context_flush_for_dma ( V3DDevice * device , V3DContext * context ) ;
#line 70 "mgl\context.h"
#pragma amiga-align
#line 27 "graphics\gfx.h"
 struct Rectangle
 {
 WORD MinX , MinY ;
 WORD MaxX , MaxY ;
 } ;

 struct Rect32
 {
 LONG MinX , MinY ;
 LONG MaxX , MaxY ;
 } ;

 typedef struct tPoint
 {
 WORD x , y ;
 } Point ;

 typedef UBYTE * PLANEPTR ;

 struct BitMap
 {
 UWORD BytesPerRow ;
 UWORD Rows ;
 UBYTE Flags ;
 UBYTE Depth ;
 UWORD pad ;
 PLANEPTR Planes [ 8 ] ;
 } ;
#line 22 "exec\nodes.h"
 struct Node {
 struct Node * ln_Succ ;
 struct Node * ln_Pred ;
 UBYTE ln_Type ;
 BYTE ln_Pri ;
 char * ln_Name ;
 } ;


 struct MinNode {
 struct MinNode * mln_Succ ;
 struct MinNode * mln_Pred ;
 } ;
#line 20 "exec\lists.h"
 struct List {
 struct Node * lh_Head ;
 struct Node * lh_Tail ;
 struct Node * lh_TailPred ;
 UBYTE lh_Type ;
 UBYTE l_pad ;
 } ;




 struct MinList {
 struct MinNode * mlh_Head ;
 struct MinNode * mlh_Tail ;
 struct MinNode * mlh_TailPred ;
 } ;
#line 24 "exec\tasks.h"
 struct Task {
 struct Node tc_Node ;
 UBYTE tc_Flags ;
 UBYTE tc_State ;
 BYTE tc_IDNestCnt ;
 BYTE tc_TDNestCnt ;
 ULONG tc_SigAlloc ;
 ULONG tc_SigWait ;
 ULONG tc_SigRecvd ;
 ULONG tc_SigExcept ;
 UWORD tc_TrapAlloc ;
 UWORD tc_TrapAble ;
 APTR tc_ExceptData ;
 APTR tc_ExceptCode ;
 APTR tc_TrapData ;
 APTR tc_TrapCode ;
 APTR tc_SPReg ;
 APTR tc_SPLower ;
 APTR tc_SPUpper ;
 void ( * tc_Switch ) ( ) ;
 void ( * tc_Launch ) ( ) ;
 struct List tc_MemEntry ;
 APTR tc_UserData ;
 } ;




 struct StackSwapStruct {
 APTR stk_Lower ;
 ULONG stk_Upper ;
 APTR stk_Pointer ;
 } ;
#line 28 "exec\ports.h"
 struct MsgPort {
 struct Node mp_Node ;
 UBYTE mp_Flags ;
 UBYTE mp_SigBit ;
 void * mp_SigTask ;
 struct List mp_MsgList ;
 } ;












 struct Message {
 struct Node mn_Node ;
 struct MsgPort * mn_ReplyPort ;
 UWORD mn_Length ;


 } ;
#line 33 "exec\semaphores.h"
 struct SemaphoreRequest
 {
 struct MinNode sr_Link ;
 struct Task * sr_Waiter ;
 } ;


 struct SignalSemaphore
 {
 struct Node ss_Link ;
 WORD ss_NestCount ;
 struct MinList ss_WaitQueue ;
 struct SemaphoreRequest ss_MultipleLink ;
 struct Task * ss_Owner ;
 WORD ss_QueueCount ;
 } ;


 struct SemaphoreMessage
 {
 struct Message ssm_Message ;
 struct SignalSemaphore * ssm_Semaphore ;
 } ;






 struct Semaphore
 {
 struct MsgPort sm_MsgPort ;
 WORD sm_Bids ;
 } ;
#line 28 "utility\hooks.h"
 struct Hook
 {
 struct MinNode h_MinNode ;
 ULONG ( * h_Entry ) ( ) ;
 ULONG ( * h_SubEntry ) ( ) ;
 APTR h_Data ;
 } ;




 typedef unsigned long ( * HOOKFUNC ) ( ) ;
#line 27 "graphics\clip.h"
 struct Layer
 {
 struct Layer * front , * back ;
 struct ClipRect * ClipRect ;
 struct RastPort * rp ;
 struct Rectangle bounds ;
 UBYTE reserved [ 4 ] ;
 UWORD priority ;
 UWORD Flags ;
 struct BitMap * SuperBitMap ;
 struct ClipRect * SuperClipRect ;

 APTR Window ;
 WORD Scroll_X , Scroll_Y ;
 struct ClipRect * cr , * cr2 , * crnew ;
 struct ClipRect * SuperSaveClipRects ;
 struct ClipRect * _cliprects ;
 struct Layer_Info * LayerInfo ;
 struct SignalSemaphore Lock ;
 struct Hook * BackFill ;
 ULONG reserved1 ;
 struct Region * ClipRegion ;
 struct Region * saveClipRects ;
 WORD Width , Height ;
 UBYTE reserved2 [ 18 ] ;

 struct Region * DamageList ;

 } ;

 struct ClipRect
 {
 struct ClipRect * Next ;
 struct ClipRect * prev ;
 struct Layer * lobs ;
 struct BitMap * BitMap ;
 struct Rectangle bounds ;
 void * _p1 ;
 void * _p2 ;
 LONG reserved ;




 } ;
#line 24 "graphics\copper.h"
 struct CopIns
 {
 WORD OpCode ;
 union
 {
 struct CopList * nxtlist ;
 struct
 {
 union
 {
 WORD VWaitPos ;
 WORD DestAddr ;
 } u1 ;
 union
 {
 WORD HWaitPos ;
 WORD DestData ;
 } u2 ;
 } u4 ;
 } u3 ;
 } ;










 struct cprlist
 {
 struct cprlist * Next ;
 UWORD * start ;
 WORD MaxCount ;
 } ;

 struct CopList
 {
 struct CopList * Next ;
 struct CopList * _CopList ;
 struct ViewPort * _ViewPort ;
 struct CopIns * CopIns ;
 struct CopIns * CopPtr ;
 UWORD * CopLStart ;
 UWORD * CopSStart ;
 WORD Count ;
 WORD MaxCount ;
 WORD DyOffset ;






 UWORD SLRepeat ;
 UWORD Flags ;
 } ;






 struct UCopList
 {
 struct UCopList * Next ;
 struct CopList * FirstCopList ;
 struct CopList * CopList ;
 } ;





 struct copinit
 {
 UWORD vsync_hblank [ 2 ] ;
 UWORD diagstrt [ 12 ] ;
 UWORD fm0 [ 2 ] ;
 UWORD diwstart [ 10 ] ;
 UWORD bplcon2 [ 2 ] ;
 UWORD sprfix [ 2 * 8 ] ;
 UWORD sprstrtup [ ( 2 * 8 * 2 ) ] ;
 UWORD wait14 [ 2 ] ;
 UWORD norm_hblank [ 2 ] ;
 UWORD jump [ 2 ] ;
 UWORD wait_forever [ 6 ] ;
 UWORD sprstop [ 8 ] ;
 } ;
#line 17 "graphics\gfxnodes.h"
 struct ExtendedNode {
 struct Node * xln_Succ ;
 struct Node * xln_Pred ;
 UBYTE xln_Type ;
 BYTE xln_Pri ;
 char * xln_Name ;
 UBYTE xln_Subsystem ;
 UBYTE xln_Subtype ;
 LONG xln_Library ;
 LONG ( * xln_Init ) ( ) ;
 } ;
#line 25 "graphics\monitor.h"
 struct MonitorSpec
 {
 struct ExtendedNode ms_Node ;
 UWORD ms_Flags ;
 LONG ratioh ;
 LONG ratiov ;
 UWORD total_rows ;
 UWORD total_colorclocks ;
 UWORD DeniseMaxDisplayColumn ;
 UWORD BeamCon0 ;
 UWORD min_row ;
 struct SpecialMonitor * ms_Special ;
 UWORD ms_OpenCount ;
 LONG ( * ms_transform ) ( ) ;
 LONG ( * ms_translate ) ( ) ;
 LONG ( * ms_scale ) ( ) ;
 UWORD ms_xoffset ;
 UWORD ms_yoffset ;
 struct Rectangle ms_LegalView ;
 LONG ( * ms_maxoscan ) ( ) ;
 LONG ( * ms_videoscan ) ( ) ;
 UWORD DeniseMinDisplayColumn ;
 ULONG DisplayCompatible ;
 struct List DisplayInfoDataBase ;
 struct SignalSemaphore DisplayInfoDataBaseSemaphore ;
 LONG ( * ms_MrgCop ) ( ) ;
 LONG ( * ms_LoadView ) ( ) ;
 LONG ( * ms_KillView ) ( ) ;
 } ;


































































































 struct AnalogSignalInterval
 {
 UWORD asi_Start ;
 UWORD asi_Stop ;
 } ;

 struct SpecialMonitor
 {
 struct ExtendedNode spm_Node ;
 UWORD spm_Flags ;
 LONG ( * do_monitor ) ( ) ;
 LONG ( * reserved1 ) ( ) ;
 LONG ( * reserved2 ) ( ) ;
 LONG ( * reserved3 ) ( ) ;
 struct AnalogSignalInterval hblank ;
 struct AnalogSignalInterval vblank ;
 struct AnalogSignalInterval hsync ;
 struct AnalogSignalInterval vsync ;
 } ;
#line 30 "utility\tagitem.h"
 typedef ULONG Tag ;

 struct TagItem
 {
 Tag ti_Tag ;
 ULONG ti_Data ;
 } ;
#line 35 "graphics\displayinfo.h"
 typedef APTR DisplayInfoHandle ;









 struct QueryHeader
 {
 ULONG StructID ;
 ULONG DisplayID ;
 ULONG SkipID ;
 ULONG Length ;
 } ;

 struct DisplayInfo
 {
 struct QueryHeader Header ;
 UWORD NotAvailable ;
 ULONG PropertyFlags ;
 Point Resolution ;
 UWORD PixelSpeed ;
 UWORD NumStdSprites ;
 UWORD PaletteRange ;
 Point SpriteResolution ;
 UBYTE pad [ 4 ] ;
 UBYTE RedBits ;
 UBYTE GreenBits ;
 UBYTE BlueBits ;
 UBYTE pad2 [ 5 ] ;
 ULONG reserved [ 2 ] ;
 } ;



















































 struct DimensionInfo
 {
 struct QueryHeader Header ;
 UWORD MaxDepth ;
 UWORD MinRasterWidth ;
 UWORD MinRasterHeight ;
 UWORD MaxRasterWidth ;
 UWORD MaxRasterHeight ;
 struct Rectangle Nominal ;
 struct Rectangle MaxOScan ;
 struct Rectangle VideoOScan ;
 struct Rectangle TxtOScan ;
 struct Rectangle StdOScan ;
 UBYTE pad [ 14 ] ;
 ULONG reserved [ 2 ] ;
 } ;

 struct MonitorInfo
 {
 struct QueryHeader Header ;
 struct MonitorSpec * Mspc ;
 Point ViewPosition ;
 Point ViewResolution ;
 struct Rectangle ViewPositionRange ;
 UWORD TotalRows ;
 UWORD TotalColorClocks ;
 UWORD MinRow ;
 WORD Compatibility ;
 UBYTE pad [ 32 ] ;
 Point MouseTicks ;
 Point DefaultViewPosition ;
 ULONG PreferredModeID ;
 ULONG reserved [ 2 ] ;
 } ;









 struct NameInfo
 {
 struct QueryHeader Header ;
 UBYTE Name [ 32 ] ;
 ULONG reserved [ 2 ] ;
 } ;







 struct VecInfo
 {
 struct QueryHeader Header ;
 APTR Vec ;
 APTR Data ;
 UWORD Type ;
 UWORD pad [ 3 ] ;
 ULONG reserved [ 2 ] ;
 } ;
#line 25 "hardware\custom.h"
 struct Custom {
 UWORD bltddat ;
 UWORD dmaconr ;
 UWORD vposr ;
 UWORD vhposr ;
 UWORD dskdatr ;
 UWORD joy0dat ;
 UWORD joy1dat ;
 UWORD clxdat ;
 UWORD adkconr ;
 UWORD pot0dat ;
 UWORD pot1dat ;
 UWORD potinp ;
 UWORD serdatr ;
 UWORD dskbytr ;
 UWORD intenar ;
 UWORD intreqr ;
 APTR dskpt ;
 UWORD dsklen ;
 UWORD dskdat ;
 UWORD refptr ;
 UWORD vposw ;
 UWORD vhposw ;
 UWORD copcon ;
 UWORD serdat ;
 UWORD serper ;
 UWORD potgo ;
 UWORD joytest ;
 UWORD strequ ;
 UWORD strvbl ;
 UWORD strhor ;
 UWORD strlong ;
 UWORD bltcon0 ;
 UWORD bltcon1 ;
 UWORD bltafwm ;
 UWORD bltalwm ;
 APTR bltcpt ;
 APTR bltbpt ;
 APTR bltapt ;
 APTR bltdpt ;
 UWORD bltsize ;
 UBYTE pad2d ;
 UBYTE bltcon0l ;
 UWORD bltsizv ;
 UWORD bltsizh ;
 UWORD bltcmod ;
 UWORD bltbmod ;
 UWORD bltamod ;
 UWORD bltdmod ;
 UWORD pad34 [ 4 ] ;
 UWORD bltcdat ;
 UWORD bltbdat ;
 UWORD bltadat ;
 UWORD pad3b [ 3 ] ;
 UWORD deniseid ;
 UWORD dsksync ;
 ULONG cop1lc ;
 ULONG cop2lc ;
 UWORD copjmp1 ;
 UWORD copjmp2 ;
 UWORD copins ;
 UWORD diwstrt ;
 UWORD diwstop ;
 UWORD ddfstrt ;
 UWORD ddfstop ;
 UWORD dmacon ;
 UWORD clxcon ;
 UWORD intena ;
 UWORD intreq ;
 UWORD adkcon ;
 struct AudChannel {
 UWORD * ac_ptr ;
 UWORD ac_len ;
 UWORD ac_per ;
 UWORD ac_vol ;
 UWORD ac_dat ;
 UWORD ac_pad [ 2 ] ;
 } aud [ 4 ] ;
 APTR bplpt [ 8 ] ;
 UWORD bplcon0 ;
 UWORD bplcon1 ;
 UWORD bplcon2 ;
 UWORD bplcon3 ;
 UWORD bpl1mod ;
 UWORD bpl2mod ;
 UWORD bplcon4 ;
 UWORD clxcon2 ;
 UWORD bpldat [ 8 ] ;
 APTR sprpt [ 8 ] ;
 struct SpriteDef {
 UWORD pos ;
 UWORD ctl ;
 UWORD dataa ;
 UWORD datab ;
 } spr [ 8 ] ;
 UWORD color [ 32 ] ;
 UWORD htotal ;
 UWORD hsstop ;
 UWORD hbstrt ;
 UWORD hbstop ;
 UWORD vtotal ;
 UWORD vsstop ;
 UWORD vbstrt ;
 UWORD vbstop ;
 UWORD sprhstrt ;
 UWORD sprhstop ;
 UWORD bplhstrt ;
 UWORD bplhstop ;
 UWORD hhposw ;
 UWORD hhposr ;
 UWORD beamcon0 ;
 UWORD hsstrt ;
 UWORD vsstrt ;
 UWORD hcenter ;
 UWORD diwhigh ;
 UWORD padf3 [ 11 ] ;
 UWORD fmode ;
 } ;
#line 47 "graphics\view.h"
 struct ViewPort
 {
 struct ViewPort * Next ;
 struct ColorMap * ColorMap ;

 struct CopList * DspIns ;
 struct CopList * SprIns ;
 struct CopList * ClrIns ;
 struct UCopList * UCopIns ;
 WORD DWidth , DHeight ;
 WORD DxOffset , DyOffset ;
 UWORD Modes ;
 UBYTE SpritePriorities ;
 UBYTE ExtendedModes ;
 struct RasInfo * RasInfo ;
 } ;

 struct View
 {
 struct ViewPort * ViewPort ;
 struct cprlist * LOFCprList ;
 struct cprlist * SHFCprList ;
 WORD DyOffset , DxOffset ;

 UWORD Modes ;
 } ;



 struct ViewExtra
 {
 struct ExtendedNode n ;
 struct View * View ;
 struct MonitorSpec * Monitor ;
 UWORD TopLine ;
 } ;



 struct ViewPortExtra
 {
 struct ExtendedNode n ;
 struct ViewPort * ViewPort ;
 struct Rectangle DisplayClip ;

 APTR VecTable ;
 APTR DriverData [ 2 ] ;
 UWORD Flags ;
 Point Origin [ 2 ] ;


 ULONG cop1ptr ;
 ULONG cop2ptr ;
 } ;



































 struct RasInfo
 {
 struct RasInfo * Next ;
 struct BitMap * BitMap ;
 WORD RxOffset , RyOffset ;
 } ;

 struct ColorMap
 {
 UBYTE Flags ;
 UBYTE Type ;
 UWORD Count ;
 APTR ColorTable ;
 struct ViewPortExtra * cm_vpe ;
 APTR LowColorBits ;
 UBYTE TransparencyPlane ;
 UBYTE SpriteResolution ;
 UBYTE SpriteResDefault ;
 UBYTE AuxFlags ;
 struct ViewPort * cm_vp ;
 APTR NormalDisplayInfo ;
 APTR CoerceDisplayInfo ;
 struct TagItem * cm_batch_items ;
 ULONG VPModeID ;
 struct PaletteExtra * PalExtra ;
 UWORD SpriteBase_Even ;
 UWORD SpriteBase_Odd ;
 UWORD Bp_0_base ;
 UWORD Bp_1_base ;

 } ;











































 struct PaletteExtra
 {
 struct SignalSemaphore pe_Semaphore ;
 UWORD pe_FirstFree ;
 UWORD pe_NFree ;
 UWORD pe_FirstShared ;
 UWORD pe_NShared ;
 UBYTE * pe_RefCnt ;
 UBYTE * pe_AllocList ;
 struct ViewPort * pe_ViewPort ;
 UWORD pe_SharableColors ;
 } ;

























































 struct DBufInfo {
 APTR dbi_Link1 ;
 ULONG dbi_Count1 ;
 struct Message dbi_SafeMessage ;
 APTR dbi_UserData1 ;

 APTR dbi_Link2 ;
 ULONG dbi_Count2 ;
 struct Message dbi_DispMessage ;

 APTR dbi_UserData2 ;
 ULONG dbi_MatchLong ;
 APTR dbi_CopPtr1 ;
 APTR dbi_CopPtr2 ;
 APTR dbi_CopPtr3 ;
 UWORD dbi_BeamPos1 ;
 UWORD dbi_BeamPos2 ;
 } ;
#line 19 "graphics\rastport.h"
 struct AreaInfo
 {
 WORD * VctrTbl ;
 WORD * VctrPtr ;
 BYTE * FlagTbl ;
 BYTE * FlagPtr ;
 WORD Count ;
 WORD MaxCount ;
 WORD FirstX , FirstY ;
 } ;

 struct TmpRas
 {
 BYTE * RasPtr ;
 LONG Size ;
 } ;


 struct GelsInfo
 {
 BYTE sprRsrvd ;

 UBYTE Flags ;
 struct VSprite * gelHead , * gelTail ;

 WORD * nextLine ;

 WORD * * lastColor ;
 struct collTable * collHandler ;
 WORD leftmost , rightmost , topmost , bottommost ;
 APTR firstBlissObj , lastBlissObj ;
 } ;

 struct RastPort
 {
 struct Layer * Layer ;
 struct BitMap * BitMap ;
 UWORD * AreaPtrn ;
 struct TmpRas * TmpRas ;
 struct AreaInfo * AreaInfo ;
 struct GelsInfo * GelsInfo ;
 UBYTE Mask ;
 BYTE FgPen ;
 BYTE BgPen ;
 BYTE AOlPen ;
 BYTE DrawMode ;
 BYTE AreaPtSz ;
 BYTE linpatcnt ;
 BYTE dummy ;
 UWORD Flags ;
 UWORD LinePtrn ;
 WORD cp_x , cp_y ;
 UBYTE minterms [ 8 ] ;
 WORD PenWidth ;
 WORD PenHeight ;
 struct TextFont * Font ;
 UBYTE AlgoStyle ;
 UBYTE TxFlags ;
 UWORD TxHeight ;
 UWORD TxWidth ;
 UWORD TxBaseline ;
 WORD TxSpacing ;
 APTR * RP_User ;
 ULONG longreserved [ 2 ] ;

 UWORD wordreserved [ 7 ] ;
 UBYTE reserved [ 8 ] ;

 } ;
#line 31 "graphics\layers.h"
 struct Layer_Info
 {
 struct Layer * top_layer ;
 struct Layer * check_lp ;
 struct ClipRect * obs ;
 struct ClipRect * FreeClipRects ;
 LONG PrivateReserve1 ;
 LONG PrivateReserve2 ;
 struct SignalSemaphore Lock ;
 struct MinList gs_Head ;
 WORD PrivateReserve3 ;
 void * PrivateReserve4 ;
 UWORD Flags ;
 BYTE fatten_count ;
 BYTE LockLayersCount ;
 WORD PrivateReserve5 ;
 void * BlankHook ;
 void * LayerInfo_extra ;
 } ;
#line 65 "graphics\text.h"
 struct TextAttr {
 STRPTR ta_Name ;
 UWORD ta_YSize ;
 UBYTE ta_Style ;
 UBYTE ta_Flags ;
 } ;

 struct TTextAttr {
 STRPTR tta_Name ;
 UWORD tta_YSize ;
 UBYTE tta_Style ;
 UBYTE tta_Flags ;
 struct TagItem * tta_Tags ;
 } ;










 struct TextFont {
 struct Message tf_Message ;

 UWORD tf_YSize ;
 UBYTE tf_Style ;
 UBYTE tf_Flags ;
 UWORD tf_XSize ;
 UWORD tf_Baseline ;
 UWORD tf_BoldSmear ;

 UWORD tf_Accessors ;

 UBYTE tf_LoChar ;
 UBYTE tf_HiChar ;
 APTR tf_CharData ;

 UWORD tf_Modulo ;
 APTR tf_CharLoc ;

 APTR tf_CharSpace ;
 APTR tf_CharKern ;
 } ;








 struct TextFontExtension {
 UWORD tfe_MatchWord ;
 UBYTE tfe_Flags0 ;
 UBYTE tfe_Flags1 ;
 struct TextFont * tfe_BackPtr ;
 struct MsgPort * tfe_OrigReplyPort ;
 struct TagItem * tfe_Tags ;
 UWORD * tfe_OFontPatchS ;
 UWORD * tfe_OFontPatchK ;

 } ;













 struct ColorFontColors {
 UWORD cfc_Reserved ;
 UWORD cfc_Count ;
 UWORD * cfc_ColorTable ;
 } ;


 struct ColorTextFont {
 struct TextFont ctf_TF ;
 UWORD ctf_Flags ;
 UBYTE ctf_Depth ;
 UBYTE ctf_FgColor ;
 UBYTE ctf_Low ;
 UBYTE ctf_High ;
 UBYTE ctf_PlanePick ;
 UBYTE ctf_PlaneOnOff ;
 struct ColorFontColors * ctf_ColorFontColors ;
 APTR ctf_CharData [ 8 ] ;
 } ;


 struct TextExtent {
 UWORD te_Width ;
 UWORD te_Height ;
 struct Rectangle te_Extent ;
 } ;
#line 18 "exec\io.h"
 struct IORequest {
 struct Message io_Message ;
 struct Device * io_Device ;
 struct Unit * io_Unit ;
 UWORD io_Command ;
 UBYTE io_Flags ;
 BYTE io_Error ;
 } ;

 struct IOStdReq {
 struct Message io_Message ;
 struct Device * io_Device ;
 struct Unit * io_Unit ;
 UWORD io_Command ;
 UBYTE io_Flags ;
 BYTE io_Error ;
 ULONG io_Actual ;
 ULONG io_Length ;
 APTR io_Data ;
 ULONG io_Offset ;
 } ;
#line 30 "devices\timer.h"
 struct timeval {
 ULONG tv_secs ;
 ULONG tv_micro ;
 } ;

 struct EClockVal {
 ULONG ev_hi ;
 ULONG ev_lo ;
 } ;

 struct timerequest {
 struct IORequest tr_node ;
 struct timeval tr_time ;
 } ;
#line 97 "devices\inputevent.h"
 struct IEPointerPixel {
 struct Screen * iepp_Screen ;
 struct {
 WORD X ;
 WORD Y ;
 } iepp_Position ;
 } ;













 struct IEPointerTablet {
 struct {
 UWORD X ;
 UWORD Y ;
 } iept_Range ;
 struct {
 UWORD X ;
 UWORD Y ;
 } iept_Value ;

 WORD iept_Pressure ;
 } ;









 struct IENewTablet
 {








 struct Hook * ient_CallBack ;














 UWORD ient_ScaledX , ient_ScaledY ;
 UWORD ient_ScaledXFraction , ient_ScaledYFraction ;


 ULONG ient_TabletX , ient_TabletY ;




 ULONG ient_RangeX , ient_RangeY ;




 struct TagItem * ient_TagList ;
 } ;














































































 struct InputEvent {
 struct InputEvent * ie_NextEvent ;
 UBYTE ie_Class ;
 UBYTE ie_SubClass ;
 UWORD ie_Code ;
 UWORD ie_Qualifier ;
 union {
 struct {
 WORD ie_x ;
 WORD ie_y ;
 } ie_xy ;
 APTR ie_addr ;
 struct {
 UBYTE ie_prev1DownCode ;
 UBYTE ie_prev1DownQual ;
 UBYTE ie_prev2DownCode ;
 UBYTE ie_prev2DownQual ;
 } ie_dead ;
 } ie_position ;
 struct timeval ie_TimeStamp ;
 } ;
#line 60 "intuition\intuition.h"
 struct Menu
 {
 struct Menu * NextMenu ;
 WORD LeftEdge , TopEdge ;
 WORD Width , Height ;
 UWORD Flags ;
 BYTE * MenuName ;
 struct MenuItem * FirstItem ;


 WORD JazzX , JazzY , BeatX , BeatY ;
 } ;
















 struct MenuItem
 {
 struct MenuItem * NextItem ;
 WORD LeftEdge , TopEdge ;
 WORD Width , Height ;
 UWORD Flags ;

 LONG MutualExclude ;

 APTR ItemFill ;




 APTR SelectFill ;

 BYTE Command ;

 struct MenuItem * SubItem ;




 UWORD NextSelect ;
 } ;































 struct Requester
 {
 struct Requester * OlderRequest ;
 WORD LeftEdge , TopEdge ;
 WORD Width , Height ;
 WORD RelLeft , RelTop ;

 struct Gadget * ReqGadget ;
 struct Border * ReqBorder ;
 struct IntuiText * ReqText ;
 UWORD Flags ;


 UBYTE BackFill ;

 struct Layer * ReqLayer ;

 UBYTE ReqPad1 [ 32 ] ;







 struct BitMap * ImageBMap ;
 struct Window * RWindow ;

 struct Image * ReqImage ;

 UBYTE ReqPad2 [ 32 ] ;
 } ;






































 struct Gadget
 {
 struct Gadget * NextGadget ;

 WORD LeftEdge , TopEdge ;
 WORD Width , Height ;

 UWORD Flags ;

 UWORD Activation ;

 UWORD GadgetType ;





 APTR GadgetRender ;




 APTR SelectRender ;

 struct IntuiText * GadgetText ;












 LONG MutualExclude ;




 APTR SpecialInfo ;

 UWORD GadgetID ;
 APTR UserData ;
 } ;


 struct ExtGadget
 {

 struct ExtGadget * NextGadget ;
 WORD LeftEdge , TopEdge ;
 WORD Width , Height ;
 UWORD Flags ;
 UWORD Activation ;
 UWORD GadgetType ;
 APTR GadgetRender ;
 APTR SelectRender ;
 struct IntuiText * GadgetText ;
 LONG MutualExclude ;
 APTR SpecialInfo ;
 UWORD GadgetID ;
 APTR UserData ;


 ULONG MoreFlags ;
 WORD BoundsLeftEdge ;
 WORD BoundsTopEdge ;
 WORD BoundsWidth ;
 WORD BoundsHeight ;
 } ;



































































































































































































































 struct BoolInfo
 {
 UWORD Flags ;
 UWORD * Mask ;





 ULONG Reserved ;
 } ;













 struct PropInfo
 {
 UWORD Flags ;









 UWORD HorizPot ;
 UWORD VertPot ;


















 UWORD HorizBody ;
 UWORD VertBody ;


 UWORD CWidth ;
 UWORD CHeight ;
 UWORD HPotRes , VPotRes ;
 UWORD LeftBorder ;
 UWORD TopBorder ;
 } ;






























 struct StringInfo
 {

 UBYTE * Buffer ;
 UBYTE * UndoBuffer ;
 WORD BufferPos ;
 WORD MaxChars ;
 WORD DispPos ;


 WORD UndoPos ;
 WORD NumChars ;
 WORD DispCount ;
 WORD CLeft , CTop ;








 struct StringExtend * Extension ;






 LONG LongInt ;






 struct KeyMap * AltKeyMap ;
 } ;








 struct IntuiText
 {
 UBYTE FrontPen , BackPen ;
 UBYTE DrawMode ;
 WORD LeftEdge ;
 WORD TopEdge ;
 struct TextAttr * ITextFont ;
 UBYTE * IText ;
 struct IntuiText * NextText ;
 } ;


















 struct Border
 {
 WORD LeftEdge , TopEdge ;
 UBYTE FrontPen , BackPen ;
 UBYTE DrawMode ;
 BYTE Count ;
 WORD * XY ;
 struct Border * NextBorder ;
 } ;












 struct Image
 {
 WORD LeftEdge ;
 WORD TopEdge ;
 WORD Width ;
 WORD Height ;
 WORD Depth ;
 UWORD * ImageData ;

































 UBYTE PlanePick , PlaneOnOff ;





 struct Image * NextImage ;
 } ;









 struct IntuiMessage
 {
 struct Message ExecMessage ;




 ULONG Class ;


 UWORD Code ;


 UWORD Qualifier ;




 APTR IAddress ;







 WORD MouseX , MouseY ;




 ULONG Seconds , Micros ;




 struct Window * IDCMPWindow ;


 struct IntuiMessage * SpecialLink ;
 } ;


















 struct ExtIntuiMessage
 {
 struct IntuiMessage eim_IntuiMessage ;
 struct TabletData * eim_TabletData ;
 } ;





































































 struct IBox
 {
 WORD Left ;
 WORD Top ;
 WORD Width ;
 WORD Height ;
 } ;






 struct Window
 {
 struct Window * NextWindow ;

 WORD LeftEdge , TopEdge ;
 WORD Width , Height ;

 WORD MouseY , MouseX ;

 WORD MinWidth , MinHeight ;
 UWORD MaxWidth , MaxHeight ;

 ULONG Flags ;

 struct Menu * MenuStrip ;

 UBYTE * Title ;

 struct Requester * FirstRequest ;

 struct Requester * DMRequest ;

 WORD ReqCount ;

 struct Screen * WScreen ;
 struct RastPort * RPort ;











 BYTE BorderLeft , BorderTop , BorderRight , BorderBottom ;
 struct RastPort * BorderRPort ;







 struct Gadget * FirstGadget ;


 struct Window * Parent , * Descendant ;




 UWORD * Pointer ;
 BYTE PtrHeight ;
 BYTE PtrWidth ;
 BYTE XOffset , YOffset ;


 ULONG IDCMPFlags ;
 struct MsgPort * UserPort , * WindowPort ;
 struct IntuiMessage * MessageKey ;

 UBYTE DetailPen , BlockPen ;





 struct Image * CheckMark ;

 UBYTE * ScreenTitle ;







 WORD GZZMouseX ;
 WORD GZZMouseY ;



 WORD GZZWidth ;
 WORD GZZHeight ;

 UBYTE * ExtData ;

 BYTE * UserData ;




 struct Layer * WLayer ;




 struct TextFont * IFont ;





 ULONG MoreFlags ;


 } ;




































































 struct NewWindow
 {
 WORD LeftEdge , TopEdge ;
 WORD Width , Height ;

 UBYTE DetailPen , BlockPen ;

 ULONG IDCMPFlags ;

 ULONG Flags ;






 struct Gadget * FirstGadget ;





 struct Image * CheckMark ;

 UBYTE * Title ;






 struct Screen * Screen ;





 struct BitMap * BitMap ;













 WORD MinWidth , MinHeight ;
 UWORD MaxWidth , MaxHeight ;






 UWORD Type ;

 } ;








 struct ExtNewWindow
 {
 WORD LeftEdge , TopEdge ;
 WORD Width , Height ;

 UBYTE DetailPen , BlockPen ;
 ULONG IDCMPFlags ;
 ULONG Flags ;
 struct Gadget * FirstGadget ;

 struct Image * CheckMark ;

 UBYTE * Title ;
 struct Screen * Screen ;
 struct BitMap * BitMap ;

 WORD MinWidth , MinHeight ;
 UWORD MaxWidth , MaxHeight ;









 UWORD Type ;










 struct TagItem * Extension ;
 } ;
#line 66 "intuition\screens.h"
 struct DrawInfo
 {
 UWORD dri_Version ;
 UWORD dri_NumPens ;
 UWORD * dri_Pens ;

 struct TextFont * dri_Font ;
 UWORD dri_Depth ;

 struct {
 UWORD X ;
 UWORD Y ;
 } dri_Resolution ;

 ULONG dri_Flags ;

 struct Image * dri_CheckMark ;


 struct Image * dri_AmigaKey ;


 ULONG dri_Reserved [ 5 ] ;
 } ;











































 struct Screen
 {
 struct Screen * NextScreen ;
 struct Window * FirstWindow ;

 WORD LeftEdge , TopEdge ;
 WORD Width , Height ;

 WORD MouseY , MouseX ;

 UWORD Flags ;

 UBYTE * Title ;
 UBYTE * DefaultTitle ;









 BYTE BarHeight , BarVBorder , BarHBorder , MenuVBorder , MenuHBorder ;
 BYTE WBorTop , WBorLeft , WBorRight , WBorBottom ;

 struct TextAttr * Font ;


 struct ViewPort ViewPort ;
 struct RastPort RastPort ;
 struct BitMap BitMap ;
 struct Layer_Info LayerInfo ;




 struct Gadget * FirstGadget ;

 UBYTE DetailPen , BlockPen ;




 UWORD SaveColor0 ;


 struct Layer * BarLayer ;

 UBYTE * ExtData ;

 UBYTE * UserData ;


 } ;

































































































































































































































































































 struct NewScreen
 {
 WORD LeftEdge , TopEdge , Width , Height , Depth ;

 UBYTE DetailPen , BlockPen ;

 UWORD ViewModes ;

 UWORD Type ;

 struct TextAttr * Font ;

 UBYTE * DefaultTitle ;

 struct Gadget * Gadgets ;







 struct BitMap * CustomBitMap ;
 } ;












 struct ExtNewScreen
 {
 WORD LeftEdge , TopEdge , Width , Height , Depth ;
 UBYTE DetailPen , BlockPen ;
 UWORD ViewModes ;
 UWORD Type ;
 struct TextAttr * Font ;
 UBYTE * DefaultTitle ;
 struct Gadget * Gadgets ;
 struct BitMap * CustomBitMap ;

 struct TagItem * Extension ;



 } ;




















 struct PubScreenNode {
 struct Node psn_Node ;
 struct Screen * psn_Screen ;
 UWORD psn_Flags ;
 WORD psn_Size ;
 WORD psn_VisitorCount ;
 struct Task * psn_SigTask ;
 UBYTE psn_SigBit ;
 } ;























































































 struct ScreenBuffer
 {
 struct BitMap * sb_BitMap ;
 struct DBufInfo * sb_DBufInfo ;
 } ;
#line 58 "intuition\preferences.h"
 struct Preferences
 {

 BYTE FontHeight ;


 UBYTE PrinterPort ;


 UWORD BaudRate ;


 struct timeval KeyRptSpeed ;
 struct timeval KeyRptDelay ;
 struct timeval DoubleClick ;


 UWORD PointerMatrix [ ( 1 + 16 + 1 ) * 2 ] ;
 BYTE XOffset ;
 BYTE YOffset ;
 UWORD color17 ;
 UWORD color18 ;
 UWORD color19 ;
 UWORD PointerTicks ;


 UWORD color0 ;
 UWORD color1 ;
 UWORD color2 ;
 UWORD color3 ;


 BYTE ViewXOffset ;
 BYTE ViewYOffset ;
 WORD ViewInitX , ViewInitY ;

 BOOL EnableCLI ;


 UWORD PrinterType ;
 UBYTE PrinterFilename [ 30 ] ;


 UWORD PrintPitch ;
 UWORD PrintQuality ;
 UWORD PrintSpacing ;
 UWORD PrintLeftMargin ;
 UWORD PrintRightMargin ;
 UWORD PrintImage ;
 UWORD PrintAspect ;
 UWORD PrintShade ;
 WORD PrintThreshold ;


 UWORD PaperSize ;
 UWORD PaperLength ;
 UWORD PaperType ;



 UBYTE SerRWBits ;

 UBYTE SerStopBuf ;

 UBYTE SerParShk ;

 UBYTE LaceWB ;

 UBYTE Pad [ 12 ] ;
 UBYTE PrtDevName [ 16 ] ;


 UBYTE DefaultPrtUnit ;
 UBYTE DefaultSerUnit ;

 BYTE RowSizeChange ;
 BYTE ColumnSizeChange ;

 UWORD PrintFlags ;
 UWORD PrintMaxWidth ;
 UWORD PrintMaxHeight ;
 UBYTE PrintDensity ;
 UBYTE PrintXOffset ;

 UWORD wb_Width ;
 UWORD wb_Height ;
 UBYTE wb_Depth ;

 UBYTE ext_size ;

 } ;
#line 1430 "intuition\intuition.h"
 struct Remember
 {
 struct Remember * NextRemember ;
 ULONG RememberSize ;
 UBYTE * Memory ;
 } ;










 struct ColorSpec
 {
 WORD ColorIndex ;
 UWORD Red ;
 UWORD Green ;
 UWORD Blue ;
 } ;




 struct EasyStruct {
 ULONG es_StructSize ;
 ULONG es_Flags ;
 UBYTE * es_Title ;
 UBYTE * es_TextFormat ;
 UBYTE * es_GadgetFormat ;
 } ;




























































































































































































 struct TabletData
 {



 UWORD td_XFraction , td_YFraction ;


 ULONG td_TabletX , td_TabletY ;




 ULONG td_RangeX , td_RangeY ;




 struct TagItem * td_TagList ;
 } ;

















 struct TabletHookData
 {





 struct Screen * thd_Screen ;




 ULONG thd_Width ;
 ULONG thd_Height ;




 LONG thd_ScreenChanged ;
 } ;
#line 74 "mgl\context.h"
 typedef struct LockTimeHandle_s
 {
 ULONG s_hi , s_lo ;
 ULONG e_freq ;
 } LockTimeHandle ;

#pragma default-align

 typedef struct GLcontext_t * GLcontext ;

 struct GLcontext_t ;

 typedef void ( * DrawFn ) ( struct GLcontext_t * ) ;

 typedef enum
 {
 MGLKEY_F1 , MGLKEY_F2 , MGLKEY_F3 , MGLKEY_F4 , MGLKEY_F5 , MGLKEY_F6 , MGLKEY_F7 , MGLKEY_F8 ,
 MGLKEY_F9 , MGLKEY_F10 ,
 MGLKEY_CUP , MGLKEY_CDOWN , MGLKEY_CLEFT , MGLKEY_CRIGHT
 } MGLspecial ;

 typedef enum
 {
 GLCS_TEXTURE = 0x01 ,
 GLCS_COLOR = 0x02 ,
 GLCS_VERTEX = 0x04 ,
 GLCS_MASK = 0x07 ,
 } ClientStates ;

 typedef void ( * KeyHandlerFn ) ( char key ) ;
 typedef void ( * SpecialHandlerFn ) ( MGLspecial special_key ) ;
 typedef void ( * MouseHandlerFn ) ( GLint x , GLint y , GLbitfield buttons ) ;
 typedef void ( * IdleFn ) ( void ) ;
































 typedef enum
 {
 MGLA_COLOR_UBYTE_RGB = 0 ,
 MGLA_COLOR_UBYTE_RGBA = 1 ,
 MGLA_COLOR_FLOAT_RGB = 2 ,
 MGLA_COLOR_FLOAT_RGBA = 3
 } MGLAColorMode ;

 typedef struct MGLAPointer_s
 {
 GLubyte * texcoords ;
 GLint texcoordstride ;
 GLint texcoordsize ;

 GLubyte * colors ;
 GLint colorstride ;
 ULONG colormode ;

 GLubyte * verts ;
 GLint vertexstride ;
 GLint vertexsize ;


 GLuint lockfirst ;
 GLsizei locksize ;
 GLuint transformed ;

 GLbitfield state ;

 } MGLAPointer ;


 typedef struct GLarray_t
 {




 GLint size ;
 GLenum type ;
 GLsizei stride ;
 GLvoid * pointer ;
 } GLarray ;

 struct GLcontext_t
 {





 GLenum CurrentPrimitive ;




 GLenum CurrentError ;







 int ModelViewStackPointer ;
 Matrix ModelViewStack [ 40 ] ;

 int ProjectionStackPointer ;
 Matrix ProjectionStack [ 5 ] ;






 GLuint ModelViewNr ;
 Matrix ModelView [ 2 ] ;




 GLuint ProjectionNr ;
 Matrix Projection [ 2 ] ;





 GLuint CurrentMatrixMode ;






 GLfloat * WBuffer ;

 UWORD * ElementIndex ;








 MGLVertex * VertexBuffer ;
 GLuint VertexBufferPointer ;
 GLuint VertexBufferSize ;

 MGLNormal * NormalBuffer ;
 GLuint NormalBufferPointer ;




 GLuint ClearColor ;
 double ClearDepth ;
 MGLColor CurrentColor ;


 GLboolean UpdateCurrentColor ;

 GLfloat CurrentTexS , CurrentTexT , CurrentTexQ ;
 GLboolean CurrentTexQValid ;





 GLboolean CombinedValid ;
 Matrix CombinedMatrix ;







 GLfloat sx , ax ;
 GLfloat sy , ay ;
 GLfloat sz , az ;
 GLdouble near , far ;

 GLuint ClipFlags ;
 GLboolean GuardBand ;


 GLenum CurrentCullFace ;
 GLenum CurrentFrontFace ;






 GLint CurrentCullSign ;


 GLint PackAlign ;
 GLint UnpackAlign ;




 GLboolean AlphaTest_State ;
 GLboolean Blend_State ;
 GLboolean Texture2D_State [ 2 ] ;
 GLboolean TextureGenS_State ;
 GLboolean TextureGenT_State ;
 GLboolean Fog_State ;
 GLboolean Scissor_State ;
 GLboolean CullFace_State ;
 GLboolean DepthTest_State ;
 GLboolean PointSmooth_State ;
 GLboolean Dither_State ;
 GLboolean ZOffset_State ;





 GLboolean FogDirty ;

 GLdouble FogStart ;
 GLdouble FogEnd ;





 DrawFn CurrentDraw ;










 V3DDevice device ;
 V3DContext backend ;

 struct Window * v3dWindow ;
 struct Screen * v3dScreen ;














 struct Window * inputWindow ;

 GLboolean ArrayTexBound ;

 GLint CurrentBinding ;


 GLint VirtualBinding ;
 GLuint VirtualTexUnits ;
 GLuint ActiveTexture ;

 V3DTexture * * textureObjects ;
 GLint textureObjectCount ;
 struct ScreenBuffer * Buffers [ 3 ] ;
 struct BitMap * v3dBitMap ;
 struct RastPort * v3dRastPort ;
 int BufNr ;
 int NumBuffers ;

 APTR bitmapLock ;
 ULONG vmembase ;
 ULONG bprow ;





 APTR bitmapLocks [ 2 ] ;
 ULONG vmembases [ 3 ] ;












 struct MsgPort * dbufSafePort ;
 struct MsgPort * dbufDispPort ;
 GLboolean dbufSafeToWrite ;
 GLboolean dbufSafeToChange ;
















 struct BitMap * dbufBitMaps [ 2 ] ;
 struct DBufInfo * dbufInfo ;

 GLboolean v3dLocked ;


 GLenum LockMode ;
 LockTimeHandle LockTime ;

 GLboolean DoSync ;

 GLenum TexEnv [ 2 ] ;
 GLenum CurTexEnv ;
 GLenum MinFilter ;
 GLenum MagFilter ;
 GLenum WrapS ;
 GLenum WrapT ;

 GLfloat FogRange ;
 GLfloat FogMult ;
 GLenum ShadeModel ;
 GLboolean DepthMask ;

 GLboolean NoMipMapping ;
 GLboolean NoFallbackAlpha ;

 KeyHandlerFn KeyHandler ;
 MouseHandlerFn MouseHandler ;
 SpecialHandlerFn SpecialHandler ;
 IdleFn Idle ;
 GLboolean Running ;

 GLenum SrcAlpha ;
 GLenum DstAlpha ;
 GLboolean AlphaFellBack ;

 GLfloat InvRot [ 9 ] ;

 GLboolean InvRotValid ;
 GLboolean WOne_Hint ;
 GLboolean FixpointTrans_Hint ;

 GLfloat ZOffset ;

 void * PaletteData ;
 GLenum PaletteFormat ;
 GLint PaletteSize ;





 GLint CurPolygonMode ;
 GLint CurShadeModel ;
 GLint CurBlendSrc ;
 GLint CurBlendDst ;
 GLint CurUnpackRowLength ;
 GLint CurUnpackSkipPixels ;
 GLint CurUnpackSkipRows ;








 GLboolean CurWriteMask ;
 GLboolean CurDepthTest ;








 GLbitfield ClientState ;

 MGLAPointer ArrayPointer ;
 GLboolean VertexArrayPipeline ;

 GLfloat MinTriArea ;

 GLfloat CurrentPointSize ;

 GLubyte * GeneratedTextures ;

 } ;
#line 24 "mgl\clip.h"
 typedef struct MGLPolygon_t
 {
 int numverts ;
 int verts [ 1024 ] ;
 } MGLPolygon ;




 typedef struct PolyBuffer_s
 {
 int numverts ;
 int nextfree ;
 ULONG type ;
 ULONG verts [ 64 ] ;
 } PolyBuffer ;


 void dh_DrawLine ( GLcontext context , MGLPolygon * poly ) ;
 void dh_DrawPoly ( GLcontext context , MGLPolygon * poly ) ;

 void hc_ClipPoly ( GLcontext context , MGLPolygon * poly , PolyBuffer * out , int clipstart , ULONG or_codes ) ;

 void hc_ClipPolyFF ( GLcontext context , MGLPolygon * poly , ULONG or_codes ) ;

 GLboolean hc_DecideFrontface ( GLcontext context , MGLVertex * a , MGLVertex * b , MGLVertex * c ) ;
#line 24 "mgl\modes.h"
 typedef struct
 {
 GLint id ;
 GLint width , height ;
 GLint bit_depth ;
 char mode_name [ 80 ] ;
 } MGLScreenMode ;

 typedef struct
 {
 ULONG width , height , depth ;
 ULONG pixel_format ;
 void * base_address ;
 ULONG pitch ;
 } MGLLockInfo ;

 typedef GLboolean ( * MGLScreenModeCallback ) ( MGLScreenMode * ) ;
#line 348 "mgl\gl.h"
 extern GLcontext mini_CurrentContext ;






























 void GLActiveTextureARB ( GLcontext context , GLenum unit ) ;
 void GLMultiTexCoord2fARB ( GLcontext context , GLenum unit , GLfloat s , GLfloat t ) ;
 void GLMultiTexCoord2fvARB ( GLcontext context , GLenum unit , GLfloat * v ) ;

 void MGLDrawMultitexBuffer ( GLcontext context , GLenum BSrc , GLenum BDst , GLenum TexEnv ) ;



 void GLAlphaFunc ( GLcontext context , GLenum func , GLclampf ref ) ;

 void GLArrayElement ( GLcontext context , GLint i ) ;
 void GLBegin ( GLcontext context , GLenum mode ) ;
 void GLBindTexture ( GLcontext context , GLenum target , GLuint texture ) ;
 void GLBlendFunc ( GLcontext context , GLenum sfactor , GLenum dfactor ) ;
 void GLClear ( GLcontext context , GLbitfield mask ) ;
 void GLClearColor ( GLcontext context , GLclampf red , GLclampf green , GLclampf blue , GLclampf alpha ) ;
 void GLClearDepth ( GLcontext context , GLclampd depth ) ;
 void GLColor3fv ( GLcontext context , GLfloat * v ) ;
 void GLColor3ubv ( GLcontext context , GLubyte * v ) ;
 void GLColor4f ( GLcontext context , GLfloat red , GLfloat green , GLfloat blue , GLfloat alpha ) ;
 void GLColor4fv ( GLcontext context , GLfloat * v ) ;
 void GLColor4ub ( GLcontext context , GLubyte red , GLubyte green , GLubyte blue , GLubyte alhpa ) ;
 void GLColor4ubv ( GLcontext context , GLubyte * v ) ;

 void GLColorTable ( GLcontext context , GLenum target , GLenum internalformat , GLint width , GLenum format , GLenum type , GLvoid * data ) ;
 void GLCullFace ( GLcontext context , GLenum mode ) ;
 void GLDeleteTextures ( GLcontext context , GLsizei n , const GLuint * textures ) ;
 void GLDepthFunc ( GLcontext context , GLenum func ) ;
 void GLDepthMask ( GLcontext context , GLboolean flag ) ;
 void GLDepthRange ( GLcontext context , GLclampd n , GLclampd f ) ;
 void GLDisableClientState ( GLcontext context , GLenum cap ) ;

 void GLDrawBuffer ( GLcontext context , GLenum mode ) ;




 void GLEnableClientState ( GLcontext context , GLenum cap ) ;

 void GLInterleavedArrays ( GLcontext context , GLenum format , GLsizei stride , const GLvoid * pointer ) ;

 void GLEnd ( GLcontext context ) ;
 void GLFinish ( GLcontext context ) ;
 void GLFlush ( GLcontext context ) ;
 void GLFogf ( GLcontext context , GLenum pname , GLfloat param ) ;
 void GLFogfv ( GLcontext context , GLenum pname , GLfloat * param ) ;
 void GLFrontFace ( GLcontext context , GLenum mode ) ;
 void GLFrustum ( GLcontext context , GLdouble left , GLdouble right , GLdouble bottom , GLdouble top , GLdouble zNear , GLdouble zFar ) ;
 void GLGenTextures ( GLcontext context , GLsizei n , GLuint * textures ) ;
 void GLGetBooleanv ( GLcontext context , GLenum pname , GLboolean * params ) ;

 GLenum GLGetError ( GLcontext context ) ;
 void GLGetFloatv ( GLcontext context , GLenum pname , GLfloat * params ) ;
 void GLGetIntegerv ( GLcontext context , GLenum pname , GLint * params ) ;
 void GLGetPointerv ( GLcontext context , GLenum pname , GLvoid * * params ) ;
 const GLubyte * GLGetString ( GLcontext context , GLenum name ) ;
 void GLHint ( GLcontext context , GLenum target , GLenum mode ) ;




 GLboolean GLIsEnabled ( GLcontext context , GLenum cap ) ;

 void GLLoadIdentity ( GLcontext context ) ;
 void GLLoadMatrixd ( GLcontext context , const GLdouble * m ) ;
 void GLLoadMatrixf ( GLcontext context , const GLfloat * m ) ;
 void GLMatrixMode ( GLcontext context , GLenum mode ) ;
 void GLMultMatrixd ( GLcontext context , const GLdouble * m ) ;
 void GLMultMatrixf ( GLcontext context , const GLfloat * m ) ;
 void GLNormal3f ( GLcontext context , GLfloat x , GLfloat y , GLfloat z ) ;
 void GLOrtho ( GLcontext context , GLdouble left , GLdouble right , GLdouble bottom , GLdouble top , GLdouble zNear , GLdouble zFar ) ;
 void GLPixelStorei ( GLcontext context , GLenum pname , GLint param ) ;
 void GLPolygonMode ( GLcontext context , GLenum face , GLenum mode ) ;


 void GLPointSize ( GLcontext context , GLfloat size ) ;

 void GLPopMatrix ( GLcontext context ) ;

 void GLPushMatrix ( GLcontext context ) ;

 void GLReadPixels ( GLcontext context , GLint x , GLint y , GLsizei width , GLsizei height , GLenum format , GLenum type , GLvoid * pixels ) ;
 void GLRotated ( GLcontext context , GLdouble angle , GLdouble x , GLdouble y , GLdouble z ) ;
 void GLRotatef ( GLcontext context , GLfloat angle , GLfloat x , GLfloat y , GLfloat z ) ;













 void GLRotatefEXT ( GLcontext context , GLfloat angle , const GLint xyz ) ;

 void GLRotatefEXTs ( GLcontext context , GLfloat sin_an , GLfloat cos_an , const GLint xyz ) ;

 void GLScaled ( GLcontext context , GLdouble x , GLdouble y , GLdouble z ) ;
 void GLScalef ( GLcontext context , GLfloat x , GLfloat y , GLfloat z ) ;
 void GLScissor ( GLcontext context , GLint x , GLint y , GLsizei width , GLsizei height ) ;
 void GLShadeModel ( GLcontext context , GLenum mode ) ;
 void GLTexCoord2f ( GLcontext context , GLfloat s , GLfloat t ) ;
 void GLTexCoord2fv ( GLcontext context , GLfloat * v ) ;
 void GLTexCoord4f ( GLcontext context , GLfloat s , GLfloat t , GLfloat r , GLfloat q ) ;
 void GLTexCoord4fv ( GLcontext context , GLfloat * v ) ;

 void GLTexEnvi ( GLcontext context , GLenum target , GLenum pname , GLint param ) ;
 void GLTexGeni ( GLcontext context , GLenum coord , GLenum mode , GLenum map ) ;
 void GLTexImage2D ( GLcontext context , GLenum target , GLint level , GLint internalformat , GLsizei width , GLsizei height , GLint border , GLenum format , GLenum type , const GLvoid * pixels ) ;
 void GLTexParameteri ( GLcontext context , GLenum target , GLenum pname , GLint param ) ;
 void GLTexSubImage2D ( GLcontext context , GLenum target , GLint level , GLint xoffset , GLint yoffset , GLsizei width , GLsizei height , GLenum format , GLenum type , const GLvoid * pixels ) ;
 void GLTranslated ( GLcontext context , GLdouble x , GLdouble y , GLdouble z ) ;
 void GLTranslatef ( GLcontext context , GLfloat x , GLfloat y , GLfloat z ) ;
 void GLVertex4f ( GLcontext context , GLfloat x , GLfloat y , GLfloat z , GLfloat w ) ;
 void GLVertex2f ( GLcontext context , GLfloat x , GLfloat y ) ;

 void GLVertex4fv ( GLcontext context , GLfloat * v ) ;
 void GLVertex3fv ( GLcontext context , GLfloat * v ) ;
 void GLVertex2fv ( GLcontext context , GLfloat * v ) ;


 void GLViewport ( GLcontext context , GLint x , GLint y , GLsizei width , GLsizei height ) ;

 void GLLockArrays ( GLcontext context , GLuint first , GLsizei count ) ;
 void GLUnlockArrays ( GLcontext context ) ;

 void mglChooseGuardBand ( GLboolean flag ) ;
 void mglChooseNumberOfBuffers ( int number ) ;
 void mglChoosePixelDepth ( int depth ) ;
 void mglChooseTextureBufferSize ( int size ) ;
 void mglChooseVertexBufferSize ( int size ) ;
 void mglChooseMtexBufferSize ( int size ) ;
 void mglChooseWindowMode ( GLboolean flag ) ;
 void * MGLCreateContext ( int offx , int offy , int w , int h ) ;
 void MGLDeleteContext ( GLcontext context ) ;
 void MGLEnableSync ( GLcontext context , GLboolean enable ) ;
 void MGLExit ( GLcontext context ) ;
 void * MGLGetWindowHandle ( GLcontext context ) ;
 void MGLIdleFunc ( GLcontext context , IdleFn i ) ;
 void MGLKeyFunc ( GLcontext context , KeyHandlerFn k ) ;
 GLboolean MGLLockDisplay ( GLcontext context ) ;
 void MGLMainLoop ( GLcontext context ) ;
 void MGLMinTriArea ( GLcontext context , GLfloat area ) ;
 void MGLMouseFunc ( GLcontext context , MouseHandlerFn m ) ;
 void mglProhibitAlphaFallback ( GLboolean flag ) ;
 void mglProhibitMipMapping ( GLboolean flag ) ;
 void mglProposeCloseDesktop ( GLboolean closeme ) ;
 void MGLResizeContext ( GLcontext context , GLsizei width , GLsizei height ) ;
 void MGLSetState ( GLcontext context , GLenum cap , GLboolean state ) ;
 void MGLSpecialFunc ( GLcontext context , SpecialHandlerFn s ) ;
 void MGLSwitchBuffer ( GLcontext context , int bufnr ) ;
 void MGLSwitchDisplay ( GLcontext context ) ;
 void MGLTexMemStat ( GLcontext context , GLint * Current , GLint * Peak ) ;
 void MGLUnlockDisplay ( GLcontext context ) ;
 void MGLWriteShotPPM ( GLcontext context , char * filename ) ;
 GLboolean MGLInit ( void ) ;
 void MGLTerm ( void ) ;


 void MGLLockMode ( GLcontext context , GLenum lockMode ) ;


 void MGLPrintMatrix ( GLcontext context , int mode ) ;
 void MGLPrintMatrixStack ( GLcontext context , int mode ) ;

 void MGLSetZOffset ( GLcontext context , GLfloat offset ) ;

 void GLULookAt ( GLfloat ex , GLfloat ey , GLfloat ez , GLfloat cx , GLfloat cy , GLfloat cz , GLfloat ux , GLfloat uy , GLfloat uz ) ;
 void GLUPerspective ( GLfloat fovy , GLfloat aspect , GLfloat znear , GLfloat zfar ) ;

 GLint mglGetSupportedScreenModes ( MGLScreenModeCallback CallbackFn ) ;
 void * MGLCreateContextFromID ( GLint ID , GLint * w , GLint * h ) ;
 GLboolean MGLLockBack ( GLcontext context , MGLLockInfo * info ) ;

 void GLEnableClientState ( GLcontext context , GLenum state ) ;
 void GLDisableClientState ( GLcontext context , GLenum state ) ;

 void GLTexCoordPointer ( GLcontext context , GLint size , GLenum type , GLsizei stride , const GLvoid * pointer ) ;
 void GLColorPointer ( GLcontext context , GLint size , GLenum type , GLsizei stride , const GLvoid * pointer ) ;
 void GLVertexPointer ( GLcontext context , GLint size , GLenum type , GLsizei stride , const GLvoid * pointer ) ;

 void GLDrawElements ( GLcontext context , GLenum mode , GLsizei count , GLenum type , const GLvoid * indices ) ;
 void GLDrawArrays ( GLcontext context , GLenum mode , GLint first , GLsizei count ) ;
#line 33 "glquake.h"
 void GL_BeginRendering ( int * x , int * y , int * width , int * height ) ;
 void GL_EndRendering ( void ) ;























 extern int texture_extension_number ;
 extern int texture_mode ;
 extern float gldepthmin , gldepthmax ;

 void GL_Upload32 ( unsigned * data , int width , int height , qboolean mipmap , qboolean alpha ) ;
 void GL_Upload8 ( byte * data , int width , int height , qboolean mipmap , qboolean alpha ) ;
 int GL_LoadTexture ( char * identifier , int width , int height , byte * data , qboolean mipmap , qboolean alpha ) ;
 int GL_FindTexture ( char * identifier ) ;

 typedef struct
 {
 float x , y , z ;
 float s , t ;
 float r , g , b ;

 } glvert_t ;

 extern glvert_t glv ;

 extern int glx , gly , glwidth , glheight ;


















 void R_TimeRefresh_f ( void ) ;
 void R_ReadPointFile_f ( void ) ;
 texture_t * R_TextureAnimation ( texture_t * base ) ;

 typedef struct surfcache_s
 {
 struct surfcache_s * next ;
 struct surfcache_s * * owner ;
 int lightadj [ 4 ] ;
 int dlight ;
 int size ;
 unsigned width ;
 unsigned height ;
 float mipscale ;
 struct texture_s * texture ;
 byte data [ 4 ] ;

 } surfcache_t ;


 typedef struct
 {
 pixel_t * surfdat ;
 int rowbytes ;
 msurface_t * surf ;
 fixed8_t lightadj [ 4 ] ;

 texture_t * texture ;
 int surfmip ;
 int surfwidth ;
 int surfheight ;

 } drawsurf_t ;


 typedef enum
 {
 pt_static , pt_grav , pt_slowgrav , pt_fire , pt_explode , pt_explode2 , pt_blob , pt_blob2

 } ptype_t ;


 typedef struct particle_s
 {

 vec3_t org ;
 float color ;


 struct particle_s * next ;
 vec3_t vel ;
 float ramp ;
 float die ;
 ptype_t type ;

 } particle_t ;





 extern entity_t r_worldentity ;
 extern qboolean r_cache_thrash ;
 extern vec3_t modelorg , r_entorigin ;
 extern entity_t * currententity ;
 extern int r_visframecount ;
 extern int r_framecount ;
 extern mplane_t frustum [ 4 ] ;
 extern int c_brush_polys , c_alias_polys ;





 extern vec3_t vup ;
 extern vec3_t vpn ;
 extern vec3_t vright ;
 extern vec3_t r_origin ;




 extern refdef_t r_refdef ;
 extern mleaf_t * r_viewleaf , * r_oldviewleaf ;
 extern texture_t * r_notexture_mip ;
 extern int d_lightstylevalue [ 256 ] ;

 extern qboolean envmap ;
 extern int currenttexture ;
 extern int cnttextures [ 2 ] ;
 extern int particletexture ;
 extern int playertextures ;

 extern int skytexturenum ;

 extern cvar_t r_norefresh ;
 extern cvar_t r_drawentities ;
 extern cvar_t r_drawworld ;
 extern cvar_t r_drawviewmodel ;
 extern cvar_t r_speeds ;
 extern cvar_t r_waterwarp ;
 extern cvar_t r_fullbright ;
 extern cvar_t r_lightmap ;
 extern cvar_t r_shadows ;
 extern cvar_t r_mirroralpha ;
 extern cvar_t r_wateralpha ;
 extern cvar_t r_dynamic ;
 extern cvar_t r_novis ;


 extern cvar_t r_vertexarrays ;
 extern cvar_t r_litfiles ;
 extern cvar_t r_skybox_format ;
 extern cvar_t r_cull_aggressive ;
 extern cvar_t r_flames ;
 extern cvar_t r_elim_areasize ;
 extern cvar_t gl_fake_multitexture ;
 extern cvar_t gl_triplebuffer ;
 extern cvar_t gl_glows ;


 extern cvar_t gl_clear ;
 extern cvar_t gl_cull ;
 extern cvar_t gl_poly ;
 extern cvar_t gl_texsort ;
 extern cvar_t gl_smoothmodels ;
 extern cvar_t gl_affinemodels ;
 extern cvar_t gl_polyblend ;
 extern cvar_t gl_keeptjunctions ;
 extern cvar_t gl_reporttjunctions ;
 extern cvar_t gl_flashblend ;
 extern cvar_t gl_nocolors ;
 extern cvar_t gl_doubleeyes ;


 extern int gl_lightmap_format ;















 extern cvar_t gl_max_size ;
 extern cvar_t gl_playermip ;

 extern int mirrortexturenum ;
 extern qboolean mirror ;
 extern mplane_t * mirror_plane ;

 extern float r_world_matrix [ 16 ] ;

 extern const char * gl_vendor ;
 extern const char * gl_renderer ;
 extern const char * gl_version ;
 extern const char * gl_extensions ;

 void R_TranslatePlayerSkin ( int playernum ) ;













 extern qboolean gl_mtexable ;

 void GL_DisableMultitexture ( void ) ;
 void GL_EnableMultitexture ( void ) ;




 extern void GL_SubdivideSurface ( msurface_t * fa ) ;
 extern void EmitBothSkyLayers ( msurface_t * fa ) ;
 extern void EmitWaterPolys ( msurface_t * fa ) ;
 extern void EmitSkyPolys ( msurface_t * fa ) ;
 extern void R_DrawSkyChain ( msurface_t * s ) ;




 extern int GL_LoadPicTexture ( qpic_t * pic ) ;
 extern void GL_Set2D ( void ) ;












 extern void R_MarkLights ( dlight_t * light , int bit , mnode_t * node ) ;
 extern void R_AnimateLight ( void ) ;
 extern void R_RenderDlights ( void ) ;
 extern int R_LightPoint ( vec3_t p ) ;




 extern void R_StoreEfrags ( efrag_t * * ppefrag ) ;




 extern void GL_MakeAliasModelDisplayLists ( model_t * m , aliashdr_t * hdr ) ;




 extern void R_DrawBrushModel ( entity_t * e ) ;
 extern void R_DrawWorld ( void ) ;
 extern void GL_BuildLightmaps ( void ) ;
#line 10 "location.h"
 typedef struct
 {
 vec3_t a ;
 vec3_t b ;
 char name [ 32 ] ;
 } location_t ;


 void LOC_LoadLocations ( void ) ;


 char * LOC_GetLocation ( vec3_t p ) ;
#line 288 "quakedef.h"
 typedef struct
 {
 char * basedir ;
 char * cachedir ;
 int argc ;
 char * * argv ;
 void * membase ;
 int memsize ;

 } quakeparms_t ;






 extern qboolean noclip_anglehack ;





 extern quakeparms_t host_parms ;

 extern cvar_t sys_ticrate ;
 extern cvar_t sys_nostdout ;
 extern cvar_t developer ;
 extern cvar_t max_edicts ;

 extern qboolean host_initialized ;
 extern double host_frametime ;
 extern byte * host_basepal ;
 extern byte * host_colormap ;
 extern int host_framecount ;
 extern double realtime ;

 extern double last_angle_time ;

 void Host_ClearMemory ( void ) ;
 void Host_ServerFrame ( void ) ;
 void Host_InitCommands ( void ) ;
 void Host_Init ( quakeparms_t * parms ) ;
 void Host_Shutdown ( void ) ;
 void Host_Error ( char * error , ... ) ;
 void Host_EndGame ( char * message , ... ) ;
 void Host_Frame ( float time ) ;
 void Host_Quit_f ( void ) ;
 void Host_ClientCommands ( char * fmt , ... ) ;
 void Host_ShutdownServer ( qboolean crash ) ;

 extern qboolean msg_suppress_1 ;

 extern int current_skill ;



 extern qboolean isDedicated ;

 extern int minimum_memory ;





 extern cvar_t chase_active ;

 void Chase_Init ( void ) ;
 void Chase_Reset ( void ) ;
 void Chase_Update ( void ) ;
#line 24 "mgl\mglmacros.h"
 extern void d_DrawPoints ( GLcontext context ) ;
 extern void d_DrawLines ( GLcontext context ) ;
 extern void d_DrawLineStrip ( GLcontext context ) ;
 extern void d_DrawTriangleFan ( GLcontext context ) ;
 extern void d_DrawTriangleStrip ( GLcontext context ) ;
 extern void d_DrawQuads ( GLcontext context ) ;
 extern void d_DrawNormalPoly ( GLcontext context ) ;
 extern void d_DrawSmoothPoly ( GLcontext context ) ;
 extern void d_DrawMtexPoly ( GLcontext context ) ;
 extern void d_DrawQuadStrip ( GLcontext context ) ;
 extern void d_DrawFlat ( GLcontext context ) ;

 extern GLcontext mini_CurrentContext ;



 static MGLVertex * mglvert = ( ( void * ) 0 ) ;
#line 10 "miniglext.h"
 extern void mglTV23fv ( GLcontext context , GLsizei stride , glpoly_t * p ) ;
 extern void mglTV23fvLM ( GLcontext context , GLsizei stride , glpoly_t * p ) ;
 extern void mglTV23fvLMalpha ( GLcontext context , GLsizei stride , glpoly_t * p ) ;
 extern void mglMtexTV23fv ( GLcontext context , GLsizei stride , glpoly_t * p ) ;
#line 8 "gl_mirror.c"
 qboolean mirror ;
 int mirrortexturenum ;
 mplane_t * mirror_plane ;

 cvar_t r_mirroralpha = { "r_mirroralpha" , "1" } ;




 float r_base_world_matrix [ 16 ] ;






 extern void R_RenderScene ( ) ;











 static void mglTV23fvalpha ( GLcontext context , GLsizei stride , glpoly_t * p )
 {
 int count ;
 GLfloat * v ;

 v = p -> verts [ 0 ] ;
 count = p -> numverts ;

 do
 {
 GLTexCoord2f ( context , v [ 3 ] , v [ 4 ] ) ;
 GLVertex3fv ( context , v ) ;

 v += stride ;

 } while ( -- count ) ;
 }


 void Mirror_Scale ( void )
 {
 if ( mirror_plane -> normal [ 2 ] )
 GLScaled ( mini_CurrentContext , ( GLdouble ) ( 1 ) , ( GLdouble ) ( - 1 ) , ( GLdouble ) ( 1 ) ) ;

 else
 GLScaled ( mini_CurrentContext , ( GLdouble ) ( - 1 ) , ( GLdouble ) ( 1 ) , ( GLdouble ) ( 1 ) ) ;

 GLCullFace ( mini_CurrentContext , GL_BACK ) ;
 }





 void R_Mirror ( void )
 {
 float d ;
 msurface_t * s ;
 entity_t * ent ;
 glpoly_t * p ;
 float mirror_alpha ;
 int cl_oldvisedicts ;


 vec3_t oldvieworg ;
 vec3_t oldviewangles ;
 vec3_t oldvpn ;

 r_drawviewmodel . value = 0 ;

 __asm_memcpy ( r_base_world_matrix , r_world_matrix , sizeof ( r_base_world_matrix ) ) ;

 cl_oldvisedicts = cl_numvisedicts ;


 ent = & cl_entities [ cl . viewentity ] ;

 if ( cl_numvisedicts < 4096 )
 {
 cl_visedicts [ cl_numvisedicts ] = ent ;
 cl_numvisedicts ++ ;
 }

 { oldvieworg [ 0 ] = r_refdef . vieworg [ 0 ] ; oldvieworg [ 1 ] = r_refdef . vieworg [ 1 ] ; oldvieworg [ 2 ] = r_refdef . vieworg [ 2 ] ; } ;
 { oldviewangles [ 0 ] = r_refdef . viewangles [ 0 ] ; oldviewangles [ 1 ] = r_refdef . viewangles [ 1 ] ; oldviewangles [ 2 ] = r_refdef . viewangles [ 2 ] ; } ;
 { oldvpn [ 0 ] = vpn [ 0 ] ; oldvpn [ 1 ] = vpn [ 1 ] ; oldvpn [ 2 ] = vpn [ 2 ] ; } ;

 mirror_alpha = r_mirroralpha . value * ( 1.0 / 2.5 ) ;

 s = cl . worldmodel -> textures [ mirrortexturenum ] -> texturechain ;

 for ( ; s ; s = s -> texturechain )
 {
 mirror_plane = s -> plane ;

 d = ( ( ( mirror_plane ) -> type < 3 ) ? ( oldvieworg ) [ ( mirror_plane ) -> type ] - ( mirror_plane ) -> dist : ( ( oldvieworg ) [ 0 ] * ( mirror_plane ) -> normal [ 0 ] + ( oldvieworg ) [ 1 ] * ( mirror_plane ) -> normal [ 1 ] + ( oldvieworg ) [ 2 ] * ( mirror_plane ) -> normal [ 2 ] ) - ( mirror_plane ) -> dist ) ;




 VectorMA ( oldvieworg , - 2 * d , mirror_plane -> normal , r_refdef . vieworg ) ;

 d = ( oldvpn [ 0 ] * mirror_plane -> normal [ 0 ] + oldvpn [ 1 ] * mirror_plane -> normal [ 1 ] + oldvpn [ 2 ] * mirror_plane -> normal [ 2 ] ) ;
 VectorMA ( oldvpn , - 2 * d , mirror_plane -> normal , vpn ) ;

 r_refdef . viewangles [ 0 ] = - __asm_asin ( vpn [ 2 ] ) / 3.14159265358979323846 * 180 ;
 r_refdef . viewangles [ 1 ] = atan2 ( vpn [ 1 ] , vpn [ 0 ] ) / 3.14159265358979323846 * 180 ;
 r_refdef . viewangles [ 2 ] = - oldviewangles [ 2 ] ;


 gldepthmin = 0.5 ;
 gldepthmax = 1 ;
 GLDepthRange ( mini_CurrentContext , gldepthmin , gldepthmax ) ;
 GLDepthFunc ( mini_CurrentContext , GL_LEQUAL ) ;

 R_RenderScene ( ) ;


 gldepthmin = 0 ;
 gldepthmax = 0.5 ;
 GLDepthRange ( mini_CurrentContext , gldepthmin , gldepthmax ) ;
 GLDepthFunc ( mini_CurrentContext , GL_LEQUAL ) ;







 MGLSetState ( mini_CurrentContext , GL_BLEND , 1 ) ;
 GLTexEnvi ( mini_CurrentContext , GL_TEXTURE_ENV , GL_TEXTURE_ENV_MODE , ( GLint ) GL_MODULATE ) ;

 GLMatrixMode ( mini_CurrentContext , GL_PROJECTION ) ;

 if ( mirror_plane -> normal [ 2 ] )
 GLScaled ( mini_CurrentContext , ( GLdouble ) ( 1 ) , ( GLdouble ) ( - 1 ) , ( GLdouble ) ( 1 ) ) ;

 else
 GLScaled ( mini_CurrentContext , ( GLdouble ) ( - 1 ) , ( GLdouble ) ( 1 ) , ( GLdouble ) ( 1 ) ) ;

 GLCullFace ( mini_CurrentContext , GL_FRONT ) ;

 GLMatrixMode ( mini_CurrentContext , GL_MODELVIEW ) ;

 GLLoadMatrixf ( mini_CurrentContext , r_base_world_matrix ) ;

 { mini_CurrentContext -> CurrentColor . r = ( W3D_Float ) ( 1 ) ; mini_CurrentContext -> CurrentColor . g = ( W3D_Float ) ( 1 ) ; mini_CurrentContext -> CurrentColor . b = ( W3D_Float ) ( 1 ) ; mini_CurrentContext -> CurrentColor . a = ( W3D_Float ) ( mirror_alpha ) ; mini_CurrentContext -> UpdateCurrentColor = 1 ; } ;

 { GLBindTexture ( mini_CurrentContext , GL_TEXTURE_2D , s -> texinfo -> texture -> gl_texturenum ) ; } ;

 p = s -> polys ;

















 { mini_CurrentContext -> CurrentTexQValid = 0 ; switch ( ( int ) ( GL_POLYGON ) ) { case GL_POINTS : mini_CurrentContext -> CurrentPrimitive = ( GLenum ) ( GL_POLYGON ) ; mini_CurrentContext -> CurrentDraw = ( DrawFn ) d_DrawPoints ; break ; case GL_LINES : mini_CurrentContext -> CurrentPrimitive = ( GLenum ) ( GL_POLYGON ) ; mini_CurrentContext -> CurrentDraw = ( DrawFn ) d_DrawLines ; break ; case GL_LINE_STRIP : mini_CurrentContext -> CurrentPrimitive = ( GLenum ) ( GL_POLYGON ) ; mini_CurrentContext -> CurrentDraw = ( DrawFn ) d_DrawLineStrip ; break ; case GL_LINE_LOOP : mini_CurrentContext -> CurrentPrimitive = ( GLenum ) ( GL_POLYGON ) ; mini_CurrentContext -> CurrentDraw = ( DrawFn ) d_DrawLineStrip ; break ; case GL_TRIANGLES : mini_CurrentContext -> CurrentPrimitive = ( GLenum ) ( GL_POLYGON ) ; break ; case GL_TRIANGLE_STRIP : mini_CurrentContext -> CurrentPrimitive = ( GLenum ) ( GL_POLYGON ) ; mini_CurrentContext -> CurrentDraw = ( DrawFn ) d_DrawTriangleStrip ; break ; case GL_TRIANGLE_FAN : mini_CurrentContext -> CurrentPrimitive = ( GLenum ) ( GL_POLYGON ) ; mini_CurrentContext -> CurrentDraw = ( DrawFn ) d_DrawTriangleFan ; break ; case GL_QUADS : mini_CurrentContext -> CurrentPrimitive = ( GLenum ) ( GL_POLYGON ) ; mini_CurrentContext -> CurrentDraw = ( DrawFn ) d_DrawQuads ; break ; case GL_QUAD_STRIP : mini_CurrentContext -> CurrentPrimitive = ( GLenum ) ( GL_POLYGON ) ; mini_CurrentContext -> CurrentDraw = ( DrawFn ) d_DrawQuadStrip ; break ; case GL_POLYGON : mini_CurrentContext -> CurrentPrimitive = ( GLenum ) ( GL_POLYGON ) ; if ( mini_CurrentContext -> Texture2D_State [ 1 ] == 1 ) mini_CurrentContext -> CurrentDraw = ( DrawFn ) d_DrawMtexPoly ; else if ( mini_CurrentContext -> ShadeModel == GL_SMOOTH ) mini_CurrentContext -> CurrentDraw = ( DrawFn ) d_DrawSmoothPoly ; else mini_CurrentContext -> CurrentDraw = ( DrawFn ) d_DrawNormalPoly ; break ; case MGL_FLATFAN : mini_CurrentContext -> CurrentPrimitive = ( GLenum ) ( GL_POLYGON ) ; mini_CurrentContext -> CurrentDraw = ( DrawFn ) d_DrawFlat ; break ; case MGL_FLATSTRIP : mini_CurrentContext -> CurrentPrimitive = ( GLenum ) ( GL_POLYGON ) ; mini_CurrentContext -> CurrentDraw = ( DrawFn ) d_DrawFlat ; break ; default : ; break ; } mini_CurrentContext -> VertexBufferPointer = 0 ; mglvert = & ( mini_CurrentContext -> VertexBuffer [ 0 ] ) ; } ;
 mglTV23fvalpha ( mini_CurrentContext , 7 , p ) ;
 GLEnd ( mini_CurrentContext ) ;





 }

 MGLSetState ( mini_CurrentContext , GL_BLEND , 0 ) ;

 GLTexEnvi ( mini_CurrentContext , GL_TEXTURE_ENV , GL_TEXTURE_ENV_MODE , ( GLint ) GL_REPLACE ) ;
 { mini_CurrentContext -> CurrentColor . r = ( W3D_Float ) ( 1 ) ; mini_CurrentContext -> CurrentColor . g = ( W3D_Float ) ( 1 ) ; mini_CurrentContext -> CurrentColor . b = ( W3D_Float ) ( 1 ) ; mini_CurrentContext -> CurrentColor . a = ( W3D_Float ) ( 1 ) ; mini_CurrentContext -> UpdateCurrentColor = 1 ; } ;

 cl . worldmodel -> textures [ mirrortexturenum ] -> texturechain = ( ( void * ) 0 ) ;

 cl_numvisedicts = cl_oldvisedicts ;

 r_drawviewmodel . value = 1 ;

 }









































































































































































 endif