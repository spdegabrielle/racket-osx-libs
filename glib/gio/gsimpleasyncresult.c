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

#include "config.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "gsimpleasyncresult.h"
#include "gasyncresult.h"
#include "gcancellable.h"
#include "gioscheduler.h"
#include <gio/gioerror.h>
#include "glibintl.h"


/**
 * SECTION:gsimpleasyncresult
 * @short_description: Simple asynchronous results implementation
 * @include: gio/gio.h
 * @see_also: #GAsyncResult
 *
 * Implements #GAsyncResult for simple cases. Most of the time, this
 * will be all an application needs, and will be used transparently.
 * Because of this, #GSimpleAsyncResult is used throughout GIO for
 * handling asynchronous functions.
 *
 * GSimpleAsyncResult handles #GAsyncReadyCallback<!-- -->s, error
 * reporting, operation cancellation and the final state of an operation,
 * completely transparent to the application. Results can be returned
 * as a pointer e.g. for functions that return data that is collected
 * asynchronously, a boolean value for checking the success or failure
 * of an operation, or a #gssize for operations which return the number
 * of bytes modified by the operation; all of the simple return cases
 * are covered.
 *
 * Most of the time, an application will not need to know of the details
 * of this API; it is handled transparently, and any necessary operations
 * are handled by #GAsyncResult's interface. However, if implementing a
 * new GIO module, for writing language bindings, or for complex
 * applications that need better control of how asynchronous operations
 * are completed, it is important to understand this functionality.
 *
 * GSimpleAsyncResults are tagged with the calling function to ensure
 * that asynchronous functions and their finishing functions are used
 * together correctly.
 *
 * To create a new #GSimpleAsyncResult, call g_simple_async_result_new().
 * If the result needs to be created for a #GError, use
 * g_simple_async_result_new_from_error() or
 * g_simple_async_result_new_take_error(). If a #GError is not available
 * (e.g. the asynchronous operation's doesn't take a #GError argument),
 * but the result still needs to be created for an error condition, use
 * g_simple_async_result_new_error() (or g_simple_async_result_set_error_va()
 * if your application or binding requires passing a variable argument list
 * directly), and the error can then be propagated through the use of
 * g_simple_async_result_propagate_error().
 *
 * An asynchronous operation can be made to ignore a cancellation event by
 * calling g_simple_async_result_set_handle_cancellation() with a
 * #GSimpleAsyncResult for the operation and %FALSE. This is useful for
 * operations that are dangerous to cancel, such as close (which would
 * cause a leak if cancelled before being run).
 *
 * GSimpleAsyncResult can integrate into GLib's event loop, #GMainLoop,
 * or it can use #GThread<!-- -->s.
 * g_simple_async_result_complete() will finish an I/O task directly
 * from the point where it is called. g_simple_async_result_complete_in_idle()
 * will finish it from an idle handler in the <link
 * linkend="g-main-context-push-thread-default">thread-default main
 * context</link>. g_simple_async_result_run_in_thread() will run the
 * job in a separate thread and then deliver the result to the
 * thread-default main context.
 *
 * To set the results of an asynchronous function,
 * g_simple_async_result_set_op_res_gpointer(),
 * g_simple_async_result_set_op_res_gboolean(), and
 * g_simple_async_result_set_op_res_gssize()
 * are provided, setting the operation's result to a gpointer, gboolean, or
 * gssize, respectively.
 *
 * Likewise, to get the result of an asynchronous function,
 * g_simple_async_result_get_op_res_gpointer(),
 * g_simple_async_result_get_op_res_gboolean(), and
 * g_simple_async_result_get_op_res_gssize() are
 * provided, getting the operation's result as a gpointer, gboolean, and
 * gssize, respectively.
 *
 * For the details of the requirements implementations must respect, see
 * #GAsyncResult.  A typical implementation of an asynchronous operation
 * using GSimpleAsyncResult looks something like this:
 *
 * |[
 * static void
 * baked_cb (Cake    *cake,
 *           gpointer user_data)
 * {
 *   /&ast; In this example, this callback is not given a reference to the cake, so
 *    &ast; the GSimpleAsyncResult has to take a reference to it.
 *    &ast;/
 *   GSimpleAsyncResult *result = user_data;
 *
 *   if (cake == NULL)
 *     g_simple_async_result_set_error (result,
 *                                      BAKER_ERRORS,
 *                                      BAKER_ERROR_NO_FLOUR,
 *                                      "Go to the supermarket");
 *   else
 *     g_simple_async_result_set_op_res_gpointer (result,
 *                                                g_object_ref (cake),
 *                                                g_object_unref);
 *
 *
 *   /&ast; In this example, we assume that baked_cb is called as a callback from
 *    &ast; the mainloop, so it's safe to complete the operation synchronously here.
 *    &ast; If, however, _baker_prepare_cake () might call its callback without
 *    &ast; first returning to the mainloop — inadvisable, but some APIs do so —
 *    &ast; we would need to use g_simple_async_result_complete_in_idle().
 *    &ast;/
 *   g_simple_async_result_complete (result);
 *   g_object_unref (result);
 * }
 *
 * void
 * baker_bake_cake_async (Baker              *self,
 *                        guint               radius,
 *                        GAsyncReadyCallback callback,
 *                        gpointer            user_data)
 * {
 *   GSimpleAsyncResult *simple;
 *   Cake               *cake;
 *
 *   if (radius < 3)
 *     {
 *       g_simple_async_report_error_in_idle (G_OBJECT (self),
 *                                            callback,
 *                                            user_data,
 *                                            BAKER_ERRORS,
 *                                            BAKER_ERROR_TOO_SMALL,
 *                                            "%ucm radius cakes are silly",
 *                                            radius);
 *       return;
 *     }
 *
 *   simple = g_simple_async_result_new (G_OBJECT (self),
 *                                       callback,
 *                                       user_data,
 *                                       baker_bake_cake_async);
 *   cake = _baker_get_cached_cake (self, radius);
 *
 *   if (cake != NULL)
 *     {
 *       g_simple_async_result_set_op_res_gpointer (simple,
 *                                                  g_object_ref (cake),
 *                                                  g_object_unref);
 *       g_simple_async_result_complete_in_idle (simple);
 *       g_object_unref (simple);
 *       /&ast; Drop the reference returned by _baker_get_cached_cake(); the
 *        &ast; GSimpleAsyncResult has taken its own reference.
 *        &ast;/
 *       g_object_unref (cake);
 *       return;
 *     }
 *
 *   _baker_prepare_cake (self, radius, baked_cb, simple);
 * }
 *
 * Cake *
 * baker_bake_cake_finish (Baker        *self,
 *                         GAsyncResult *result,
 *                         GError      **error)
 * {
 *   GSimpleAsyncResult *simple;
 *   Cake               *cake;
 *
 *   g_return_val_if_fail (g_simple_async_result_is_valid (result,
 *                                                         G_OBJECT (self),
 *                                                         baker_bake_cake_async),
 *                         NULL);
 *
 *   simple = (GSimpleAsyncResult *) result;
 *
 *   if (g_simple_async_result_propagate_error (simple, error))
 *     return NULL;
 *
 *   cake = CAKE (g_simple_async_result_get_op_res_gpointer (simple));
 *   return g_object_ref (cake);
 * }
 * ]|
 */

static void g_simple_async_result_async_result_iface_init (GAsyncResultIface       *iface);

struct _GSimpleAsyncResult
{
  GObject parent_instance;

  GObject *source_object;
  GAsyncReadyCallback callback;
  gpointer user_data;
  GMainContext *context;
  GError *error;
  gboolean failed;
  gboolean handle_cancellation;

  gpointer source_tag;

  union {
    gpointer v_pointer;
    gboolean v_boolean;
    gssize   v_ssize;
  } op_res;

  GDestroyNotify destroy_op_res;
};

struct _GSimpleAsyncResultClass
{
  GObjectClass parent_class;
};


G_DEFINE_TYPE_WITH_CODE (GSimpleAsyncResult, g_simple_async_result, G_TYPE_OBJECT,
			 G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_RESULT,
						g_simple_async_result_async_result_iface_init))

static void
clear_op_res (GSimpleAsyncResult *simple)
{
  if (simple->destroy_op_res)
    simple->destroy_op_res (simple->op_res.v_pointer);
  simple->destroy_op_res = NULL;
  simple->op_res.v_ssize = 0;
}

static void
g_simple_async_result_finalize (GObject *object)
{
  GSimpleAsyncResult *simple;

  simple = G_SIMPLE_ASYNC_RESULT (object);

  if (simple->source_object)
    g_object_unref (simple->source_object);

  g_main_context_unref (simple->context);

  clear_op_res (simple);

  if (simple->error)
    g_error_free (simple->error);

  G_OBJECT_CLASS (g_simple_async_result_parent_class)->finalize (object);
}

static void
g_simple_async_result_class_init (GSimpleAsyncResultClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
 
  gobject_class->finalize = g_simple_async_result_finalize;
}

static void
g_simple_async_result_init (GSimpleAsyncResult *simple)
{
  simple->handle_cancellation = TRUE;

  simple->context = g_main_context_ref_thread_default ();
}

/**
 * g_simple_async_result_new:
 * @source_object: (allow-none): a #GObject, or %NULL.
 * @callback: (scope async): a #GAsyncReadyCallback.
 * @user_data: (closure): user data passed to @callback.
 * @source_tag: the asynchronous function.
 *
 * Creates a #GSimpleAsyncResult.
 *
 * Returns: a #GSimpleAsyncResult.
 **/
GSimpleAsyncResult *
g_simple_async_result_new (GObject             *source_object,
                           GAsyncReadyCallback  callback,
                           gpointer             user_data,
                           gpointer             source_tag)
{
  GSimpleAsyncResult *simple;

  g_return_val_if_fail (!source_object || G_IS_OBJECT (source_object), NULL);

  simple = g_object_new (G_TYPE_SIMPLE_ASYNC_RESULT, NULL);
  simple->callback = callback;
  if (source_object)
    simple->source_object = g_object_ref (source_object);
  else
    simple->source_object = NULL;
  simple->user_data = user_data;
  simple->source_tag = source_tag;
 
  return simple;
}

/**
 * g_simple_async_result_new_from_error:
 * @source_object: (allow-none): a #GObject, or %NULL.
 * @callback: (scope async): a #GAsyncReadyCallback.
 * @user_data: (closure): user data passed to @callback.
 * @error: a #GError
 *
 * Creates a #GSimpleAsyncResult from an error condition.
 *
 * Returns: a #GSimpleAsyncResult.
 **/
GSimpleAsyncResult *
g_simple_async_result_new_from_error (GObject             *source_object,
                                      GAsyncReadyCallback  callback,
                                      gpointer             user_data,
                                      const GError        *error)
{
  GSimpleAsyncResult *simple;

  g_return_val_if_fail (!source_object || G_IS_OBJECT (source_object), NULL);

  simple = g_simple_async_result_new (source_object,
				      callback,
				      user_data, NULL);
  g_simple_async_result_set_from_error (simple, error);

  return simple;
}

/**
 * g_simple_async_result_new_take_error: (skip)
 * @source_object: (allow-none): a #GObject, or %NULL
 * @callback: (scope async): a #GAsyncReadyCallback
 * @user_data: (closure): user data passed to @callback
 * @error: a #GError
 *
 * Creates a #GSimpleAsyncResult from an error condition, and takes over the
 * caller's ownership of @error, so the caller does not need to free it anymore.
 *
 * Returns: a #GSimpleAsyncResult
 *
 * Since: 2.28
 **/
GSimpleAsyncResult *
g_simple_async_result_new_take_error (GObject             *source_object,
                                      GAsyncReadyCallback  callback,
                                      gpointer             user_data,
                                      GError              *error)
{
  GSimpleAsyncResult *simple;

  g_return_val_if_fail (!source_object || G_IS_OBJECT (source_object), NULL);

  simple = g_simple_async_result_new (source_object,
				      callback,
				      user_data, NULL);
  g_simple_async_result_take_error (simple, error);

  return simple;
}

/**
 * g_simple_async_result_new_error:
 * @source_object: (allow-none): a #GObject, or %NULL.
 * @callback: (scope async): a #GAsyncReadyCallback.
 * @user_data: (closure): user data passed to @callback.
 * @domain: a #GQuark.
 * @code: an error code.
 * @format: a string with format characters.
 * @...: a list of values to insert into @format.
 *
 * Creates a new #GSimpleAsyncResult with a set error.
 *
 * Returns: a #GSimpleAsyncResult.
 **/
GSimpleAsyncResult *
g_simple_async_result_new_error (GObject             *source_object,
                                 GAsyncReadyCallback  callback,
                                 gpointer             user_data,
                                 GQuark               domain,
                                 gint                 code,
                                 const char          *format,
                                 ...)
{
  GSimpleAsyncResult *simple;
  va_list args;
 
  g_return_val_if_fail (!source_object || G_IS_OBJECT (source_object), NULL);
  g_return_val_if_fail (domain != 0, NULL);
  g_return_val_if_fail (format != NULL, NULL);

  simple = g_simple_async_result_new (source_object,
				      callback,
				      user_data, NULL);

  va_start (args, format);
  g_simple_async_result_set_error_va (simple, domain, code, format, args);
  va_end (args);
 
  return simple;
}


static gpointer
g_simple_async_result_get_user_data (GAsyncResult *res)
{
  return G_SIMPLE_ASYNC_RESULT (res)->user_data;
}

static GObject *
g_simple_async_result_get_source_object (GAsyncResult *res)
{
  if (G_SIMPLE_ASYNC_RESULT (res)->source_object)
    return g_object_ref (G_SIMPLE_ASYNC_RESULT (res)->source_object);
  return NULL;
}

static void
g_simple_async_result_async_result_iface_init (GAsyncResultIface *iface)
{
  iface->get_user_data = g_simple_async_result_get_user_data;
  iface->get_source_object = g_simple_async_result_get_source_object;
}

/**
 * g_simple_async_result_set_handle_cancellation:
 * @simple: a #GSimpleAsyncResult.
 * @handle_cancellation: a #gboolean.
 *
 * Sets whether to handle cancellation within the asynchronous operation.
 *
 **/
void
g_simple_async_result_set_handle_cancellation (GSimpleAsyncResult *simple,
                                               gboolean            handle_cancellation)
{
  g_return_if_fail (G_IS_SIMPLE_ASYNC_RESULT (simple));
  simple->handle_cancellation = handle_cancellation;
}

/**
 * g_simple_async_result_get_source_tag: (skip)
 * @simple: a #GSimpleAsyncResult.
 *
 * Gets the source tag for the #GSimpleAsyncResult.
 *
 * Returns: a #gpointer to the source object for the #GSimpleAsyncResult.
 **/
gpointer
g_simple_async_result_get_source_tag (GSimpleAsyncResult *simple)
{
  g_return_val_if_fail (G_IS_SIMPLE_ASYNC_RESULT (simple), NULL);
  return simple->source_tag;
}

/**
 * g_simple_async_result_propagate_error:
 * @simple: a #GSimpleAsyncResult.
 * @dest: (out): a location to propagate the error to.
 *
 * Propagates an error from within the simple asynchronous result to
 * a given destination.
 *
 * Returns: %TRUE if the error was propagated to @dest. %FALSE otherwise.
 **/
gboolean
g_simple_async_result_propagate_error (GSimpleAsyncResult  *simple,
                                       GError             **dest)
{
  g_return_val_if_fail (G_IS_SIMPLE_ASYNC_RESULT (simple), FALSE);

  if (simple->failed)
    {
      g_propagate_error (dest, simple->error);
      simple->error = NULL;
      return TRUE;
    }

  return FALSE;
}

/**
 * g_simple_async_result_set_op_res_gpointer: (skip)
 * @simple: a #GSimpleAsyncResult.
 * @op_res: a pointer result from an asynchronous function.
 * @destroy_op_res: a #GDestroyNotify function.
 *
 * Sets the operation result within the asynchronous result to a pointer.
 **/
void
g_simple_async_result_set_op_res_gpointer (GSimpleAsyncResult *simple,
                                           gpointer            op_res,
                                           GDestroyNotify      destroy_op_res)
{
  g_return_if_fail (G_IS_SIMPLE_ASYNC_RESULT (simple));

  clear_op_res (simple);
  simple->op_res.v_pointer = op_res;
  simple->destroy_op_res = destroy_op_res;
}

/**
 * g_simple_async_result_get_op_res_gpointer: (skip)
 * @simple: a #GSimpleAsyncResult.
 *
 * Gets a pointer result as returned by the asynchronous function.
 *
 * Returns: a pointer from the result.
 **/
gpointer
g_simple_async_result_get_op_res_gpointer (GSimpleAsyncResult *simple)
{
  g_return_val_if_fail (G_IS_SIMPLE_ASYNC_RESULT (simple), NULL);
  return simple->op_res.v_pointer;
}

/**
 * g_simple_async_result_set_op_res_gssize:
 * @simple: a #GSimpleAsyncResult.
 * @op_res: a #gssize.
 *
 * Sets the operation result within the asynchronous result to
 * the given @op_res.
 **/
void
g_simple_async_result_set_op_res_gssize (GSimpleAsyncResult *simple,
                                         gssize              op_res)
{
  g_return_if_fail (G_IS_SIMPLE_ASYNC_RESULT (simple));
  clear_op_res (simple);
  simple->op_res.v_ssize = op_res;
}

/**
 * g_simple_async_result_get_op_res_gssize:
 * @simple: a #GSimpleAsyncResult.
 *
 * Gets a gssize from the asynchronous result.
 *
 * Returns: a gssize returned from the asynchronous function.
 **/
gssize
g_simple_async_result_get_op_res_gssize (GSimpleAsyncResult *simple)
{
  g_return_val_if_fail (G_IS_SIMPLE_ASYNC_RESULT (simple), 0);
  return simple->op_res.v_ssize;
}

/**
 * g_simple_async_result_set_op_res_gboolean:
 * @simple: a #GSimpleAsyncResult.
 * @op_res: a #gboolean.
 *
 * Sets the operation result to a boolean within the asynchronous result.
 **/
void
g_simple_async_result_set_op_res_gboolean (GSimpleAsyncResult *simple,
                                           gboolean            op_res)
{
  g_return_if_fail (G_IS_SIMPLE_ASYNC_RESULT (simple));
  clear_op_res (simple);
  simple->op_res.v_boolean = !!op_res;
}

/**
 * g_simple_async_result_get_op_res_gboolean:
 * @simple: a #GSimpleAsyncResult.
 *
 * Gets the operation result boolean from within the asynchronous result.
 *
 * Returns: %TRUE if the operation's result was %TRUE, %FALSE
 *     if the operation's result was %FALSE.
 **/
gboolean
g_simple_async_result_get_op_res_gboolean (GSimpleAsyncResult *simple)
{
  g_return_val_if_fail (G_IS_SIMPLE_ASYNC_RESULT (simple), FALSE);
  return simple->op_res.v_boolean;
}

/**
 * g_simple_async_result_set_from_error:
 * @simple: a #GSimpleAsyncResult.
 * @error: #GError.
 *
 * Sets the result from a #GError.
 **/
void
g_simple_async_result_set_from_error (GSimpleAsyncResult *simple,
                                      const GError       *error)
{
  g_return_if_fail (G_IS_SIMPLE_ASYNC_RESULT (simple));
  g_return_if_fail (error != NULL);

  if (simple->error)
    g_error_free (simple->error);
  simple->error = g_error_copy (error);
  simple->failed = TRUE;
}

/**
 * g_simple_async_result_take_error: (skip)
 * @simple: a #GSimpleAsyncResult
 * @error: a #GError
 *
 * Sets the result from @error, and takes over the caller's ownership
 * of @error, so the caller does not need to free it any more.
 *
 * Since: 2.28
 **/
void
g_simple_async_result_take_error (GSimpleAsyncResult *simple,
                                  GError             *error)
{
  g_return_if_fail (G_IS_SIMPLE_ASYNC_RESULT (simple));
  g_return_if_fail (error != NULL);

  if (simple->error)
    g_error_free (simple->error);
  simple->error = error;
  simple->failed = TRUE;
}

/**
 * g_simple_async_result_set_error_va: (skip)
 * @simple: a #GSimpleAsyncResult.
 * @domain: a #GQuark (usually #G_IO_ERROR).
 * @code: an error code.
 * @format: a formatted error reporting string.
 * @args: va_list of arguments.
 *
 * Sets an error within the asynchronous result without a #GError.
 * Unless writing a binding, see g_simple_async_result_set_error().
 **/
void
g_simple_async_result_set_error_va (GSimpleAsyncResult *simple,
                                    GQuark              domain,
                                    gint                code,
                                    const char         *format,
                                    va_list             args)
{
  g_return_if_fail (G_IS_SIMPLE_ASYNC_RESULT (simple));
  g_return_if_fail (domain != 0);
  g_return_if_fail (format != NULL);

  if (simple->error)
    g_error_free (simple->error);
  simple->error = g_error_new_valist (domain, code, format, args);
  simple->failed = TRUE;
}

/**
 * g_simple_async_result_set_error: (skip)
 * @simple: a #GSimpleAsyncResult.
 * @domain: a #GQuark (usually #G_IO_ERROR).
 * @code: an error code.
 * @format: a formatted error reporting string.
 * @...: a list of variables to fill in @format.
 *
 * Sets an error within the asynchronous result without a #GError.
 **/
void
g_simple_async_result_set_error (GSimpleAsyncResult *simple,
                                 GQuark              domain,
                                 gint                code,
                                 const char         *format,
                                 ...)
{
  va_list args;

  g_return_if_fail (G_IS_SIMPLE_ASYNC_RESULT (simple));
  g_return_if_fail (domain != 0);
  g_return_if_fail (format != NULL);

  va_start (args, format);
  g_simple_async_result_set_error_va (simple, domain, code, format, args);
  va_end (args);
}

/**
 * g_simple_async_result_complete:
 * @simple: a #GSimpleAsyncResult.
 *
 * Completes an asynchronous I/O job immediately. Must be called in
 * the thread where the asynchronous result was to be delivered, as it
 * invokes the callback directly. If you are in a different thread use
 * g_simple_async_result_complete_in_idle().
 *
 * Calling this function takes a reference to @simple for as long as
 * is needed to complete the call.
 **/
void
g_simple_async_result_complete (GSimpleAsyncResult *simple)
{
#ifndef G_DISABLE_CHECKS
  GSource *current_source;
  GMainContext *current_context;
#endif

  g_return_if_fail (G_IS_SIMPLE_ASYNC_RESULT (simple));

#ifndef G_DISABLE_CHECKS
  current_source = g_main_current_source ();
  if (current_source && !g_source_is_destroyed (current_source))
    {
      current_context = g_source_get_context (current_source);
      if (simple->context != current_context)
	g_warning ("g_simple_async_result_complete() called from wrong context!");
    }
#endif

  if (simple->callback)
    {
      g_main_context_push_thread_default (simple->context);
      simple->callback (simple->source_object,
			G_ASYNC_RESULT (simple),
			simple->user_data);
      g_main_context_pop_thread_default (simple->context);
    }
}

static gboolean
complete_in_idle_cb (gpointer data)
{
  GSimpleAsyncResult *simple = G_SIMPLE_ASYNC_RESULT (data);

  g_simple_async_result_complete (simple);

  return FALSE;
}

/**
 * g_simple_async_result_complete_in_idle:
 * @simple: a #GSimpleAsyncResult.
 *
 * Completes an asynchronous function in an idle handler in the <link
 * linkend="g-main-context-push-thread-default">thread-default main
 * loop</link> of the thread that @simple was initially created in
 * (and re-pushes that context around the invocation of the callback).
 *
 * Calling this function takes a reference to @simple for as long as
 * is needed to complete the call.
 */
void
g_simple_async_result_complete_in_idle (GSimpleAsyncResult *simple)
{
  GSource *source;

  g_return_if_fail (G_IS_SIMPLE_ASYNC_RESULT (simple));

  g_object_ref (simple);
 
  source = g_idle_source_new ();
  g_source_set_priority (source, G_PRIORITY_DEFAULT);
  g_source_set_callback (source, complete_in_idle_cb, simple, g_object_unref);

  g_source_attach (source, simple->context);
  g_source_unref (source);
}

typedef struct {
  GSimpleAsyncResult *simple;
  GCancellable *cancellable;
  GSimpleAsyncThreadFunc func;
} RunInThreadData;


static gboolean
complete_in_idle_cb_for_thread (gpointer _data)
{
  RunInThreadData *data = _data;
  GSimpleAsyncResult *simple;

  simple = data->simple;
 
  if (simple->handle_cancellation &&
      g_cancellable_is_cancelled (data->cancellable))
    g_simple_async_result_set_error (simple,
                                     G_IO_ERROR,
                                     G_IO_ERROR_CANCELLED,
                                     "%s", _("Operation was cancelled"));
 
  g_simple_async_result_complete (simple);

  if (data->cancellable)
    g_object_unref (data->cancellable);
  g_object_unref (data->simple);
  g_free (data);
 
  return FALSE;
}

static gboolean
run_in_thread (GIOSchedulerJob *job,
               GCancellable    *c,
               gpointer         _data)
{
  RunInThreadData *data = _data;
  GSimpleAsyncResult *simple = data->simple;
  GSource *source;
 
  if (simple->handle_cancellation &&
      g_cancellable_is_cancelled (c))
    g_simple_async_result_set_error (simple,
                                     G_IO_ERROR,
                                     G_IO_ERROR_CANCELLED,
                                     "%s", _("Operation was cancelled"));
  else
    data->func (simple,
                simple->source_object,
                c);

  source = g_idle_source_new ();
  g_source_set_priority (source, G_PRIORITY_DEFAULT);
  g_source_set_callback (source, complete_in_idle_cb_for_thread, data, NULL);

  g_source_attach (source, simple->context);
  g_source_unref (source);

  return FALSE;
}

/**
 * g_simple_async_result_run_in_thread: (skip)
 * @simple: a #GSimpleAsyncResult.
 * @func: a #GSimpleAsyncThreadFunc.
 * @io_priority: the io priority of the request.
 * @cancellable: (allow-none): optional #GCancellable object, %NULL to ignore.
 *
 * Runs the asynchronous job in a separate thread and then calls
 * g_simple_async_result_complete_in_idle() on @simple to return
 * the result to the appropriate main loop.
 *
 * Calling this function takes a reference to @simple for as long as
 * is needed to run the job and report its completion.
 */
void
g_simple_async_result_run_in_thread (GSimpleAsyncResult     *simple,
                                     GSimpleAsyncThreadFunc  func,
                                     int                     io_priority,
                                     GCancellable           *cancellable)
{
  RunInThreadData *data;

  g_return_if_fail (G_IS_SIMPLE_ASYNC_RESULT (simple));
  g_return_if_fail (func != NULL);

  data = g_new (RunInThreadData, 1);
  data->func = func;
  data->simple = g_object_ref (simple);
  data->cancellable = cancellable;
  if (cancellable)
    g_object_ref (cancellable);
  g_io_scheduler_push_job (run_in_thread, data, NULL, io_priority, cancellable);
}

/**
 * g_simple_async_result_is_valid:
 * @result: the #GAsyncResult passed to the _finish function.
 * @source: the #GObject passed to the _finish function.
 * @source_tag: the asynchronous function.
 *
 * Ensures that the data passed to the _finish function of an async
 * operation is consistent.  Three checks are performed.
 *
 * First, @result is checked to ensure that it is really a
 * #GSimpleAsyncResult.  Second, @source is checked to ensure that it
 * matches the source object of @result.  Third, @source_tag is
 * checked to ensure that it is either %NULL (as it is when the result was
 * created by g_simple_async_report_error_in_idle() or
 * g_simple_async_report_gerror_in_idle()) or equal to the
 * @source_tag argument given to g_simple_async_result_new() (which, by
 * convention, is a pointer to the _async function corresponding to the
 * _finish function from which this function is called).
 *
 * Returns: #TRUE if all checks passed or #FALSE if any failed.
 *
 * Since: 2.20
 **/
gboolean
g_simple_async_result_is_valid (GAsyncResult *result,
                                GObject      *source,
                                gpointer      source_tag)
{
  GSimpleAsyncResult *simple;
  GObject *cmp_source;

  if (!G_IS_SIMPLE_ASYNC_RESULT (result))
    return FALSE;
  simple = (GSimpleAsyncResult *)result;

  cmp_source = g_async_result_get_source_object (result);
  if (cmp_source != source)
    {
      if (cmp_source != NULL)
        g_object_unref (cmp_source);
      return FALSE;
    }
  if (cmp_source != NULL)
    g_object_unref (cmp_source);

  return source_tag == NULL ||
         source_tag == g_simple_async_result_get_source_tag (simple);
}

/**
 * g_simple_async_report_error_in_idle: (skip)
 * @object: (allow-none): a #GObject, or %NULL.
 * @callback: a #GAsyncReadyCallback.
 * @user_data: user data passed to @callback.
 * @domain: a #GQuark containing the error domain (usually #G_IO_ERROR).
 * @code: a specific error code.
 * @format: a formatted error reporting string.
 * @...: a list of variables to fill in @format.
 *
 * Reports an error in an asynchronous function in an idle function by
 * directly setting the contents of the #GAsyncResult with the given error
 * information.
 **/
void
g_simple_async_report_error_in_idle (GObject             *object,
                                     GAsyncReadyCallback  callback,
                                     gpointer             user_data,
                                     GQuark               domain,
                                     gint                 code,
                                     const char          *format,
                                     ...)
{
  GSimpleAsyncResult *simple;
  va_list args;
 
  g_return_if_fail (!object || G_IS_OBJECT (object));
  g_return_if_fail (domain != 0);
  g_return_if_fail (format != NULL);

  simple = g_simple_async_result_new (object,
				      callback,
				      user_data, NULL);

  va_start (args, format);
  g_simple_async_result_set_error_va (simple, domain, code, format, args);
  va_end (args);
  g_simple_async_result_complete_in_idle (simple);
  g_object_unref (simple);
}

/**
 * g_simple_async_report_gerror_in_idle:
 * @object: (allow-none): a #GObject, or %NULL
 * @callback: (scope async): a #GAsyncReadyCallback.
 * @user_data: (closure): user data passed to @callback.
 * @error: the #GError to report
 *
 * Reports an error in an idle function. Similar to
 * g_simple_async_report_error_in_idle(), but takes a #GError rather
 * than building a new one.
 **/
void
g_simple_async_report_gerror_in_idle (GObject *object,
				      GAsyncReadyCallback callback,
				      gpointer user_data,
				      const GError *error)
{
  GSimpleAsyncResult *simple;
 
  g_return_if_fail (!object || G_IS_OBJECT (object));
  g_return_if_fail (error != NULL);

  simple = g_simple_async_result_new_from_error (object,
						 callback,
						 user_data,
						 error);
  g_simple_async_result_complete_in_idle (simple);
  g_object_unref (simple);
}

/**
 * g_simple_async_report_take_gerror_in_idle: (skip)
 * @object: (allow-none): a #GObject, or %NULL
 * @callback: a #GAsyncReadyCallback.
 * @user_data: user data passed to @callback.
 * @error: the #GError to report
 *
 * Reports an error in an idle function. Similar to
 * g_simple_async_report_gerror_in_idle(), but takes over the caller's
 * ownership of @error, so the caller does not have to free it any more.
 *
 * Since: 2.28
 **/
void
g_simple_async_report_take_gerror_in_idle (GObject *object,
                                           GAsyncReadyCallback callback,
                                           gpointer user_data,
                                           GError *error)
{
  GSimpleAsyncResult *simple;

  g_return_if_fail (!object || G_IS_OBJECT (object));
  g_return_if_fail (error != NULL);

  simple = g_simple_async_result_new_take_error (object,
                                                 callback,
                                                 user_data,
                                                 error);
  g_simple_async_result_complete_in_idle (simple);
  g_object_unref (simple);
}
