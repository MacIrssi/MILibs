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
 */

#include <config.h>

#include <string.h>

#include "giomodule.h"
#include "giomodule-priv.h"
#include "glocalfilemonitor.h"
#include "glocaldirectorymonitor.h"
#include "gnativevolumemonitor.h"
#include "gvfs.h"

#include "gioalias.h"

/**
 * SECTION:giomodule
 * @short_description: Loadable GIO Modules
 * @include: gio.h
 *
 * Provides an interface and default functions for loading and unloading 
 * modules. This is used internally to make gio extensible, but can also
 * be used by other to implement module loading.
 * 
 **/

struct _GIOModule {
  GTypeModule parent_instance;
  
  gchar       *filename;
  GModule     *library;
  
  void (* load)   (GIOModule *module);
  void (* unload) (GIOModule *module);
};

struct _GIOModuleClass
{
  GTypeModuleClass parent_class;

};

static void      g_io_module_finalize      (GObject      *object);
static gboolean  g_io_module_load_module   (GTypeModule  *gmodule);
static void      g_io_module_unload_module (GTypeModule  *gmodule);

G_DEFINE_TYPE (GIOModule, g_io_module, G_TYPE_TYPE_MODULE);

static void
g_io_module_class_init (GIOModuleClass *class)
{
  GObjectClass     *object_class      = G_OBJECT_CLASS (class);
  GTypeModuleClass *type_module_class = G_TYPE_MODULE_CLASS (class);

  object_class->finalize     = g_io_module_finalize;

  type_module_class->load    = g_io_module_load_module;
  type_module_class->unload  = g_io_module_unload_module;
}

static void
g_io_module_init (GIOModule *module)
{
}

static void
g_io_module_finalize (GObject *object)
{
  GIOModule *module = G_IO_MODULE (object);

  g_free (module->filename);

  G_OBJECT_CLASS (g_io_module_parent_class)->finalize (object);
}

static gboolean
g_io_module_load_module (GTypeModule *gmodule)
{
  GIOModule *module = G_IO_MODULE (gmodule);

  if (!module->filename)
    {
      g_warning ("GIOModule path not set");
      return FALSE;
    }

  module->library = g_module_open (module->filename, G_MODULE_BIND_LAZY | G_MODULE_BIND_LOCAL);

  if (!module->library)
    {
      g_printerr ("%s\n", g_module_error ());
      return FALSE;
    }

  /* Make sure that the loaded library contains the required methods */
  if (! g_module_symbol (module->library,
                         "g_io_module_load",
                         (gpointer) &module->load) ||
      ! g_module_symbol (module->library,
                         "g_io_module_unload",
                         (gpointer) &module->unload))
    {
      g_printerr ("%s\n", g_module_error ());
      g_module_close (module->library);

      return FALSE;
    }

  /* Initialize the loaded module */
  module->load (module);

  return TRUE;
}

static void
g_io_module_unload_module (GTypeModule *gmodule)
{
  GIOModule *module = G_IO_MODULE (gmodule);

  module->unload (module);

  g_module_close (module->library);
  module->library = NULL;

  module->load   = NULL;
  module->unload = NULL;
}

/**
 * g_io_module_new:
 * @filename: filename of the shared library module.
 * 
 * Creates a new GIOModule that will load the specific
 * shared library when in use.
 * 
 * Returns: a #GIOModule from given @filename, 
 * or %NULL on error.
 **/
GIOModule *
g_io_module_new (const gchar *filename)
{
  GIOModule *module;

  g_return_val_if_fail (filename != NULL, NULL);

  module = g_object_new (G_IO_TYPE_MODULE, NULL);
  module->filename = g_strdup (filename);

  return module;
}

static gboolean
is_valid_module_name (const gchar *basename)
{
#if !defined(G_OS_WIN32) && !defined(G_WITH_CYGWIN)
  return
    g_str_has_prefix (basename, "lib") &&
    g_str_has_suffix (basename, ".so");
#else
  return g_str_has_suffix (basename, ".dll");
#endif
}

/**
 * g_io_modules_load_all_in_directory:
 * @dirname: pathname for a directory containing modules to load.
 * 
 * Loads all the modules in the the specified directory. 
 * 
 * Returns: a list of #GIOModules loaded from the directory,
 *      All the modules are loaded into memory, if you want to
 *      unload them (enabling on-demand loading) you must call
 *      g_type_module_unuse() on all the modules. Free the list
 *      with g_list_free().
 **/
GList *
g_io_modules_load_all_in_directory (const char *dirname)
{
  const gchar *name;
  GDir        *dir;
  GList *modules;

  if (!g_module_supported ())
    return NULL;

  dir = g_dir_open (dirname, 0, NULL);
  if (!dir)
    return NULL;

  modules = NULL;
  while ((name = g_dir_read_name (dir)))
    {
      if (is_valid_module_name (name))
        {
          GIOModule *module;
          gchar     *path;

          path = g_build_filename (dirname, name, NULL);
          module = g_io_module_new (path);

          if (!g_type_module_use (G_TYPE_MODULE (module)))
            {
              g_printerr ("Failed to load module: %s\n", path);
              g_object_unref (module);
              g_free (path);
              continue;
            }
	  
          g_free (path);

          modules = g_list_prepend (modules, module);
        }
    }
  
  g_dir_close (dir);

  return modules;
}

G_LOCK_DEFINE_STATIC (loaded_dirs);

extern GType _g_inotify_directory_monitor_get_type (void);
extern GType _g_inotify_file_monitor_get_type (void);
extern GType _g_unix_volume_monitor_get_type (void);
extern GType _g_local_vfs_get_type (void);

void
_g_io_modules_ensure_loaded (void)
{
  GList *modules, *l;
  static gboolean loaded_dirs = FALSE;
  GIOExtensionPoint *ep;

  G_LOCK (loaded_dirs);

  if (!loaded_dirs)
    {
      loaded_dirs = TRUE;

      ep = g_io_extension_point_register (G_LOCAL_DIRECTORY_MONITOR_EXTENSION_POINT_NAME);
      g_io_extension_point_set_required_type (ep, G_TYPE_LOCAL_DIRECTORY_MONITOR);
      
      ep = g_io_extension_point_register (G_LOCAL_FILE_MONITOR_EXTENSION_POINT_NAME);
      g_io_extension_point_set_required_type (ep, G_TYPE_LOCAL_FILE_MONITOR);

      ep = g_io_extension_point_register (G_VOLUME_MONITOR_EXTENSION_POINT_NAME);
      g_io_extension_point_set_required_type (ep, G_TYPE_VOLUME_MONITOR);
      
      ep = g_io_extension_point_register (G_NATIVE_VOLUME_MONITOR_EXTENSION_POINT_NAME);
      g_io_extension_point_set_required_type (ep, G_TYPE_NATIVE_VOLUME_MONITOR);
      
      ep = g_io_extension_point_register (G_VFS_EXTENSION_POINT_NAME);
      g_io_extension_point_set_required_type (ep, G_TYPE_VFS);
      
      modules = g_io_modules_load_all_in_directory (GIO_MODULE_DIR);

      /* Initialize types from built-in "modules" */
#if defined(HAVE_SYS_INOTIFY_H) || defined(HAVE_LINUX_INOTIFY_H)
      _g_inotify_directory_monitor_get_type ();
      _g_inotify_file_monitor_get_type ();
#endif
#ifdef G_OS_UNIX
      _g_unix_volume_monitor_get_type ();
#endif
      _g_local_vfs_get_type ();
    
      for (l = modules; l != NULL; l = l->next)
	g_type_module_unuse (G_TYPE_MODULE (l->data));
      
      g_list_free (modules);
    }

  G_UNLOCK (loaded_dirs);
}

struct _GIOExtension {
  char *name;
  GType type;
  gint priority;
};

struct _GIOExtensionPoint {
  GType required_type;
  char *name;
  GList *extensions;
};

static GHashTable *extension_points = NULL;
G_LOCK_DEFINE_STATIC(extension_points);


static void
g_io_extension_point_free (GIOExtensionPoint *ep)
{
  g_free (ep->name);
  g_free (ep);
}

GIOExtensionPoint *
g_io_extension_point_register (const char *name)
{
  GIOExtensionPoint *ep;
  
  G_LOCK (extension_points);
  if (extension_points == NULL)
    extension_points = g_hash_table_new_full (g_str_hash,
					      g_str_equal,
					      NULL,
					      (GDestroyNotify)g_io_extension_point_free);

  if (g_hash_table_lookup (extension_points, name) != NULL)
    {
      g_warning ("Extension point %s registered multiple times", name);
      G_UNLOCK (extension_points);
      return NULL;
    }

  ep = g_new0 (GIOExtensionPoint, 1);
  ep->name = g_strdup (name);
  
  g_hash_table_insert (extension_points, ep->name, ep);
  
  G_UNLOCK (extension_points);

  return ep;
}

GIOExtensionPoint *
g_io_extension_point_lookup (const char *name)
{
  GIOExtensionPoint *ep;

  G_LOCK (extension_points);
  ep = NULL;
  if (extension_points != NULL)
    ep = g_hash_table_lookup (extension_points, name);
  
  G_UNLOCK (extension_points);

  return ep;
  
}

void
g_io_extension_point_set_required_type (GIOExtensionPoint *extension_point,
					GType              type)
{
  extension_point->required_type = type;
}

GType
g_io_extension_point_get_required_type (GIOExtensionPoint *extension_point)
{
  return extension_point->required_type;
}

GList *
g_io_extension_point_get_extensions (GIOExtensionPoint *extension_point)
{
  return extension_point->extensions;
}

GIOExtension *
g_io_extension_point_get_extension_by_name (GIOExtensionPoint *extension_point,
					    const char        *name)
{
  GList *l;

  for (l = extension_point->extensions; l != NULL; l = l->next)
    {
      GIOExtension *e = l->data;

      if (e->name != NULL &&
	  strcmp (e->name, name) == 0)
	return e;
    }
  
  return NULL;
}

static gint
extension_prio_compare (gconstpointer  a,
			gconstpointer  b)
{
  const GIOExtension *extension_a = a, *extension_b = b;

  return extension_b->priority - extension_a->priority;
}

GIOExtension *
g_io_extension_point_implement (const char *extension_point_name,
				GType type,
				const char *extension_name,
				gint priority)
{
  GIOExtensionPoint *extension_point;
  GIOExtension *extension;
  GList *l;

  g_return_val_if_fail (extension_point_name != NULL, NULL);

  extension_point = g_io_extension_point_lookup (extension_point_name);
  if (extension_point == NULL)
    {
      g_warning ("Tried to implement non-registered extension point %s", extension_point_name);
      return NULL;
    }
  
  if (extension_point->required_type != 0 &&
      !g_type_is_a (type, extension_point->required_type))
    {
      g_warning ("Tried to register an extension of the type %s to extension point %s. "
		 "Expected type is %s.",
		 g_type_name (type),
		 extension_point_name, 
		 g_type_name (extension_point->required_type));
      return NULL;
    }      

  /* Its safe to register the same type multiple times */
  for (l = extension_point->extensions; l != NULL; l = l->next)
    {
      extension = l->data;
      if (extension->type == type)
	return extension;
    }
  
  extension = g_slice_new0 (GIOExtension);
  extension->type = type;
  extension->name = g_strdup (extension_name);
  extension->priority = priority;
  
  extension_point->extensions = g_list_insert_sorted (extension_point->extensions,
						      extension, extension_prio_compare);
  
  return extension;
}

GTypeClass *
g_io_extension_ref_class (GIOExtension *extension)
{
  return g_type_class_ref (extension->type);
}


GType
g_io_extension_get_type (GIOExtension *extension)
{
  return extension->type;
}

const char *
g_io_extension_get_name (GIOExtension *extension)
{
  return extension->name;
}

gint
g_io_extension_get_priority (GIOExtension *extension)
{
  return extension->priority;
}

#define __G_IO_MODULE_C__
#include "gioaliasdef.c"
