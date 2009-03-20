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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <errno.h>
#include <string.h>

#include <glib.h>
#include <glib/gstdio.h>
#include "glibintl.h"
#include "gioerror.h"
#include "glocalfileoutputstream.h"
#include "glocalfileinfo.h"

#ifdef G_OS_WIN32
#include <io.h>
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif
#ifndef S_ISREG
#define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
#endif
#endif

#include "gioalias.h"

#define g_local_file_output_stream_get_type _g_local_file_output_stream_get_type
G_DEFINE_TYPE (GLocalFileOutputStream, g_local_file_output_stream, G_TYPE_FILE_OUTPUT_STREAM);

/* Some of the file replacement code was based on the code from gedit,
 * relicenced to LGPL with permissions from the authors.
 */

#define BACKUP_EXTENSION "~"

struct _GLocalFileOutputStreamPrivate {
  char *tmp_filename;
  char *original_filename;
  char *backup_filename;
  char *etag;
  int fd;
};

static gssize     g_local_file_output_stream_write        (GOutputStream      *stream,
							   const void         *buffer,
							   gsize               count,
							   GCancellable       *cancellable,
							   GError            **error);
static gboolean   g_local_file_output_stream_close        (GOutputStream      *stream,
							   GCancellable       *cancellable,
							   GError            **error);
static GFileInfo *g_local_file_output_stream_query_info   (GFileOutputStream  *stream,
							   char               *attributes,
							   GCancellable       *cancellable,
							   GError            **error);
static char *     g_local_file_output_stream_get_etag     (GFileOutputStream  *stream);
static goffset    g_local_file_output_stream_tell         (GFileOutputStream  *stream);
static gboolean   g_local_file_output_stream_can_seek     (GFileOutputStream  *stream);
static gboolean   g_local_file_output_stream_seek         (GFileOutputStream  *stream,
							   goffset             offset,
							   GSeekType           type,
							   GCancellable       *cancellable,
							   GError            **error);
static gboolean   g_local_file_output_stream_can_truncate (GFileOutputStream  *stream);
static gboolean   g_local_file_output_stream_truncate     (GFileOutputStream  *stream,
							   goffset             size,
							   GCancellable       *cancellable,
							   GError            **error);

static void
g_local_file_output_stream_finalize (GObject *object)
{
  GLocalFileOutputStream *file;
  
  file = G_LOCAL_FILE_OUTPUT_STREAM (object);
  
  g_free (file->priv->tmp_filename);
  g_free (file->priv->original_filename);
  g_free (file->priv->backup_filename);
  g_free (file->priv->etag);
  
  if (G_OBJECT_CLASS (g_local_file_output_stream_parent_class)->finalize)
    (*G_OBJECT_CLASS (g_local_file_output_stream_parent_class)->finalize) (object);
}

static void
g_local_file_output_stream_class_init (GLocalFileOutputStreamClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GOutputStreamClass *stream_class = G_OUTPUT_STREAM_CLASS (klass);
  GFileOutputStreamClass *file_stream_class = G_FILE_OUTPUT_STREAM_CLASS (klass);
  
  g_type_class_add_private (klass, sizeof (GLocalFileOutputStreamPrivate));
  
  gobject_class->finalize = g_local_file_output_stream_finalize;

  stream_class->write_fn = g_local_file_output_stream_write;
  stream_class->close_fn = g_local_file_output_stream_close;
  file_stream_class->query_info = g_local_file_output_stream_query_info;
  file_stream_class->get_etag = g_local_file_output_stream_get_etag;
  file_stream_class->tell = g_local_file_output_stream_tell;
  file_stream_class->can_seek = g_local_file_output_stream_can_seek;
  file_stream_class->seek = g_local_file_output_stream_seek;
  file_stream_class->can_truncate = g_local_file_output_stream_can_truncate;
  file_stream_class->truncate_fn = g_local_file_output_stream_truncate;
}

static void
g_local_file_output_stream_init (GLocalFileOutputStream *stream)
{
  stream->priv = G_TYPE_INSTANCE_GET_PRIVATE (stream,
					      G_TYPE_LOCAL_FILE_OUTPUT_STREAM,
					      GLocalFileOutputStreamPrivate);
}

static gssize
g_local_file_output_stream_write (GOutputStream  *stream,
				  const void     *buffer,
				  gsize           count,
				  GCancellable   *cancellable,
				  GError        **error)
{
  GLocalFileOutputStream *file;
  gssize res;

  file = G_LOCAL_FILE_OUTPUT_STREAM (stream);

  while (1)
    {
      if (g_cancellable_set_error_if_cancelled (cancellable, error))
	return -1;
      res = write (file->priv->fd, buffer, count);
      if (res == -1)
	{
	  if (errno == EINTR)
	    continue;
	  
	  g_set_error (error, G_IO_ERROR,
		       g_io_error_from_errno (errno),
		       _("Error writing to file: %s"),
		       g_strerror (errno));
	}
      
      break;
    }
  
  return res;
}

static gboolean
g_local_file_output_stream_close (GOutputStream  *stream,
				  GCancellable   *cancellable,
				  GError        **error)
{
  GLocalFileOutputStream *file;
  struct stat final_stat;
  int res;

  file = G_LOCAL_FILE_OUTPUT_STREAM (stream);

  if (file->priv->tmp_filename)
    {
      /* We need to move the temp file to its final place,
       * and possibly create the backup file
       */

      if (file->priv->backup_filename)
	{
	  if (g_cancellable_set_error_if_cancelled (cancellable, error))
	    goto err_out;
	  
#ifdef HAVE_LINK
	  /* create original -> backup link, the original is then renamed over */
	  if (unlink (file->priv->backup_filename) != 0 &&
	      errno != ENOENT)
	    {
	      g_set_error (error, G_IO_ERROR,
			   G_IO_ERROR_CANT_CREATE_BACKUP,
			   _("Error removing old backup link: %s"),
			   g_strerror (errno));
	      goto err_out;
	    }

	  if (link (file->priv->original_filename, file->priv->backup_filename) != 0)
	    {
	      g_set_error (error, G_IO_ERROR,
			   G_IO_ERROR_CANT_CREATE_BACKUP,
			   _("Error creating backup link: %s"),
			   g_strerror (errno));
	      goto err_out;
	    }
#else
	    /* If link not supported, just rename... */
	  if (!rename (file->priv->original_filename, file->priv->backup_filename) != 0)
	    {
	      g_set_error (error, G_IO_ERROR,
			   G_IO_ERROR_CANT_CREATE_BACKUP,
			   _("Error creating backup copy: %s"),
			   g_strerror (errno));
	      goto err_out;
	    }
#endif
	}
      

      if (g_cancellable_set_error_if_cancelled (cancellable, error))
	goto err_out;

      /* tmp -> original */
      if (rename (file->priv->tmp_filename, file->priv->original_filename) != 0)
	{
	  g_set_error (error, G_IO_ERROR,
		       g_io_error_from_errno (errno),
		       _("Error renaming temporary file: %s"),
		       g_strerror (errno));
	  goto err_out;
	}
    }
  
  if (g_cancellable_set_error_if_cancelled (cancellable, error))
    goto err_out;
      
  if (fstat (file->priv->fd, &final_stat) == 0)
    file->priv->etag = _g_local_file_info_create_etag (&final_stat);

  while (1)
    {
      res = close (file->priv->fd);
      if (res == -1)
	{
	  g_set_error (error, G_IO_ERROR,
		       g_io_error_from_errno (errno),
		       _("Error closing file: %s"),
		       g_strerror (errno));
	}
      break;
    }
  
  return res != -1;

 err_out:
  /* A simple try to close the fd in case we fail before the actual close */
  close (file->priv->fd);
  return FALSE;
}

static char *
g_local_file_output_stream_get_etag (GFileOutputStream *stream)
{
  GLocalFileOutputStream *file;

  file = G_LOCAL_FILE_OUTPUT_STREAM (stream);
  
  return g_strdup (file->priv->etag);
}

static goffset
g_local_file_output_stream_tell (GFileOutputStream *stream)
{
  GLocalFileOutputStream *file;
  off_t pos;

  file = G_LOCAL_FILE_OUTPUT_STREAM (stream);
  
  pos = lseek (file->priv->fd, 0, SEEK_CUR);

  if (pos == (off_t)-1)
    return 0;
  
  return pos;
}

static gboolean
g_local_file_output_stream_can_seek (GFileOutputStream *stream)
{
  GLocalFileOutputStream *file;
  off_t pos;

  file = G_LOCAL_FILE_OUTPUT_STREAM (stream);
  
  pos = lseek (file->priv->fd, 0, SEEK_CUR);

  if (pos == (off_t)-1 && errno == ESPIPE)
    return FALSE;
  
  return TRUE;
}

static int
seek_type_to_lseek (GSeekType type)
{
  switch (type)
    {
    default:
    case G_SEEK_CUR:
      return SEEK_CUR;
      
    case G_SEEK_SET:
      return SEEK_SET;
      
    case G_SEEK_END:
      return SEEK_END;
    }
}

static gboolean
g_local_file_output_stream_seek (GFileOutputStream  *stream,
				 goffset             offset,
				 GSeekType           type,
				 GCancellable       *cancellable,
				 GError            **error)
{
  GLocalFileOutputStream *file;
  off_t pos;

  file = G_LOCAL_FILE_OUTPUT_STREAM (stream);

  pos = lseek (file->priv->fd, offset, seek_type_to_lseek (type));

  if (pos == (off_t)-1)
    {
      g_set_error (error, G_IO_ERROR,
		   g_io_error_from_errno (errno),
		   _("Error seeking in file: %s"),
		   g_strerror (errno));
      return FALSE;
    }
  
  return TRUE;
}

static gboolean
g_local_file_output_stream_can_truncate (GFileOutputStream *stream)
{
  /* We can't truncate pipes and stuff where we can't seek */
  return g_local_file_output_stream_can_seek (stream);
}

static gboolean
g_local_file_output_stream_truncate (GFileOutputStream  *stream,
				     goffset             size,
				     GCancellable       *cancellable,
				     GError            **error)
{
  GLocalFileOutputStream *file;
  int res;

  file = G_LOCAL_FILE_OUTPUT_STREAM (stream);

 restart:
#ifdef G_OS_WIN32
  res = g_win32_ftruncate (file->priv->fd, size);
#else
  res = ftruncate (file->priv->fd, size);
#endif
  
  if (res == -1)
    {
      if (errno == EINTR)
	{
	  if (g_cancellable_set_error_if_cancelled (cancellable, error))
	    return FALSE;
	  goto restart;
	}

      g_set_error (error, G_IO_ERROR,
		   g_io_error_from_errno (errno),
		   _("Error truncating file: %s"),
		   g_strerror (errno));
      return FALSE;
    }
  
  return TRUE;
}


static GFileInfo *
g_local_file_output_stream_query_info (GFileOutputStream  *stream,
				       char               *attributes,
				       GCancellable       *cancellable,
				       GError            **error)
{
  GLocalFileOutputStream *file;

  file = G_LOCAL_FILE_OUTPUT_STREAM (stream);

  if (g_cancellable_set_error_if_cancelled (cancellable, error))
    return NULL;
  
  return _g_local_file_info_get_from_fd (file->priv->fd,
					 attributes,
					 error);
}

GFileOutputStream *
_g_local_file_output_stream_create  (const char        *filename,
				     GFileCreateFlags   flags,
				     GCancellable      *cancellable,
				     GError           **error)
{
  GLocalFileOutputStream *stream;
  int mode;
  int fd;

  if (g_cancellable_set_error_if_cancelled (cancellable, error))
    return NULL;

  if (flags & G_FILE_CREATE_PRIVATE)
    mode = 0600;
  else
    mode = 0666;
  
  fd = g_open (filename, O_CREAT | O_EXCL | O_WRONLY, mode);
  if (fd == -1)
    {
      int errsv = errno;

      if (errsv == EINVAL)
	/* This must be an invalid filename, on e.g. FAT */
	g_set_error (error, G_IO_ERROR,
		     G_IO_ERROR_INVALID_FILENAME,
		     _("Invalid filename"));
      else
	g_set_error (error, G_IO_ERROR,
		     g_io_error_from_errno (errsv),
		     _("Error opening file '%s': %s"),
		     filename, g_strerror (errsv));
      return NULL;
    }
  
  stream = g_object_new (G_TYPE_LOCAL_FILE_OUTPUT_STREAM, NULL);
  stream->priv->fd = fd;
  return G_FILE_OUTPUT_STREAM (stream);
}

GFileOutputStream *
_g_local_file_output_stream_append  (const char        *filename,
				     GFileCreateFlags   flags,
				     GCancellable      *cancellable,
				     GError           **error)
{
  GLocalFileOutputStream *stream;
  int mode;
  int fd;

  if (g_cancellable_set_error_if_cancelled (cancellable, error))
    return NULL;

  if (flags & G_FILE_CREATE_PRIVATE)
    mode = 0600;
  else
    mode = 0666;

  fd = g_open (filename, O_CREAT | O_APPEND | O_WRONLY, mode);
  if (fd == -1)
    {
      int errsv = errno;

      if (errsv == EINVAL)
	/* This must be an invalid filename, on e.g. FAT */
	g_set_error (error, G_IO_ERROR,
		     G_IO_ERROR_INVALID_FILENAME,
		     _("Invalid filename"));
      else
	g_set_error (error, G_IO_ERROR,
		     g_io_error_from_errno (errsv),
		     _("Error opening file '%s': %s"),
		     filename, g_strerror (errsv));
      return NULL;
    }
  
  stream = g_object_new (G_TYPE_LOCAL_FILE_OUTPUT_STREAM, NULL);
  stream->priv->fd = fd;
  
  return G_FILE_OUTPUT_STREAM (stream);
}

static char *
create_backup_filename (const char *filename)
{
  return g_strconcat (filename, BACKUP_EXTENSION, NULL);
}

#define BUFSIZE	8192 /* size of normal write buffer */

static gboolean
copy_file_data (gint     sfd,
		gint     dfd,
		GError **error)
{
  gboolean ret = TRUE;
  gpointer buffer;
  const gchar *write_buffer;
  gssize bytes_read;
  gssize bytes_to_write;
  gssize bytes_written;
  
  buffer = g_malloc (BUFSIZE);
  
  do
    {
      bytes_read = read (sfd, buffer, BUFSIZE);
      if (bytes_read == -1)
	{
	  if (errno == EINTR)
	    continue;
	  
	  g_set_error (error, G_IO_ERROR,
		       g_io_error_from_errno (errno),
		       _("Error reading from file: %s"),
		       g_strerror (errno));
	  ret = FALSE;
	  break;
	}
      
      bytes_to_write = bytes_read;
      write_buffer = buffer;
      
      do
	{
	  bytes_written = write (dfd, write_buffer, bytes_to_write);
	  if (bytes_written == -1)
	    {
	      if (errno == EINTR)
		continue;
	      
	      g_set_error (error, G_IO_ERROR,
			   g_io_error_from_errno (errno),
			   _("Error writing to file: %s"),
			   g_strerror (errno));
	      ret = FALSE;
	      break;
	    }
	  
	  bytes_to_write -= bytes_written;
	  write_buffer += bytes_written;
	}
      while (bytes_to_write > 0);
      
    } while ((bytes_read != 0) && (ret == TRUE));

  g_free (buffer);
  
  return ret;
}

static int
handle_overwrite_open (const char    *filename,
		       const char    *etag,
		       gboolean       create_backup,
		       char         **temp_filename,
		       GCancellable  *cancellable,
		       GError       **error)
{
  int fd = -1;
  struct stat original_stat;
  char *current_etag;
  gboolean is_symlink;
  int open_flags;

  /* We only need read access to the original file if we are creating a backup.
   * We also add O_CREATE to avoid a race if the file was just removed */
  if (create_backup)
    open_flags = O_RDWR | O_CREAT;
  else
    open_flags = O_WRONLY | O_CREAT;
  
  /* Some systems have O_NOFOLLOW, which lets us avoid some races
   * when finding out if the file we opened was a symlink */
#ifdef O_NOFOLLOW
  is_symlink = FALSE;
  fd = g_open (filename, open_flags | O_NOFOLLOW, 0666);
  if (fd == -1 && errno == ELOOP)
    {
      /* Could be a symlink, or it could be a regular ELOOP error,
       * but then the next open will fail too. */
      is_symlink = TRUE;
      fd = g_open (filename, open_flags, 0666);
    }
#else
  fd = g_open (filename, open_flags, 0666);
  /* This is racy, but we do it as soon as possible to minimize the race */
  is_symlink = g_file_test (filename, G_FILE_TEST_IS_SYMLINK);
#endif
    
  if (fd == -1)
    {
      g_set_error (error, G_IO_ERROR,
		   g_io_error_from_errno (errno),
		   _("Error opening file '%s': %s"),
		   filename, g_strerror (errno));
      return -1;
    }
  
  if (fstat (fd, &original_stat) != 0) 
    {
      g_set_error (error, G_IO_ERROR,
		   g_io_error_from_errno (errno),
		   _("Error stating file '%s': %s"),
		   filename, g_strerror (errno));
      goto err_out;
    }
  
  /* not a regular file */
  if (!S_ISREG (original_stat.st_mode))
    {
      if (S_ISDIR (original_stat.st_mode))
	g_set_error (error,
		     G_IO_ERROR,
		     G_IO_ERROR_IS_DIRECTORY,
		     _("Target file is a directory"));
      else
	g_set_error (error,
		     G_IO_ERROR,
		     G_IO_ERROR_NOT_REGULAR_FILE,
		     _("Target file is not a regular file"));
      goto err_out;
    }
  
  if (etag != NULL)
    {
      current_etag = _g_local_file_info_create_etag (&original_stat);
      if (strcmp (etag, current_etag) != 0)
	{
	  g_set_error (error,
		       G_IO_ERROR,
		       G_IO_ERROR_WRONG_ETAG,
		       _("The file was externally modified"));
	  g_free (current_etag);
	  goto err_out;
	}
      g_free (current_etag);
    }

  /* We use two backup strategies.
   * The first one (which is faster) consist in saving to a
   * tmp file then rename the original file to the backup and the
   * tmp file to the original name. This is fast but doesn't work
   * when the file is a link (hard or symbolic) or when we can't
   * write to the current dir or can't set the permissions on the
   * new file. 
   * The second strategy consist simply in copying the old file
   * to a backup file and rewrite the contents of the file.
   */
  
  if (!(original_stat.st_nlink > 1) && !is_symlink)
    {
      char *dirname, *tmp_filename;
      int tmpfd;
      
      dirname = g_path_get_dirname (filename);
      tmp_filename = g_build_filename (dirname, ".goutputstream-XXXXXX", NULL);
      g_free (dirname);

      tmpfd = g_mkstemp (tmp_filename);
      if (tmpfd == -1)
	{
	  g_free (tmp_filename);
	  goto fallback_strategy;
	}
      
      /* try to keep permissions */

      if (
#ifdef F_CHOWN
	  fchown (tmpfd, original_stat.st_uid, original_stat.st_gid) == -1 ||
#endif
#ifdef F_CHMOD
	  fchmod (tmpfd, original_stat.st_mode) == -1 ||
#endif
	  0
	  )
	{
	  struct stat tmp_statbuf;
	  
	  /* Check that we really needed to change something */
	  if (fstat (tmpfd, &tmp_statbuf) != 0 ||
	      original_stat.st_uid != tmp_statbuf.st_uid ||
	      original_stat.st_gid != tmp_statbuf.st_gid ||
	      original_stat.st_mode != tmp_statbuf.st_mode)
	    {
	      close (tmpfd);
	      unlink (tmp_filename);
	      g_free (tmp_filename);
	      goto fallback_strategy;
	    }
	}

      close (fd);
      *temp_filename = tmp_filename;
      return tmpfd;
    }

 fallback_strategy:

  if (create_backup)
    {
      struct stat tmp_statbuf;      
      char *backup_filename;
      int bfd;
      
      backup_filename = create_backup_filename (filename);

      if (unlink (backup_filename) == -1 && errno != ENOENT)
	{
	  g_set_error (error,
		       G_IO_ERROR,
		       G_IO_ERROR_CANT_CREATE_BACKUP,
		       _("Backup file creation failed"));
	  g_free (backup_filename);
	  goto err_out;
	}

      bfd = g_open (backup_filename,
		    O_WRONLY | O_CREAT | O_EXCL,
		    original_stat.st_mode & 0777);

      if (bfd == -1)
	{
	  g_set_error (error,
		       G_IO_ERROR,
		       G_IO_ERROR_CANT_CREATE_BACKUP,
		       _("Backup file creation failed"));
	  g_free (backup_filename);
	  goto err_out;
	}

      /* If needed, Try to set the group of the backup same as the
       * original file. If this fails, set the protection
       * bits for the group same as the protection bits for
       * others. */
#ifdef HAVE_FCHOWN
      if (fstat (bfd, &tmp_statbuf) != 0)
	{
	  g_set_error (error,
		       G_IO_ERROR,
		       G_IO_ERROR_CANT_CREATE_BACKUP,
		       _("Backup file creation failed"));
	  unlink (backup_filename);
	  g_free (backup_filename);
	  goto err_out;
	}
      
      if ((original_stat.st_gid != tmp_statbuf.st_gid)  &&
	  fchown (bfd, (uid_t) -1, original_stat.st_gid) != 0)
	{
	  if (fchmod (bfd,
		      (original_stat.st_mode & 0707) |
		      ((original_stat.st_mode & 07) << 3)) != 0)
	    {
	      g_set_error (error,
			   G_IO_ERROR,
			   G_IO_ERROR_CANT_CREATE_BACKUP,
			   _("Backup file creation failed"));
	      unlink (backup_filename);
	      close (bfd);
	      g_free (backup_filename);
	      goto err_out;
	    }
	}
#endif

      if (!copy_file_data (fd, bfd, NULL))
	{
	  g_set_error (error,
		       G_IO_ERROR,
		       G_IO_ERROR_CANT_CREATE_BACKUP,
		       _("Backup file creation failed"));
	  unlink (backup_filename);
	  close (bfd);
	  g_free (backup_filename);
	  
	  goto err_out;
	}
      
      close (bfd);
      g_free (backup_filename);

      /* Seek back to the start of the file after the backup copy */
      if (lseek (fd, 0, SEEK_SET) == -1)
	{
	  g_set_error (error, G_IO_ERROR,
		       g_io_error_from_errno (errno),
		       _("Error seeking in file: %s"),
		       g_strerror (errno));
	  goto err_out;
	}
    }

  /* Truncate the file at the start */
#ifdef G_OS_WIN32
  if (g_win32_ftruncate (fd, 0) == -1)
#else
  if (ftruncate (fd, 0) == -1)
#endif
    {
      g_set_error (error, G_IO_ERROR,
		   g_io_error_from_errno (errno),
		   _("Error truncating file: %s"),
		   g_strerror (errno));
      goto err_out;
    }
    
  return fd;

 err_out:
  close (fd);
  return -1;
}

GFileOutputStream *
_g_local_file_output_stream_replace (const char        *filename,
				     const char        *etag,
				     gboolean           create_backup,
				     GFileCreateFlags   flags,
				     GCancellable      *cancellable,
				     GError           **error)
{
  GLocalFileOutputStream *stream;
  int mode;
  int fd;
  char *temp_file;

  if (g_cancellable_set_error_if_cancelled (cancellable, error))
    return NULL;

  temp_file = NULL;

  if (flags & G_FILE_CREATE_PRIVATE)
    mode = 0600;
  else
    mode = 0666;

  /* If the file doesn't exist, create it */
  fd = g_open (filename, O_CREAT | O_EXCL | O_WRONLY, mode);

  if (fd == -1 && errno == EEXIST)
    {
      /* The file already exists */
      fd = handle_overwrite_open (filename, etag, create_backup, &temp_file,
				  cancellable, error);
      if (fd == -1)
	return NULL;
    }
  else if (fd == -1)
    {
      int errsv = errno;

      if (errsv == EINVAL)
	/* This must be an invalid filename, on e.g. FAT */
	g_set_error (error, G_IO_ERROR,
		     G_IO_ERROR_INVALID_FILENAME,
		     _("Invalid filename"));
      else
	g_set_error (error, G_IO_ERROR,
		     g_io_error_from_errno (errsv),
		     _("Error opening file '%s': %s"),
		     filename, g_strerror (errsv));
      return NULL;
    }
  
 
  stream = g_object_new (G_TYPE_LOCAL_FILE_OUTPUT_STREAM, NULL);
  stream->priv->fd = fd;
  stream->priv->tmp_filename = temp_file;
  if (create_backup)
    stream->priv->backup_filename = create_backup_filename (filename);
  stream->priv->original_filename =  g_strdup (filename);
  
  return G_FILE_OUTPUT_STREAM (stream);
}
