/* VTY top level
 * Copyright (C) 1997, 98 Kunihiro Ishiguro
 *
 * Revisions: Copyright (C) 2010 Chris Hall (GMCH), Highwayman
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
 * along with GNU Zebra; see the file COPYING.  If not, write to the Free
 * Software Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#ifndef _ZEBRA_VTY_COMMON_H
#define _ZEBRA_VTY_COMMON_H

#include "misc.h"
#include "qstring.h"
#include "command_common.h"

/*==============================================================================
 * These are things required by:
 *
 *   vty.h            -- which is used by all "external" code.
 *
 *   vty_local.h      -- which is used by all "internal" code on the I/O side.
 *
 *   command_local.h  -- which is used by all "internal" code on the command
 *                       processing side.
 *
 * This allows some things not to be published to "external" code.
 */

/*------------------------------------------------------------------------------
 * Structure used in the collection of integrated configuration.
 *
 * Is carried by the vty structure, so need to have a name for it.
 */
typedef struct config_collection* config_collection ;

/*==============================================================================
 * VTY Types and the VTY structure.
 *
 * The "struct vty" is used extensively across the Quagga daemons, where it
 * has two functions relating to command handling as:
 *
 *   1) a "handle" for output produced by commands
 *
 *   2) the holder of some context -- notably the current command "node" -- for
 *      command execution to use
 *
 * The bulk of "struct vty" is, therefore, private to vty.c et al and is
 * factored out into the "struct vty_io" -- opaque to users of the struct vty.
 *
 * There is also context used when parsing and executing commands which is
 * private to command.c et al, and is factored out into the "struct cmd_exec"
 * -- also opaque to users of the struct vty.
 */
enum vty_type           /* Command output                               */
{
  VTY_STDOUT,           /* stdout -- eg: when reading configuration     */

  VTY_TERMINAL,         /* a telnet terminal server                     */
  VTY_VTYSH_SERVER,     /* a vtysh server                               */

  VTY_VTYSH,            /* the vtysh itself                             */
} ;
typedef enum vty_type vty_type_t ;

/* Most of the contents of the vty structure live in two opaque structures,
 * which are forward referenced here.
 */
struct vty_io ;
typedef struct vty_io* vty_io ;

struct cmd_exec ;
typedef struct cmd_exec* cmd_exec ;

/* All command execution functions take a vty argument, and this is it.
 */
typedef struct vty* vty ;
typedef struct vty* svty ;
struct vty
{
  vty_type_t    type ;          /* see above    */

  /*----------------------------------------------------------------------
   * The following are the context in which commands are executed.
   *
   * While a command has the vty in its hands, it can access and change these
   * because they are not touched by the CLI thread until the command has
   * completed.
   */

  /* Node status of this vty.
   *
   * This is valid while a command is executing, and carries the initial state
   * before a command loop is entered.
   */
  node_type_t   node ;

  /* For current referencing point of interface, route-map, access-list
   * etc...
   *
   * NB: this value is private to the command execution, which is assumed
   *     to all be in the one thread... so no lock required.
   */
  void* index ;

  /* For multiple level index treatment such as key chain and key.
   *
   * NB: this value is private to the command execution, which is assumed
   *     to all be in the one thread... so no lock required.
   */
  void* index_sub ;

  /* When outputting configuration for vtysh to process, may wish to add
   * extra information.
   *
   * And for construction and output of the integrated configuration, need a
   * pointer to the collection of same.
   */
  bool  config_to_vtysh ;

  config_collection collection ;

  /*----------------------------------------------------------------------------
   * The current cmd_exec environment -- used in command_execute.c et al
   *
   * This is accessed freely by the command handling code because there is
   * only one thread of execution per vty -- though for some vty types (notably
   * VTY_TERMINAL) that may be in the vty_cli_thread or in the vty_cmd_thread
   * at different times.
   *
   * While a command is being executed, any CLI is waiting for the command to
   * complete, and the exec object may point at things which "belong" to the
   * vio and the CLI.
   */
  cmd_exec   exec ;             /* one per vty          */

  /*----------------------------------------------------------------------
   * The following is used inside vty.c etc only -- under VTY_LOCK.
   *
   * The lock is required because the vty_cli_thread may be doing I/O and
   * other stuff at the same time as the vty_cmd_thread is doing I/O, or at the
   * same time as other vty are being serviced.
   *
   * Could have one lock per vty -- but would then need a lock for the common
   * parts of the cli thread, so one lock keeps things relatively simple.
   */
  struct vty_io* vio ;                  /* one per vty          */
} ;

#endif /* _ZEBRA_VTY_COMMON_H */
