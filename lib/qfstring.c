/* Some string handling
 * Copyright (C) 2010 Chris Hall (GMCH), Highwayman
 *
 * This file is part of GNU Zebra.
 *
 * GNU Zebra is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2, or (at your
 * option) any later version.
 *
 * GNU Zebra is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU Zebra; see the file COPYING.  If not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */
#include "misc.h"

#include <stdio.h>

#include "qfstring.h"

/*==============================================================================
 * Initialise, allocate etc.
 */

/*------------------------------------------------------------------------------
 * Initialise qf_str -- to given size, zero offset and zero overflow.
 *
 * Note that does not terminate the string -- that must be done separately.
 *
 * This operation is async-signal-safe.
 */
extern void
qfs_init(qf_str qfs, char* str, uint size)
{
  qfs->str      = str ;
  qfs->ptr      = str ;
  qfs->end      = str + size ;
  qfs->offset   = 0 ;
  qfs->overflow = 0 ;
} ;

/*------------------------------------------------------------------------------
 * Reset qf_str to completely empty, with zero offset and zero overflow (again)
 */
extern void
qfs_reset(qf_str qfs)
{
  qfs->ptr      = qfs->str ;
  qfs->offset   = 0 ;
  qfs->overflow = 0 ;
} ;

/*------------------------------------------------------------------------------
 * Initialise qf_str -- to given size, with given offset and zero overflow.
 *
 * Note that does not terminate the string -- that must be done separately.
 *
 * This operation is async-signal-safe.
  */
extern void
qfs_init_offset(qf_str qfs, char* str, uint size, uint offset)
{
  qfs->str      = str ;
  qfs->ptr      = str ;
  qfs->end      = str + size ;
  qfs->offset   = offset ;
  qfs->overflow = 0 ;
} ;

/*------------------------------------------------------------------------------
 * Reset given qf_str -- with the given offset and zero overflow.
 *
 * Sets ptr back to the start of the string and set the given offset.
 *
 * This operation is async-signal-safe.
 */
extern void
qfs_reset_offset(qf_str qfs, uint offset)
{
  qfs->ptr      = qfs->str ;
  qfs->offset   = offset ;
  qfs->overflow = 0 ;
} ;

/*------------------------------------------------------------------------------
 * Initialise qf_str which already contains string -- to given size with zero
 * overflow.
 *
 * This may be used to prepare for appending to a buffer which already contains
 * something.
 *
 * Sets pointers, setting the write pointer to the existing '\0'.
 *
 * This operation is async-signal-safe.
 *
 * NB: it is a mistake if the size given is less than the length of the
 *     string (excluding the trailing '\0').
 */
extern void
qfs_init_as_is(qf_str qfs, char* str, uint size)
{
  assert(size > 0) ;

  qfs->str      = str ;
  qfs->end      = str + size ;
  qfs->offset   = 0 ;
  qfs->overflow = 0 ;

  while (*str != '\0')
    ++str ;

  qfs->ptr = str ;      /* point at '\0'        */

  assert(qfs->ptr <= qfs->end) ;
} ;

/*------------------------------------------------------------------------------
 * Terminate string with the given string of given length (which may include
 * a '\0').
 *
 * This is for when the qstring has overflowed, and wish to indicate that at
 * the end -- so takes no notice of offset.
 *
 * If necessary, characters are discarded from the end of the string in order
 * to fit in the terminating stuff.
 *
 * If the terminating stuff won't fit, as much of the end if the terminating
 * stuff as possible is copied to the string -- displacing any existing
 * contents.
 *
 * This operation is async-signal-safe.
 */
extern void
qfs_term_string(qf_str qfs, const char* src, uint n)
{
  uint h ;

  h = qfs->end - qfs->ptr ;             /* space available              */

  if (h < n)
    {
      h = qfs->end - qfs->str ;         /* total space                  */
      if (h < n)
        {
          src += n - h ;                /* past what will not fit       */
          n = h ;
        } ;
      qfs->ptr = qfs->end - n ;
    } ;

  while (n--)
    *qfs->ptr++ = *src++ ;
} ;

/*==============================================================================
 * Appending to the string
 */

/*------------------------------------------------------------------------------
 * Append as much as possible of the source string to the given qf_str.
 *
 * May append nothing at all !
 *
 * This operation is async-signal-safe.  Takes into account the offset, and
 * adds up any overflow
 */
extern void
qfs_append(qf_str qfs, const char* src)
{
  if (src == NULL)
    return ;

  while (qfs->offset > 0)
    {
      if (*src++ == '\0')
        return ;
      --qfs->offset ;
    } ;

  while (*src != '\0')
    {
      if (qfs->ptr < qfs->end)
        *qfs->ptr++ = *src++ ;
      else
        ++qfs->overflow ;
    } ;
} ;

/*------------------------------------------------------------------------------
 * Append as much as possible of the first 'n' bytes of the source string to
 * the given qf_str.
 *
 * May append nothing at all !
 *
 * src may be NULL iff n == 0
 *
 * This operation is async-signal-safe.  Takes into account the offset, and
 * adds up any overflow
 */
extern void
qfs_append_n(qf_str qfs, const char* src, uint n)
{
  uint h ;

  if (qfs->offset > 0)
    {
      if (qfs->offset >= n)
        {
          qfs->offset -= n ;
          return ;
        } ;

      src += qfs->offset ;
      n   -= qfs->offset ;

      qfs->offset = 0 ;
    } ;

  h = (qfs->end - qfs->ptr) ;
  if (n > h)
    {
      qfs->overflow += n - h ;
      n = h ;
    } ;

  while (n--)
    *qfs->ptr++ = *src++ ;
} ;

/*------------------------------------------------------------------------------
 * Append upto 'n' copies of the given character to the qf_str
 *
 * May append nothing at all !
 *
 * This operation is async-signal-safe.  Takes into account the offset, and
 * adds up any overflow
 */
extern void
qfs_append_ch_x_n(qf_str qfs, char ch, uint n)
{
  uint h ;

  if (qfs->offset > 0)
    {
      if (qfs->offset >= n)
        {
          qfs->offset -= n ;
          return ;
        } ;

      n   -= qfs->offset ;

      qfs->offset = 0 ;
    } ;

  h = (qfs->end - qfs->ptr) ;
  if (n > h)
    {
      qfs->overflow += n - h ;
      n = h ;
    } ;

  while (n--)
    *qfs->ptr++ = ch ;
} ;

/*------------------------------------------------------------------------------
 * Append as much as possible of the source string to the given qf_str, left or
 * right justified to the given width.
 *
 * Ignores the width if the string is longer than it.
 *
 * Negative width => left justify.
 *
 * May append nothing at all !
 *
 * This operation is async-signal-safe.  Takes into account the offset, and
 * adds up any overflow
 */
extern void
qfs_append_justified(qf_str qfs, const char* src, int width)
{
  qfs_append_justified_n(qfs, src, qfs_strlen(src), width) ;
} ;

/*------------------------------------------------------------------------------
 * Append as much as possible of the first 'n' bytes of the source string to
 * the given qf_str, left or right justified to the given width.
 *
 * Ignores the width if the string is longer than it.
 *
 * Negative width => left justify.
 *
 * May append nothing at all !
 *
 * This operation is async-signal-safe.  Takes into account the offset, and
 * adds up any overflow
 */
extern void
qfs_append_justified_n(qf_str qfs, const char* src, uint n, int width)
{
  if ((int)n >= abs(width))
    width = 0 ;

  if (width > 0)
    qfs_append_ch_x_n(qfs, ' ', width - n) ;

  qfs_append_n(qfs, src, n) ;

  if (width < 0)
    qfs_append_ch_x_n(qfs, ' ', - width - n) ;
} ;

/*------------------------------------------------------------------------------
 * Append single character.
 *
 * This operation is async-signal-safe.  Takes into account the offset, and
 * adds up any overflow
 */
inline static void
qfs_append_ch(qf_str qfs, char ch)
{
  if      (qfs->offset > 0)
    --qfs->offset ;
  else if (qfs->ptr < qfs->end)
    *qfs->ptr++ = ch ;
  else
    ++qfs->overflow ;
} ;

/*==============================================================================
 * Number conversion
 */

static void
qfs_number(qf_str qfs, uintmax_t val, int sign, pf_flags_t flags,
                                                     int width, int precision) ;

/*------------------------------------------------------------------------------
 * Signed integer -- converted as per flags, width and precision.
 *
 * Result is appended to the given qf_str.
 *
 * This operation is async-signal-safe.  Takes into account the offset, and
 * adds up any overflow
 */
extern void
qfs_signed(qf_str qfs, intmax_t s_val, pf_flags_t flags,
                                                       int width, int precision)
{
  uintmax_t u_val ;
  int       sign ;

  if (s_val < 0)
    {
      sign  = -1 ;
      u_val = (uintmax_t)(-(s_val + 1)) + 1 ;
    }
  else
    {
      sign  = (s_val > 0) ? +1 : 0 ;
      u_val = s_val ;
    } ;

  qfs_number(qfs, u_val, sign, flags & ~pf_unsigned, width, precision) ;
} ;

/*------------------------------------------------------------------------------
 * Unsigned integer -- converted as per flags, width and precision.
 *
 * Result is appended to the given qf_str.
 *
 * This operation is async-signal-safe.  Takes into account the offset, and
 * adds up any overflow
 */
extern void
qfs_unsigned(qf_str qfs, uintmax_t u_val, pf_flags_t flags,
                                                       int width, int precision)
{
  qfs_number(qfs, u_val, 0, flags | pf_unsigned, width, precision) ;
} ;

/*------------------------------------------------------------------------------
 * Address -- converted as per flags, width and precision.
 *
 * Result is appended to the given qf_str.
 *
 * This operation is async-signal-safe.  Takes into account the offset, and
 * adds up any overflow
 */
extern void
qfs_pointer(qf_str qfs, void* p_val, pf_flags_t flags,
                                                       int width, int precision)
{
  confirm(sizeof(uintmax_t) >= sizeof(uintptr_t)) ;
  qfs_number(qfs, (uintptr_t)p_val, 0, flags | pf_unsigned, width, precision) ;
} ;

/*------------------------------------------------------------------------------
 * Number conversion function.
 *
 * All number conversion ends up here.
 *
 * Accepts: pf_commas     -- format with commas
 *          pf_plus       -- requires '+' or '-'
 *          pf_space      -- requires ' ' or '-' (if not already added same)
 *          pf_zeros      -- zero fill to width
 *          pf_alt        -- add '0x' or '0X' if hex -- depending on pf_uc
 *                           add '0' if octal and not zero.
 *                           no effect otherwise
 *          pf_plus_nz    -- requires '+' if is > 0
 *
 *          pf_precision  -- explicit precision (needed if precision == 0)
 *
 *          pf_hex        -- render in hex
 *          pf_uc         -- render in upper case
 *
 *          pf_unsigned   -- value is unsigned
 *          pf_ptr        -- value is a void* pointer
 *
 * NB: pf_hex does NOT imply pf_unsigned.
 *     pf_uc  does NOT imply pf_hex
 *
 * If the width is < 0  -- left justify in abs(width) -- zero fill ignored
 *                == 0  -- no width                   -- zero fill ignored
 *                 > 0  -- right justify in width     -- zero filling if req.
 *
 * If the precision is < 0 it is ignored (unless pf_hex, see below).
 *
 * If the precision is 0 it is ignored unless pf_precision is set.
 *
 * Precedence issues:
 *
 *   * precision comes first.  Disables zero fill.
 *
 *   * commas come before zero fill.
 *
 *   * signs and prefixes come before zero fill
 *
 *   * pf_plus takes precedence over pf_space
 *
 *   * pf_unsigned or sign == 0 takes precedence over pf_plus and pf_space.
 *
 * For decimal output, pf_commas groups digits in 3's, separated by ','.
 * For hex output,     pf_commas groups digits in 4's, separated by '_'.
 * For oct output,     pf_commas is ignored.
 *
 * Note that pf_commas is a glibc extension, which does not apply to hex !
 *
 * For hex output if precision is:
 *
 *   -1 set precision to multiple of 2, just long enough for the value
 *   -2 set precision to multiple of 4, just long enough for the value
 *
 * (under all other conditions, -ve precision is ignored).
 *
 * Note: if the precision is explicitly 0, and the value is 0, and no other
 *       characters are to be generated -- ie no: pf_plus, pf_space, pf_zeros,
 *       or pf_alt (with pf_hex) -- then nothing is generated.
 *
 * This operation is async-signal-safe.  Takes into account the offset, and
 * adds up any overflow
 */
static void
qfs_number(qf_str qfs, uintmax_t val, int sign, pf_flags_t flags,
                                                       int width, int precision)
{
  enum
  {
    max_bits   = 256,   /* size of number can convert           */
    max_digits =  90,   /* could do octal !                     */
    buf_size   = 128,   /* buffer to use for that               */
  } ;

  confirm((sizeof(uintmax_t) * 8) <= max_bits) ;  /* check max_bits     */
  confirm((max_digits * 3) >= max_bits) ;         /* check max_digits   */

  /* Buffer requires space for sign, '0x', digits, '00', commas, '\0'
   *
   * The '00' is for zero fill will commas, and is enough to extend the
   * number to "000,...." -- that is, a full leading triple.
   */
  confirm(buf_size > (1 + 2 + max_digits + (2 + (max_digits / 3)) + 1)) ;

  /* For hex commas the sum is similar, but smaller.                    */
  confirm((3 + (max_digits / 4)) < (2 + (max_digits / 3))) ;

  unsigned    base ;
  const char* digits ;
  const char* radix_str ;
  const char* sign_str ;
  char  num[buf_size] ;
  char* p ;
  char* e ;
  int len ;
  int radix_len ;
  int sign_len ;
  uintmax_t v ;

  char  comma ;
  int   interval ;

  int   zeros ;

  static const char lc[] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
                             'a', 'b', 'c', 'd', 'e', 'f' } ;
  static const char uc[] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
                             'A', 'B', 'C', 'D', 'E', 'F' } ;

  /* Tidy up the options                                                */
  if (precision < 0)
    {
      if ((flags & pf_hex) && (precision >= -2))
        {
          /* special precision for hex output           */
          int unit = (precision == -1) ? 2 : 4 ;
          v = val | 1 ;
          precision = 0 ;
          while (v != 0)
            {
              precision += unit ;
              v >>= (unit * 4) ;
            } ;
        }
      else
        {
          /* mostly, -ve precision is ignored           */
          precision = 0 ;
          flags &= ~pf_precision ;      /* ignore precision < 0 */
        } ;
    } ;

  if (precision > 0)
    flags |= pf_precision ;     /* act on precision > 0 */

  if ((flags & pf_precision) || (width <= 0))
    flags &= ~pf_zeros ;        /* turn off zero fill           */

  if (flags & pf_oct)
    flags &= ~pf_commas ;       /* turn off commas              */

  /* Set up any required sign and radix prefix                          */
  if (flags & pf_unsigned)
    {
      sign_str = "" ;
      sign_len = 0 ;
    }
  else if (sign < 0)
    {
      sign_str = "-" ;
      sign_len = 1 ;
    }
  else if ((flags & pf_plus) || ((flags & pf_plus_nz) && (sign > 0)))
    {
      sign_str = "+" ;
      sign_len = 1 ;
    }
  else if (flags & pf_space)
    {
      sign_str = " " ;
      sign_len = 1 ;
    }
  else
    {
      sign_str = "" ;
      sign_len = 0 ;
    } ;

  radix_str = "" ;
  radix_len = 0 ;

  if (flags & pf_alt)
    {
      if (flags & pf_hex)
        {
          confirm(pf_uc != 0) ;
          radix_str = (flags & pf_uc) ? "0X" : "0x" ;
          radix_len = 2 ;
        }
      else if ((flags & pf_oct) && (val != 0))
        {
          radix_str = "0" ;
          radix_len = 1 ;
        } ;
    } ;

  /* Turn off zero fill if left justify (width < 0)                     */
  if (width < 0)
    flags &= ~pf_zeros ;

  /* Special case of explicit zero precision and value == 0             */
  if ((flags & pf_precision) && (precision == 0) && (val == 0))
    {
      if (((flags & pf_zeros) == 0) && (sign_len == 0) && (radix_len == 0))
        {
          qfs_append_justified_n(qfs, NULL, 0, width) ;
          return ;
        } ;
    } ;

  /* Start with the basic digit conversion.                             */
  base   = 10 ;
  if      (flags & pf_hex)
    base = 16 ;
  else if (flags & pf_oct)
    base = 8 ;

  digits = (flags & pf_uc)  ? uc : lc ;
  confirm(pf_uc != 0) ;

  e = p = num + sizeof(num) ;
  v = val ;
  do
    {
      *--p = digits[v % base] ;
      v /= base ;
    } while ((v > 0) && (p > num)) ;

  assert(v == 0) ;

  len = e - p ;

  /* Worry about the precision          */
  while ((precision > len) && (len < max_digits))
    {
      *--p = '0' ;
      ++len ;
    } ;

  /* Worry about commas                 */
  comma    = (flags & pf_hex) ? '_' : ',' ;
  interval = (flags & pf_hex) ?  4  :  3  ;

  if (flags & pf_commas)
    {
      int   c ;
      int   t ;
      char* cq ;
      char* cp ;

      c = (len - 1) / interval ;        /* number of commas to insert   */
      t = len % interval ;              /* digits before first comma    */
      if (t == 0)
        t = interval ;

      len += c ;                        /* account for the commas       */

      cq = p ;
      p -= c ;
      cp = p ;

      assert(p > num) ;

      while (c--)
        {
          while (t--)
            *cp++ = *cq++ ;
          *cp++ = comma ;
          t = interval ;
        } ;

      assert(len == (e - p)) ;

      /* commas and zero fill interact.  Here fill the leading group.   */
      zeros = width - (sign_len + radix_len + len) ;
      if ((flags & pf_zeros) && (zeros > 0))
        {
          int group_fill = interval - (len % (interval + 1)) ;
          assert(group_fill < interval) ;
          if (group_fill > zeros)
            group_fill = zeros ;

          len += group_fill ;
          while (group_fill--)
            {
              assert(p > num) ;
              *--p = '0' ;
            } ;
        } ;
    } ;

  assert(len == (e - p)) ;

  /* See if still need to worry about zero fill                         */
  zeros = width - (sign_len + radix_len + len) ;
  if ((flags & pf_zeros) && (zeros > 0))
    {
      /* Need to insert zeros and possible commas between sign and radix
       * and the start of the number.
       *
       * Note that for commas the number has been arranged to have a full
       * leading group.
       *
       * The width can be large... so do this by appending any sign and
       * radix to the qf_str, and then the required leading zeros (with or
       * without commas).
       */
      if (sign_len != 0)
        qfs_append_n(qfs, sign_str, sign_len) ;

      if (radix_len != 0)
        qfs_append_n(qfs, radix_str, radix_len) ;

      if (flags & pf_commas)
        {
          /* Leading zeros with commas !
           *
           * Start with ',', '0,', '00,' etc to complete the first group.
           * Thereafter add complete groups.
           */
          int g ;
          int r ;
          g = (zeros + interval - 1) / (interval + 1) ;
          r = (zeros - 1)            % (interval + 1) ;

          if (r == 0)
            {
              qfs_append_ch_x_n(qfs, comma, 1) ;
              r = interval ;
            }

          while (g--)
            {
              qfs_append_ch_x_n(qfs, '0', r) ;
              qfs_append_ch_x_n(qfs, comma, 1) ;
              r = interval ;
            } ;
        }
      else
        qfs_append_ch_x_n(qfs, '0', zeros) ;

      width = 0 ;               /* have dealt with the width.   */
    }
  else
    {
      /* No leading zeros, so complete the number by adding any sign
       * and radix.
       */
      char* cp ;

      p   -= sign_len + radix_len ;
      len += sign_len + radix_len ;
      assert(p >= num) ;

      cp = p ;
      while (sign_len--)
        *cp++ = *sign_str++ ;
      while (radix_len--)
        *cp++ = *radix_str++ ;
    } ;

  /* Finally, can append the number -- respecting any remaining width   */
  assert(len == (e - p)) ;

  qfs_append_justified_n(qfs, p, len, width) ;
} ;

/*==============================================================================
 * printf() and vprintf() type functions
 */

enum pf_phase
{
  pfp_null,             /* in ascending order   */
  pfp_flags,
  pfp_width,
  pfp_precision,
  pfp_int_type,
  pfp_float_type,

  pfp_done,
  pfp_failed
} ;

CONFIRM(pfp_float_type > pfp_int_type) ;

/* Number types for printing                                    */
enum arg_num_type
{
  ant_char,             /* hh           */
  ant_short,            /* h            */
  ant_int,              /* default      */
  ant_long,             /* l            */
  ant_long_long,        /* ll           */
  ant_intmax_t,         /* j            */
  ant_size_t,           /* z            */
  ant_ptr_t,            /* void*        */
  ant_long_double,      /* L for float  */

  ant_default    = ant_int,
};

static enum pf_phase qfs_arg_string(qf_str qfs, const char* src,
                                pf_flags_t flags, int width, int precision) ;
static enum pf_phase qfs_arg_char(qf_str qfs, char ch,
                                pf_flags_t flags, int width, int precision) ;
static enum pf_phase qfs_arg_integer(qf_str qfs, va_list* p_va,
         pf_flags_t flags, int width, int precision, enum arg_num_type ant) ;
static enum pf_phase qfs_arg_float(qf_str qfs, va_list* p_va,
                        const char* start, size_t flen, enum arg_num_type ant) ;

/*------------------------------------------------------------------------------
 * Formatted print to qf_str -- cf printf() -- appends to the qf_str.
 *
 * This operation is async-signal-safe -- EXCEPT for floating point values.
 * Takes into account the offset, and adds up any overflow.
 *
 * Returns:  the resulting length of the qf_str -- unterminated
 */
extern uint
qfs_printf(qf_str qfs, const char* format, ...)
{
  va_list va ;
  uint did ;

  va_start (va, format);
  did = qfs_vprintf(qfs, format, va);
  va_end (va);

  return did ;
} ;

/*------------------------------------------------------------------------------
 * Formatted print to qfb_gen_t -- cf printf() -- fills and returns qfb_get_t.
 *
 * This operation is async-signal-safe -- EXCEPT for floating point values.
 * Takes into account the offset, and adds up any overflow.
 *
 * This is for use when wish to construct modest size strings out of a mix
 * of other strings numbers etc.  If constructed string does not fit, it is
 * quietly truncated.
 *
 * Returns:  the resulting qfb_gen_t -- '\0' terminated
 *
 * NB: this returns a qfb_gen_t so it is the caller's responsibility to ensure
 *     that has the required lifetime -- which may only be the life of a called
 *     function, but if that function wishes to keep the value, then it had
 *     better copy it !
 */
extern qfb_gen_t
qfs_gen(const char* format, ...)
{
  qfb_gen_t QFB_QFS(buf, qfs) ;
  va_list va ;

  va_start (va, format);
  qfs_vprintf(qfs, format, va);
  va_end (va);

  qfs_term(qfs) ;
  return buf ;
} ;

/*------------------------------------------------------------------------------
 * Formatted print to qf_str -- cf vprintf() -- appends to the qf_str.
 *
 * This operation is async-signal-safe -- EXCEPT for floating point values.
 * Takes into account the offset, and adds up any overflow.
 *
 * Operates on a copy of the va_list -- so the original is *unchanged*.
 *
 * Returns:  the resulting length of the qf_str -- unterminated
 */
extern uint
qfs_vprintf(qf_str qfs, const char *format, va_list va)
{
  va_list vac ;

  if (format == NULL)
    return qfs_len(qfs) ;

  va_copy(vac, va) ;

  while (*format != '\0')
    {
      /* Have space for one byte and current format byte is not '\0'    */
      if (*format != '%')
        qfs_append_ch(qfs, *format++) ;
      else
        {
          const char* start = format++ ;  /* start points at the '%' ...
                                             ... step past it now       */
          bool star      = false ;
          bool digit     = false ;
          int d          = 0 ;
          int width_sign = +1 ;
          int width      = 0 ;
          int precision  = 0 ;
          enum arg_num_type ant = ant_default ;
          pf_flags_t flags   = pf_none ;
          enum pf_phase phase   = pfp_null ;

          while (phase < pfp_done)
            {
              switch (*format++)        /* get next and step past it    */
              {
                case '%':       /* %% only                              */
                  if (phase == pfp_null)
                    {
                      qfs_append_ch(qfs, '%') ;
                      phase = pfp_done ;
                    }
                  else
                    phase = pfp_failed ;
                  break ;

                case '\'':
                  flags |= pf_commas ;
                  phase = (phase <= pfp_flags) ? pfp_flags : pfp_failed ;
                  break ;

                case '-':
                  width_sign = -1 ;
                  phase = (phase <= pfp_flags) ? pfp_flags : pfp_failed ;
                  break ;

                case '+':
                  flags |= pf_plus ;
                  phase = (phase <= pfp_flags) ? pfp_flags : pfp_failed ;
                  break ;

                case '#':
                  flags |= pf_alt ;
                  phase = (phase <= pfp_flags) ? pfp_flags : pfp_failed ;
                  break ;

                case ' ':
                  flags |= pf_space ;
                  phase = (phase <= pfp_flags) ? pfp_flags : pfp_failed ;
                  break ;

                case '0':
                  if (phase <= pfp_flags)
                    {
                      flags |= pf_zeros ;
                      phase = pfp_flags ;
                      break ;
                    } ;
                  /* fall through       */
                case '1':
                case '2':
                case '3':
                case '4':
                case '5':
                case '6':
                case '7':
                case '8':
                case '9':
                  d = *(format - 1) - '0' ;
                  if      (!star && (phase <= pfp_width))
                    {
                      phase = pfp_width ;
                      width = (width * 10) + (d * width_sign) ;
                    }
                  else if (!star && (phase == pfp_precision))
                    precision = (precision * 10) + d ;
                  else
                    phase = pfp_failed ;

                  digit = true ;
                  break ;

                case '*':
                  if      (!star && !digit && (phase <= pfp_width))
                    {
                      phase = pfp_width ;
                      width = va_arg(vac, int) ;
                    }
                  else if (!star && !digit && (phase == pfp_precision))
                    {
                      precision = va_arg(vac, int) ;
                      if (precision < 0)
                        {
                          precision = 0 ;
                          flags &= ~pf_precision ;  /* completely ignore */
                        } ;
                    }
                  else
                    phase = pfp_failed ;

                  star = true ;
                  break ;

                case '.':
                  phase = (phase < pfp_precision) ? pfp_precision : pfp_failed ;
                  flags |= pf_precision ;
                  precision = 0 ;
                  break ;

                case 'l':       /* 1 or 2 'l', not 'h', 'j' or 'z'      */
                  phase = (phase <= pfp_int_type) ? pfp_int_type : pfp_failed ;
                  if      (ant == ant_default)
                    ant = ant_long ;
                  else if (ant == ant_long)
                    ant = ant_long_long ;
                  else
                    phase = pfp_failed ;
                  break ;

                case 'h':       /* 1 or 2 'h', not 'l', 'j' or 'z'      */
                  phase = (phase <= pfp_int_type) ? pfp_int_type : pfp_failed ;
                  if      (ant == ant_default)
                    ant = ant_short ;
                  else if (ant == ant_short)
                    ant = ant_char ;
                  else
                    phase = pfp_failed ;
                  break ;

                case 'j':       /* 1 'j', not 'h', 'l' or 'z'           */
                  phase = (phase <= pfp_int_type) ? pfp_int_type : pfp_failed ;
                  ant = ant_intmax_t ;
                  break ;

                case 'z':       /* 1 'z', not 'h', 'l' or 'j'           */
                  phase = (phase <= pfp_int_type) ? pfp_int_type : pfp_failed ;
                  ant = ant_size_t ;
                  break ;

                case 'L':       /* 1 'L', not for integers !            */
                  phase = (phase < pfp_int_type) ? pfp_float_type : pfp_failed ;
                  ant = ant_long_double ;
                  break ;

                case 's':
                  if (phase == pfp_int_type)
                    phase = pfp_failed ;        /* don't do 'l' etc.    */
                  else
                    phase = qfs_arg_string(qfs, va_arg(vac, char*),
                                                      flags, width, precision) ;
                  break ;

                case 'c':
                  if (phase == pfp_int_type)
                    phase = pfp_failed ;        /* don't do 'l' etc.    */
                  else
                    phase = qfs_arg_char(qfs, (char)va_arg(vac, int),
                                                      flags, width, precision) ;
                  break ;

                case 'd':
                case 'i':
                  phase = qfs_arg_integer(qfs, &vac, flags, width, precision,
                                                                          ant) ;
                  break ;

                case 'u':
                  phase = qfs_arg_integer(qfs, &vac, flags | pf_unsigned, width,
                                                               precision, ant) ;
                  break ;

                case 'o':
                  phase = qfs_arg_integer(qfs, &vac, flags | pf_oct, width,
                                                               precision, ant) ;
                  break ;

                case 'x':
                  phase = qfs_arg_integer(qfs, &vac, flags | pf_hex_x, width,
                                                               precision, ant) ;
                  break ;

                case 'X':
                  phase = qfs_arg_integer(qfs, &vac, flags | pf_hex_X, width,
                                                               precision, ant) ;
                  break ;

                case 'p':
                  if (phase == pfp_int_type)
                    phase = pfp_failed ;
                  else
                    phase = qfs_arg_integer(qfs, &vac, flags | pf_void_p, width,
                                                         precision, ant_ptr_t) ;
                  break ;

                case 'e':
                case 'E':
                case 'f':
                case 'F':
                case 'g':
                case 'G':
                case 'a':
                case 'A':
                  if (phase == pfp_int_type)
                    phase = pfp_failed ;
                  else
                    phase = qfs_arg_float(qfs, &vac, start, format - start,
                                                                          ant) ;
                  break ;

                default:                /* unrecognised format          */
                  phase = pfp_failed ;
                  break ;
              } ;
            } ;

          if (phase == pfp_failed)
            {
              format = start ;          /* back to the start            */
              qfs_append_ch(qfs, *format++) ;
            } ;
        } ;
    } ;

  va_end(vac) ;

  return qfs_len(qfs) ;
} ;

/*------------------------------------------------------------------------------
 * %s handler -- tolerates NULL pointer
 *
 * Accepts:    width
 *             precision    -- ignored if < 0
 *             pf_precision -- explicit precision
 *
 * Rejects:    pf_commas    -- "'" seen
 *             pf_plus      -- "+" seen
 *             pf_space     -- " " seen
 *             pf_zeros     -- "0" seen
 *             pf_alt       -- "#" seen
 *
 * Won't get:  pf_hex
 *             pf_uc
 *             pf_unsigned
 *             pf_ptr
 *
 * This operation is async-signal-safe.  Takes into account the offset, and
 * adds up any overflow
 */
static enum pf_phase
qfs_arg_string(qf_str qfs, const char* src, pf_flags_t flags,
                                                       int width, int precision)
{
  int len ;

  if (flags != (flags & pf_precision))
    return pfp_failed ;

  if (precision < 0)            /* make sure            */
    {
      precision = 0 ;
      flags &= ~pf_precision ;
    } ;

  len = 0 ;
  if (src != NULL)
    while (*(src + len) != '\0') ++len ;

  if (((precision > 0) || (flags & pf_precision)) && (len > precision))
    len = precision ;

  qfs_append_justified_n(qfs, src, len, width) ;

  return pfp_done ;
} ;

/*------------------------------------------------------------------------------
 * %c handler
 *
 * Accepts:    width
 *
 * Rejects:    precision
 *             pf_precision -- explicit precision
 *             pf_commas    -- "'" seen
 *             pf_plus      -- "+" seen
 *             pf_space     -- " " seen
 *             pf_zeros     -- "0" seen
 *             pf_alt       -- "#" seen
 *
 * Won't get:  pf_hex
 *             pf_uc
 *             pf_unsigned
 *             pf_ptr
 *
 * This operation is async-signal-safe.  Takes into account the offset, and
 * adds up any overflow
 */
static enum pf_phase
qfs_arg_char(qf_str qfs, char ch, pf_flags_t flags, int width, int precision)
{
  if ((flags != 0) || (precision != 0))
    return pfp_failed ;

  qfs_append_justified_n(qfs, (char*)&ch, 1, width) ;

  return pfp_done ;
} ;

/*------------------------------------------------------------------------------
 * %d, %i, %u, %o, %x, %X and %p handler
 *
 * Accepts: pf_commas     -- format with commas or '_' for hex (non-standard)
 *                           ignored for octal.
 *          pf_minus      -- left justify (any width will be -ve)
 *          pf_plus       -- requires sign
 *          pf_space      -- requires space or '-'
 *          pf_zeros      -- zero fill to width
 *          pf_alt        -- '0x' or '0X' for hex
 *                           '0' for octal
 *
 *          pf_precision  -- precision specified
 *
 *          pf_unsigned   -- value is unsigned
 *          pf_ptr        -- value is a void* pointer
 *          pf_hex        -- render in hex
 *          pf_uc         -- render hex in upper case
 *
 *     and: all the number argument types.
 *
 * Rejects: ant == ant_long_double -- which is how the parser spots an
 *          erroneous %Ld for example.
 *
 * This operation is async-signal-safe.  Takes into account the offset, and
 * adds up any overflow
 */
static enum pf_phase
qfs_arg_integer(qf_str qfs, va_list* p_va, pf_flags_t flags,
                                int width, int precision, enum arg_num_type ant)
{
  uintmax_t     u_val ;
  intmax_t      s_val ;

  /* Reject if seen an 'L'
   */
  if (ant == ant_long_double)
    return pfp_failed ;

  /* Special for hex with '0...  if no explicit precision, set -1 for byte
   * and -2 for everything else -- see qfs_number().
   */
  if ((flags & (pf_hex | pf_precision)) == pf_hex)
    {
      if ((flags & (pf_commas | pf_zeros)) == (pf_commas | pf_zeros))
        {
          precision = (ant == ant_char) ? -1 : -2 ;
          flags |= pf_precision ;
        } ;
    } ;

  /* It is assumed that all values can be mapped to a uintmax_t         */
  confirm(sizeof(uintmax_t) >= sizeof(uintptr_t)) ;

  if (flags & pf_unsigned)
    {
      switch (ant)
      {
        case ant_char:
        case ant_short:
        case ant_int:
          u_val = va_arg(*p_va, unsigned int) ;
          break ;

        case ant_long:
          u_val = va_arg(*p_va, unsigned long) ;
          break ;

        case ant_long_long:
          u_val = va_arg(*p_va, unsigned long long) ;
          break ;

        case ant_intmax_t:
          u_val = va_arg(*p_va, uintmax_t) ;
          break ;

        case ant_size_t:
          u_val = va_arg(*p_va, size_t) ;
          break ;

        case ant_ptr_t:
          u_val = va_arg(*p_va, uintptr_t) ;
          break ;

        default:
          zabort("impossible integer size") ;
      } ;

      qfs_unsigned(qfs, u_val, flags, width, precision) ;
    }
  else
    {
      switch (ant)
      {
        case ant_char:
        case ant_short:
        case ant_int:
          s_val = va_arg(*p_va, signed int) ;
          break ;

        case ant_long:
          s_val = va_arg(*p_va, signed long) ;
          break ;

        case ant_long_long:
          s_val = va_arg(*p_va, signed long long) ;
          break ;

        case ant_intmax_t:
          s_val = va_arg(*p_va, intmax_t) ;
          break ;

        case ant_size_t:
          s_val = va_arg(*p_va, ssize_t) ;
          break ;

        case ant_ptr_t:
          s_val = va_arg(*p_va, intptr_t) ;
          break ;

        default:
          zabort("impossible integer size") ;
      } ;

      qfs_signed(qfs, s_val, flags, width, precision) ;
    } ;

  return pfp_done ;
} ;

/*------------------------------------------------------------------------------
 * %e, %E, %f, %F, %g, %G, %a and %A handler
 *
 * This uses the standard library sprintf() to do the business, so this is
 * NOT async-signal-safe.  This means that we get the full precision supported
 * by the system !  Attempting to construct async-signal-safe conversion is
 * doomed to failure, because any floating point operation may affect flags
 * and other state in the processor :-(
 *
 * This operation is *NOT* async-signal-safe.  Takes into account the offset,
 * and adds up any overflow
 */

union float_value
{
  double       d ;
  long double  ld ;
} ;

static int
qfs_arg_float_snprintf(void* buf, int have, const char* format,
                                union float_value* p_val, enum arg_num_type ant)
{
  if (ant == ant_default)
    return snprintf(buf, have, format, p_val->d) ;
  else
    return snprintf(buf, have, format, p_val->ld) ;
} ;

static enum pf_phase
qfs_arg_float(qf_str qfs, va_list* p_va, const char* start, size_t flen,
                                                          enum arg_num_type ant)
{
  union float_value val ;
  char format[flen + 1] ;
  int want ;

  if (ant == ant_default)
    val.d  = va_arg(*p_va, double) ;
  else
    val.ld = va_arg(*p_va, long double) ;

  memcpy(format, start, flen) ;
  format[flen + 1] = '\0' ;

  if (qfs->offset == 0)
    {
      /* No offset, so can use the qfs directly.
       */
      int have ;

      have = qfs_left(qfs) ;
      want = qfs_arg_float_snprintf(qfs->ptr, have + 1, format, &val, ant) ;

      if (want > 0)
        {
          if (want <= have)
            qfs->ptr += want ;
          else
            {
              qfs->ptr = qfs->end ;
              qfs->overflow += (want - have) ;
            } ;
        } ;
    }
  else
    {
      /* Because the offset is not zero, need to use an intermediate
       * buffer and then copy part after the offset.
       *
       * First, discover full extent of the formatted value, then if that
       * exceeds the offset, construct buffer and copy what we can to the
       * qps; otherwise, reduce the offset.
       */
      want = qfs_arg_float_snprintf(NULL, 0, format, &val, ant) ;

      if (want > 0)
        {
          int take ;

          take = qfs->offset + qfs_left(qfs) ;
          if (take > want)
            take = want ;

          {
            char tmp[take + 1] ;
            want = qfs_arg_float_snprintf(tmp, take + 1, format, &val, ant) ;

            if (want > 0)
              qfs_append_n(qfs, tmp, want) ;
          } ;
        } ;
    } ;

  return (want >= 0) ? pfp_done : pfp_failed ;
} ;

/*==============================================================================
 * Construction of scaled numbers.
 *
 *
 *
 *
 */

enum { scale_max = 6 } ;

static const char* scale_d_tags [] =
{
    [0] = " " ,
    [1] = "k",
    [2] = "m",
    [3] = "g",
    [4] = "t",          /* Tera 10^12   */
    [5] = "p",          /* Peta 10^15   */
    [6] = "e",          /* Exa  10^18   */
} ;
CONFIRM((sizeof(scale_d_tags) / sizeof(char*)) == (scale_max + 1)) ;

static const char* scale_b_tags [] =
{
    [0] = " " ,
    [1] = "K",
    [2] = "M",
    [3] = "G",
    [4] = "T",
    [5] = "P",
    [6] = "E",
} ;
CONFIRM((sizeof(scale_b_tags) / sizeof(char*)) == (scale_max + 1)) ;

static const ulong p10 [] =
{
    [ 0] = 1l,
    [ 1] = 10l,
    [ 2] = 100l,
    [ 3] = 1000l,
    [ 4] = 10000l,
    [ 5] = 100000l,
    [ 6] = 1000000l,
    [ 7] = 10000000l,
    [ 8] = 100000000l,
    [ 9] = 1000000000l,
    [10] = 10000000000l,
    [11] = 100000000000l,
    [12] = 1000000000000l,
    [13] = 10000000000000l,
    [14] = 100000000000000l,
    [15] = 1000000000000000l,
    [16] = 10000000000000000l,
    [17] = 100000000000000000l,
    [18] = 1000000000000000000l,
    [19] = ULONG_MAX,           /* all abs(signed values) < this        */
} ;
CONFIRM((sizeof(p10) / sizeof(ulong)) == ((scale_max * 3) + 2)) ;
CONFIRM((LONG_MAX / 10) < 1000000000000000000l) ;  /* LONG_MAX < 10^19  */

static const ulong q10 [] =
{
    [ 0] = 1l / 2,
    [ 1] = 10l / 2,
    [ 2] = 100l / 2,
    [ 3] = 1000l / 2,
    [ 4] = 10000l / 2,
    [ 5] = 100000l / 2,
    [ 6] = 1000000l / 2,
    [ 7] = 10000000l / 2,
    [ 8] = 100000000l / 2,
    [ 9] = 1000000000l / 2,
    [10] = 10000000000l / 2,
    [11] = 100000000000l / 2,
    [12] = 1000000000000l / 2,
    [13] = 10000000000000l / 2,
    [14] = 100000000000000l / 2,
    [15] = 1000000000000000l / 2,
    [16] = 10000000000000000l / 2,
    [17] = 100000000000000000l / 2,
    [18] = 1000000000000000000l / 2,
} ;
CONFIRM((sizeof(q10) / sizeof(ulong)) == ((scale_max * 3) + 1)) ;

static ulong qfs_form_sign(qf_str qfs, long val, pf_flags_t flags) ;

static void qfs_form_scaled(qf_str qfs, ulong v, int d,
                                            const char* tag, pf_flags_t flags) ;

/*------------------------------------------------------------------------------
 * Form value scaled to 4 significant digits, or as simple decimal.
 *
 * When scaling, scale by powers of 1,000, to produce (with pf_commas):
 *
 *        0..999            1, 2 or 3 digits     ) optionally followed by ' '
 *    1,000..9,999          4 digits with comma  )
 *
 *    10,000..99,994        as 99.99k -- rounded
 *    99,995..999,949       as 999.9k -- rounded
 *    999,950..9,999,499    as 9,999k -- rounded
 *
 *    thereafter, as for 'k', but with 'm', 'g', etc.
 *
 * When not scaling, produce simple decimal with optional trailing space.
 *
 * In any case, produce a leading sign if required.
 *
 * Accepts the following pf_xxx flags:
 *
 *   pf_scale    -- scale as above (if not, no scaling)
 *   pf_trailing -- include blank scale for units
 *   pf_commas   -- format with commas
 *   pf_plus     -- add '+' sign if >= 0
 *   pf_plus_nz  -- add '+' sign if >  0
 *   pf_space    -- add ' ' sign if >= 0 *and* not already added '+'
 *
 * Produces the minimum number of characters possible.  With pf_trailing, the
 * result can be right aligned to line up the digits -- a field of 6 is
 * required -- or 7 with sign character.
 */
extern qfs_num_str_t
qfs_dec_value(long val, pf_flags_t flags)
{
  qfs_num_str_t QFB_QFS(str, qfs) ;
  int   d, t ;
  ulong v ;

  flags &= (pf_commas | pf_plus | pf_plus_nz | pf_space
                                                     | pf_scale | pf_trailing) ;
  v = qfs_form_sign(qfs, val, flags) ;

  t = 0 ;
  d = 0 ;

  if ((flags & pf_scale) != 0)
    {
      int i ;

      /* Find 'i' such that:
       *
       *   1) i is multiple of 3
       *
       *   2) v < 10^(i + 4)
       *
       *   3) i <= (scale_max - 1) * 3
       *
       * For:          0..9,999          i = 0
       *          10,000..9,999,999      i = 3
       *      10,000,000..9,999,999,999  i = 6
       *      etc.
       *
       * So, where i > 0, need to divide by 10^(i), 10^(i-1) or 10^(i-2) in
       * in order to get the ms 4 digits.
       */
      i = 0 ;
      while ((v >= p10[i + 4]) && (i < ((scale_max - 1) * 3)))
        i += 3 ;

      if (i > 0)
        {
          /* Maximum i == (scale_max - 1) * 3 -- and have p10 upto and
           * including scale_max * 3.
           */
          qassert(v >= p10[i + 1]) ;

          if      (v < p10[i + 2])
            d = 2 ;
          else if (v < p10[i + 3])
            d = 1 ;
          else
            d = 0 ;

          /* Scale down to required number of decimals and round.
           *
           * If is thousands, then i = 3, if value = 10,000 (smallest possible)
           * then d == 2.  So divide by 5 (q10[3 - 2]) to make ls bit the
           * rounding bit, add one and shift off the rounding bit.
           *
           * The result should be 1000..9999, unless value is greater than our
           * ability to scale, or has rounded up one decade.
           */
          v = ((v / q10[i - d]) + 1) >> 1 ;
          t = i / 3 ;

          qassert(v >= 1000) ;

          /* Deal with having rounded up to too many digits.
           *
           * Adjusts the number of digits after the '.' and divides the
           * value by 10 -- changes up the thousands scaling if required.
           *
           * Unless have d == 0 and t == scale_max, in which case we leave the
           * rounded up value as it is.
           */
          if ((v > 9999) && !((d == 0) && (t == scale_max)))
            {
              qassert(v == (9999 + 1)) ;

              --d ;
              v /= 10 ;

              if (d < 0)
                {
                  d = 2 ;       /* wrap round   */
                  ++t ;         /* upscale      */
                } ;
            } ;
        } ;
    } ;

  qfs_form_scaled(qfs, v, d, scale_d_tags[t], flags) ;
  qfs_term(qfs) ;

  return str ;
} ;

/*------------------------------------------------------------------------------
 * Form value scaled to 4 significant digits, or as simple decimal.
 *
 * When scaling, scale by powers of 1,024, to produce:
 *
 *            0..999         1, 2 or 3 digits, optionally followed by " "
 *
 *         1000..10239       0.977K..9.999K  )
 *        10240..102394      10.00K..99.99K  )
 *       102395..1023948     100.0K..999.9K  ) -- rounded
 *      1023949..10485235    1.000M..9.999M  )
 *     10485236..104852357   10.00M..99.99M  )
 *
 *    ..etc for 'G', 'T', 'P' etc.
 *
 * When not scaling, produce simple decimal with optional trailing space.
 *
 * In any case, produce a leading sign if required.
 *
 * Accepts the following pf_xxx flags:
 *
 *   pf_scale    -- scale as above (if not, no scaling)
 *   pf_trailing -- include blank scale for units
 *   pf_commas   -- format with commas
 *   pf_plus     -- add '+' sign if >= 0
 *   pf_plus_nz  -- add '+' sign if >  0
 *   pf_space    -- add ' ' sign if >= 0 *and* not already added '+'
 *
 * Produces the minimum number of characters possible.  With pf_trailing, the
 * result can be right aligned to line up the digits -- a field of 6 is
 * required -- or 7 with sign character.
 */
extern qfs_num_str_t
qfs_bin_value(long val, pf_flags_t flags)
{
  qfs_num_str_t QFB_QFS(str, qfs) ;
  ulong v ;
  int d, p ;

  flags &= (pf_commas | pf_plus | pf_plus_nz | pf_space
                                                     | pf_scale | pf_trailing) ;
  v = qfs_form_sign(qfs, val, flags) ;

  p = 0 ;
  d = 0 ;

  if ((flags & pf_scale) != 0)
    {
      ulong vs ;

      /* Find the power of 1024 which leaves the value < 1000,
       * and set v = val / 1024^p -- subject to p <= scale_max
       */
      vs = v ;
      while ((vs >= 1000) && (p < scale_max))
        {
          vs >>= 10 ;           /* find power of 1024 scale     */
          p += 1 ;
        } ;

      if (p > 0)
        {
          ulong e ;
          int   is ;

          /* value is >= 1024, so v is whole number of KiB, MiB, ...
           *
           * Need to know how many decimal fraction digits we need to get to
           * 4 significant figures (3 if v is zero).
           *
           * Will scale up by 10^d.  d == 0 iff p == scale_max
           */
          if      (vs < 10)
            d = 3 ;             /* number of decimals expected  */
          else if (vs < 100)
            d = 2 ;
          else if (vs < 1000)
            d = 1 ;
          else
            d = 0 ;             /* where p == scale_max         */

          /* Scale up to the required number of decimals, shift down so that
           * only ms bit of fraction is left, round and shift off rounding bit.
           *
           * If d != 0, then will scale up by 10, 100 or 1000.  If the value is
           * greater than ULONG_MAX / 1024, then we do the bottom 10 bits
           * separately, and scale the calculation down by 10 bits.
           */
          e  = 0 ;              /* assume no extra bits         */
          is = p * 10 ;         /* the shift down               */

          if ((d != 0) && (v > (ULONG_MAX >> 10)))
            {
              e = (v & 0x3FF) * p10[d] ;        /* take bottom 10 bits  */
              e >>= 10 ;        /* discard 10 bits of extra part        */
              v >>= 10 ;        /* scale down value                     */
              is -= 10 ;        /* reduce shift                         */
            } ;

          v = ((((v * p10[d]) + e) >> (is - 1)) + 1) >> 1 ;

          qassert(v >= 975) ; /* 999 / 1024 = 0.9756                  */

          /* Deal with having rounded up to too many digits.
           *
           * Adjusts the number of digits after the '.' and divides the
           * value by 10 -- changes up the thousands scaling if required.
           *
           * Will have d == 0 iff p == scale_max, in which case we leave the
           * rounded up value as it is.
           */
          if (d == 0)
            qassert(p == scale_max) ;

          if ((v > 9999) && (d > 0))
            {
              qassert(v == (9999 + 1)) ;

              --d ;
              v /= 10 ;

              if (d == 0)
                {
                  d = 3 ;       /* wrap round   */
                  ++p ;         /* upscale      */
              } ;

            } ;
       } ;
    } ;

  qfs_form_scaled(qfs, v, d, scale_b_tags[p], flags) ;
  qfs_term(qfs) ;

  return str ;
} ;

/*------------------------------------------------------------------------------
 * Form a time period value.
 *
 *    +/-999d99h99m99h99.999s
 *
 * Accepts the following pf_xxx flags:
 *
 *   pf_commas   -- format with commas
 *   pf_plus     -- add '+' sign if >= 0
 *   pf_plus_nz  -- add '+' sign if >  0
 *   pf_space    -- add ' ' sign if >= 0 *and* not already added '+'
 */
extern qfs_num_str_t
qfs_time_period(qtime_t val, pf_flags_t flags)
{
  qfs_num_str_t QFB_QFS(str, qfs) ;
  ulong v ;
  int w ;

  confirm(sizeof(v) >= sizeof(qtime_t)) ;

  /* Worry about the sign
   */
  v = qfs_form_sign(qfs, val, flags) ;

  flags &= pf_commas ;  /* unlikely though that is !    */

  /* Round value to milli seconds
   */
  v = (v + (QTIME_SECOND / 2000)) / (QTIME_SECOND / 1000) ;

  w = 0 ;

  if (v >= (2 * 24 * 60 * 60 * 1000))
    {
      qfs_unsigned(qfs, v / (24 * 60 * 60 * 1000), flags, w, w) ;
      qfs_append_ch(qfs, 'd') ;

      v %= (24 * 60 * 60 * 1000) ;
      flags = pf_zeros ;
      w = 2 ;
    } ;

  if ((v >= (2 * 60 * 60 * 1000)) || (w > 0))
    {
      qfs_unsigned(qfs, v / (60 * 60 * 1000), flags, w, w) ;
      qfs_append_ch(qfs, 'h') ;

      v %= (60 * 60 * 1000) ;
      flags = pf_zeros ;
      w = 2 ;
    } ;

  if ((v >= (2 * 60 * 1000)) || (w > 0))
    {
      qfs_unsigned(qfs, v / (60 * 1000), flags, w, w) ;
      qfs_append_ch(qfs, 'm') ;

      v %= (60 * 1000) ;
      flags = pf_zeros ;
      w = 2 ;
    } ;

  qfs_unsigned(qfs, v / 1000, flags, w, w) ;
  qfs_append_ch(qfs, '.') ;
  qfs_unsigned(qfs, v % 1000, pf_zeros, 3, 3) ;
  qfs_append_ch(qfs, 's') ;

  qfs_term(qfs) ;

  return str ;
} ;

/*------------------------------------------------------------------------------
 * Form string for number, with commas and "d" decimal digits, followed
 * by the given tag -- where d = 0..4
 *
 * Flags:  pf_commas     => insert commas before '.' if required
 *         pf_trailing   => include blank scale for units
 *
 * So: val=1234567, d=2, tag="k" -> "12,345.67k" (with pf_commas)
 *     val=1234,    d=0, tag=""  -> "1,234"
 */
static void
qfs_form_scaled(qf_str qfs, ulong v, int d, const char* tag, pf_flags_t flags)
{
  if (d == 0)
    qfs_unsigned(qfs, v, flags, 0, 0) ;
  else
    {
      ldiv_t r = ldiv((long)v, p10[d]) ;

      qfs_unsigned(qfs, r.quot, flags, 0, 0) ;
      qfs_append_ch(qfs, '.') ;
      qfs_unsigned(qfs, r.rem, pf_zeros, d, 0) ;
    } ;

  if ((*tag != ' ') || ((flags & pf_trailing) != 0))
    qfs_append(qfs, tag) ;
} ;

/*------------------------------------------------------------------------------
 * Sort out sign for value and return the abs(val)
 */
static ulong
qfs_form_sign(qf_str qfs, long val, pf_flags_t flags)
{
  if       (val < 0)
    {
      qfs_append_ch(qfs, '-') ;

      return (ulong)labs(val + 1) + 1 ;
    }
  else if ((flags & pf_plus) || ((flags & pf_plus_nz) && (val > 0)))
    qfs_append_ch(qfs, '+') ;
  else if (flags & pf_space)
    qfs_append_ch(qfs, ' ') ;

  return val ;
} ;

/*==============================================================================
 * Simple keyword handling
 */

static qfs_keyword_t deny_permit_table[] =
{
  { .word = "deny",    .val = 0 },
  { .word = "permit",  .val = 1 },
  { .word = NULL }
} ;

extern void test_keyword(void) ;
extern void
test_keyword(void)
{
  qfs_keyword_lookup(deny_permit_table, "den", true) ;
} ;

/*------------------------------------------------------------------------------
 * Keyword lookup -- case sensitive, optional partial match.
 *
 * Given a keyword table, see if given string matches.
 *
 * Keyword table is an array of qfs_keyword_t items.  Each item is a keyword
 * (const char*) and an unsigned value 0..INT_MAX.  Table is terminated by a
 * NULL keyword.
 *
 * This is not very clever, but does not require the keyword table to be in any
 * particular order.
 *
 * If "strict", requires string to completely match a keyword.  Otherwise,
 * requires the string to be the leading substring of only one of the given
 * keywords -- but stops immediately if gets a complete match.
 *
 * Returns: >=  0 => found -- this is value from table
 *          == -1 => not found
 *          == -2 => found 2 or more matches (and no exact match)
 *
 * NB: match is case sensitive.
 */
extern int
qfs_keyword_lookup(qfs_keyword_t* table, const char* str, bool strict)
{
  qfs_keyword_t* e ;
  const char* word ;
  uint len, wlen ;
  int  r ;

  len = strlen(str) ;

  e = table ;
  r = -1 ;

  while ((word = e->word) != NULL)
    {
      qassert(e->val <= INT_MAX) ;

      wlen = strlen(word) ;

      if      (wlen > len)
        {
          if (!strict && (strncmp(str, word, len) == 0))
            {
              if (r == -1)
                r = e->val ;
              else
                r = -2 ;                /* ambiguous    */
            } ;
        }
      else if (wlen == len)
        {
          if (strcmp(str, word) == 0)
            return e->val ;             /* exact match  */
        } ;

      ++e ;
    } ;

  return r ;
} ;

/*------------------------------------------------------------------------------
 * Keyword lookup -- case *insensitive*, optional partial match.
 *
 * Same as qfs_keyword_lookup() except case insensitive.
 *
 * strncasecmp() and strcasecmp() appear to have been POSIX Base since 2001.
 */
extern int
qfs_keyword_lookup_nocase(qfs_keyword_t* table, const char* str, bool strict)
{
  qfs_keyword_t* e ;
  const char* word ;
  uint len, wlen ;
  int  r ;

  len = strlen(str) ;

  e = table ;
  r = -1 ;

  while ((word = e->word) != NULL)
    {
      qassert(e->val <= INT_MAX) ;

      wlen = strlen(word) ;

      if      (wlen > len)
        {
          if (!strict && (strncasecmp(str, word, len) == 0))
            {
              if (r == -1)
                r = e->val ;
              else
                r = -2 ;                /* ambiguous    */
            } ;
        }
      else if (wlen == len)
        {
          if (strcasecmp(str, word) == 0)
            return e->val ;             /* exact match  */
        } ;

      ++e ;
    } ;

  return r ;
} ;

/*------------------------------------------------------------------------------
 * Keyword extract -- case sensitive, optional partial match.
 *
 * Extracts keyword from an abstract array, accessed using the given function,
 * starting from 0 and increasing by 1 until function returns NULL.
 *
 * Note that the function can return an empty string, for index values which
 * are to be ignored.
 *
 * Returns: >=  0 => found -- this is the index of the abstract array entry
 *          == -1 => not found
 *          == -2 => found 2 or more matches (and no exact match)
 *
 * NB: match is case sensitive.
 */
extern int
qfs_keyword_lookup_abstract(void* a_array, const char* str, bool strict,
                             const char* (*a_lookup)(void* a_array, uint index))
{
  uint i ;
  const char* word ;
  uint len, wlen ;
  int  r ;

  len = strlen(str) ;

  i =  0 ;
  r = -1 ;

  while (1)
    {
      word = a_lookup(a_array, i) ;

      if (word == NULL)
        break ;

      wlen = strlen(word) ;

      if      (wlen > len)
        {
          if (!strict && (strncmp(str, word, len) == 0))
            {
              if (r == -1)
                r = i ;
              else
                r = -2 ;                /* ambiguous    */
            } ;
        }
      else if (wlen == len)
        {
          if (strcmp(str, word) == 0)
            return i ;                  /* exact match  */
        } ;

      ++i ;
    } ;

  return r ;
} ;
