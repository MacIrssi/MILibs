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
#include "gmount.h"
#include "gvolume.h"
#include "gsimpleasyncresult.h"
#include "glibintl.h"

#include "gioalias.h"

/**
 * SECTION:gvolume
 * @short_description: Volume management
 * @include: gio.h
 * 
 * The #GVolume interface represents user-visible objects that can be
 * mounted. Note, when porting from GnomeVFS, #GVolume is the moral
 * equivalent of #GnomeVFSDrive.
 *
 * Mounting a #GVolume instance is an asynchronous operation. For more
 * information about asynchronous operations, see #GAsyncReady and
 * #GSimpleAsyncReady. To mount a #GVolume, first call
 * g_volume_mount() with (at least) the #GVolume instance, optionally
 * a #GMountOperation object and a #GAsyncReadyCallback. 
 *
 * Typically, one will only want to pass %NULL for the
 * #GMountOperation if automounting all volumes when a desktop session
 * starts since it's not desirable to put up a lot of dialogs asking
 * for credentials.
 *
 * The callback will be fired when the operation has resolved (either
 * with success or failure), and a #GAsyncReady structure will be
 * passed to the callback.  That callback should then call
 * g_volume_mount_finish() with the #GVolume instance and the
 * #GAsyncReady data to see if the operation was completed
 * successfully.  If an @error is present when g_volume_mount_finish()
 * is called, then it will be filled with any error information.
 **/

static void g_volume_base_init (gpointer g_class);
static void g_volume_class_init (gpointer g_class,
                                 gpointer class_data);

GType
g_volume_get_type (void)
{
  static GType volume_type = 0;

  if (! volume_type)
    {
      static const GTypeInfo volume_info =
      {
        sizeof (GVolumeIface), /* class_size */
	g_volume_base_init,   /* base_init */
	NULL,		/* base_finalize */
	g_volume_class_init,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
	0,
	0,              /* n_preallocs */
	NULL
      };

      volume_type =
	g_type_register_static (G_TYPE_INTERFACE, I_("GVolume"),
				&volume_info, 0);

      g_type_interface_add_prerequisite (volume_type, G_TYPE_OBJECT);
    }

  return volume_type;
}

static void
g_volume_class_init (gpointer g_class,
                     gpointer class_data)
{
}

static void
g_volume_base_init (gpointer g_class)
{
  static gboolean initialized = FALSE;

  if (! initialized)
    {
     /**
      * GVolume::changed:
      * 
      * Emitted when the volume has been changed.
      **/
      g_signal_new (I_("changed"),
                    G_TYPE_VOLUME,
                    G_SIGNAL_RUN_LAST,
                    G_STRUCT_OFFSET (GVolumeIface, changed),
                    NULL, NULL,
                    g_cclosure_marshal_VOID__VOID,
                    G_TYPE_NONE, 0);

     /**
      * GVolume::removed:
      * 
      * This signal is emitted when the #GVolume have been removed. If
      * the recipient is holding references to the object they should
      * release them so the object can be finalized.
      **/
      g_signal_new (I_("removed"),
                    G_TYPE_VOLUME,
                    G_SIGNAL_RUN_LAST,
                    G_STRUCT_OFFSET (GVolumeIface, removed),
                    NULL, NULL,
                    g_cclosure_marshal_VOID__VOID,
                    G_TYPE_NONE, 0);

      initialized = TRUE;
    }
}

/**
 * g_volume_get_name:
 * @volume: a #GVolume.
 * 
 * Gets the name of @volume.
 * 
 * Returns: the name for the given @volume. The returned string should 
 * be freed when no longer needed.
 **/
char *
g_volume_get_name (GVolume *volume)
{
  GVolumeIface *iface;

  g_return_val_if_fail (G_IS_VOLUME (volume), NULL);

  iface = G_VOLUME_GET_IFACE (volume);

  return (* iface->get_name) (volume);
}

/**
 * g_volume_get_icon:
 * @volume: a #GVolume.
 * 
 * Gets the icon for @volume.
 * 
 * Returns: a #GIcon.
 **/
GIcon *
g_volume_get_icon (GVolume *volume)
{
  GVolumeIface *iface;

  g_return_val_if_fail (G_IS_VOLUME (volume), NULL);

  iface = G_VOLUME_GET_IFACE (volume);

  return (* iface->get_icon) (volume);
}

/**
 * g_volume_get_uuid:
 * @volume: a #GVolume.
 * 
 * Gets the UUID for the @volume. The reference is typically based on
 * the file system UUID for the volume in question and should be
 * considered an opaque string. Returns %NULL if there is no UUID
 * available.
 * 
 * Returns: the UUID for @volume or %NULL if no UUID can be computed.
 **/
char *
g_volume_get_uuid (GVolume *volume)
{
  GVolumeIface *iface;

  g_return_val_if_fail (G_IS_VOLUME (volume), NULL);

  iface = G_VOLUME_GET_IFACE (volume);

  return (* iface->get_uuid) (volume);
}
  
/**
 * g_volume_get_drive:
 * @volume: a #GVolume.
 * 
 * Gets the drive for the @volume.
 * 
 * Returns: a #GDrive or %NULL if @volume is not associated with a drive.
 **/
GDrive *
g_volume_get_drive (GVolume *volume)
{
  GVolumeIface *iface;

  g_return_val_if_fail (G_IS_VOLUME (volume), NULL);

  iface = G_VOLUME_GET_IFACE (volume);

  return (* iface->get_drive) (volume);
}

/**
 * g_volume_get_mount:
 * @volume: a #GVolume.
 * 
 * Gets the mount for the @volume.
 * 
 * Returns: a #GMount or %NULL if @volume isn't mounted.
 **/
GMount *
g_volume_get_mount (GVolume *volume)
{
  GVolumeIface *iface;

  g_return_val_if_fail (G_IS_VOLUME (volume), NULL);

  iface = G_VOLUME_GET_IFACE (volume);

  return (* iface->get_mount) (volume);
}


/**
 * g_volume_can_mount:
 * @volume: a #GVolume.
 * 
 * Checks if a volume can be mounted.
 * 
 * Returns: %TRUE if the @volume can be mounted. %FALSE otherwise.
 **/
gboolean
g_volume_can_mount (GVolume *volume)
{
  GVolumeIface *iface;

  g_return_val_if_fail (G_IS_VOLUME (volume), FALSE);

  iface = G_VOLUME_GET_IFACE (volume);

  if (iface->can_mount == NULL)
    return FALSE;

  return (* iface->can_mount) (volume);
}

/**
 * g_volume_can_eject:
 * @volume: a #GVolume.
 * 
 * Checks if a volume can be ejected.
 * 
 * Returns: %TRUE if the @volume can be ejected. %FALSE otherwise.
 **/
gboolean
g_volume_can_eject (GVolume *volume)
{
  GVolumeIface *iface;

  g_return_val_if_fail (G_IS_VOLUME (volume), FALSE);

  iface = G_VOLUME_GET_IFACE (volume);

  if (iface->can_eject == NULL)
    return FALSE;

  return (* iface->can_eject) (volume);
}

/**
 * g_volume_mount:
 * @volume: a #GVolume.
 * @mount_operation: a #GMountOperation or %NULL to avoid user interaction.
 * @cancellable: optional #GCancellable object, %NULL to ignore.
 * @callback: a #GAsyncReadyCallback, or %NULL.
 * @user_data: a #gpointer.
 * 
 * Mounts a volume.
 **/
void
g_volume_mount (GVolume    *volume,
                GMountOperation     *mount_operation,
                GCancellable        *cancellable,
                GAsyncReadyCallback  callback,
                gpointer             user_data)
{
  GVolumeIface *iface;

  g_return_if_fail (G_IS_VOLUME (volume));

  iface = G_VOLUME_GET_IFACE (volume);

  if (iface->mount_fn == NULL)
    {
      g_simple_async_report_error_in_idle (G_OBJECT (volume), callback, user_data,
					   G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
					   _("volume doesn't implement mount"));
      
      return;
    }
  
  (* iface->mount_fn) (volume, mount_operation, cancellable, callback, user_data);
}

/**
 * g_volume_mount_finish:
 * @volume: pointer to a #GVolume.
 * @result: a #GAsyncResult.
 * @error: a #GError.
 * 
 * Finishes mounting a volume.
 * 
 * Returns: %TRUE, %FALSE if operation failed.
 **/
gboolean
g_volume_mount_finish (GVolume  *volume,
                       GAsyncResult      *result,
                       GError           **error)
{
  GVolumeIface *iface;

  g_return_val_if_fail (G_IS_VOLUME (volume), FALSE);
  g_return_val_if_fail (G_IS_ASYNC_RESULT (result), FALSE);

  if (G_IS_SIMPLE_ASYNC_RESULT (result))
    {
      GSimpleAsyncResult *simple = G_SIMPLE_ASYNC_RESULT (result);
      if (g_simple_async_result_propagate_error (simple, error))
	return FALSE;
    }
  
  iface = G_VOLUME_GET_IFACE (volume);
  return (* iface->mount_finish) (volume, result, error);
}

/**
 * g_volume_eject:
 * @volume: a #GVolume.
 * @flags: flags affecting the unmount if required for eject
 * @cancellable: optional #GCancellable object, %NULL to ignore.
 * @callback: a #GAsyncReadyCallback, or %NULL.
 * @user_data: a #gpointer.
 * 
 * Ejects a volume.
 **/
void
g_volume_eject (GVolume    *volume,
		GMountUnmountFlags   flags,
                GCancellable        *cancellable,
                GAsyncReadyCallback  callback,
                gpointer             user_data)
{
  GVolumeIface *iface;

  g_return_if_fail (G_IS_VOLUME (volume));

  iface = G_VOLUME_GET_IFACE (volume);

  if (iface->eject == NULL)
    {
      g_simple_async_report_error_in_idle (G_OBJECT (volume), callback, user_data,
					   G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
					   _("volume doesn't implement eject"));
      
      return;
    }
  
  (* iface->eject) (volume, flags, cancellable, callback, user_data);
}

/**
 * g_volume_eject_finish:
 * @volume: pointer to a #GVolume.
 * @result: a #GAsyncResult.
 * @error: a #GError.
 * 
 * Finishes ejecting a volume.
 * 
 * Returns: %TRUE, %FALSE if operation failed.
 **/
gboolean
g_volume_eject_finish (GVolume  *volume,
                       GAsyncResult      *result,
                       GError           **error)
{
  GVolumeIface *iface;

  g_return_val_if_fail (G_IS_VOLUME (volume), FALSE);
  g_return_val_if_fail (G_IS_ASYNC_RESULT (result), FALSE);

  if (G_IS_SIMPLE_ASYNC_RESULT (result))
    {
      GSimpleAsyncResult *simple = G_SIMPLE_ASYNC_RESULT (result);
      if (g_simple_async_result_propagate_error (simple, error))
	return FALSE;
    }
  
  iface = G_VOLUME_GET_IFACE (volume);
  return (* iface->eject_finish) (volume, result, error);
}

/**
 * g_volume_get_identifier:
 * @volume: a #GVolume
 * @kind: the kind of identifier to return
 *
 * Gets the identifier of the given kind for @volume.
 *
 * Returns: a newly allocated string containing the
 *   requested identfier, or %NULL if the #GVolume
 *   doesn't have this kind of identifier
 */
char *
g_volume_get_identifier (GVolume    *volume,
			 const char *kind)
{
  GVolumeIface *iface;

  g_return_val_if_fail (G_IS_VOLUME (volume), NULL);
  g_return_val_if_fail (kind != NULL, NULL);

  iface = G_VOLUME_GET_IFACE (volume);

  if (iface->get_identifier == NULL)
    return NULL;
  
  return (* iface->get_identifier) (volume, kind);
}

/**
 * g_volume_enumerate_identifiers:
 * @volume: a #GVolume
 *
 * Gets the kinds of identifiers that @volume has. 
 * Use g_volume_get_identifer() to obtain the identifiers
 * themselves.
 *
 * Returns: a %NULL-terminated array of strings containing
 *   kinds of identifiers. Use g_strfreev() to free.
 */
char **
g_volume_enumerate_identifiers (GVolume *volume)
{
  GVolumeIface *iface;

  g_return_val_if_fail (G_IS_VOLUME (volume), NULL);
  iface = G_VOLUME_GET_IFACE (volume);

  if (iface->enumerate_identifiers == NULL)
    return NULL;
  
  return (* iface->enumerate_identifiers) (volume);
}


#define __G_VOLUME_C__
#include "gioaliasdef.c"
