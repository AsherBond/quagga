/* VTY I/O FIFO
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

#include "vio_fifo.h"
#include "qfstring.h"
#include "network.h"

#include "memory.h"

/*==============================================================================
 * VTY I/O FIFO manages an arbitrary length byte-wise FIFO buffer.
 *
 * The FIFO is arranged as lumps of some given size.  Lumps are allocated
 * as and when necessary, and released once emptied.
 *
 * The last lump is never released.  So, it may be that only one lump is
 * ever needed.
 *
 * When releasing lumps, keeps one lump "spare", to be reused as necessary.
 *
 *------------------------------------------------------------------------------
 * Implementation notes:
 *
 * The FIFO is initialised with one lump in it (the "own_lump", which is
 * embedded in the FIFO structure).  There is always at least one lump in
 * the FIFO.
 *
 * The hold_ptr allows the get_ptr to move forward, but retaining the data in
 * the FIFO until the hold_ptr is cleared.  Can move the get_ptr back to the
 * hold_ptr to reread the data.
 *
 * The end_ptr allows put_ptr to move forward, but the new data cannot be got
 * from the FIFO until the end_ptr is cleared.  Can discard the new data
 * and move the put_ptr back to the end_ptr.
 *
 * There are four lumps of interest:
 *
 *   * head      -- where the hold_mark is, if there is one.
 *
 *   * get_lump  -- where the get_ptr is.
 *                  Same as head when no hold_mark.
 *
 *   * end_lump  -- where the end_mark is, if there is one.
 *                  Same as tail when no end mark.
 *
 *   * tail      -- where the put_ptr is.
 *
 * Some or all of those may be the same, depending on how big the FIFO is.
 *
 * The following are expected to be true:
 *
 *   * p_start == &get_ptr   => no hold mark
 *                &hold_ptr  => have hold mark
 *
 *   * put_ptr == get_ptr    => FIFO empty -- unless *p_start != get_ptr.
 *
 *   * put_end == tail->end  -- at all times
 *
 *     put_ptr >= tail->data )  otherwise something is broken
 *     put_ptr <= tail->end  )
 *
 *   * p_get_end == &get_lump->end -- when get_lump != end_lump
 *               == &end_ptr       -- when get_lump == end_lump & end mark set
 *               == &put_ptr       -- when get_lump == end_lump & no end mark
 *
 *     get_ptr >= get_lump->data )  otherwise something is broken
 *     get_ptr <= get_lump->end  )
 *
 *   * put_ptr == put_end    => tail lump is full
 *     put_ptr <  put_end    => space exists in the tail lump
 *     put_ptr >  put_end    => broken
 *
 *   * get_ptr == *p_get_end => nothing to get -- get_lump == end_lump
 *     get_ptr <  *p_get_end => data exists in the current get_lump
 *     get_ptr >  *p_get_end => broken
 *
 *   * p_end   == &end_ptr   -- when end mark set
 *             == &put_ptr   -- when no end mark
 *
 * Note that:
 *
 *   * while get_ptr < *p_get_end can get stuff without worrying about other
 *     pointers or moving between lumps etc.
 *
 *     When get_ptr reaches *p_get_end, however, must move to the next lump,
 *     if possible, or collapse the pointers if have hit the put_ptr.  Keeping
 *     the get_ptr honest in this way: (a) ensures that will always put at
 *     the beginning of a lump if possible; (b) simplifies the handling of
 *     hold_ptr et al (because get_ptr is never in the ambiguous position
 *     at the end of one lump, which is the same as the start of the next).
 *
 *     Similarly, while put_ptr < put_end, can put stuff without worrying
 *     about other pointers or moving between lumps etc.  Will leave the
 *     put_ptr at the very end of the current lump if just fills it.
 *
 *   * the value of p_get_end depends on whether get_lump == end_lump, and
 *     then whether there is an end_ptr.  But at any time, points to the end
 *     of what can be read by get_ptr without stepping between lumps etc.
 *
 *     Note that get_ptr == p_get_end <=> is at the current end of the FIFO,
 *     because after any get operation, will advance to the next lump.  If
 *     FIFO is empty after advancing the get_ptr, will reset the pointers
 *     back to the start of the then current (and only) lump.
 *
 *   * some care must be taken to ensure that if the fifo is empty, the
 *     pointers will be at the start of one empty lump.
 *
 *     In this context, empty means nothing between *p_start and put_ptr.
 *     Noting that *p_start is &hold_ptr or &get_ptr, depending on whether
 *     there is a hold mark or not.  (If *p_start == put_ptr, there may be
 *     an end mark, but it must be end_ptr == put_ptr !)
 *
 *       - get_ptr   -- as above, if this hits *p_get_end, must:
 *
 *                        * step to the next lump, if there is one, and,
 *                          unless something is held behind the get_ptr,
 *                          release the lump just stepped from.
 *
 *                        * if has hit put_ptr, reset pointers -- unless there
 *                          is something held behind the get_ptr.
 *
 *       - put_ptr   -- is always somewhere in the tail lump.
 *
 *       - end_ptr   -- when a new lump is added, if the end_ptr is at the
 *                      end of the last lump, it is moved to the start of the
 *                      new last lump (along with the put_ptr).
 *
 *                      If the end_ptr is equal to the put_ptr and the get_ptr
 *                      hits it, if the pointers are reset, the end_ptr will be
 *                      reset along with the put_ptr.
 *
 *                      If the put_ptr is reset back to the end_ptr, need to
 *                      see if the FIFO is empty, and reset pointers if it is.
 *
 *       - hold_ptr  -- because the pointers are reset whenever the FIFO
 *                      becomes empty, when hold_ptr is set, it will be at
 *                      the start of an empty FIFO, or somewhere in a not-
 *                      empty one.   When a hold_ptr is cleared, the get_ptr
 *                      may be equal to the put_ptr, and pointers must be
 *                      reset.
 */
inline static void vio_fifo_set_get_ptr(vio_fifo vff, vio_fifo_lump lump,
                                                                    char* ptr) ;
inline static void vio_fifo_set_get_end(vio_fifo vff) ;
inline static void vio_fifo_release_upto(vio_fifo vff, vio_fifo_lump upto) ;
static void vio_fifo_release_lump(vio_fifo vff, vio_fifo_lump lump) ;

/*------------------------------------------------------------------------------
 * Test whether there is a hold mark.
 */
inline static bool
vio_fifo_have_hold_mark(vio_fifo vff)
{
  return vff->p_start == &vff->hold_ptr ;
} ;

/*------------------------------------------------------------------------------
 * Test whether there is an end mark.
 */
inline static bool
vio_fifo_have_end_mark(vio_fifo vff)
{
  return vff->p_end == &vff->end_ptr ;
} ;

/*------------------------------------------------------------------------------
 * The FIFO is empty, with one lump -- reset all pointers.
 *
 * Preserves and hold mark or end mark -- so no need to change p_start or p_end.
 *
 * HOWEVER: does not set p_get_end -- so if get_lump or end_lump or p_end have
 *          changed, then must also call vio_fifo_set_get_end().
 *
 * ALSO:    does not set put_end -- so if the tail lumps has changed, that must
 *          be updated.
 */
inline static void
vio_fifo_reset_ptrs(vio_fifo vff)
{
  char* ptr = ddl_tail(vff->base)->data ;

  if (vio_fifo_debug)
    assert(ddl_head(vff->base) == ddl_tail(vff->base)) ;

  vff->hold_ptr = ptr ;
  vff->get_ptr  = ptr ;
  vff->end_ptr  = ptr ;
  vff->put_ptr  = ptr ;
} ;

/*------------------------------------------------------------------------------
 * This is called *iff* get_ptr >= *p_get_end -- and preferably only after
 * it has been adjusted forwards by at least 1 (but that is not required).
 *
 * If there is anything available to be got, adjust get_ptr and/or get_end
 * in order to be able to get it -- discarding lumps as required.
 */
Private void
vio_fifo_sync_get(vio_fifo vff)
{
  if (vio_fifo_debug)
    assert(vff->get_ptr == *vff->p_get_end) ;

  if (vff->get_lump == vff->end_lump)
    {
      if (vio_fifo_debug)
        assert(vff->get_ptr == *vff->p_end) ;

      /* We are in the end_lump, and there is nothing more to be read.
       *
       * If have reached the put_ptr, then unless there is something held
       * behind the get_ptr, the fifo is completely empty, and pointers can
       * be reset to the start of the end_lump (which is the only lump).
       *
       * p_start == &hold_ptr or &get_ptr, so can check for empty in one test.
       */
      if (*vff->p_start == vff->put_ptr)
        vio_fifo_reset_ptrs(vff) ;
    }
  else
    {
      /* Good news, can advance the get_ptr
       *
       * Step the get_ptr to the start of the next lump, and if no hold mark,
       * discard any lumps which precede the new get_lump.
       */
      vio_fifo_lump get_lump ;

      if (vio_fifo_debug)
        {
          assert(vff->get_lump != vff->end_lump) ;
          assert(vff->get_ptr == vff->get_lump->end) ;
        } ;

      get_lump = ddl_next(vff->get_lump, list) ;
      vio_fifo_set_get_ptr(vff, get_lump, get_lump->data) ;

      if (!vio_fifo_have_hold_mark(vff))
        vio_fifo_release_upto(vff, get_lump) ;
    } ;
} ;

/*------------------------------------------------------------------------------
 * Set get_lump/get_ptr and p_get_end to suit.
 */
inline static void
vio_fifo_set_get_ptr(vio_fifo vff, vio_fifo_lump lump, char* ptr)
{
  vff->get_lump = lump ;
  vff->get_ptr  = lump->data ;

  vio_fifo_set_get_end(vff) ;
} ;

/*------------------------------------------------------------------------------
 * Set the p_get_end depending on whether the get_lump == end_lump, or not.
 *
 * This must be called if the get_lump or the end_lump are changed, or if
 * p_end changes.
 */
inline static void
vio_fifo_set_get_end(vio_fifo vff)
{
  vff->p_get_end = (vff->get_lump == vff->end_lump) ? vff->p_end
                                                    : &vff->get_lump->end ;
} ;

/*------------------------------------------------------------------------------
 * Release all lumps upto (but excluding) the given lump.
 *
 * NB: takes no notice of hold_ptr or anything else.
 */
inline static void
vio_fifo_release_upto(vio_fifo vff, vio_fifo_lump upto)
{
  vio_fifo_lump lump ;
  while (ddl_head(vff->base) != upto)
    vio_fifo_release_lump(vff, ddl_pop(&lump, vff->base, list)) ;
} ;

/*==============================================================================
 * Initialisation, allocation and freeing of FIFO and lumps thereof.
 */

/*------------------------------------------------------------------------------
 * Allocate and initialise a new FIFO.
 *
 * The size given is the size for all lumps in the FIFO.  0 => default size.
 *
 * Size is rounded up to a 128 byte boundary.
 *
 * Once allocated and initialised, the FIFO contains one lump, and if it
 * grows to more than one, will retain a spare lump once it shrinks again.
 *
 * Keeping a pair of lumps allows the get_ptr to lag behind the put_ptr by
 * about a lump full, without requiring repeated memory allocation.  Also,
 * vio_fifo_write_nb() can be asked to write only lumps -- so if called
 * regularly while putting stuff to a FIFO, will write entire lumps at once.
 */
extern vio_fifo
vio_fifo_new(ulen size)
{
  vio_fifo vff ;
  ulen     total_size ;

  if (size == 0)
    size = VIO_FIFO_DEFAULT_LUMP_SIZE ;

  size = ((size + 128 - 1) / 128) * 128 ;

  if (vio_fifo_debug)
    size = 29 ;

  total_size = offsetof(struct vio_fifo, own_lump[0].data[size]) ;

  vff = XCALLOC(MTYPE_VIO_FIFO, total_size) ;

  /* Zeroising the the vio_fifo_t has set:
   *
   *    base      -- base pair, both pointers NULL => list is empty
   *
   *    p_start   -- X      -- see vio_fifo_ptr_set()
   *
   *    hold_ptr  -- NULL   -- not relevant until hold mark is set
   *
   *    get_lump  -- X )
   *    get_ptr   -- X )    -- see vio_fifo_ptr_set()
   *    p_get_end -- X )
   *
   *    end_lump  -- X      -- see vio_fifo_ptr_set()
   *
   *    end_ptr   -- NULL   -- not relevant until hold mark is set
   *
   *    put_ptr   -- X )
   *    put_end   -- X )    -- see vio_fifo_ptr_set()
   *    p_end     -- X )
   *
   *    size      -- X      -- set below
   *
   *    spare     -- NULL   -- no spare lump
   *
   *    own_lump  -- all zeros:  list  -- pointers NULL, set below
   *                             end   -- set below
   */
  vff->size          = size ;
  vff->own_lump->end = vff->own_lump->data + vff->size ;

  if (vio_fifo_debug)
    assert(vff->own_lump->end == ((char*)vff + total_size)) ;

  ddl_append(vff->base, vff->own_lump, list) ;

  vff->p_start    = &vff->get_ptr ;

  vff->get_lump   = vff->own_lump ;
  vff->get_ptr    = vff->own_lump->data ;
  vff->p_get_end  = &vff->put_ptr  ;

  vff->end_lump   = vff->own_lump ;

  vff->put_ptr    = vff->own_lump->data ;
  vff->put_end    = vff->own_lump->end ;

  vff->p_end      = &vff->put_ptr ;

  VIO_FIFO_DEBUG_VERIFY(vff) ;

  return vff ;
} ;

/*------------------------------------------------------------------------------
 * Free contents of given FIFO and the FIFO structure as well.
 *
 * Does nothing if given a NULL pointer -- must already have been freed !
 *
 * Returns:  NULL
 */
extern vio_fifo
vio_fifo_free(vio_fifo vff)
{
  if (vff != NULL)
    {
      vio_fifo_lump lump ;

      lump = vff->spare ;
      vff->spare = NULL ;

      do
        {
          if (lump != vff->own_lump)
            XFREE(MTYPE_VIO_FIFO_LUMP, lump) ;  /* accepts lump == NULL */

          ddl_pop(&lump, vff->base, list) ;
        }
      while (lump != NULL) ;

      XFREE(MTYPE_VIO_FIFO, vff) ;
    } ;

  return NULL ;
} ;

/*------------------------------------------------------------------------------
 * Clear out contents of FIFO -- will continue to use the FIFO.
 *
 * If required, clears any hold mark and/or end mark.
 *
 * Keeps one spare lump.
 *
 * Does nothing if there is no FIFO !
 */
extern void
vio_fifo_clear(vio_fifo vff, bool clear_marks)
{
  vio_fifo_lump lump ;

  if (vff == NULL)
    return ;

  VIO_FIFO_DEBUG_VERIFY(vff) ;

  lump = ddl_tail(vff->base) ;

  vio_fifo_release_upto(vff, lump) ;

  vff->get_lump = lump ;
  vff->end_lump = lump ;

  vio_fifo_reset_ptrs(vff) ;

  if (clear_marks)
    {
      vff->p_start = &vff->get_ptr ;
      vff->p_end   = &vff->put_ptr ;
    } ;

  vff->p_get_end = vff->p_end ;

  VIO_FIFO_DEBUG_VERIFY(vff) ;
} ;

/*------------------------------------------------------------------------------
 * Add a new lump to put stuff into -- work-horse for putting to the FIFO.
 *
 * Call when (vff->put_ptr >= vff->put_end) -- asserts that they are equal.
 *
 * The FIFO cannot be empty -- if it were, the pointers would have been reset,
 * and could not be vff->put_ptr >= vff->put_end !!
 *
 * Allocates a new lump (or reuses the spare) and updates the put_ptr.
 *
 * If the end_ptr and the put_ptr were equal, then advances that too, which
 * ensures that the end_ptr is not ambiguous.

 * If the get_ptr and the put_ptr were equal, then advances that too, which
 * ensures that the get_ptr is not ambiguous.  This can be the case if there
 * is a hold_ptr.
 */
Private void
vio_fifo_add_lump(vio_fifo vff)
{
  vio_fifo_lump lump ;

  assert(vff->put_ptr == vff->put_end) ;    /* must be end of tail lump */
  assert(vff->put_ptr != *vff->p_start) ;   /* cannot be empty !        */

  VIO_FIFO_DEBUG_VERIFY(vff) ;

  /* If we can use the spare, do so, otherwise make a new one and
   * add to the end of the FIFO.
   */
  lump       = vff->spare ;
  vff->spare = NULL ;

  if (lump == NULL)
    {
      ulen lump_size = offsetof(vio_fifo_lump_t, data[vff->size]) ;

      lump = XMALLOC(MTYPE_VIO_FIFO_LUMP, lump_size) ;
      lump->end = (char*)lump + lump_size ;

      if (vio_fifo_debug)
        assert(lump->end == (lump->data + vff->size)) ;
    } ;

  ddl_append(vff->base, lump, list) ;

  /* Allocated new lump on the end of FIFO.
   *
   * If the get_ptr == put_ptr, advance the get_ptr.  If there is an end_ptr,
   * it must be == put_ptr, and is about to advance too.
   *
   * If put_ptr == *p_end, advance the end_lump and the end_ptr.  If there is
   * no end_mark, then p_end == &put_ptr, and the end_lump must follow the
   * put_ptr.  If there is an end_mark, then p_end == &end_ptr, and that must
   * follow the put_ptr if they are equal.
   *
   * The get_lump may or may not have been the end_lump, and that may or may
   * not have changed.  Simplest thing is to set p_get_end to what it should
   * be now.
   */
  if (vff->get_ptr == vff->put_ptr)
    {
      if (vio_fifo_debug)
        assert(vio_fifo_have_hold_mark(vff)) ;

      vff->get_lump  = lump ;
      vff->get_ptr   = lump->data ;
    } ;

  if (vff->put_ptr == *vff->p_end)
    {
      vff->end_lump  = lump ;
      vff->end_ptr   = lump->data ;     /* no effect if no end_mark     */
    } ;

  vff->put_ptr = lump->data ;
  vff->put_end = lump->end ;

  vio_fifo_set_get_end(vff) ;

  VIO_FIFO_DEBUG_VERIFY(vff) ;
} ;

/*------------------------------------------------------------------------------
 * Release the given lump, provided it is neither get_lump nor end_lump.
 *
 * If don't have a spare lump, keep this one.
 * If do have a spare lump, discard this one, unless it is "own_lump".
 */
static void
vio_fifo_release_lump(vio_fifo vff, vio_fifo_lump lump)
{
  assert(lump != NULL) ;
  assert(lump != vff->get_lump) ;
  assert(lump != vff->end_lump) ;

  if (vff->spare == NULL)
    vff->spare = lump ;
  else
    {
      if (lump == vff->own_lump)
        {
          lump = vff->spare ;   /* free the spare instead       */
          vff->spare = vff->own_lump ;
        } ;

      XFREE(MTYPE_VIO_FIFO_LUMP, lump) ;
    } ;
} ;

/*==============================================================================
 * Put data to the FIFO.
 */

/*------------------------------------------------------------------------------
 * Put 'n' bytes -- allocating as required.
 */
extern void
vio_fifo_put_bytes(vio_fifo vff, const char* src, ulen n)
{
  VIO_FIFO_DEBUG_VERIFY(vff) ;

  while (n > 0)
    {
      ulen take ;

      if (vff->put_ptr >= vff->put_end)
        vio_fifo_add_lump(vff) ;         /* traps put_ptr > put_end    */

      take = (vff->put_end - vff->put_ptr) ;
      if (take > n)
        take = n ;

      memcpy(vff->put_ptr, src, take) ;
      vff->put_ptr += take ;

      src += take ;
      n   -= take ;
    } ;

  VIO_FIFO_DEBUG_VERIFY(vff) ;
} ;

/*------------------------------------------------------------------------------
 * Formatted print to FIFO -- cf printf()
 *
 * Returns: >= 0 -- number of bytes written
 *           < 0 -- failed (unlikely though that is)
 */
extern int
vio_fifo_printf(vio_fifo vff, const char* format, ...)
{
  va_list args;
  int      len ;

  va_start (args, format);
  len = vio_fifo_vprintf(vff, format, args);
  va_end (args);

  return len;
} ;

/*------------------------------------------------------------------------------
 * Formatted print to FIFO -- cf vprintf()
 *
 * Does nothing if vff is NULL !
 *
 * Returns: >= 0 -- number of bytes written
 *           < 0 -- failed (unlikely though that is)
 *
 * NB: uses qfs_vprintf(qfs, format, va), which allows the result to be
 *     collected a section at a time, if required.  With reasonable size
 *     lumps, expect to need no more than two sections, and then only
 *     occasionally.
 */
extern int
vio_fifo_vprintf(vio_fifo vff, const char *format, va_list va)
{
  qf_str_t qfs ;
  ulen     done ;

  if (vff == NULL)
    return 0 ;

  VIO_FIFO_DEBUG_VERIFY(vff) ;

  done = 0 ;
  do
    {
      ulen did ;

      if (vff->put_ptr >= vff->put_end)
        vio_fifo_add_lump(vff) ;         /* traps put_ptr > put_end      */

      qfs_init_offset(qfs, vff->put_ptr, vff->put_end - vff->put_ptr, done) ;

      did = qfs_vprintf(qfs, format, va) ;

      done         += did ;
      vff->put_ptr += did ;
    }
  while (qfs_overflow(qfs) != 0) ;

  VIO_FIFO_DEBUG_VERIFY(vff) ;

  return done ;
} ;

/*------------------------------------------------------------------------------
 * Read part of file into FIFO -- assuming non-blocking file
 *
 * Will read up to the end of the current lump, then will read as may whole
 * lumps as are requested -- request of 0 reads up to the end of the current
 * lump (at least 1 byte).  Will stop if would block.
 *
 * Except where blocking intervenes, this reads in units of the lump size.
 *
 * Returns: 0..n -- number of bytes read
 *         -1 => failed -- see errno
 *         -2 => EOF met immediately
 *
 * Note: will work perfectly well for a non-blocking file -- which should
 *       never return EAGAIN/EWOULDBLOCK, so will return from here with
 *       something, error or EOF.
 */
extern int
vio_fifo_read_nb(vio_fifo vff, int fd, ulen request)
{
  ulen total ;

  VIO_FIFO_DEBUG_VERIFY(vff) ;

  total = 0 ;

  do
    {
      int  got ;

      if (vff->put_ptr >= vff->put_end)
        {
          vio_fifo_add_lump(vff) ;       /* traps put_ptr > put_end      */

          if (request > 0)
            --request ;
        } ;

      got = read_nb(fd, vff->put_ptr, vff->put_end - vff->put_ptr) ;

      if (got <= 0)
        {
          if (got == -2)                /* EOF met                      */
            return (total > 0) ? (int)total : got ;
          else
            return (got  == 0) ? (int)total : got ;
        } ;

      vff->put_ptr += got ;
      total        += got ;

    } while (request > 0) ;

  VIO_FIFO_DEBUG_VERIFY(vff) ;

  return total ;
} ;

/*==============================================================================
 * Copy operations -- from one FIFO to another.
 */

/*------------------------------------------------------------------------------
 * Copy src FIFO (everything from get_ptr to end mark or put_ptr) to dst FIFO.
 *
 * Create a dst FIFO if there isn't one.  There must be a src FIFO.
 *
 * Appends to the dst FIFO.
 *
 * Does not change the src FIFO in any way.
 */
extern vio_fifo
vio_fifo_copy(vio_fifo dst, vio_fifo src)
{
  vio_fifo_lump src_lump ;
  char*         src_ptr ;

  if (dst == NULL)
    dst = vio_fifo_new(src->size) ;

  VIO_FIFO_DEBUG_VERIFY(src) ;
  VIO_FIFO_DEBUG_VERIFY(dst) ;

  src_lump = src->get_lump ;
  src_ptr  = src->get_ptr ;

  while (1)
    {
      char* src_end ;

      if (src_lump != src->end_lump)
        src_end = src_lump->end ;       /* end of not end_lump  */
      else
        src_end = *src->p_end ;         /* end of end_lump      */

      vio_fifo_put_bytes(dst, src_ptr, src_end - src_ptr) ;

      if (src_lump == src->end_lump)
        break ;

      src_lump = ddl_next(src_lump, list) ;
      src_ptr  = src_lump->data ;
    } ;

  VIO_FIFO_DEBUG_VERIFY(dst) ;

  return dst ;
} ;

/*------------------------------------------------------------------------------
 * Copy tail of src FIFO (everything from end mark to put_ptr) to dst FIFO.
 *
 * Create a dst FIFO if there isn't one.  There must be a src FIFO.
 *
 * Appends to the dst FIFO.
 *
 * Does not change the src FIFO in any way.
 */
extern vio_fifo
vio_fifo_copy_tail(vio_fifo dst, vio_fifo src)
{
  vio_fifo_lump src_lump ;
  char*         src_ptr ;
  vio_fifo_lump tail ;

  if (dst == NULL)
    dst = vio_fifo_new(src->size) ;

  VIO_FIFO_DEBUG_VERIFY(src) ;
  VIO_FIFO_DEBUG_VERIFY(dst) ;

  if (!vio_fifo_have_end_mark(src))
    return dst ;

  src_lump = src->end_lump ;
  src_ptr  = src->end_ptr ;
  tail     = ddl_tail(src->base) ;

  while (1)
    {
      char* src_end ;

      if (src_lump != tail)
        src_end = src_lump->end ;
      else
        src_end = src->put_ptr ;

      vio_fifo_put_bytes(dst, src_ptr, src_end - src_ptr) ;

      if (src_lump == tail)
        break ;

      src_lump = ddl_next(src_lump, list) ;
      src_ptr  = src_lump->data ;
    } ;

  VIO_FIFO_DEBUG_VERIFY(dst) ;

  return dst ;
} ;

/*==============================================================================
 * End Mark Operations.
 *
 * Set/Clear end mark is pretty straightforward:
 *
 *   * if there was an end_ptr before and the put_ptr is ahead of it:
 *
 *     this adds one or more bytes between the get_ptr and the (new) end.
 *
 *   * if there was no end_ptr, or it is the same as the put_ptr:
 *
 *     setting/clearing the end_ptr makes no difference, because whenever
 *     the get_ptr reaches the put_ptr, the pointers are reset if they can
 *     be -- so need not worry about that here.
 *
 * Set the p_end and p_get_end to the new reality.
 *
 * The put_ptr stays in its current lump, so no need to worry about put_end.
 */

/*------------------------------------------------------------------------------
 * Set end_mark at the current put position.
 *
 * If there was an end_mark before, move it (forward) to the current put_ptr,
 * which keeps everything in between in the FIFO.
 *
 * If the put_ptr is at the end of the last lump, then the end_ptr will follow
 * it if another lump is added to the FIFO.
 *
 * Set the p_end and p_get_end to the new reality.
 */
extern void
vio_fifo_set_end_mark(vio_fifo vff)
{
  vff->p_end   = &vff->end_ptr ;

  vff->end_ptr = vff->put_ptr ;
  vff->end_lump = ddl_tail(vff->base) ;

  vio_fifo_set_get_end(vff) ;   /* in case end_lump or p_end changed    */

  VIO_FIFO_DEBUG_VERIFY(vff) ;
} ;

/*------------------------------------------------------------------------------
 * If there is an end mark, advance it to the put_ptr.
 *
 * If there was an end_mark before, move it (forward) to the current put_ptr,
 * which keeps everything in between in the FIFO.
 *
 * If there was no end mark before, do nothing.
 */
extern void
vio_fifo_step_end_mark(vio_fifo vff)
{
  if (vio_fifo_have_end_mark(vff))
    {
      vff->end_ptr  = vff->put_ptr ;
      vff->end_lump = ddl_tail(vff->base) ;

      vio_fifo_set_get_end(vff) ;   /* in case end_lump changed     */

      VIO_FIFO_DEBUG_VERIFY(vff) ;
    }
} ;

/*------------------------------------------------------------------------------
 * If there is an end_ptr, clear it -- everything between end_ptr and the
 * current put_ptr is kept in the FIFO.
 *
 * Set the p_end and p_get_end to the new reality.
 */
extern void
vio_fifo_clear_end_mark(vio_fifo vff)
{
  vff->p_end   = &vff->put_ptr ;

  vff->end_lump = ddl_tail(vff->base) ;

  vio_fifo_set_get_end(vff) ;   /* in case end_lump or p_end changed    */

  VIO_FIFO_DEBUG_VERIFY(vff) ;
} ;

/*------------------------------------------------------------------------------
 * Move put_ptr back to the end mark, if any, and discard data.
 *
 * If there is an end_mark, keep it if required.
 *
 * If there is no end mark, do nothing.
 *
 * If there is an end mark but it is the same as the put_ptr, then need do
 * nothing at all.
 *
 * Note that: if there is an end mark, then p_end == &end_ptr, so if
 * *p_end == put_ptr, then end_ptr == put_ptr ; if there is no end mark,
 * then p_end == &put_ptr, so *p_end == put_ptr !!
 */
extern void
vio_fifo_back_to_end_mark(vio_fifo vff, bool keep)
{
  if (*vff->p_end != vff->put_ptr)      /* test for not-empty end mark  */
    {
      if (vio_fifo_debug)
        assert(vio_fifo_have_end_mark(vff)) ;

      if (vff->end_lump != ddl_tail(vff->base))
        {
          vio_fifo_lump lump ;
          do
            vio_fifo_release_lump(vff, ddl_crop(&lump, vff->base, list)) ;
          while (vff->end_lump != ddl_tail(vff->base)) ;

          vff->put_end = vff->end_lump->end ;
        } ;

      if (*vff->p_start == vff->end_ptr)
        vio_fifo_reset_ptrs(vff) ;
      else
        vff->put_ptr = vff->end_ptr ;
    } ;

  if (!keep)
    {
      vff->p_end = &vff->put_ptr ;
      vio_fifo_set_get_end(vff) ;       /* in case get_lump == end_lump */
    } ;

  VIO_FIFO_DEBUG_VERIFY(vff) ;
} ;

/*==============================================================================
 * Get data from the FIFO.
 */

/*------------------------------------------------------------------------------
 * Get upto 'n' bytes -- steps past the bytes fetched.
 *
 * Returns: number of bytes got -- may be zero.
 */
extern ulen
vio_fifo_get_bytes(vio_fifo vff, void* dst, ulen n)
{
  void* dst_start ;

  VIO_FIFO_DEBUG_VERIFY(vff) ;

  dst_start = dst ;
  while (n > 0)
    {
      ulen  take ;

      take = *vff->p_get_end - vff->get_ptr ;

      if      (take > n)
        take = n ;
      else if (take == 0)
        break ;

      memcpy(dst, vff->get_ptr, take) ;
      dst = (char*)dst + take ;

      n -= take ;

      vio_fifo_step(vff, take) ;
    } ;

  VIO_FIFO_DEBUG_VERIFY(vff) ;

  return (char*)dst - (char*)dst_start ;
} ;

/*------------------------------------------------------------------------------
 * Write contents of FIFO -- assuming non-blocking file
 *
 * Will write all of FIFO up to end mark or put_ptr, or upto but excluding
 * the end_lump.
 *
 * Returns: > 0 => blocked
 *            0 => all gone (up to last lump if !all)
 *          < 0 => failed -- see errno
 *
 * Note: will work perfectly well for a non-blocking file -- which should
 *       never return EAGAIN/EWOULDBLOCK, so will return from here "all gone".
 */
extern int
vio_fifo_write_nb(vio_fifo vff, int fd, bool all)
{
  VIO_FIFO_DEBUG_VERIFY(vff) ;

  while (1)
    {
      ulen   have ;
      int    done ;

      if ((vff->get_lump == vff->end_lump) && !all)
        break ;                 /* don't write last lump        */

      have = vio_fifo_get(vff) ;

      if (have == 0)
        break ;

      done = write_nb(fd, vio_fifo_get_ptr(vff), have) ;

      if (done < 0)
        return -1 ;             /* failed                       */

      vio_fifo_step(vff, done) ;

      if (done < (int)have)
        return 1 ;              /* blocked                      */
    } ;

  VIO_FIFO_DEBUG_VERIFY(vff) ;

  return 0 ;                    /* all gone                     */
} ;

/*------------------------------------------------------------------------------
 * Write contents of FIFO -- assuming blocking file
 *
 * Will write all of FIFO up to end mark or put_ptr.
 *
 * Returns:   0 => all gone
 *          < 0 => failed -- see errno
 *
 * Note: will work perfectly well for a non-blocking file -- which should
 *       never return EAGAIN/EWOULDBLOCK, so will return from here "all gone".
 */
extern int
vio_fifo_fwrite(vio_fifo vff, FILE* file)
{
  VIO_FIFO_DEBUG_VERIFY(vff) ;

  while (1)
    {
      int   done ;
      ulen  have ;

      have = vio_fifo_get(vff) ;
      if (have == 0)
        break ;

      done = fwrite(vio_fifo_get_ptr(vff), have, 1, file) ;

      if (done < 1)
        return -1 ;             /* failed                       */

      vio_fifo_step(vff, have) ;
    } ;

  VIO_FIFO_DEBUG_VERIFY(vff) ;

  return 0 ;                    /* all gone                     */
} ;

/*------------------------------------------------------------------------------
 * Skip get_ptr to the current end -- which may be the current end mark.
 *
 * Does not clear any hold_ptr or end_ptr.
 */
extern void
vio_fifo_skip_to_end(vio_fifo vff)
{
  /* Advance directly to the current end and then synchronise get_ptr
   */
  vff->get_lump  = vff->end_lump ;
  vff->get_ptr   = *vff->p_end ;
  vff->p_get_end = vff->p_end ;

  vio_fifo_sync_get(vff) ;
} ;

/*==============================================================================
 * Hold Mark Operations.
 *
 * Set or clear hold_ptr.
 *
 * The get_ptr is unambiguous -- so the hold_ptr is, because it is only ever
 * set equal to the get_ptr !
 *
 * The put_ptr stays in its current lump, so no need to worry about put_end.
 */

/*------------------------------------------------------------------------------
 * Set hold mark -- clearing existing one, if any.
 *
 * Discard all contents up to the current get_ptr (easy if no hold mark), then
 * set hold mark at get_ptr.
 */
extern void
vio_fifo_set_hold_mark(vio_fifo vff)
{
  vio_fifo_release_upto(vff, vff->get_lump) ;

  if (vff->get_ptr == vff->put_ptr)
    vio_fifo_reset_ptrs(vff) ;
  else
    vff->hold_ptr = vff->get_ptr ;

  vff->p_start = &vff->hold_ptr ;

  VIO_FIFO_DEBUG_VERIFY(vff) ;
} ;

/*------------------------------------------------------------------------------
 * Clear hold mark -- if any.
 *
 * Discard all contents up to the current get_ptr (easy if no hold mark), then
 * clear hold mark (no effect if not set).
 *
 * Note that clearing a hold_ptr in an empty FIFO resets all the pointers.  To
 * avoid that could test for an empty hold mark (*p_start == get_ptr), but the
 * extra step in the majority case seems worse than the extra work in the
 * minority one.
 */
extern void
vio_fifo_clear_hold_mark(vio_fifo vff)
{
  vio_fifo_release_upto(vff, vff->get_lump) ;

  if (vff->get_ptr == vff->put_ptr)
    vio_fifo_reset_ptrs(vff) ;

  vff->p_start = &vff->get_ptr ;

  VIO_FIFO_DEBUG_VERIFY(vff) ;
} ;

/*------------------------------------------------------------------------------
 * If there is an hold_mark, reset get_ptr *back* to it, and leave the mark
 * set or clear.
 *
 * If there is no hold mark, set one at the current position, if required.
 *
 * Setting the get_ptr back to the hold_ptr sets it to an unambiguous
 * position.  If the get_ptr == hold_ptr then if the FIFO is empty, the
 * pointers will have been reset.
 */
extern void
vio_fifo_back_to_hold_mark(vio_fifo vff, on_off_b set)
{
  if (vio_fifo_have_hold_mark(vff))
    vio_fifo_set_get_ptr(vff, ddl_head(vff->base), vff->hold_ptr) ;

  vff->p_start = (set) ? &vff->hold_ptr : &vff->get_ptr ;


  VIO_FIFO_DEBUG_VERIFY(vff) ;
} ;

/*==============================================================================
 * For debug purposes -- verify the state of the given FIFO
 */
Private void
vio_fifo_verify(vio_fifo vff)
{
  vio_fifo_lump head ;
  vio_fifo_lump lump ;
  vio_fifo_lump tail ;
  bool  own_seen ;

  head = ddl_head(vff->base) ;
  tail = ddl_tail(vff->base) ;

  /* FIFO always has at least one lump.                                 */
  if (head == NULL)
    zabort("head is NULL") ;
  if (tail == NULL)
    zabort("tail is NULL") ;

  /* Make sure that the lump pointers all work
   *
   * When finished, know that head <= get_lump <= end_lump <= tail.
   */
  own_seen = false ;

  lump = head ;
  while (1)
    {
      if (lump == vff->own_lump)
        own_seen = true ;

      if (lump == vff->get_lump)
        break ;

      lump = ddl_next(lump, list) ;
      if (lump == NULL)
        zabort("ran out of lumps looking for get_lump") ;
    } ;

  while (lump != vff->end_lump)
    {
      lump = ddl_next(lump, list) ;

      if (lump == NULL)
        zabort("ran out of lumps looking for end_lump") ;

      if (lump == vff->own_lump)
        own_seen = true ;
    } ;

  while (lump != tail)
    {
      lump = ddl_next(lump, list) ;

      if (lump == NULL)
        zabort("ran out of lumps looking for tail") ;

      if (lump == vff->own_lump)
        own_seen = true ;
    } ;

  if (vff->spare == vff->own_lump)
    {
      if (own_seen)
        zabort("own seen in FIFO, but is also spare") ;
    }
  else
    {
      if (!own_seen)
        zabort("not found own lump in the FIFO") ;
    } ;

  /* Check that the p_start, p_get_end and p_end are valid
   */
  if ((vff->p_start != &vff->hold_ptr) && (vff->p_start != &vff->get_ptr))
    zabort("p_start is neither &get_ptr nor &hold_ptr") ;

  if ((vff->p_end   != &vff->end_ptr)  && (vff->p_end   != &vff->put_ptr))
    zabort("p_end is neither &put_ptr nor &end_ptr") ;

  if (vff->get_lump == vff->end_lump)
    {
      if (vff->p_get_end != vff->p_end)
        zabort("p_get_end not equal to p_end and is in end_lump") ;
    }
  else
    {
      if (vff->p_get_end != &vff->get_lump->end)
        zabort("p_get_end not equal to get_lump->end and is not in end_lump") ;
    } ;

  /* Check that all the pointers are within respective lumps
   *
   * Know that put_end is always tail->end, but get_end need not be.
   *
   * When finished, know that:
   *
   *   - get_lump == head if !hold_mark
   *   - end_lump == tail if !end_mark
   *   - that all pointers are within their respective lumps
   *   - all ptr are <= their respective ends
   *   - if hold_mark: hold_ptr <= get_ptr or head != get_lump
   *   - if end_mark:  end_ptr  <= put_ptr or tail != end_lump
   */
  if (vio_fifo_have_hold_mark(vff))
    {
      if ( (head->data > vff->hold_ptr)
          ||            (vff->hold_ptr > head->end) )
        zabort("hold_ptr outside the head lump") ;

      if ((vff->get_lump == head) && (vff->hold_ptr > vff->get_ptr))
        zabort("hold_ptr greater than get_ptr") ;
    }
  else
    {
      if (vff->get_lump != head)
        zabort("no hold_ptr, but get_lump is not head") ;
    } ;

  if ( (vff->get_lump->data > vff->get_ptr)
      ||                     (vff->get_ptr > *vff->p_get_end)
      ||                                    (*vff->p_get_end > vff->get_lump->end))
    zabort("get pointers outside the get lump") ;

  if (vio_fifo_have_end_mark(vff))
    {
      if ( (vff->end_lump->data > vff->end_ptr)
          ||                     (vff->end_ptr > vff->end_lump->end) )
        zabort("end pointer outside the end lump") ;

      if ((vff->end_lump == tail) && (vff->end_ptr > vff->put_ptr))
        zabort("end pointer greater than put pointer") ;
    }
  else
    {
      if (vff->end_lump != tail)
        zabort("no end_ptr, but end_lump is not tail") ;
    } ;

  if ( (tail->data > vff->put_ptr)
      ||            (vff->put_ptr > vff->put_end)
      ||                           (vff->put_end != tail->end) )
    zabort("put pointers outside the tail lump") ;

  /* Check that if get_ptr == p_get_end, that it is empty, or there is some
   * not-empty hold or end mark.
   *
   * The point is to trap any failure to reset pointers or advance the get_ptr
   * when it hits *p_get_end.
   */
  if (vff->get_ptr == *vff->p_get_end)
    {
      if (*vff->p_start != vff->put_ptr)
        {
          /* Not empty -- so must have a hold and/or end        */
          if (!(vio_fifo_have_hold_mark(vff) || vio_fifo_have_end_mark(vff)))
            zabort("get_ptr is at get_end, is not empty by no marks set") ;
        } ;
    } ;

  /* Check that if is empty, the pointers are reset.
   */
  if (*vff->p_start == vff->put_ptr)
    {
      if (    (tail           != head)
          ||  (vff->get_lump  != head)
          ||  (vff->end_lump  != head)
          ||  (vff->get_ptr   != head->data)
          ||  (vff->put_ptr   != head->data)
          ||  (vff->put_end   != head->end)
          || !( (vff->hold_ptr == NULL) || (vff->hold_ptr == head->data) )
          || !( (vff->end_ptr  == NULL) || (vff->end_ptr  == head->data) )
        )
          zabort("pointers not valid for empty fifo") ;
    } ;
} ;