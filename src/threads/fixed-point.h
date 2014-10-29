#ifndef THREADS_FIXED_POINT_H
#define THREADS_FIXED_POINT_H

#define F 16384 /* f = 2 * q; q = 14 */


int convert_int_to_fp(int n);
int convert_fp_to_int_rtz(int x);
int convert_fp_to_int_rtn(int x);
int add_fp(int x, int y);
int sub_fp(int x, int y);
int add_fp_and_int(int x, int n);
int sub_fp_and_int(int x, int n);
int mul_fp(int x, int y);
int mul_fp_and_int(int x, int n);
int div_fp(int x, int y);
int div_fp_and_int(int x, int n);


/* Convert n to fixed point. */
int convert_int_to_fp(int n) 
{
  return n * F;
}

/* Convert x to integer (rounding toward zero). */
int convert_fp_to_int_rtz(int x) 
{
  return x / F;
}

/* Convert x to integer (rounding to nearest). */
int convert_fp_to_int_rtn(int x) 
{
  if(x >= 0) return (x + F / 2) / F;
  return (x - F / 2) / F;
}

/* Add x and y. */
int add_fp(int x, int y) 
{
  return x + y;
}

/* Subtract y from x. */
int sub_fp(int x, int y) 
{
  return x - y;
}

/* Add x and n. */
int add_fp_and_int(int x, int n) 
{
  return x + n * F;
}

/* Subtract n from x. */
int sub_fp_and_int(int x, int n) 
{
  return x - n * F;
}

/* Multiply x and y. */
int mul_fp(int x, int y) 
{
  return ((int64_t) x) * y / F;
}

/* Multiply x and n. */
int mul_fp_and_int(int x, int n) 
{
  return x * n;
}

/* Divide x and y. */
int div_fp(int x, int y) 
{
  return ((int64_t) x) * F / y;
}

/* Divide x and n. */
int div_fp_and_int(int x, int n) 
{
  return x / n;
}

#endif /* threads/fixed-point.h */
