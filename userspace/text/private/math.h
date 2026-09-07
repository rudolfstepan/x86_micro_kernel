#ifndef REIST_TEXT_PRIVATE_MATH_H
#define REIST_TEXT_PRIVATE_MATH_H
#include "../../math/include/math.h"
long double reist_text_frexpl(long double,int *);
#define frexpl reist_text_frexpl
#endif
