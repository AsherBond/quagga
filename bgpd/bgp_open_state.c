/* BGP Open State -- functions
 * Copyright (C) 2009 Chris Hall (GMCH), Highwayman
 *
 * This file is part of GNU Zebra.
 *
 * GNU Zebra is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
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

#include "zebra.h"

#include "bgpd/bgpd.h"
#include "bgpd/bgp_peer.h"
#include "bgpd/bgp_session.h"
#include "bgpd/bgp_open_state.h"

#include "lib/memory.h"

/*==============================================================================
 * BGP Open State.
 *
 * This structure encapsulates all the information that may be sent/received
 * in a BGP OPEN Message.
 *
 */

/*------------------------------------------------------------------------------
 * Initialise new bgp_open_state structure -- allocate if required.
 *
 * Returns:  a bgp_open_state object which has been zeroized and the
 *           vector of unknowns has been emptied.
 */
extern bgp_open_state
bgp_open_state_init_new(bgp_open_state state)
{
  if (state == NULL)
    state = XCALLOC(MTYPE_BGP_OPEN_STATE, sizeof(struct bgp_open_state)) ;
  else
    memset(state, 0, sizeof(struct bgp_open_state)) ;

  vector_init_new(state->unknowns, 0) ;
  vector_init_new(state->afi_safi, 0) ;

  return state ;
}

/*------------------------------------------------------------------------------
 * Free bgp_open_state structure (if any)
 *
 * Returns NULL.
 */
extern bgp_open_state
bgp_open_state_free(bgp_open_state state)
{
  bgp_cap_unknown   unknown ;
  bgp_cap_afi_safi  afi_safi ;

  if (state != NULL)
    {
      while ((unknown = vector_ream(state->unknowns, keep_it)) != NULL)
        XFREE(MTYPE_TMP, unknown) ;

      while ((afi_safi = vector_ream(state->afi_safi, keep_it)) != NULL)
        XFREE(MTYPE_TMP, afi_safi) ;

      XFREE(MTYPE_BGP_OPEN_STATE, state) ;
    } ;

  return NULL ;
}

/*------------------------------------------------------------------------------
 * Unset pointer to open_state structure and free structure (if any).
 */
extern void
bgp_open_state_unset(bgp_open_state* p_state)
{
  bgp_open_state_free(*p_state) ;
  *p_state = NULL ;
} ;

/*------------------------------------------------------------------------------
 * Set pointer to open_state and unset source pointer
 *
 * Frees any existing open_state at the destination.
 *
 * NB: responsibility for the open_state structure passes to the destination.
 */
extern void
bgp_open_state_set_mov(bgp_open_state* p_dst, bgp_open_state* p_src)
{
  bgp_open_state_free(*p_dst) ;
  *p_dst = *p_src ;
  *p_src = NULL ;
} ;

/*==============================================================================
 * Construct new bgp_open_state for the given peer -- allocate if required.
 *
 * Initialises the structure according to the current peer state.
 */

bgp_open_state
bgp_peer_open_state_init_new(bgp_open_state state, bgp_peer peer)
{
  safi_t      safi ;
  afi_t       afi ;

  state = bgp_open_state_init_new(state) ;  /* allocate if req.  Zeroise.   */

  /* Choose the appropriate ASN                         */
  if (peer->change_local_as)
    state->my_as = peer->change_local_as ;
  else
    state->my_as = peer->local_as ;

  /* Choose the appropriate hold time -- this follows the peer's configuration
   * or the default for the bgp instance.
   *
   * It is probably true already, but enforces a minimum of 3 seconds for the
   * hold time (if it is is not zero) -- per RFC4271.
   */
  state->holdtime = peer->v_holdtime ;

  if ((state->holdtime < 3) && (state->holdtime != 0))
    state->holdtime = 3 ;

  /* Choose the appropriate keepalive time -- this follows the peer's
   * configuration or the default for the bgp instance.
   *
   * It is probably true already, but enforces a maximum of holdtime / 3 for
   * the keepalive time -- noting that holdtime cannot be 1 or 2 !
   */
  state->keepalive = peer->v_keepalive ;

  if (state->keepalive > (state->holdtime / 3))
    state->keepalive = (state->holdtime / 3) ;

  /* Set our bgpd_id
   */
  state->bgp_id = peer->local_id.s_addr ;

  /* Whether to send capability or not
   */
  state->can_capability = ! CHECK_FLAG(peer->flags, PEER_FLAG_DONT_CAPABILITY) ;

  /* Announce self as AS4 speaker if required
   */
  if (!bm->as2_speaker)
    SET_FLAG(peer->cap, PEER_CAP_AS4_ADV) ;

  state->can_as4 = (peer->cap & PEER_CAP_AS4_ADV) ;

  state->my_as2 = (state->my_as > BGP_AS2_MAX ) ? BGP_ASN_TRANS
                                                : state->my_as ;

  /* Fill in the supported AFI/SAFI
   */
  for (afi = qAFI_min ; afi <= qAFI_max ; ++afi)
    for (safi = qSAFI_min ; safi <= qSAFI_max ; ++safi)
      if (peer->afc[afi][safi])
        state->can_mp_ext |= qafx_bit(qafx_num_from_qAFI_qSAFI(afi, safi)) ;

  /* Route refresh -- always
   */
  SET_FLAG(peer->cap, PEER_CAP_REFRESH_ADV) ;
  state->can_r_refresh = CHECK_FLAG(peer->cap, PEER_CAP_REFRESH_ADV)
                                        ? (bgp_form_pre | bgp_form_rfc)
                                        : bgp_form_none ;

  /* ORF capability.
   */
  for (afi = qAFI_min ; afi <= qAFI_max ; ++afi)
    for (safi = qSAFI_min ; safi <= qSAFI_max ; ++safi)
      {
        if (peer->af_flags[afi][safi] & PEER_FLAG_ORF_PREFIX_SM)
          state->can_orf_prefix_send |=
              qafx_bit(qafx_num_from_qAFI_qSAFI(afi, safi)) ;
        if (peer->af_flags[afi][safi] & PEER_FLAG_ORF_PREFIX_RM)
          state->can_orf_prefix_recv |=
              qafx_bit(qafx_num_from_qAFI_qSAFI(afi, safi)) ;
      } ;

  state->can_orf_prefix = (state->can_orf_prefix_send |
                           state->can_orf_prefix_recv)
                                        ? (bgp_form_pre | bgp_form_rfc)
                                        : bgp_form_none  ;

  /* Dynamic Capabilities       TODO: check requirement
   */
  state->can_dynamic = ( CHECK_FLAG(peer->flags, PEER_FLAG_DYNAMIC_CAPABILITY)
                                                                        != 0 ) ;
  if (state->can_dynamic)
    SET_FLAG(peer->cap, PEER_CAP_DYNAMIC_ADV) ;

  /* Graceful restart capability
   */
  if (bgp_flag_check(peer->bgp, BGP_FLAG_GRACEFUL_RESTART))
    {
      SET_FLAG(peer->cap, PEER_CAP_RESTART_ADV) ;
      state->can_g_restart = 1 ;
      state->restart_time  = peer->bgp->restart_time ;
    }
  else
    {
      state->can_g_restart = 0 ;
      state->restart_time  = 0 ;
    } ;

  /* TODO: check not has restarted and not preserving forwarding state (?)
   */
  state->can_preserve    = 0 ;        /* cannot preserve forwarding     */
  state->has_preserved   = 0 ;        /* has not preserved forwarding   */
  state->has_restarted   = 0 ;        /* has not restarted              */

  return state;
}

/*==============================================================================
 * Unknown capabilities handling.
 *
 */

/*------------------------------------------------------------------------------
 * Add given unknown capability and its value to the given open_state.
 */
extern void
bgp_open_state_unknown_add(bgp_open_state state, uint8_t code,
                                               void* value, bgp_size_t length)
{
  bgp_cap_unknown unknown ;

  unknown = XCALLOC(MTYPE_TMP, sizeof(struct bgp_cap_unknown) + length) ;

  unknown->code   = code ;
  unknown->length = length ;

  if (length != 0)
    memcpy(unknown->value, value, length) ;

  vector_push_item(state->unknowns, unknown) ;
} ;

/*------------------------------------------------------------------------------
 * Get count of number of unknown capabilities in given open_state.
 */
extern int
bgp_open_state_unknown_count(bgp_open_state state)
{
  return vector_end(state->unknowns) ;
} ;

/*------------------------------------------------------------------------------
 * Get n'th unknown capability -- if exists.
 */
extern bgp_cap_unknown
bgp_open_state_unknown_cap(bgp_open_state state, unsigned index)
{
  return vector_get_item(state->unknowns, index) ;
} ;

/*==============================================================================
 * Generic afi/safi capabilities handling.
 *
 */

/*------------------------------------------------------------------------------
 * Add given afi/safi capability and its value to the given open_state.
 */
extern bgp_cap_afi_safi
bgp_open_state_afi_safi_add(bgp_open_state state, iAFI_t afi, iSAFI_t safi,
                                                   bool known, uint8_t cap_code)
{
  bgp_cap_afi_safi afi_safi ;

  afi_safi = XCALLOC(MTYPE_TMP, sizeof(struct bgp_cap_afi_safi)) ;

  afi_safi->known_afi_safi   = known ;
  afi_safi->afi              = afi ;
  afi_safi->safi             = safi ;
  afi_safi->cap_code         = cap_code ;

  vector_push_item(state->afi_safi, afi_safi) ;

  return afi_safi ;
} ;

/*------------------------------------------------------------------------------
 * Get count of number of afi/safi capabilities in given open_state.
 */
extern int
bgp_open_state_afi_safi_count(bgp_open_state state)
{
  return vector_end(state->afi_safi) ;
} ;

/*------------------------------------------------------------------------------
 * Get n'th afi_safi capability -- if exists.
 */
extern bgp_cap_afi_safi
bgp_open_state_afi_safi_cap(bgp_open_state state, unsigned index)
{
  return vector_get_item(state->afi_safi, index) ;
} ;

/*==============================================================================
 *
 */

/* Received an open, update the peer's state
 *
 * Takes the peer->session->open_recv and fills in:
 *
 *   peer->v_holdtime       ) per negotiated values
 *   peer->v_keepalive      )
 *
 *   peer->remote_id.s_addr
 *
 *   peer->cap              ) updated per open_recv -- assumes all recv flags
 *   peer->af_cap           ) have been cleared.
 *
 *   peer->v_gr_restart       set to value received (if any)
 *
 *   peer->afc_recv           set/cleared according to what is advertised
 *                            BUT if !open_recv->can_capability or
 *                                     neighbor override-capability, then
 *                                                        all flags are cleared.
 *
 *   peer->afc_nego           set/cleared according to what is advertised and
 *                            what is activated.
 *                            BUT if !open_recv->can_capability or
 *                                     neighbor override-capability, then
 *                                      set everything which has been activated.
 *
 *
 *
 * NB: for safety, best to have the session locked -- though won't, in fact,
 *     change any of this information after the session is established.
 */
extern void
bgp_peer_open_state_receive(bgp_peer peer)
{
  bgp_session    session   = peer->session;
  bgp_open_state open_recv = session->open_recv;
  qAFI_t  afi;
  qSAFI_t safi;
  qafx_bit_t qbs ;
  int recv ;

  /* Check neighbor as number.
   */
  assert(open_recv->my_as == peer->as);

  /* If had to suppress sending of capabilities, note that
   */
  if (session->cap_suppress)
    SET_FLAG (peer->cap, PEER_CAP_SUPPRESSED) ;

  /* The BGP Engine sets the session's HoldTimer and KeepaliveTimer intervals
   * to the values negotiated when the OPEN messages were exchanged.
   *
   * Take copies of that information -- converting back to seconds.
   */
  peer->v_holdtime  = session->hold_timer_interval      / QTIME(1) ;
  peer->v_keepalive = session->keepalive_timer_interval / QTIME(1) ;

  /* Set remote router-id
   */
  peer->remote_id.s_addr = open_recv->bgp_id;

  /* AS4
   */
  if (open_recv->can_as4)
    SET_FLAG (peer->cap, PEER_CAP_AS4_RCV);

  /* AFI/SAFI -- as received, or assumed or overridden
   */
  if (!open_recv->can_capability || session->cap_override)
    {
      /* There were no capabilities, or are OVERRIDING AFI/SAFI, so force
       * not having received any AFI/SAFI, but apply all known.
       */
      recv = 0 ;
      qbs  = qafx_known_bits ;
    }
  else
    {
      /* Use the AFI/SAFI received, if any.                             */
      recv = 1 ;
      qbs  = open_recv->can_mp_ext ;
    }

  for (afi = qAFI_min ; afi <= qAFI_max ; ++afi)
    for (safi = qSAFI_min ; safi <= qSAFI_max ; ++safi)
      {
        qafx_bit_t qb = qafx_bit_from_qAFI_qSAFI(afi, safi) ;
        if (qb & qbs)
          {
            peer->afc_recv[afi][safi] = recv ;
            peer->afc_nego[afi][safi] = peer->afc[afi][safi] ;
          }
        else
          {
            peer->afc_recv[afi][safi] = 0 ;
            peer->afc_nego[afi][safi] = 0 ;
          } ;
      } ;

  /* Route refresh.
   */
  if (open_recv->can_r_refresh & bgp_form_pre)
    SET_FLAG (peer->cap, PEER_CAP_REFRESH_OLD_RCV);
  else if (open_recv->can_r_refresh & bgp_form_rfc)
    SET_FLAG (peer->cap, PEER_CAP_REFRESH_NEW_RCV);

  /* ORF
   */
  for (afi = qAFI_min ; afi <= qAFI_max ; ++afi)
     for (safi = qSAFI_min ; safi <= qSAFI_max ; ++safi)
       {
         qafx_bit_t qb = qafx_bit_from_qAFI_qSAFI(afi, safi);
         if (qb & open_recv->can_orf_prefix_send)
           SET_FLAG (peer->af_cap[afi][safi], PEER_CAP_ORF_PREFIX_SM_RCV);
         if (qb & open_recv->can_orf_prefix_recv)
           SET_FLAG (peer->af_cap[afi][safi], PEER_CAP_ORF_PREFIX_RM_RCV);
       }

  /* ORF prefix.
   */
  if (open_recv->can_orf_prefix_send)
    {
      if (open_recv->can_orf_prefix & bgp_form_pre)
        SET_FLAG (peer->cap, PEER_CAP_ORF_PREFIX_SM_OLD_RCV);
      else if (open_recv->can_orf_prefix & bgp_form_rfc)
        SET_FLAG (peer->cap, PEER_CAP_ORF_PREFIX_SM_RCV);
    }
  if (open_recv->can_orf_prefix_recv)
    {
      if (open_recv->can_orf_prefix & bgp_form_pre)
        SET_FLAG (peer->cap, PEER_CAP_ORF_PREFIX_RM_OLD_RCV);
      else if (open_recv->can_orf_prefix & bgp_form_rfc)
        SET_FLAG (peer->cap, PEER_CAP_ORF_PREFIX_RM_RCV);
    }

  /* Dynamic Capabilities
   */
  if (open_recv->can_dynamic)
    SET_FLAG (peer->cap, PEER_CAP_DYNAMIC_RCV);

  /* Graceful restart
   *
   * NB: appear not to care about open_recv->has_restarted !
   */
  if (open_recv->can_g_restart)
    SET_FLAG (peer->cap, PEER_CAP_RESTART_RCV) ;

  for (afi = qAFI_min ; afi <= qAFI_max ; ++afi)
     for (safi = qSAFI_min ; safi <= qSAFI_max ; ++safi)
       {
         qafx_bit_t qb = qafx_bit_from_qAFI_qSAFI(afi, safi);
         if (peer->afc[afi][safi] && (qb & open_recv->can_preserve))
           {
             SET_FLAG (peer->af_cap[afi][safi], PEER_CAP_RESTART_AF_RCV);
             if (qb & open_recv->has_preserved)
               SET_FLAG (peer->af_cap[afi][safi], PEER_CAP_RESTART_AF_PRESERVE_RCV);
           }
    }

  peer->v_gr_restart = open_recv->restart_time;
  /* TODO: should we do anything with this? */
#if 0
  int         restarting ;            /* Restart State flag                 */
#endif
}
