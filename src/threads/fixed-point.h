#ifndef THREADS_FIXED_POINT_H
#define THREADS_FIXED_POINT_H

#define F 16384 /* f = 2 * q; q = 14 */

/* Convert n to fixed point. */
uint32_t convert_int_to_fp(uint32_t n) 
{
  return n * F;
}

/* Convert x to integer (rounding toward zero). */
uint32_t convert_fp_to_int_rtz(uint32_t x) 
{
  return x / F;
}

/* Convert x to integer (rounding to nearest). */
uint32_t convert_fp_to_int_rtn(uint32_t x) 
{
  if(x >= 0) return (x + F / 2) / F;
  return (x - F / 2) / F;
}

/* Add x and y. */
uint32_t add_fp(uint32_t x, uint32_t y) 
{
  return x + y;
}

/* Subtract y from x. */
uint32_t sub_fp(uint32_t x, uint32_t y) 
{
  return x - y;
}

/* Add x and n. */
uint32_t add_fp_and_int(uint32_t x, uint32_t n) 
{
  return x + n * F;
}

/* Subtract n from x. */
uint32_t sub_fp_and_int(uint32_t x, uint32_t n) 
{
  return x - n * F;
}

/* Multiply x and y. */
uint32_t mul_fp(uint32_t x, uint32_t y) 
{
  return ((int64_t) x) * y / F;
}

/* Multiply x and n. */
uint32_t mul_fp_and_int(uint32_t x, uint32_t n) 
{
  return x * n;
}

/* Divide x and y. */
uint32_t div_fp(uint32_t x, uint32_t y) 
{
  return ((int64_t) x) * F / y;
}

/* Divide x and n. */
uint32_t div_fp_and_int(uint32_t x, uint32_t n) 
{
  return x / n;
}

#endif /* threads/fixed-point.h */
