/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

#include "config.h"

#include <string.h>

#include <glib/gi18n-lib.h>

#include <meta/display.h>

#include "shell-app-private.h"
#include "shell-enum-types.h"
#include "shell-global.h"
#include "shell-util.h"
#include "shell-app-system-private.h"
#include "shell-window-tracker-private.h"
#include "st.h"
#include "gactionmuxer.h"

typedef enum {
  MATCH_NONE,
  MATCH_SUBSTRING, /* Not prefix, substring */
  MATCH_PREFIX, /* Strict prefix */
} ShellAppSearchMatch;

/* This is mainly a memory usage optimization - the user is going to
 * be running far fewer of the applications at one time than they have
 * installed.  But it also just helps keep the code more logically
 * separated.
 */
typedef struct {
  guint refcount;

  /* Signal connection to dirty window sort list on workspace changes */
  guint workspace_switch_id;

  GSList *windows;

  /* Whether or not we need to resort the windows; this is done on demand */
  gboolean window_sort_stale : 1;

  /* See GApplication documentation */
  gint              name_watcher_id;
  gchar            *dbus_name;
  GDBusProxy       *app_proxy;
  GActionGroup     *remote_actions;
  GMenuModel       *remote_menu;
  GActionMuxer     *muxer;
  GCancellable     *dbus_cancellable;
} ShellAppRunningState;

/**
 * SECTION:shell-app
 * @short_description: Object representing an application
 *
 * This object wraps a #GMenuTreeEntry, providing methods and signals
 * primarily useful for running applications.
 */
struct _ShellApp
{
  GObject parent;

  int started_on_workspace;

  ShellAppState state;

  GMenuTreeEntry *entry; /* If NULL, this app is backed by one or more
                          * MetaWindow.  For purposes of app title
                          * etc., we use the first window added,
                          * because it's most likely to be what we
                          * want (e.g. it will be of TYPE_NORMAL from
                          * the way shell-window-tracker.c works).
                          */

  ShellAppRunningState *running_state;

  char *window_id_string;

  char *casefolded_name;
  char *name_collation_key;
  char *casefolded_description;
  char *casefolded_exec;
};

enum {
  PROP_0,
  PROP_STATE,
  PROP_ID,
  PROP_DBUS_ID,
  PROP_ACTION_GROUP,
  PROP_MENU
};

enum {
  WINDOWS_CHANGED,
  LAST_SIGNAL
};

static guint shell_app_signals[LAST_SIGNAL] = { 0 };

static void create_running_state (ShellApp *app);
static void unref_running_state (ShellAppRunningState *state);
static void on_dbus_name_appeared (GDBusConnection *bus,
                                   const gchar     *name,
                                   const gchar     *name_owner,
                                   gpointer         user_data);
static void on_dbus_name_disappeared (GDBusConnection *bus,
                                      const gchar     *name,
                                      gpointer         user_data);

G_DEFINE_TYPE (ShellApp, shell_app, G_TYPE_OBJECT)

static void
shell_app_get_property (GObject    *gobject,
                        guint       prop_id,
                        GValue     *value,
                        GParamSpec *pspec)
{
  ShellApp *app = SHELL_APP (gobject);

  switch (prop_id)
    {
    case PROP_STATE:
      g_value_set_enum (value, app->state);
      break;
    case PROP_ID:
      g_value_set_string (value, shell_app_get_id (app));
      break;
    case PROP_DBUS_ID:
      g_value_set_string (value, shell_app_get_dbus_id (app));
      break;
    case PROP_ACTION_GROUP:
      if (app->running_state)
        g_value_set_object (value, app->running_state->muxer);
      break;
    case PROP_MENU:
      if (app->running_state)
        g_value_set_object (value, app->running_state->remote_menu);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
    }
}

const char *
shell_app_get_id (ShellApp *app)
{
  if (app->entry)
    return gmenu_tree_entry_get_desktop_file_id (app->entry);
  return app->window_id_string;
}

static MetaWindow *
window_backed_app_get_window (ShellApp     *app)
{
  g_assert (app->entry == NULL);
  g_assert (app->running_state);
  g_assert (app->running_state->windows);
  return app->running_state->windows->data;
}

static ClutterActor *
window_backed_app_get_icon (ShellApp *app,
                            int       size)
{
  MetaWindow *window;
  ClutterActor *actor;

  /* During a state transition from running to not-running for
   * window-backend apps, it's possible we get a request for the icon.
   * Avoid asserting here and just return an empty image.
   */
  if (app->running_state == NULL)
    {
      actor = clutter_texture_new ();
      g_object_set (actor, "opacity", 0, "width", (float) size, "height", (float) size, NULL);
      return actor;
    }

  window = window_backed_app_get_window (app);
  actor = st_texture_cache_bind_pixbuf_property (st_texture_cache_get_default (),
                                                               G_OBJECT (window),
                                                               "icon");
  g_object_set (actor, "width", (float) size, "height", (float) size, NULL);
  return actor;
}

const char *
shell_app_get_dbus_id (ShellApp *app)
{
  if (app->running_state)
    return app->running_state->dbus_name;
  else
    return NULL;
}

/**
 * shell_app_create_icon_texture:
 *
 * Look up the icon for this application, and create a #ClutterTexture
 * for it at the given size.
 *
 * Return value: (transfer none): A floating #ClutterActor
 */
ClutterActor *
shell_app_create_icon_texture (ShellApp   *app,
                               int         size)
{
  GIcon *icon;
  ClutterActor *ret;

  ret = NULL;

  if (app->entry == NULL)
    return window_backed_app_get_icon (app, size);

  icon = g_app_info_get_icon (G_APP_INFO (gmenu_tree_entry_get_app_info (app->entry)));
  if (icon != NULL)
    ret = st_texture_cache_load_gicon (st_texture_cache_get_default (), NULL, icon, size);

  if (ret == NULL)
    {
      icon = g_themed_icon_new ("application-x-executable");
      ret = st_texture_cache_load_gicon (st_texture_cache_get_default (), NULL, icon, size);
      g_object_unref (icon);
    }

  return ret;
}

static GdkPixbuf *
shell_app_grab_icon_pixbuf (ShellApp          *app,
                            guint              size,
                            GtkIconLookupFlags flags)
{
  GIcon *icon;
  GtkIconInfo *info;
  GdkPixbuf *pixbuf;

  if (!app->entry)
    {
      MetaWindow *window;

      window = window_backed_app_get_window (app);
      g_object_get (window, "icon", &pixbuf, NULL);
      return pixbuf;
    }

  info = NULL;

  icon = g_app_info_get_icon (G_APP_INFO (gmenu_tree_entry_get_app_info (app->entry)));
  if (icon != NULL)
    {
      info = gtk_icon_theme_lookup_by_gicon (gtk_icon_theme_get_default (),
                                             icon, size, flags);
    }

  if (info == NULL)
    {
      icon = g_themed_icon_new ("application-x-executable");
      info = gtk_icon_theme_lookup_by_gicon (gtk_icon_theme_get_default (),
                                             icon, size, flags);
      g_object_unref (icon);
    }

  if (info == NULL)
    return COGL_INVALID_HANDLE;

  pixbuf = gtk_icon_info_load_icon (info, NULL);
  gtk_icon_info_free (info);

  return pixbuf;
}

/* Traditional Graphics Gems algorithm for converting to hsl */
static void
convert_to_hsl (gfloat  red,
                gfloat  green,
                gfloat  blue,
                gfloat *hue,
                gfloat *saturation,
                gfloat *luminance)
{
  gfloat min, max, delta;
  gfloat h, l, s;

  max = MAX (red, MAX (green, blue));
  min = MIN (red, MIN (green, blue));

  l = (max + min) / 2;
  s = 0;
  h = 0;

  if (max != min)
    {
      if (l <= 0.5)
	s = (max - min) / (max + min);
      else
	s = (max - min) / (2.0 - max - min);

      delta = max - min;

      if (red == max)
	h = (green - blue) / delta;
      else if (green == max)
	h = 2.0 + (blue - red) / delta;
      else if (blue == max)
	h = 4.0 + (red - green) / delta;

      h *= 60;

      if (h < 0)
	h += 360.0;
    }

  *hue = h;
  *luminance = l;
  *saturation = s;
}

#define BIN_SIZE 16
#define ALPHA_THRESHOLD 40
#define SATURATION_THRESHOLD 0.2

static guint
pixbuf_get_prominent_icon_color (GdkPixbuf *pixbuf)
{
  /* The algorithm is a basic 3D histogram.
   * We scan an image, fitting each color into one of BIN_SIZE**3 bins
   * Then we look for the bin with the most pixels in it.
   */

  gint width, height, rowstride;
  guint i, j, k;
  guint8 n_channels;
  gboolean have_alpha;
  guint8 *pixels;
  guint bins[BIN_SIZE][BIN_SIZE][BIN_SIZE];
  guint max_bin_color, max_bin_count;
  guint8 red, green, blue;
  gfloat hue, sat, lit;

  memset (bins, 0, sizeof (bins));

  width = gdk_pixbuf_get_width (pixbuf);
  height = gdk_pixbuf_get_height (pixbuf);
  rowstride = gdk_pixbuf_get_rowstride (pixbuf);
  n_channels = gdk_pixbuf_get_n_channels (pixbuf);
  have_alpha = gdk_pixbuf_get_has_alpha (pixbuf);
  pixels = gdk_pixbuf_get_pixels (pixbuf);

  for (i = 0; i < width; i++)
    {
      for (j = 0; j < height; j++)
        {
          guint8 *pixel = &pixels[j * rowstride + i * n_channels];

          /* Ignore pixels that are so transparent they won't contribute */
          if (have_alpha && pixel[3] < ALPHA_THRESHOLD)
            continue;

          convert_to_hsl (pixel[0] / 255.0,
                          pixel[1] / 255.0,
                          pixel[2] / 255.0,
                          &hue, &sat, &lit);

          /* Ignore pixels that are so dark they won't contribute */
          if (sat < SATURATION_THRESHOLD)
            continue;

          red   = pixel[0] * BIN_SIZE / 256;
          g_assert (red < BIN_SIZE);

          green = pixel[1] * BIN_SIZE / 256;
          g_assert (green < BIN_SIZE);

          blue  = pixel[2] * BIN_SIZE / 256;
          g_assert (blue < BIN_SIZE);

          bins[red][green][blue] ++;
        }
    }

  max_bin_color = 0;
  max_bin_count = bins[0][0][0];
  for (i = 0; i < BIN_SIZE; i++)
    {
      for (j = 0; j < BIN_SIZE; j++)
        {
          for (k = 0; k < BIN_SIZE; k++)
            {
              guint count;

              count = bins[i][j][k];

              if (count > max_bin_count)
                {
                  red   = i * 255 / BIN_SIZE;
                  green = j * 255 / BIN_SIZE;
                  blue  = k * 255 / BIN_SIZE;

                  max_bin_color = (red << 16) | (green << 8) | blue;
                  max_bin_count = count;
                }
            }
        }
    }

  /* in the case that *all* pixels are unsuitable,
   * fall back to a nice gray */
  if (G_UNLIKELY (max_bin_count == 0))
    return 0x6F6F80;

  return max_bin_color;
}

#undef BIN_SIZE
#undef ALPHA_THRESHOLD
#undef SATURATION_THRESHOLD

#define PROMINENT_ICON_COLOR_SIZE 64

/**
 * shell_app_get_prominent_icon_color_1:
 * @app: A #ShellApp
 *
 * Scan over a small version of the app icon and try to find
 * the most prominent hue inside it.
 *
 * Returns: (transfer full): the color
 */
ClutterColor *
shell_app_get_prominent_icon_color (ShellApp *app)
{
  GdkPixbuf *pixbuf;
  ClutterColor *color;

  pixbuf = shell_app_grab_icon_pixbuf (app, PROMINENT_ICON_COLOR_SIZE, 0);
  color = clutter_color_new (0, 0, 0, 0);

  if (pixbuf)
    {
      /* Clutter expects a color in RGBA format so add the alpha channel. */
      clutter_color_from_pixel (color,
                                pixbuf_get_prominent_icon_color (pixbuf) << 8 | 0xFF);
    }

  return color;
}

#undef PROMINENT_ICON_COLOR_SIZE

typedef struct {
  ShellApp *app;
  int size;
} CreateFadedIconData;

static CoglHandle
shell_app_create_faded_icon_cpu (StTextureCache *cache,
                                 const char     *key,
                                 void           *datap,
                                 GError        **error)
{
  CreateFadedIconData *data = datap;
  GdkPixbuf *pixbuf;
  CoglHandle texture;
  gint width, height, rowstride;
  guint8 n_channels;
  gboolean have_alpha;
  gint fade_start;
  gint fade_range;
  guint i, j;
  guint pixbuf_byte_size;
  guint8 *orig_pixels;
  guint8 *pixels;

  pixbuf = shell_app_grab_icon_pixbuf (data->app, data->size, GTK_ICON_LOOKUP_FORCE_SIZE);

  if (pixbuf == NULL)
    return COGL_INVALID_HANDLE;

  width = gdk_pixbuf_get_width (pixbuf);
  height = gdk_pixbuf_get_height (pixbuf);
  rowstride = gdk_pixbuf_get_rowstride (pixbuf);
  n_channels = gdk_pixbuf_get_n_channels (pixbuf);
  orig_pixels = gdk_pixbuf_get_pixels (pixbuf);
  have_alpha = gdk_pixbuf_get_has_alpha (pixbuf);

  pixbuf_byte_size = (height - 1) * rowstride +
    + width * ((n_channels * gdk_pixbuf_get_bits_per_sample (pixbuf) + 7) / 8);

  pixels = g_malloc0 (rowstride * height);
  memcpy (pixels, orig_pixels, pixbuf_byte_size);

  fade_start = width / 2;
  fade_range = width - fade_start;
  for (i = fade_start; i < width; i++)
    {
      for (j = 0; j < height; j++)
        {
          guchar *pixel = &pixels[j * rowstride + i * n_channels];
          float fade = 1.0 - ((float) i - fade_start) / fade_range;
          pixel[0] = 0.5 + pixel[0] * fade;
          pixel[1] = 0.5 + pixel[1] * fade;
          pixel[2] = 0.5 + pixel[2] * fade;
          if (have_alpha)
            pixel[3] = 0.5 + pixel[3] * fade;
        }
    }

  texture = cogl_texture_new_from_data (width,
                                        height,
                                        COGL_TEXTURE_NONE,
                                        have_alpha ? COGL_PIXEL_FORMAT_RGBA_8888 : COGL_PIXEL_FORMAT_RGB_888,
                                        COGL_PIXEL_FORMAT_ANY,
                                        rowstride,
                                        pixels);
  g_free (pixels);
  g_object_unref (pixbuf);

  return texture;
}

/**
 * shell_app_get_faded_icon:
 * @app: A #ShellApp
 * @size: Size in pixels
 *
 * Return an actor with a horizontally faded look.
 *
 * Return value: (transfer none): A floating #ClutterActor, or %NULL if no icon
 */
ClutterActor *
shell_app_get_faded_icon (ShellApp *app, int size)
{
  CoglHandle texture;
  ClutterActor *result;
  char *cache_key;
  CreateFadedIconData data;

  /* Don't fade for window backed apps for now...easier to reuse the
   * property tracking bits, and this helps us visually distinguish
   * app-tracked from not.
   */
  if (!app->entry)
    return window_backed_app_get_icon (app, size);

  cache_key = g_strdup_printf ("faded-icon:%s,size=%d", shell_app_get_id (app), size);
  data.app = app;
  data.size = size;
  texture = st_texture_cache_load (st_texture_cache_get_default (),
                                   cache_key,
                                   ST_TEXTURE_CACHE_POLICY_FOREVER,
                                   shell_app_create_faded_icon_cpu,
                                   &data,
                                   NULL);
  g_free (cache_key);

  if (texture != COGL_INVALID_HANDLE)
    {
      result = clutter_texture_new ();
      clutter_texture_set_cogl_texture (CLUTTER_TEXTURE (result), texture);
    }
  else
    {
      result = clutter_texture_new ();
      g_object_set (result, "opacity", 0, "width", (float) size, "height", (float) size, NULL);

    }
  return result;
}

const char *
shell_app_get_name (ShellApp *app)
{
  if (app->entry)
    return g_app_info_get_name (G_APP_INFO (gmenu_tree_entry_get_app_info (app->entry)));
  else
    {
      MetaWindow *window = window_backed_app_get_window (app);
      const char *name;

      name = meta_window_get_wm_class (window);
      if (!name)
        name = _("Unknown");
      return name;
    }
}

const char *
shell_app_get_description (ShellApp *app)
{
  if (app->entry)
    return g_app_info_get_description (G_APP_INFO (gmenu_tree_entry_get_app_info (app->entry)));
  else
    return NULL;
}

/**
 * shell_app_is_window_backed:
 *
 * A window backed application is one which represents just an open
 * window, i.e. there's no .desktop file assocation, so we don't know
 * how to launch it again.
 */
gboolean
shell_app_is_window_backed (ShellApp *app)
{
  return app->entry == NULL;
}

typedef struct {
  MetaWorkspace *workspace;
  GSList **transients;
} CollectTransientsData;

static gboolean
collect_transients_on_workspace (MetaWindow *window,
                                 gpointer    datap)
{
  CollectTransientsData *data = datap;

  if (data->workspace && meta_window_get_workspace (window) != data->workspace)
    return TRUE;

  *data->transients = g_slist_prepend (*data->transients, window);
  return TRUE;
}

/* The basic idea here is that when we're targeting a window,
 * if it has transients we want to pick the most recent one
 * the user interacted with.
 * This function makes raising GEdit with the file chooser
 * open work correctly.
 */
static MetaWindow *
find_most_recent_transient_on_same_workspace (MetaDisplay *display,
                                              MetaWindow  *reference)
{
  GSList *transients, *transients_sorted, *iter;
  MetaWindow *result;
  CollectTransientsData data;

  transients = NULL;
  data.workspace = meta_window_get_workspace (reference);
  data.transients = &transients;

  meta_window_foreach_transient (reference, collect_transients_on_workspace, &data);

  transients_sorted = meta_display_sort_windows_by_stacking (display, transients);
  /* Reverse this so we're top-to-bottom (yes, we should probably change the order
   * returned from the sort_windows_by_stacking function)
   */
  transients_sorted = g_slist_reverse (transients_sorted);
  g_slist_free (transients);
  transients = NULL;

  result = NULL;
  for (iter = transients_sorted; iter; iter = iter->next)
    {
      MetaWindow *window = iter->data;
      MetaWindowType wintype = meta_window_get_window_type (window);

      /* Don't want to focus UTILITY types, like the Gimp toolbars */
      if (wintype == META_WINDOW_NORMAL ||
          wintype == META_WINDOW_DIALOG)
        {
          result = window;
          break;
        }
    }
  g_slist_free (transients_sorted);
  return result;
}

/**
 * shell_app_activate_window:
 * @app: a #ShellApp
 * @window: (allow-none): Window to be focused
 * @timestamp: Event timestamp
 *
 * Bring all windows for the given app to the foreground,
 * but ensure that @window is on top.  If @window is %NULL,
 * the window with the most recent user time for the app
 * will be used.
 *
 * This function has no effect if @app is not currently running.
 */
void
shell_app_activate_window (ShellApp     *app,
                           MetaWindow   *window,
                           guint32       timestamp)
{
  GSList *windows;

  if (shell_app_get_state (app) != SHELL_APP_STATE_RUNNING)
    return;

  windows = shell_app_get_windows (app);
  if (window == NULL && windows)
    window = windows->data;

  if (!g_slist_find (windows, window))
    return;
  else
    {
      GSList *iter;
      ShellGlobal *global = shell_global_get ();
      MetaScreen *screen = shell_global_get_screen (global);
      MetaDisplay *display = meta_screen_get_display (screen);
      MetaWorkspace *active = meta_screen_get_active_workspace (screen);
      MetaWorkspace *workspace = meta_window_get_workspace (window);
      guint32 last_user_timestamp = meta_display_get_last_user_time (display);
      MetaWindow *most_recent_transient;

      if (meta_display_xserver_time_is_before (display, timestamp, last_user_timestamp))
        {
          meta_window_set_demands_attention (window);
          return;
        }

      /* Now raise all the other windows for the app that are on
       * the same workspace, in reverse order to preserve the stacking.
       */
      for (iter = windows; iter; iter = iter->next)
        {
          MetaWindow *other_window = iter->data;

          if (other_window != window)
            meta_window_raise (other_window);
        }

      /* If we have a transient that the user's interacted with more recently than
       * the window, pick that.
       */
      most_recent_transient = find_most_recent_transient_on_same_workspace (display, window);
      if (most_recent_transient
          && meta_display_xserver_time_is_before (display,
                                                  meta_window_get_user_time (window),
                                                  meta_window_get_user_time (most_recent_transient)))
        window = most_recent_transient;


      if (active != workspace)
        meta_workspace_activate_with_focus (workspace, window, timestamp);
      else
        meta_window_activate (window, timestamp);
    }
}


void
shell_app_update_window_actions (ShellApp *app, MetaWindow *window)
{
  const char *object_path;

  object_path = meta_window_get_dbus_object_path (window);
  if (object_path != NULL)
    {
      GActionGroup *actions;

      actions = g_object_get_data (G_OBJECT (window), "actions");
      if (actions == NULL)
        {
          actions = G_ACTION_GROUP (g_dbus_action_group_get (g_dbus_proxy_get_connection (app->running_state->app_proxy),
                                                             meta_window_get_dbus_unique_name (window),
                                                             object_path));
          g_object_set_data_full (G_OBJECT (window), "actions", actions, g_object_unref);
        }

      if (!app->running_state->muxer)
        app->running_state->muxer = g_action_muxer_new ();

      g_action_muxer_insert (app->running_state->muxer, "win", actions);
      g_object_notify (G_OBJECT (app), "action-group");
    }
}

/**
 * shell_app_activate:
 * @app: a #ShellApp
 *
 * Like shell_app_activate_full(), but using the default workspace and
 * event timestamp.
 */
void
shell_app_activate (ShellApp      *app)
{
  return shell_app_activate_full (app, -1, 0);
}

/**
 * shell_app_activate_full:
 * @app: a #ShellApp
 * @workspace: launch on this workspace, or -1 for default. Ignored if
 *   activating an existing window
 * @timestamp: Event timestamp
 *
 * Perform an appropriate default action for operating on this application,
 * dependent on its current state.  For example, if the application is not
 * currently running, launch it.  If it is running, activate the most
 * recently used NORMAL window (or if that window has a transient, the most
 * recently used transient for that window).
 */
void
shell_app_activate_full (ShellApp      *app,
                         int            workspace,
                         guint32        timestamp)
{
  ShellGlobal *global;

  global = shell_global_get ();

  if (timestamp == 0)
    timestamp = shell_global_get_current_time (global);

  switch (app->state)
    {
      case SHELL_APP_STATE_STOPPED:
        {
          GError *error = NULL;
          if (!shell_app_launch (app,
                                 timestamp,
                                 NULL,
                                 workspace,
                                 NULL,
                                 &error))
            {
              char *msg;
              msg = g_strdup_printf (_("Failed to launch '%s'"), shell_app_get_name (app));
              shell_global_notify_error (global,
                                         msg,
                                         error->message);
              g_free (msg);
              g_clear_error (&error);
            }
        }
        break;
      case SHELL_APP_STATE_STARTING:
        break;
      case SHELL_APP_STATE_RUNNING:
        shell_app_activate_window (app, NULL, timestamp);
        break;
    }
}

/**
 * shell_app_open_new_window:
 * @app: a #ShellApp
 * @workspace: open on this workspace, or -1 for default
 *
 * Request that the application create a new window.
 */
void
shell_app_open_new_window (ShellApp      *app,
                           int            workspace)
{
  g_return_if_fail (app->entry != NULL);

  /* Here we just always launch the application again, even if we know
   * it was already running.  For most applications this
   * should have the effect of creating a new window, whether that's
   * a second process (in the case of Calculator) or IPC to existing
   * instance (Firefox).  There are a few less-sensical cases such
   * as say Pidgin.  Ideally, we have the application express to us
   * that it supports an explicit new-window action.
   */
  shell_app_launch (app,
                    0,
                    NULL,
                    workspace,
                    NULL,
                    NULL);
}

/**
 * shell_app_get_state:
 * @app: a #ShellApp
 *
 * Returns: State of the application
 */
ShellAppState
shell_app_get_state (ShellApp *app)
{
  return app->state;
}

typedef struct {
  ShellApp *app;
  MetaWorkspace *active_workspace;
} CompareWindowsData;

static int
shell_app_compare_windows (gconstpointer   a,
                           gconstpointer   b,
                           gpointer        datap)
{
  MetaWindow *win_a = (gpointer)a;
  MetaWindow *win_b = (gpointer)b;
  CompareWindowsData *data = datap;
  gboolean ws_a, ws_b;
  gboolean vis_a, vis_b;

  ws_a = meta_window_get_workspace (win_a) == data->active_workspace;
  ws_b = meta_window_get_workspace (win_b) == data->active_workspace;

  if (ws_a && !ws_b)
    return -1;
  else if (!ws_a && ws_b)
    return 1;

  vis_a = meta_window_showing_on_its_workspace (win_a);
  vis_b = meta_window_showing_on_its_workspace (win_b);

  if (vis_a && !vis_b)
    return -1;
  else if (!vis_a && vis_b)
    return 1;

  return meta_window_get_user_time (win_b) - meta_window_get_user_time (win_a);
}

/**
 * shell_app_get_windows:
 * @app:
 *
 * Get the toplevel, interesting windows which are associated with this
 * application.  The returned list will be sorted first by whether
 * they're on the active workspace, then by whether they're visible,
 * and finally by the time the user last interacted with them.
 *
 * Returns: (transfer none) (element-type MetaWindow): List of windows
 */
GSList *
shell_app_get_windows (ShellApp *app)
{
  if (app->running_state == NULL)
    return NULL;

  if (app->running_state->window_sort_stale)
    {
      CompareWindowsData data;
      data.app = app;
      data.active_workspace = meta_screen_get_active_workspace (shell_global_get_screen (shell_global_get ()));
      app->running_state->windows = g_slist_sort_with_data (app->running_state->windows, shell_app_compare_windows, &data);
      app->running_state->window_sort_stale = FALSE;
    }

  return app->running_state->windows;
}

guint
shell_app_get_n_windows (ShellApp *app)
{
  if (app->running_state == NULL)
    return 0;
  return g_slist_length (app->running_state->windows);
}

static gboolean
shell_app_has_visible_windows (ShellApp   *app)
{
  GSList *iter;

  if (app->running_state == NULL)
    return FALSE;

  for (iter = app->running_state->windows; iter; iter = iter->next)
    {
      MetaWindow *window = iter->data;

      if (meta_window_showing_on_its_workspace (window))
        return TRUE;
    }

  return FALSE;
}

gboolean
shell_app_is_on_workspace (ShellApp *app,
                           MetaWorkspace   *workspace)
{
  GSList *iter;

  if (shell_app_get_state (app) == SHELL_APP_STATE_STARTING)
    {
      if (app->started_on_workspace == -1 ||
          meta_workspace_index (workspace) == app->started_on_workspace)
        return TRUE;
      else
        return FALSE;
    }

  if (app->running_state == NULL)
    return FALSE;

  for (iter = app->running_state->windows; iter; iter = iter->next)
    {
      if (meta_window_get_workspace (iter->data) == workspace)
        return TRUE;
    }

  return FALSE;
}

static int
shell_app_get_last_user_time (ShellApp *app)
{
  GSList *iter;
  int last_user_time;

  last_user_time = 0;

  if (app->running_state != NULL)
    {
      for (iter = app->running_state->windows; iter; iter = iter->next)
        last_user_time = MAX (last_user_time, meta_window_get_user_time (iter->data));
    }

  return last_user_time;
}

/**
 * shell_app_compare:
 * @app:
 * @other: A #ShellApp
 *
 * Compare one #ShellApp instance to another, in the following way:
 *   - Running applications sort before not-running applications.
 *   - If one of them has visible windows and the other does not, the one
 *     with visible windows is first.
 *   - Finally, the application which the user interacted with most recently
 *     compares earlier.
 */
int
shell_app_compare (ShellApp *app,
                   ShellApp *other)
{
  gboolean vis_app, vis_other;

  if (app->state != other->state)
    {
      if (app->state == SHELL_APP_STATE_RUNNING)
        return -1;
      return 1;
    }

  vis_app = shell_app_has_visible_windows (app);
  vis_other = shell_app_has_visible_windows (other);

  if (vis_app && !vis_other)
    return -1;
  else if (!vis_app && vis_other)
    return 1;

  if (app->state == SHELL_APP_STATE_RUNNING)
    {
      if (app->running_state->windows && !other->running_state->windows)
        return -1;
      else if (!app->running_state->windows && other->running_state->windows)
        return 1;

      return shell_app_get_last_user_time (other) - shell_app_get_last_user_time (app);
    }

  return 0;
}

ShellApp *
_shell_app_new_for_window (MetaWindow      *window)
{
  ShellApp *app;

  app = g_object_new (SHELL_TYPE_APP, NULL);

  app->window_id_string = g_strdup_printf ("window:%d", meta_window_get_stable_sequence (window));

  _shell_app_add_window (app, window);

  return app;
}

ShellApp *
_shell_app_new (GMenuTreeEntry *info)
{
  ShellApp *app;

  app = g_object_new (SHELL_TYPE_APP, NULL);

  _shell_app_set_entry (app, info);

  return app;
}

void
_shell_app_set_entry (ShellApp       *app,
                      GMenuTreeEntry *entry)
{
  if (app->entry != NULL)
    gmenu_tree_item_unref (app->entry);
  app->entry = gmenu_tree_item_ref (entry);
  
  if (app->name_collation_key != NULL)
    g_free (app->name_collation_key);
  app->name_collation_key = g_utf8_collate_key (shell_app_get_name (app), -1);
}

static void
shell_app_state_transition (ShellApp      *app,
                            ShellAppState  state)
{
  if (app->state == state)
    return;
  g_return_if_fail (!(app->state == SHELL_APP_STATE_RUNNING &&
                      state == SHELL_APP_STATE_STARTING));
  app->state = state;

  if (app->state == SHELL_APP_STATE_STOPPED && app->running_state)
    {
      unref_running_state (app->running_state);
      app->running_state = NULL;
    }

  _shell_app_system_notify_app_state_changed (shell_app_system_get_default (), app);

  g_object_notify (G_OBJECT (app), "state");
}

static void
shell_app_on_unmanaged (MetaWindow      *window,
                        ShellApp *app)
{
  _shell_app_remove_window (app, window);
}

static void
shell_app_on_user_time_changed (MetaWindow *window,
                                GParamSpec *pspec,
                                ShellApp   *app)
{
  g_assert (app->running_state != NULL);

  /* Ideally we don't want to emit windows-changed if the sort order
   * isn't actually changing. This check catches most of those.
   */
  if (window != app->running_state->windows->data)
    {
      app->running_state->window_sort_stale = TRUE;
      g_signal_emit (app, shell_app_signals[WINDOWS_CHANGED], 0);
    }
}

static void
shell_app_on_ws_switch (MetaScreen         *screen,
                        int                 from,
                        int                 to,
                        MetaMotionDirection direction,
                        gpointer            data)
{
  ShellApp *app = SHELL_APP (data);

  g_assert (app->running_state != NULL);

  app->running_state->window_sort_stale = TRUE;

  g_signal_emit (app, shell_app_signals[WINDOWS_CHANGED], 0);
}

static void
on_dbus_application_id_changed (MetaWindow   *window,
                                GParamSpec   *pspec,
                                gpointer      user_data)
{
  const char *appid;
  ShellApp *app = SHELL_APP (user_data);

  /* Ignore changes in the appid after it's set, shouldn't happen */
  if (app->running_state->dbus_name != NULL)
    return;

  appid = meta_window_get_dbus_application_id (window);

  if (!appid)
    return;

  g_assert (app->running_state != NULL);
  app->running_state->dbus_name = g_strdup (appid);
  app->running_state->name_watcher_id = g_bus_watch_name (G_BUS_TYPE_SESSION,
                                                          appid,
                                                          G_BUS_NAME_WATCHER_FLAGS_NONE,
                                                          on_dbus_name_appeared,
                                                          on_dbus_name_disappeared,
                                                          g_object_ref (app),
                                                          g_object_unref);
}

void
_shell_app_add_window (ShellApp        *app,
                       MetaWindow      *window)
{
  if (app->running_state && g_slist_find (app->running_state->windows, window))
    return;

  g_object_freeze_notify (G_OBJECT (app));

  if (!app->running_state)
      create_running_state (app);

  app->running_state->window_sort_stale = TRUE;
  app->running_state->windows = g_slist_prepend (app->running_state->windows, g_object_ref (window));
  g_signal_connect (window, "unmanaged", G_CALLBACK(shell_app_on_unmanaged), app);
  g_signal_connect (window, "notify::user-time", G_CALLBACK(shell_app_on_user_time_changed), app);

  if (app->state != SHELL_APP_STATE_STARTING)
    shell_app_state_transition (app, SHELL_APP_STATE_RUNNING);

  g_signal_connect (window, "notify::dbus-application-id", G_CALLBACK(on_dbus_application_id_changed), app);
  on_dbus_application_id_changed (window, NULL, app);

  g_object_thaw_notify (G_OBJECT (app));

  g_signal_emit (app, shell_app_signals[WINDOWS_CHANGED], 0);
}

void
_shell_app_remove_window (ShellApp   *app,
                          MetaWindow *window)
{
  g_assert (app->running_state != NULL);

  if (!g_slist_find (app->running_state->windows, window))
    return;

  g_signal_handlers_disconnect_by_func (window, G_CALLBACK(shell_app_on_unmanaged), app);
  g_signal_handlers_disconnect_by_func (window, G_CALLBACK(shell_app_on_user_time_changed), app);
  g_signal_handlers_disconnect_by_func (window, G_CALLBACK(on_dbus_application_id_changed), app);
  g_object_unref (window);
  app->running_state->windows = g_slist_remove (app->running_state->windows, window);

  if (app->running_state->windows == NULL)
    shell_app_state_transition (app, SHELL_APP_STATE_STOPPED);

  g_signal_emit (app, shell_app_signals[WINDOWS_CHANGED], 0);
}

static void
on_dbus_proxy_gotten (GObject      *initable,
                      GAsyncResult *result,
                      gpointer      user_data)
{
  ShellApp *self = SHELL_APP (user_data);
  ShellAppRunningState *state = self->running_state;
  GError *error = NULL;
  GVariant *menu_property;

  state->app_proxy = g_dbus_proxy_new_finish (result,
                                              &error);

  if (error)
    {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED) &&
          !g_error_matches (error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD))
        {
          g_warning ("Unexpected error while creating application proxy: %s", error->message);
        }

      g_clear_error (&error);
      g_clear_object (&state->dbus_cancellable);

      if (state->name_watcher_id)
        {
          g_bus_unwatch_name (state->name_watcher_id);
          state->name_watcher_id = 0;
        }

      g_free (state->dbus_name);
      state->dbus_name = NULL;

      g_object_unref (self);
      return;
    }

  /* on to the second step, the primary action group */

  state->remote_actions = (GActionGroup*)g_dbus_action_group_get (
                           g_dbus_proxy_get_connection (state->app_proxy),
                           g_dbus_proxy_get_name (state->app_proxy),
                           g_dbus_proxy_get_object_path (state->app_proxy));

  if (!state->muxer)
    state->muxer = g_action_muxer_new ();

  g_action_muxer_insert (state->muxer, "app", state->remote_actions);
  g_strfreev (g_action_group_list_actions (state->remote_actions));

  g_object_notify (G_OBJECT (self), "action-group");

  menu_property = g_dbus_proxy_get_cached_property (state->app_proxy, "AppMenu");

  if (menu_property && g_variant_n_children (menu_property) > 0)
    {
      const gchar *object_path;

      g_variant_get_child (menu_property, 0, "&o", &object_path);

      state->remote_menu = G_MENU_MODEL (g_dbus_menu_model_get (g_dbus_proxy_get_connection (state->app_proxy),
                                                                state->dbus_name,
                                                                object_path));

      g_object_notify (G_OBJECT (self), "menu");
    }
}

static void
on_dbus_name_appeared (GDBusConnection *bus,
                       const gchar     *name,
                       const gchar     *name_owner,
                       gpointer         user_data)
{
  ShellApp *self = SHELL_APP (user_data);
  ShellAppRunningState *state = self->running_state;
  char *object_path;

  g_assert (state != NULL);

  object_path = g_strconcat ("/", name, NULL);
  g_strdelimit (object_path, ".", '/');

  if (!state->dbus_cancellable)
    state->dbus_cancellable = g_cancellable_new ();

  /* first step: the application proxy */

  g_dbus_proxy_new (bus,
                    G_DBUS_PROXY_FLAGS_NONE,
                    NULL, /* interface info */
                    name_owner,
                    object_path,
                    "org.gtk.Application",
                    state->dbus_cancellable,
                    on_dbus_proxy_gotten,
                    g_object_ref (self));

  g_object_notify (G_OBJECT (self), "dbus-id");

  g_free (object_path);
}

static void
on_dbus_name_disappeared (GDBusConnection *bus,
                          const gchar     *name,
                          gpointer         user_data)
{
  ShellApp *self = SHELL_APP (user_data);
  ShellAppRunningState *state = self->running_state;

  g_assert (state != NULL);

  if (state->dbus_cancellable)
    {
      g_cancellable_cancel (state->dbus_cancellable);
      g_clear_object (&state->dbus_cancellable);
    }

  g_clear_object (&state->app_proxy);
  g_clear_object (&state->remote_actions);
  g_clear_object (&state->remote_menu);
  g_clear_object (&state->muxer);

  g_free (state->dbus_name);
  state->dbus_name = NULL;

  g_bus_unwatch_name (state->name_watcher_id);
  state->name_watcher_id = 0;
}

/**
 * shell_app_get_pids:
 * @app: a #ShellApp
 *
 * Returns: (transfer container) (element-type int): An unordered list of process identifers associated with this application.
 */
GSList *
shell_app_get_pids (ShellApp *app)
{
  GSList *result;
  GSList *iter;

  result = NULL;
  for (iter = shell_app_get_windows (app); iter; iter = iter->next)
    {
      MetaWindow *window = iter->data;
      int pid = meta_window_get_pid (window);
      /* Note in the (by far) common case, app will only have one pid, so
       * we'll hit the first element, so don't worry about O(N^2) here.
       */
      if (!g_slist_find (result, GINT_TO_POINTER (pid)))
        result = g_slist_prepend (result, GINT_TO_POINTER (pid));
    }
  return result;
}

void
_shell_app_handle_startup_sequence (ShellApp          *app,
                                    SnStartupSequence *sequence)
{
  gboolean starting = !sn_startup_sequence_get_completed (sequence);

  /* The Shell design calls for on application launch, the app title
   * appears at top, and no X window is focused.  So when we get
   * a startup-notification for this app, transition it to STARTING
   * if it's currently stopped, set it as our application focus,
   * but focus the no_focus window.
   */
  if (starting && shell_app_get_state (app) == SHELL_APP_STATE_STOPPED)
    {
      MetaScreen *screen = shell_global_get_screen (shell_global_get ());
      MetaDisplay *display = meta_screen_get_display (screen);

      shell_app_state_transition (app, SHELL_APP_STATE_STARTING);
      meta_display_focus_the_no_focus_window (display, screen,
                                              sn_startup_sequence_get_timestamp (sequence));
      app->started_on_workspace = sn_startup_sequence_get_workspace (sequence);
    }

  if (!starting)
    {
      if (app->running_state && app->running_state->windows)
        shell_app_state_transition (app, SHELL_APP_STATE_RUNNING);
      else /* application have > 1 .desktop file */
        shell_app_state_transition (app, SHELL_APP_STATE_STOPPED);
    }
}

/**
 * shell_app_request_quit:
 * @app: A #ShellApp
 *
 * Initiate an asynchronous request to quit this application.
 * The application may interact with the user, and the user
 * might cancel the quit request from the application UI.
 *
 * This operation may not be supported for all applications.
 *
 * Returns: %TRUE if a quit request is supported for this application
 */
gboolean
shell_app_request_quit (ShellApp   *app)
{
  GSList *iter;

  if (shell_app_get_state (app) != SHELL_APP_STATE_RUNNING)
    return FALSE;

  /* TODO - check for an XSMP connection; we could probably use that */

  for (iter = app->running_state->windows; iter; iter = iter->next)
    {
      MetaWindow *win = iter->data;

      if (!shell_window_tracker_is_window_interesting (win))
        continue;

      meta_window_delete (win, shell_global_get_current_time (shell_global_get ()));
    }
  return TRUE;
}

static void
_gather_pid_callback (GDesktopAppInfo   *gapp,
                      GPid               pid,
                      gpointer           data)
{
  ShellApp *app;
  ShellWindowTracker *tracker;

  g_return_if_fail (data != NULL);

  app = SHELL_APP (data);
  tracker = shell_window_tracker_get_default ();

  _shell_window_tracker_add_child_process_app (tracker,
                                               pid,
                                               app);
}

/**
 * shell_app_launch:
 * @timestamp: Event timestamp, or 0 for current event timestamp
 * @uris: (element-type utf8): List of uris to pass to application
 * @workspace: Start on this workspace, or -1 for default
 * @startup_id: (out): Returned startup notification ID, or %NULL if none
 * @error: A #GError
 */
gboolean
shell_app_launch (ShellApp     *app,
                  guint         timestamp,
                  GList        *uris,
                  int           workspace,
                  char        **startup_id,
                  GError      **error)
{
  GDesktopAppInfo *gapp;
  GdkAppLaunchContext *context;
  gboolean ret;
  ShellGlobal *global;
  MetaScreen *screen;
  GdkDisplay *gdisplay;

  if (startup_id)
    *startup_id = NULL;

  if (app->entry == NULL)
    {
      MetaWindow *window = window_backed_app_get_window (app);
      /* We can't pass URIs into a window; shouldn't hit this
       * code path.  If we do, fix the caller to disallow it.
       */
      g_return_val_if_fail (uris == NULL, TRUE);

      meta_window_activate (window, timestamp);
      return TRUE;
    }

  global = shell_global_get ();
  screen = shell_global_get_screen (global);
  gdisplay = gdk_screen_get_display (shell_global_get_gdk_screen (global));

  if (timestamp == 0)
    timestamp = shell_global_get_current_time (global);

  if (workspace < 0)
    workspace = meta_screen_get_active_workspace_index (screen);

  context = gdk_display_get_app_launch_context (gdisplay);
  gdk_app_launch_context_set_timestamp (context, timestamp);
  gdk_app_launch_context_set_desktop (context, workspace);

  gapp = gmenu_tree_entry_get_app_info (app->entry);
  ret = g_desktop_app_info_launch_uris_as_manager (gapp, uris,
                                                   G_APP_LAUNCH_CONTEXT (context),
                                                   G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
                                                   NULL, NULL,
                                                   _gather_pid_callback, app,
                                                   error);
  g_object_unref (context);

  return ret;
}

/**
 * shell_app_get_app_info:
 * @app: a #ShellApp
 *
 * Returns: (transfer none): The #GDesktopAppInfo for this app, or %NULL if backed by a window
 */
GDesktopAppInfo *
shell_app_get_app_info (ShellApp *app)
{
  if (app->entry)
    return gmenu_tree_entry_get_app_info (app->entry);
  return NULL;
}

/**
 * shell_app_get_tree_entry:
 * @app: a #ShellApp
 *
 * Returns: (transfer none): The #GMenuTreeEntry for this app, or %NULL if backed by a window
 */
GMenuTreeEntry *
shell_app_get_tree_entry (ShellApp *app)
{
  return app->entry;
}

static void
create_running_state (ShellApp *app)
{
  MetaScreen *screen;

  g_assert (app->running_state == NULL);

  screen = shell_global_get_screen (shell_global_get ());
  app->running_state = g_slice_new0 (ShellAppRunningState);
  app->running_state->refcount = 1;
  app->running_state->workspace_switch_id =
    g_signal_connect (screen, "workspace-switched", G_CALLBACK(shell_app_on_ws_switch), app);
}

static void
unref_running_state (ShellAppRunningState *state)
{
  MetaScreen *screen;

  g_assert (state->refcount > 0);

  state->refcount--;
  if (state->refcount > 0)
    return;

  screen = shell_global_get_screen (shell_global_get ());
  g_signal_handler_disconnect (screen, state->workspace_switch_id);

  if (state->dbus_cancellable)
    {
      g_cancellable_cancel (state->dbus_cancellable);
      g_object_unref (state->dbus_cancellable);
    }

  g_clear_object (&state->app_proxy);
  g_clear_object (&state->remote_actions);
  g_clear_object (&state->remote_menu);
  g_clear_object (&state->muxer);
  g_free (state->dbus_name);

  if (state->name_watcher_id)
    g_bus_unwatch_name (state->name_watcher_id);

  g_slice_free (ShellAppRunningState, state);
}

static char *
trim_exec_line (const char *str)
{
  const char *start, *end, *pos;

  if (str == NULL)
    return NULL;

  end = strchr (str, ' ');
  if (end == NULL)
    end = str + strlen (str);

  start = str;
  while ((pos = strchr (start, '/')) && pos < end)
    start = ++pos;

  return g_strndup (start, end - start);
}

static void
shell_app_init_search_data (ShellApp *app)
{
  const char *name;
  const char *exec;
  const char *comment;
  char *normalized_exec;
  GDesktopAppInfo *appinfo;

  appinfo = gmenu_tree_entry_get_app_info (app->entry);
  name = g_app_info_get_name (G_APP_INFO (appinfo));
  app->casefolded_name = shell_util_normalize_and_casefold (name);

  comment = g_app_info_get_description (G_APP_INFO (appinfo));
  app->casefolded_description = shell_util_normalize_and_casefold (comment);

  exec = g_app_info_get_executable (G_APP_INFO (appinfo));
  normalized_exec = shell_util_normalize_and_casefold (exec);
  app->casefolded_exec = trim_exec_line (normalized_exec);
  g_free (normalized_exec);
}

/**
 * shell_app_compare_by_name:
 * @app: One app
 * @other: The other app
 *
 * Order two applications by name.
 *
 * Returns: -1, 0, or 1; suitable for use as a comparison function
 * for e.g. g_slist_sort()
 */
int
shell_app_compare_by_name (ShellApp *app, ShellApp *other)
{
  return strcmp (app->name_collation_key, other->name_collation_key);
}

static ShellAppSearchMatch
_shell_app_match_search_terms (ShellApp  *app,
                               GSList    *terms)
{
  GSList *iter;
  ShellAppSearchMatch match;

  if (G_UNLIKELY (!app->casefolded_name))
    shell_app_init_search_data (app);

  match = MATCH_NONE;
  for (iter = terms; iter; iter = iter->next)
    {
      ShellAppSearchMatch current_match;
      const char *term = iter->data;
      const char *p;

      current_match = MATCH_NONE;

      p = strstr (app->casefolded_name, term);
      if (p != NULL)
        {
          if (p == app->casefolded_name || *(p - 1) == ' ')
            current_match = MATCH_PREFIX;
          else
            current_match = MATCH_SUBSTRING;
        }

      if (app->casefolded_exec)
        {
          p = strstr (app->casefolded_exec, term);
          if (p != NULL)
            {
              if (p == app->casefolded_exec || *(p - 1) == '-')
                current_match = MATCH_PREFIX;
              else if (current_match < MATCH_PREFIX)
                current_match = MATCH_SUBSTRING;
            }
        }

      if (app->casefolded_description && current_match < MATCH_PREFIX)
        {
          /* Only do substring matches, as prefix matches are not meaningful
           * enough for descriptions
           */
          p = strstr (app->casefolded_description, term);
          if (p != NULL)
            current_match = MATCH_SUBSTRING;
        }

      if (current_match == MATCH_NONE)
        return current_match;

      if (current_match > match)
        match = current_match;
    }
  return match;
}

void
_shell_app_do_match (ShellApp         *app,
                     GSList           *terms,
                     GSList          **prefix_results,
                     GSList          **substring_results)
{
  ShellAppSearchMatch match;
  GAppInfo *appinfo;

  g_assert (app != NULL);

  /* Skip window-backed apps */ 
  appinfo = (GAppInfo*)shell_app_get_app_info (app);
  if (appinfo == NULL)
    return;
  /* Skip not-visible apps */ 
  if (!g_app_info_should_show (appinfo))
    return;

  match = _shell_app_match_search_terms (app, terms);
  switch (match)
    {
      case MATCH_NONE:
        break;
      case MATCH_PREFIX:
        *prefix_results = g_slist_prepend (*prefix_results, app);
        break;
      case MATCH_SUBSTRING:
        *substring_results = g_slist_prepend (*substring_results, app);
        break;
    }
}


static void
shell_app_init (ShellApp *self)
{
  self->state = SHELL_APP_STATE_STOPPED;
}

static void
shell_app_dispose (GObject *object)
{
  ShellApp *app = SHELL_APP (object);

  if (app->entry)
    {
      gmenu_tree_item_unref (app->entry);
      app->entry = NULL;
    }

  if (app->running_state)
    {
      while (app->running_state->windows)
        _shell_app_remove_window (app, app->running_state->windows->data);
    }
  /* We should have been transitioned when we removed all of our windows */
  g_assert (app->state == SHELL_APP_STATE_STOPPED);
  g_assert (app->running_state == NULL);

  G_OBJECT_CLASS(shell_app_parent_class)->dispose (object);
}

static void
shell_app_finalize (GObject *object)
{
  ShellApp *app = SHELL_APP (object);

  g_free (app->window_id_string);

  g_free (app->casefolded_name);
  g_free (app->name_collation_key);
  g_free (app->casefolded_description);
  g_free (app->casefolded_exec);

  G_OBJECT_CLASS(shell_app_parent_class)->finalize (object);
}

static void
shell_app_class_init(ShellAppClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->get_property = shell_app_get_property;
  gobject_class->dispose = shell_app_dispose;
  gobject_class->finalize = shell_app_finalize;

  shell_app_signals[WINDOWS_CHANGED] = g_signal_new ("windows-changed",
                                     SHELL_TYPE_APP,
                                     G_SIGNAL_RUN_LAST,
                                     0,
                                     NULL, NULL, NULL,
                                     G_TYPE_NONE, 0);

  /**
   * ShellApp:state:
   *
   * The high-level state of the application, effectively whether it's
   * running or not, or transitioning between those states.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_STATE,
                                   g_param_spec_enum ("state",
                                                      "State",
                                                      "Application state",
                                                      SHELL_TYPE_APP_STATE,
                                                      SHELL_APP_STATE_STOPPED,
                                                      G_PARAM_READABLE));

  /**
   * ShellApp:id:
   *
   * The id of this application (a desktop filename, or a special string
   * like window:0xabcd1234)
   */
  g_object_class_install_property (gobject_class,
                                   PROP_ID,
                                   g_param_spec_string ("id",
                                                        "Application id",
                                                        "The desktop file id of this ShellApp",
                                                        NULL,
                                                        G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  /**
   * ShellApp:dbus-id:
   *
   * The DBus well-known name of the application, if one can be associated
   * to this ShellApp (it means that the application is using GApplication)
   */
  g_object_class_install_property (gobject_class,
                                   PROP_DBUS_ID,
                                   g_param_spec_string ("dbus-id",
                                                        "Application DBus Id",
                                                        "The DBus well-known name of the application",
                                                        NULL,
                                                        G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  /**
   * ShellApp:action-group:
   *
   * The #GDBusActionGroup associated with this ShellApp, if any. See the
   * documentation of #GApplication and #GActionGroup for details.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_ACTION_GROUP,
                                   g_param_spec_object ("action-group",
                                                        "Application Action Group",
                                                        "The action group exported by the remote application",
                                                        G_TYPE_ACTION_GROUP,
                                                        G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  /**
   * ShellApp:menu:
   *
   * The #GMenuProxy associated with this ShellApp, if any. See the
   * documentation of #GMenuModel for details.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_MENU,
                                   g_param_spec_object ("menu",
                                                        "Application Menu",
                                                        "The primary menu exported by the remote application",
                                                        G_TYPE_MENU_MODEL,
                                                        G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

}
