#ifndef THREADS_FIXED_POINT_H
#define THREADS_FIXED_POINT_H

#define F 16384 /* f = 2 * q; q = 14 */
#define NRINTBITS 17 /* number of int bits */
#define NRFRACBITS 17 /* number of frac bits */

typedef struct fp_n fp_n;

struct fp_n {
  uint32_t int_bits = 0;
  uint16_t frac_bits = 0;
  bool signed = false;
}


/* Convert n to fixed point. */
fp_n convert_int_to_fp(uint32_t n) {
  fp_n result;
  if(n >> 31) result.signed = 1;
  result.int_bits = n * F;
  return result;
}

/* Convert x to integer (rounding toward zero). */
uint32_t convert_fp_to_int_rtz(uint32_t x) {
  return x / F;
}

/* Convert x to integer (rounding to nearest). */
uint32_t convert_fp_to_int_rtn(uint32_t x) {
  if(x >= 0) return (x + F / 2) / F;
  if(x <= 0) return (x - F / 2) / F;
}

/* Add x and y. */
fp_n add_fp(fp_n x, fp_n y) {
  fp_n result;
  result.int_bits = x.int_bits + y.int_bits;
  result.frac_bits = x.frac_bits + y.frac_bits;
  
  //check if the frac_bits carries a 1
  if(result.frac_bits>>NRFRACBITS) result.int_bits++;
  result.signed = true;
  return result;
}

/* Subtract y from x. */
uint32_t sub_fp(uint32_t x, uint32_t y) {
  return x - y;
}

/* Add x and n. */
uint32_t add_fp_and_int(uint32_t x, uint32_t n) {
  return x + n * F;
}

/* Subtract n from x. */
uint32_t add_fp_and_int(uint32_t x, uint32_t n) {
  return x - n * F;
}

/* Multiply x and y. */
uint32_t mul_fp(uint32_t x, uint32_t y) {
  return ((int64_t) x) * y / F;
}

/* Multiply x and n. */
uint32_t mul_fp_and_int(uint32_t x, uint32_t n) {
  return x * n;
}

/* Divide x and y. */
uint32_t div_fp(uint32_t x, uint32_t y) {
  return ((int64_t) x) * y / F;
}

/* Divide x and n. */
uint32_t div_fp_and_int(uint32_t x, uint32_t n) {
  return x * n;
}

#endif /* threads/fixed-point.h */
