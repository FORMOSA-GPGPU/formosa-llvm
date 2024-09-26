#include <clc/clc.h>
#include "math/clc_sqrt.h"
#include "../../../generic/lib/clcmacro.h"
#include "../../../generic/include/clc/math/rsqrt.h"

_CLC_OVERLOAD _CLC_DEF float sqrt(float x) {
  return 1.0F / rsqrt(x);
}

#ifdef cl_khr_fp16

#pragma OPENCL EXTENSION cl_khr_fp16 : enable
_CLC_DEFINE_UNARY_BUILTIN(half, sqrt, __clc_sqrt, half)

#endif

#ifdef cl_khr_fp64

#pragma OPENCL EXTENSION cl_khr_fp64 : enable

_CLC_OVERLOAD _CLC_DEF double sqrt(double x) {
  return 1.0 / rsqrt(x);
}

_CLC_UNARY_VECTORIZE(_CLC_OVERLOAD _CLC_DEF, double, sqrt, double);

#endif
