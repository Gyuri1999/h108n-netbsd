/* Isolate the floating-point "nan" bug (sysctl vm.loadavg prints nan nan nan).
 * Uses only integer printf so a broken printf/dtoa path can't hide the result.
 * Expected output:  idiv=3500 back=7 mul=6
 * Any other value means soft-float (libgcc) is miscompiled for Lexra. */
#include <stdio.h>
int main(void)
{
	volatile int n = 7, m = 2;
	volatile double d = (double)n / (double)m;   /* 3.5  : __floatsidf + __divdf3 */
	int idiv = (int)(d * 1000.0);                /* 3500 : __muldf3 + __fixdfsi   */
	int back = (int)(d * (double)m);             /* 7                              */
	volatile double e = 2.0, f = 3.0;
	int mul = (int)(e * f);                      /* 6                              */
	printf("idiv=%d back=%d mul=%d  (expect idiv=3500 back=7 mul=6)\n",
	    idiv, back, mul);
	return 0;
}
