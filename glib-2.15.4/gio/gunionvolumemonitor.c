/* GIO - GLib Input, Output and Streaming Library
 * 
 * Copyright (C) 2006-2007 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: Alexander Larsson <alexl@redhat.com>
 *         David Zeuthen <davidz@redhat.com>
 */

#include <config.h>

#include <string.h>

#include <glib.h>
#include "gunionvolumemonitor.h"
#include "gmountprivate.h"
#include "giomodule-priv.h"
#ifdef G_OS_UNIX
#include "gunixvolumemonitor.h"
#endif
#include "gnativevolumemonitor.h"

#include "glibintl.h"

#include "gioalias.h"

struct _GUnionVolumeMonitor {
  GVolumeMonitor parent;

  GList *monitors;
};

static void g_union_volume_monitor_remove_monitor (GUnionVolumeMonitor *union_monitor,
						   GVolumeMonitor *child_monitor);


#define g_union_volume_monitor_get_type _g_union_volume_monitor_get_type
G_DEFINE_TYPE (GUnionVolumeMonitor, g_union_volume_monitor, G_TYPE_VOLUME_MONITOR);


G_LOCK_DEFINE_STATIC(the_volume_monitor);
static GUnionVolumeMonitor *the_volume_monitor = NULL;

static void
g_union_volume_monitor_finalize (GObject *object)
{
  GUnionVolumeMonitor *monitor;
  GVolumeMonitor *child_monitor;

  monitor = G_UNION_VOLUME_MONITOR (object);

  while (monitor->monitors != NULL) {
    child_monitor = monitor->monitors->data;
    g_union_volume_monitor_remove_monitor (monitor, 
                                           child_monitor);
    g_object_unref (child_monitor);
  }

  
  if (G_OBJECT_CLASS (g_union_volume_monitor_parent_class)->finalize)
    (*G_OBJECT_CLASS (g_union_volume_monitor_parent_class)->finalize) (object);
}

static void
g_union_volume_monitor_dispose (GObject *object)
{
  GUnionVolumeMonitor *monitor;
  
  monitor = G_UNION_VOLUME_MONITOR (object);

  G_LOCK (the_volume_monitor);
  the_volume_monitor = NULL;
  G_UNLOCK (the_volume_monitor);
  
  if (G_OBJECT_CLASS (g_union_volume_monitor_parent_class)->dispose)
    (*G_OBJECT_CLASS (g_union_volume_monitor_parent_class)->dispose) (object);
}

static GList *
get_mounts (GVolumeMonitor *volume_monitor)
{
  GUnionVolumeMonitor *monitor;
  GVolumeMonitor *child_monitor;
  GList *res;
  GList *l;
  
  monitor = G_UNION_VOLUME_MONITOR (volume_monitor);

  res = NULL;
  
  G_LOCK (the_volume_monitor);

  for (l = monitor->monitors; l != NULL; l = l->next)
    {
      child_monitor = l->data;

      res = g_list_concat (res, g_volume_monitor_get_mounts (child_monitor));
    }
  
  G_UNLOCK (the_volume_monitor);

  return res;
}

static GList *
get_volumes (GVolumeMonitor *volume_monitor)
{
  GUnionVolumeMonitor *monitor;
  GVolumeMonitor *child_monitor;
  GList *res;
  GList *l;
  
  monitor = G_UNION_VOLUME_MONITOR (volume_monitor);

  res = NULL;
  
  G_LOCK (the_volume_monitor);

  for (l = monitor->monitors; l != NULL; l = l->next)
    {
      child_monitor = l->data;

      res = g_list_concat (res, g_volume_monitor_get_volumes (child_monitor));
    }
  
  G_UNLOCK (the_volume_monitor);

  return res;
}

static GList *
get_connected_drives (GVolumeMonitor *volume_monitor)
{
  GUnionVolumeMonitor *monitor;
  GVolumeMonitor *child_monitor;
  GList *res;
  GList *l;
  
  monitor = G_UNION_VOLUME_MONITOR (volume_monitor);

  res = NULL;
  
  G_LOCK (the_volume_monitor);

  for (l = monitor->monitors; l != NULL; l = l->next)
    {
      child_monitor = l->data;

      res = g_list_concat (res, g_volume_monitor_get_connected_drives (child_monitor));
    }
  
  G_UNLOCK (the_volume_monitor);

  return res;
}

static GVolume *
get_volume_for_uuid (GVolumeMonitor *volume_monitor, const char *uuid)
{
  GUnionVolumeMonitor *monitor;
  GVolumeMonitor *child_monitor;
  GVolume *volume;
  GList *l;
  
  monitor = G_UNION_VOLUME_MONITOR (volume_monitor);

  volume = NULL;
  
  G_LOCK (the_volume_monitor);

  for (l = monitor->monitors; l != NULL; l = l->next)
    {
      child_monitor = l->data;

      volume = g_volume_monitor_get_volume_for_uuid (child_monitor, uuid);
      if (volume != NULL)
        break;

    }
  
  G_UNLOCK (the_volume_monitor);

  return volume;
}

static GMount *
get_mount_for_uuid (GVolumeMonitor *volume_monitor, const char *uuid)
{
  GUnionVolumeMonitor *monitor;
  GVolumeMonitor *child_monitor;
  GMount *mount;
  GList *l;
  
  monitor = G_UNION_VOLUME_MONITOR (volume_monitor);

  mount = NULL;
  
  G_LOCK (the_volume_monitor);

  for (l = monitor->monitors; l != NULL; l = l->next)
    {
      child_monitor = l->data;

      mount = g_volume_monitor_get_mount_for_uuid (child_monitor, uuid);
      if (mount != NULL)
        break;

    }
  
  G_UNLOCK (the_volume_monitor);

  return mount;
}

static void
g_union_volume_monitor_class_init (GUnionVolumeMonitorClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GVolumeMonitorClass *monitor_class = G_VOLUME_MONITOR_CLASS (klass);
  
  gobject_class->finalize = g_union_volume_monitor_finalize;
  gobject_class->dispose = g_union_volume_monitor_dispose;

  monitor_class->get_connected_drives = get_connected_drives;
  monitor_class->get_volumes = get_volumes;
  monitor_class->get_mounts = get_mounts;
  monitor_class->get_volume_for_uuid = get_volume_for_uuid;
  monitor_class->get_mount_for_uuid = get_mount_for_uuid;
}

static void
child_volume_added (GVolumeMonitor      *child_monitor,
                    GVolume    *child_volume,
                    GUnionVolumeMonitor *union_monitor)
{
  g_signal_emit_by_name (union_monitor,
			 "volume_added",
			 child_volume);
}

static void
child_volume_removed (GVolumeMonitor      *child_monitor,
                      GVolume    *child_volume,
                      GUnionVolumeMonitor *union_monitor)
{
  g_signal_emit_by_name (union_monitor,
			 "volume_removed",
			 child_volume);
}

static void
child_volume_changed (GVolumeMonitor      *child_monitor,
                      GVolume    *child_volume,
                      GUnionVolumeMonitor *union_monitor)
{
  g_signal_emit_by_name (union_monitor,
			 "volume_changed",
			 child_volume);
}

static void
child_mount_added (GVolumeMonitor      *child_monitor,
                   GMount              *child_mount,
                   GUnionVolumeMonitor *union_monitor)
{
  g_signal_emit_by_name (union_monitor,
                         "mount_added",
                         child_mount);
}

static void
child_mount_removed (GVolumeMonitor      *child_monitor,
                     GMount              *child_mount,
                     GUnionVolumeMonitor *union_monitor)
{
  g_signal_emit_by_name (union_monitor,
			 "mount_removed",
			 child_mount);
}

static void
child_mount_pre_unmount (GVolumeMonitor       *child_monitor,
                          GMount              *child_mount,
                          GUnionVolumeMonitor *union_monitor)
{
  g_signal_emit_by_name (union_monitor,
			 "mount_pre_unmount",
			 child_mount);
}


static void
child_mount_changed (GVolumeMonitor       *child_monitor,
                      GMount              *child_mount,
                      GUnionVolumeMonitor *union_monitor)
{
  g_signal_emit_by_name (union_monitor,
			 "mount_changed",
			 child_mount);
}

static void
child_drive_connected (GVolumeMonitor      *child_monitor,
                       GDrive              *child_drive,
                       GUnionVolumeMonitor *union_monitor)
{
  g_signal_emit_by_name (union_monitor,
			 "drive_connected",
			 child_drive);
}

static void
child_drive_disconnected (GVolumeMonitor      *child_monitor,
                          GDrive              *child_drive,
                          GUnionVolumeMonitor *union_monitor)
{
  g_signal_emit_by_name (union_monitor,
			 "drive_disconnected",
			 child_drive);
}

static void
child_drive_changed (GVolumeMonitor      *child_monitor,
                     GDrive             *child_drive,
                     GUnionVolumeMonitor *union_monitor)
{
  g_signal_emit_by_name (union_monitor,
                         "drive_changed",
                         child_drive);
}

static void
g_union_volume_monitor_add_monitor (GUnionVolumeMonitor *union_monitor,
                                    GVolumeMonitor      *volume_monitor)
{
  if (g_list_find (union_monitor->monitors, volume_monitor))
    return;

  union_monitor->monitors =
    g_list_prepend (union_monitor->monitors,
		    g_object_ref (volume_monitor));

  g_signal_connect (volume_monitor, "volume_added", (GCallback)child_volume_added, union_monitor);
  g_signal_connect (volume_monitor, "volume_removed", (GCallback)child_volume_removed, union_monitor);
  g_signal_connect (volume_monitor, "volume_changed", (GCallback)child_volume_changed, union_monitor);
  g_signal_connect (volume_monitor, "mount_added", (GCallback)child_mount_added, union_monitor);
  g_signal_connect (volume_monitor, "mount_removed", (GCallback)child_mount_removed, union_monitor);
  g_signal_connect (volume_monitor, "mount_pre_unmount", (GCallback)child_mount_pre_unmount, union_monitor);
  g_signal_connect (volume_monitor, "mount_changed", (GCallback)child_mount_changed, union_monitor);
  g_signal_connect (volume_monitor, "drive_connected", (GCallback)child_drive_connected, union_monitor);
  g_signal_connect (volume_monitor, "drive_disconnected", (GCallback)child_drive_disconnected, union_monitor);
  g_signal_connect (volume_monitor, "drive_changed", (GCallback)child_drive_changed, union_monitor);
}

static void
g_union_volume_monitor_remove_monitor (GUnionVolumeMonitor *union_monitor,
                                       GVolumeMonitor      *child_monitor)
{
  GList *l;

  l = g_list_find (union_monitor->monitors, child_monitor);
  if (l == NULL)
    return;

  union_monitor->monitors = g_list_delete_link (union_monitor->monitors, l);

  g_signal_handlers_disconnect_by_func (child_monitor, child_volume_added, union_monitor);
  g_signal_handlers_disconnect_by_func (child_monitor, child_volume_removed, union_monitor);
  g_signal_handlers_disconnect_by_func (child_monitor, child_volume_changed, union_monitor);
  g_signal_handlers_disconnect_by_func (child_monitor, child_mount_added, union_monitor);
  g_signal_handlers_disconnect_by_func (child_monitor, child_mount_removed, union_monitor);
  g_signal_handlers_disconnect_by_func (child_monitor, child_mount_pre_unmount, union_monitor);
  g_signal_handlers_disconnect_by_func (child_monitor, child_mount_changed, union_monitor);
  g_signal_handlers_disconnect_by_func (child_monitor, child_drive_connected, union_monitor);
  g_signal_handlers_disconnect_by_func (child_monitor, child_drive_disconnected, union_monitor);
  g_signal_handlers_disconnect_by_func (child_monitor, child_drive_changed, union_monitor);
}

static GType
get_default_native_class (gpointer data)
{
  GNativeVolumeMonitorClass *klass, *native_class, **native_class_out;
  const char *use_this;
  GIOExtensionPoint *ep;
  GIOExtension *extension;
  GList *l;

  native_class_out = data;
  
  use_this = g_getenv ("GIO_USE_VOLUME_MONITOR");
  
  /* Ensure vfs in modules loaded */
  _g_io_modules_ensure_loaded ();

  ep = g_io_extension_point_lookup (G_NATIVE_VOLUME_MONITOR_EXTENSION_POINT_NAME);

  native_class = NULL;
  if (use_this)
    {
      extension = g_io_extension_point_get_extension_by_name (ep, use_this);
      if (extension)
	{
	  klass = G_NATIVE_VOLUME_MONITOR_CLASS (g_io_extension_ref_class (extension));
	  if (G_VOLUME_MONITOR_CLASS (klass)->is_supported())
	    native_class = klass;
	  else
	    g_type_class_unref (klass);
	}
    }

  if (native_class == NULL)
    {
      for (l = g_io_extension_point_get_extensions (ep); l != NULL; l = l->next)
	{
	  extension = l->data;
	  klass = G_NATIVE_VOLUME_MONITOR_CLASS (g_io_extension_ref_class (extension));
	  if (G_VOLUME_MONITOR_CLASS (klass)->is_supported())
	    {
	      native_class = klass;
	      break;
	    }
	  else
	    g_type_class_unref (klass);
	}
    }
 
  if (native_class)
    {
      *native_class_out = native_class;
      return G_TYPE_FROM_CLASS (native_class);
    }
  else
    return G_TYPE_INVALID;
}

/* We return the class, with a ref taken.
 * This way we avoid unloading the class/module
 * between selecting the type and creating the
 * instance on the first call.
 */
static GNativeVolumeMonitorClass *
get_native_class ()
{
  static GOnce once_init = G_ONCE_INIT;
  GTypeClass *type_class;

  type_class = NULL;
  g_once (&once_init, (GThreadFunc)get_default_native_class, &type_class);

  if (type_class == NULL && once_init.retval != G_TYPE_INVALID)
    type_class = g_type_class_ref ((GType)once_init.retval);
  
  return (GNativeVolumeMonitorClass *)type_class;
}

static void
g_union_volume_monitor_init (GUnionVolumeMonitor *union_monitor)
{
  GVolumeMonitor *monitor;
  GNativeVolumeMonitorClass *native_class;
  GVolumeMonitorClass *klass;
  GIOExtensionPoint *ep;
  GIOExtension *extension;
  GList *l;

  native_class = get_native_class ();

  if (native_class != NULL)
    {
      monitor = g_object_new (G_TYPE_FROM_CLASS (native_class), NULL);
      g_union_volume_monitor_add_monitor (union_monitor, monitor);
      g_object_unref (monitor);
      g_type_class_unref (native_class);
    }

  ep = g_io_extension_point_lookup (G_VOLUME_MONITOR_EXTENSION_POINT_NAME);
  for (l = g_io_extension_point_get_extensions (ep); l != NULL; l = l->next)
    {
      extension = l->data;
      
      klass = G_VOLUME_MONITOR_CLASS (g_io_extension_ref_class (extension));
      if (klass->is_supported == NULL || klass->is_supported())
	{
	  monitor = g_object_new (g_io_extension_get_type (extension), NULL);
	  g_union_volume_monitor_add_monitor (union_monitor, monitor);
	  g_object_unref (monitor);
	}
      g_type_class_unref (klass);
    }
}

static GUnionVolumeMonitor *
g_union_volume_monitor_new (void)
{
  GUnionVolumeMonitor *monitor;

  monitor = g_object_new (G_TYPE_UNION_VOLUME_MONITOR, NULL);
  
  return monitor;
}

/**
 * g_volume_monitor_get:
 * 
 * Gets the volume monitor used by gio.
 *
 * Returns: a reference to the #GVolumeMonitor used by gio. Call
 *    g_object_unref() when done with it.
 **/
GVolumeMonitor *
g_volume_monitor_get (void)
{
  GVolumeMonitor *vm;
  
  G_LOCK (the_volume_monitor);

  if (the_volume_monitor)
    vm = G_VOLUME_MONITOR (g_object_ref (the_volume_monitor));
  else
    {
      the_volume_monitor = g_union_volume_monitor_new ();
      vm = G_VOLUME_MONITOR (the_volume_monitor);
    }
  
  G_UNLOCK (the_volume_monitor);

  return vm;
}

/**
 * _g_mount_get_for_mount_path:
 * @mountpoint: a string.
 * @cancellable: a #GCancellable, or %NULL
 * 
 * Returns: a #GMount for given @mount_path or %NULL.  
 **/
GMount *
_g_mount_get_for_mount_path (const char *mount_path,
			     GCancellable *cancellable)
{
  GNativeVolumeMonitorClass *klass;
  GMount *mount;
  
  klass = get_native_class ();
  if (klass == NULL)
    return NULL;

  mount = NULL;

  if (klass->get_mount_for_mount_path)
    {
      G_LOCK (the_volume_monitor);
      mount = klass->get_mount_for_mount_path (mount_path, cancellable);
      G_UNLOCK (the_volume_monitor);
    }

  /* TODO: How do we know this succeeded? Keep in mind that the native
   *       volume monitor may fail (e.g. not being able to connect to
   *       hald). Is the get_mount_for_mount_path() method allowed to
   *       return NULL? Seems like it is ... probably the method needs
   *       to take a boolean and write if it succeeds or not.. Messy.
   *       Very messy.
   */
  
  g_type_class_unref (klass);

  return mount;
}

/**
 * g_volume_monitor_adopt_orphan_mount:
 * @mount: a #GMount object to find a parent for
 *
 * This function should be called by any #GVolumeMonitor
 * implementation when a new #GMount object is created that is not
 * associated with a #GVolume object. It must be called just before
 * emitting the @mount_added signal.
 *
 * If the return value is not %NULL, the caller must associate the
 * returned #GVolume object with the #GMount. This involves returning
 * it in it's g_mount_get_volume() implementation. The caller must
 * also listen for the "removed" signal on the returned object
 * and give up it's reference when handling that signal
 * 
 * Similary, if implementing g_volume_monitor_adopt_orphan_mount(),
 * the implementor must take a reference to @mount and return it in
 * it's g_volume_get_mount() implemented. Also, the implementor must
 * listen for the "unmounted" signal on @mount and give up it's
 * reference upon handling that signal.
 *
 * There are two main use cases for this function.
 *
 * One is when implementing a user space file system driver that reads
 * blocks of a block device that is already represented by the native
 * volume monitor (for example a CD Audio file system driver). Such
 * a driver will generate it's own #GMount object that needs to be
 * assoicated with the #GVolume object that represents the volume.
 *
 * The other is for implementing a #GVolumeMonitor whose sole purpose
 * is to return #GVolume objects representing entries in the users
 * "favorite servers" list or similar.
 *
 * Returns: the #GVolume object that is the parent for @mount or %NULL
 * if no wants to adopt the #GMount.
 */
GVolume *
g_volume_monitor_adopt_orphan_mount (GMount *mount)
{
  GVolumeMonitor *child_monitor;
  GVolumeMonitorClass *child_monitor_class;
  GVolume *volume;
  GList *l;

  g_return_val_if_fail (mount != NULL, NULL);

  if (the_volume_monitor == NULL)
    return NULL;

  volume = NULL;
  
  /* TODO: nasty locking issues because current VM's don't emit signals in idle */

  /*G_LOCK (the_volume_monitor);*/

  for (l = the_volume_monitor->monitors; l != NULL; l = l->next)
    {
      child_monitor = l->data;
      child_monitor_class = G_VOLUME_MONITOR_GET_CLASS (child_monitor);

      if (child_monitor_class->adopt_orphan_mount != NULL)
        {
          volume = child_monitor_class->adopt_orphan_mount (mount);
          if (volume != NULL)
            break;
        }
    }
  
  /*G_UNLOCK (the_volume_monitor);*/

  return volume;
}


#define __G_UNION_VOLUME_MONITOR_C__
#include "gioaliasdef.c"
