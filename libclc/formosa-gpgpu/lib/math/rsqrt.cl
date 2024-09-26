#include <clc/clc.h>

#include "../../../generic/lib/clcmacro.h"

_CLC_OVERLOAD _CLC_DEF float rsqrt(float x) {
  float x2 = x * 0.5F;
  float y = x;
  volatile long i = *(long *)&y;
  i = 0x5f3759df - (i >> 1);
  y = *(volatile float *)&i;
  y = y * (1.5F - (x2 * y * y));

  return y;
}

_CLC_UNARY_VECTORIZE(_CLC_OVERLOAD _CLC_DEF, float, rsqrt, float);

#ifdef cl_khr_fp64

#pragma OPENCL EXTENSION cl_khr_fp64 : enable

_CLC_OVERLOAD _CLC_DEF double rsqrt(double x) {
  double y = x;
  double x2 = y * 0.5;
  volatile long long i = *(long long *)&y;
  i = 0x5fe6eb50c7b537a9 - (i >> 1);
  y = *(volatile double *)&i;
  y = y * (1.5 - (x2 * y * y));
  return y;
}

_CLC_UNARY_VECTORIZE(_CLC_OVERLOAD _CLC_DEF, double, rsqrt, double);

#endif
