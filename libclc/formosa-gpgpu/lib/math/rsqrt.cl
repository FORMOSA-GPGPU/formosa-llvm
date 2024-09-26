#include <clc/clc.h>

#include "../../../generic/lib/clcmacro.h"

_CLC_OVERLOAD _CLC_DEF float rsqrt(float x) {
  union {
    float f;
    int i;
  } conv = {.f = x};
  conv.i = 0x5f3759df - (conv.i >> 1);
  conv.f *= 1.5F - (x * 0.5F * conv.f * conv.f);
  conv.f *= 1.5F - (x * 0.5F * conv.f * conv.f);
  return conv.f;
}

_CLC_UNARY_VECTORIZE(_CLC_OVERLOAD _CLC_DEF, float, rsqrt, float);

#ifdef cl_khr_fp64

#pragma OPENCL EXTENSION cl_khr_fp64 : enable

_CLC_OVERLOAD _CLC_DEF double rsqrt(double x) {
  union {
    double f;
    long long i;
  } conv = {.f = x};
  conv.i = 0x5fe6eb50c7b537a9 - (conv.i >> 1);
  conv.f *= 1.5 - (x * 0.5 * conv.f * conv.f);
  conv.f *= 1.5 - (x * 0.5 * conv.f * conv.f);
  return conv.f;
}

_CLC_UNARY_VECTORIZE(_CLC_OVERLOAD _CLC_DEF, double, rsqrt, double);

#endif
