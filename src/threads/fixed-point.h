#ifndef THREADS_FIXED_POINT_H
#define THREADS_FIXED_POINT_H

#define F 16384 /* f = 2 * q; q = 14 */


inline int convert_int_to_fp(int n);
inline int convert_fp_to_int_rtz(int x);
inline int convert_fp_to_int_rtn(int x);
inline int add_fp(int x, int y);
inline int sub_fp(int x, int y);
inline int add_fp_and_int(int x, int n);
inline int sub_fp_and_int(int x, int n);
inline int mul_fp(int x, int y);
inline int mul_fp_and_int(int x, int n);
inline int div_fp(int x, int y);
inline int div_fp_and_int(int x, int n);


/* Convert n to fixed point. */
inline int convert_int_to_fp(int n) 
{
  return n * F;
}

/* Convert x to integer (rounding toward zero). */
inline int convert_fp_to_int_rtz(int x) 
{
  return x / F;
}

/* Convert x to integer (rounding to nearest). */
inline int convert_fp_to_int_rtn(int x) 
{
  if(x >= 0) return (x + F / 2) / F;
  return (x - F / 2) / F;
}

/* Add x and y. */
inline int add_fp(int x, int y) 
{
  return x + y;
}

/* Subtract y from x. */
inline int sub_fp(int x, int y) 
{
  return x - y;
}

/* Add x and n. */
inline int add_fp_and_int(int x, int n) 
{
  return x + n * F;
}

/* Subtract n from x. */
inline int sub_fp_and_int(int x, int n) 
{
  return x - n * F;
}

/* Multiply x and y. */
inline int mul_fp(int x, int y) 
{
  return ((int64_t) x) * y / F;
}

/* Multiply x and n. */
inline int mul_fp_and_int(int x, int n) 
{
  return x * n;
}

/* Divide x and y. */
inline int div_fp(int x, int y) 
{
  return ((int64_t) x) * F / y;
}

/* Divide x and n. */
inline int div_fp_and_int(int x, int n) 
{
  return x / n;
}

#endif /* threads/fixed-point.h */
