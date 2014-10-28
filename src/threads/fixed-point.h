#ifndef THREADS_FIXED_POINT_H
#define THREADS_FIXED_POINT_H

#define F 16384 /* f = 2 * q; q = 14 */


inline int32_t convert_int_to_fp(int32_t n);
inline int32_t convert_fp_to_int_rtz(int32_t x);
inline int32_t convert_fp_to_int_rtn(int32_t x);
inline int32_t add_fp(int32_t x, int32_t y);
inline int32_t sub_fp(int32_t x, int32_t y);
inline int32_t add_fp_and_int(int32_t x, int32_t n);
inline int32_t sub_fp_and_int(int32_t x, int32_t n);
inline int32_t mul_fp(int32_t x, int32_t y);
inline int32_t mul_fp_and_int(int32_t x, int32_t n);
inline int32_t div_fp(int32_t x, int32_t y);
inline int32_t div_fp_and_int(int32_t x, int32_t n);


/* Convert n to fixed point. */
inline int32_t convert_int_to_fp(int32_t n) 
{
  return n * F;
}

/* Convert x to integer (rounding toward zero). */
inline int32_t convert_fp_to_int_rtz(int32_t x) 
{
  return x / F;
}

/* Convert x to integer (rounding to nearest). */
inline int32_t convert_fp_to_int_rtn(int32_t x) 
{
  if(x >= 0) return (x + F / 2) / F;
  return (x - F / 2) / F;
}

/* Add x and y. */
inline int32_t add_fp(int32_t x, int32_t y) 
{
  return x + y;
}

/* Subtract y from x. */
inline int32_t sub_fp(int32_t x, int32_t y) 
{
  return x - y;
}

/* Add x and n. */
inline int32_t add_fp_and_int(int32_t x, int32_t n) 
{
  return x + n * F;
}

/* Subtract n from x. */
inline int32_t sub_fp_and_int(int32_t x, int32_t n) 
{
  return x - n * F;
}

/* Multiply x and y. */
inline int32_t mul_fp(int32_t x, int32_t y) 
{
  return ((int64_t) x) * y / F;
}

/* Multiply x and n. */
inline int32_t mul_fp_and_int(int32_t x, int32_t n) 
{
  return x * n;
}

/* Divide x and y. */
inline int32_t div_fp(int32_t x, int32_t y) 
{
  return ((int64_t) x) * F / y;
}

/* Divide x and n. */
inline int32_t div_fp_and_int(int32_t x, int32_t n) 
{
  return x / n;
}

#endif /* threads/fixed-point.h */
