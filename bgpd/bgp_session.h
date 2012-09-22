/* BGP Session -- header
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

#ifndef _QUAGGA_BGP_SESSION_H
#define _QUAGGA_BGP_SESSION_H

#include <zebra.h>
#include "lib/misc.h"

#include "bgpd/bgp_common.h"
#include "bgpd/bgp_engine.h"
#include "bgpd/bgp_connection.h"
#include "bgpd/bgp_notification.h"
#include "bgpd/bgp_route_refresh.h"
#include "bgpd/bgp_peer_index.h"

#include "lib/qtimers.h"
#include "lib/qpthreads.h"
#include "lib/sockunion.h"
#include "lib/mqueue.h"

/*==============================================================================
 * BGP Session data structure.
 *
 * The bgp_session structure encapsulates a BGP session from the perspective
 * of the Routeing Engine, and that is shared with the BGP Engine.
 *
 * The session may have up to two BGP connections associated with it, managed
 * by the BGP Engine.
 *
 * The session includes the "negotiating position" for the BGP Open exchange,
 * which is managed by the BGP Engine.  Changes to that negotiating position
 * may require any existing session to be terminated.
 *
 * NB: the session structure is shared by the Routeing Engine and the BGP
 *     Engine, so there is a mutex to coordinate access.
 *
 *     For simplicity, the BGP Engine may lock the session associated with the
 *     connection it is dealing with.
 *
 *     Parts of the session structure are private to the Routing Engine, and
 *     do not require the mutex for access.
 *
 * NB: the connections associated with a BGP session are private to the BGP
 *     Engine.
 *
 *     When sessions are disabled or have failed, there will be no connections.
 */

/* Statistics */
struct bgp_session_stats
{
  u_int32_t open_in;            /* Open message input count */
  u_int32_t open_out;           /* Open message output count */
  u_int32_t update_in;          /* Update message input count */
  u_int32_t update_out;         /* Update message ouput count */
  time_t    update_time;        /* Update message received time. */
  u_int32_t keepalive_in;       /* Keepalive input count */
  u_int32_t keepalive_out;      /* Keepalive output count */
  u_int32_t notify_in;          /* Notify input count */
  u_int32_t notify_out;         /* Notify output count */
  u_int32_t refresh_in;         /* Route Refresh input count */
  u_int32_t refresh_out;        /* Route Refresh output count */
  u_int32_t dynamic_cap_in;     /* Dynamic Capability input count.  */
  u_int32_t dynamic_cap_out;    /* Dynamic Capability output count.  */
};

struct bgp_session
{
  /* The following is set when the session is created, and not changed
   * thereafter, so do not need to lock the session to access this.
   */
  bgp_peer          peer ;              /* peer whose session this is     */

  /* This is a *recursive* mutex
   */
  qpt_mutex         mutex ;             /* for access to the rest         */

  /* While sIdle and sDisabled -- aka not "active" states:
   *
   *   the session belongs to the Routing Engine.
   *
   *   The BGP Engine will not touch a session in these states and the
   *   Routing Engine may do what it likes with it.
   *
   * While sEnabled, sEstablished and sLimping -- aka "active" states:
   *
   *   the session belongs to the BGP Engine.
   *
   *   A (very) few items in the session may be accessed by the Routing Engine,
   *   as noted below.  (Subject to the mutex.)
   *
   * Only the Routing Engine creates and destroys sessions.  The BGP Engine
   * assumes that a session will not be destroyed while it is sEnabled,
   * sEstablished or sLimping.
   *
   * These are private to the Routing Engine.
   */
  bgp_session_state_t   state ;

  int                   flow_control ;  /* limits number of updates sent
                                           by the Routing Engine          */

  bool                  delete_me ;     /* when next goes sDisabled       */

  /* These are private to the Routing Engine, and are set each time a session
   * event message is received from the BGP Engine.
   */
  bgp_session_event_t   event ;         /* last event                     */
  bgp_notify            notification ;  /* if any sent/received           */
  int                   err ;           /* errno, if any                  */
  bgp_connection_ord_t  ordinal ;       /* primary/secondary connection   */

  /* The Routeing Engine sets open_send and clears open_recv before enabling
   * the session, and may not change them while sEnabled/sEstablished.
   *
   * The as_expected is the AS configured for the far end -- which is what
   * expect to see in the incoming OPEN.
   *
   * The BGP Engine sets open_recv signalling the session eEstablished, and
   * will not touch it thereafter.
   */
  bgp_open_state    open_send ;         /* how to open the session        */
  bgp_open_state    open_recv ;         /* set when session Established   */

  /* The following are set by the Routeing Engine before a session is
   * enabled, and not changed at any other time by either engine.
   */
  bool              connect ;           /* initiate connections           */
  bool              listen ;            /* listen for connections         */

  bool              cap_suppress ;      /* always set false when session is
                                           enabled.  Set to state of connection
                                           when session is established    */

  bool              cap_override ;      /* assume other end can do all afi/safi
                                           this end has active            */
  bool              cap_strict ;        /* must recognise all capabilities
                                           received and have exact afi/safi
                                           match                          */

  int               ttl ;               /* TTL to set, if not zero        */
  bool              gtsm ;              /* ttl set by ttl-security        */
  unsigned short    port ;              /* destination port for peer      */

  /* TODO: ifindex and ifaddress should be rebound if the peer hears any
   * bgp_session_eTCP_failed or bgp_session_eTCP_error -- in case interface
   * state has changed, for the better.
   */
  char*             ifname ;            /* interface to bind to, if any   */
  unsigned          ifindex ;           /* and its index, if any          */
  union sockunion*  ifaddress ;         /* address to bind to, if any     */

  as_t              as_peer ;           /* ASN of the peer                */
  union sockunion*  su_peer ;           /* Sockunion address of the peer  */

  struct zlog*      log ;               /* where to log to                */
  char*             host ;              /* copy of printable peer's addr  */

  char*             password ;          /* copy of MD5 password           */

  qtime_t   idle_hold_timer_interval ;
  qtime_t   connect_retry_timer_interval ;
  qtime_t   open_hold_timer_interval ;

  /* These are set by the Routeing Engine before a session is enabled,
   * but are affected by the capabilities received in the OPEN message.
   *
   * When the session is established, the BGP Engine sets these.
   */
  qtime_t   hold_timer_interval ;       /* subject to negotiation         */
  qtime_t   keepalive_timer_interval ;  /* subject to negotiation         */

  bool              as4 ;               /* set by OPEN                    */
  bool              route_refresh_pre ; /* use pre-RFC version            */
  bool              orf_prefix_pre ;    /* use pre-RFC version            */

  /* These are cleared by the Routeing Engine before a session is enabled,
   * and set by the BGP Engine when the session is established.
   */
  union sockunion*  su_local ;          /* set when session Established   */
  union sockunion*  su_remote ;         /* set when session Established   */

  /* Statistics                                                           */
  struct bgp_session_stats stats;

  /* These values are are private to the BGP Engine.
   *
   * They must be cleared before the session is enabled, but may not be
   * touched by the Routeing Engine at any other time.
   *
   * Before stopping a session the BGP Engine unlinks any connections from
   * the session, and sets the stopped flag.
   *
   * The active flag is set when one or more connections are activated, and
   * cleared when either the BGP Engine stops the session or the Routing
   * Engine disables it.  When not "active" all messages other than disable
   * and enable are ignored.  This deals with the hiatus that exists between
   * the BGP Engine signalling that it has stopped (because of some exception)
   * and the Routing Engine acknowledging that (by disabling the session).
   *
   * The accept flag is set when the secondary connection is completely ready
   * to accept connections.  It is cleared otherwise, or when the active flag
   * is cleared.
   */
  bgp_connection    connections[bgp_connection_count] ;

  bool          active ;
  bool          accept ;
} ;

/*==============================================================================
 * Mqueue messages related to sessions
 *
 * In all these messages arg0 is the session.
 */

struct bgp_session_enable_args          /* to BGP Engine                */
{
                                        /* no further arguments         */
} ;
MQB_ARGS_SIZE_OK(struct bgp_session_enable_args) ;

struct bgp_session_disable_args         /* to BGP Engine                */
{
  bgp_notify    notification ;          /* NOTIFICATION to send         */
} ;
MQB_ARGS_SIZE_OK(struct bgp_session_enable_args) ;

struct bgp_session_update_args          /* to and from BGP Engine       */
{
  struct stream*  buf ;
  bgp_size_t size ;
  int xon_kick;                         /* send XON when processed this */

  bgp_connection  is_pending ;          /* used inside the BGP Engine   */
                                        /* set NULL on message creation */
} ;
MQB_ARGS_SIZE_OK(struct bgp_session_update_args) ;

struct bgp_session_route_refresh_args   /* to and from BGP Engine       */
{
  bgp_route_refresh  rr ;

  bgp_connection  is_pending ;          /* used inside the BGP Engine   */
                                        /* set NULL on message creation */
} ;
MQB_ARGS_SIZE_OK(struct bgp_session_route_refresh_args) ;

struct bgp_session_end_of_rib_args      /* to and from BGP Engine       */
{
  iAFI_t    afi ;
  iSAFI_t   safi ;

  bgp_connection  is_pending ;          /* used inside the BGP Engine   */
                                        /* set NULL on message creation */
} ;
MQB_ARGS_SIZE_OK(struct bgp_session_end_of_rib_args) ;

struct bgp_session_event_args           /* to Routeing Engine           */
{
  bgp_session_event_t  event ;          /* what just happened           */
  bgp_notify           notification ;   /* sent or received (if any)    */
  int                  err ;            /* errno if any                 */
  bgp_connection_ord_t ordinal ;        /* primary/secondary connection */
  int                  stopped ;        /* session has stopped          */
} ;
MQB_ARGS_SIZE_OK(struct bgp_session_event_args) ;

struct bgp_session_XON_args             /* to Routeing Engine           */
{
                                        /* no further arguments         */
} ;
MQB_ARGS_SIZE_OK(struct bgp_session_XON_args) ;

enum { BGP_XON_REFRESH     = 40,
       BGP_XON_KICK        = 20,
} ;

struct bgp_session_ttl_args             /* to bgp Engine                */
{
  int                  ttl ;
  bool                 gtsm ;
} ;
MQB_ARGS_SIZE_OK(struct bgp_session_ttl_args) ;

/*==============================================================================
 * Session mutex lock/unlock
 */

inline static void BGP_SESSION_LOCK(bgp_session session)
{
  qpt_mutex_lock(session->mutex) ;
} ;

inline static void BGP_SESSION_UNLOCK(bgp_session session)
{
  qpt_mutex_unlock(session->mutex) ;
} ;

/*==============================================================================
 * Functions
 */
extern bgp_session bgp_session_init_new(bgp_peer peer) ;
extern void bgp_session_enable(bgp_peer peer) ;
extern bool bgp_session_disable(bgp_peer peer, bgp_notify notification) ;
extern void bgp_session_delete(bgp_peer peer);
extern void bgp_session_event(bgp_session session, bgp_session_event_t  event,
                                       bgp_notify           notification,
                                       int                  err,
                                       bgp_connection_ord_t ordinal,
                                       bool                 stopped) ;
extern void bgp_session_update_send(bgp_session session,
                                                     struct stream_fifo* fifo) ;
extern void bgp_session_route_refresh_send(bgp_session session,
                                                         bgp_route_refresh rr) ;
extern void bgp_session_end_of_rib_send(bgp_session session,
                                                          qAFI_t afi, qSAFI_t) ;
extern void bgp_session_update_recv(bgp_session session, struct stream* buf,
                                                              bgp_size_t size) ;
extern void bgp_session_route_refresh_recv(bgp_session session,
                                                         bgp_route_refresh rr) ;
extern bool bgp_session_is_XON(bgp_peer peer);
extern bool bgp_session_dec_flow_count(bgp_peer peer) ;
extern void bgp_session_set_ttl(bgp_session session, int ttl, bool gtsm) ;
extern void bgp_session_get_stats(bgp_session session,
                                              struct bgp_session_stats *stats) ;

/*==============================================================================
 * Session data access functions.
 */
extern bool bgp_session_is_active(bgp_session session) ;

#endif /* QUAGGA_BGP_SESSION_H */
