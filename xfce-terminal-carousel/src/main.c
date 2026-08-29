/*-
 * Copyright (c) 2025 xfce4-terminal-carousel contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.
 *
 * A standalone companion for xfce4-terminal (>= the Control bridge):
 * when the computer has been idle for a while, rotate through the
 * terminal tabs that have active work, with a status strip showing
 * blinking green lights for tabs waiting for input.
 */

#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <xfconf/xfconf.h>
#include <X11/extensions/scrnsaver.h>

#include <config.h>

#define POLL_INTERVAL_MS 2000
#define BLINK_INTERVAL_MS 450
#define STRIP_HEIGHT 30
#define DOT_SPACING 26
#define DOT_RADIUS 5.0
#define STRIP_BASE_WIDTH 220
#define STRIP_TITLE_MAX_CHARS 48
#define ACTIVATION_GRACE_MS 2500

/* Terminal Control bridge */
#define CONTROL_MANAGER_IFACE "org.xfce.Terminal.Control.Manager"
#define CONTROL_WINDOW_IFACE "org.xfce.Terminal.Control.Window"
#define CONTROL_TAB_IFACE "org.xfce.Terminal.Control.Tab"
#define CONTROL_PATH "/org/xfce/Terminal"
#define CONTROL_SERVICE_FMT "org.xfce.Terminal%d"
#define CONTROL_SERVICE_MIN 4
#define CONTROL_SERVICE_MAX 9

/* xfconf configuration */
#define CONF_CHANNEL "xfce4-terminal-carousel"
#define CONF_ENABLED "/enabled"            /* bool,   default TRUE  */
#define CONF_TIMEOUT "/timeout"            /* uint s, default 60    */
#define CONF_SLIDE_INTERVAL "/slide-interval" /* uint s, default 6  */
#define CONF_ACTIVITY_WINDOW "/activity-window" /* uint min, default 10 */

typedef struct
{
  gchar *path;
  gchar *window_path;
  gchar *title;
  gboolean attention;
  gboolean foreground;
  gint64 last_output; /* CLOCK_MONOTONIC usec, 0 = none */
} TabState;

typedef struct
{
  GHashTable *tabs;        /* path -> TabState* */
  GHashTable *win_proxies; /* window path -> GDBusProxy* */
  GHashTable *tab_proxies; /* tab path -> GDBusProxy* */
  gchar *manager_service;  /* resolved service name, NULL if terminal absent */

  GDBusProxy *manager;
  guint poll_id;

  /* carousel runtime state */
  gboolean running;
  GPtrArray *slides; /* TabState* */
  guint current;
  guint slide_id;
  guint blink_id;
  gint64 blink_start;
  gint64 last_switch;

  GtkWidget *strip;
  GtkWidget *strip_area;
} Carousel;

static Carousel carousel = { NULL, };
static XfconfChannel *conf_channel = NULL;



/* ------------------------------------------------------------------ */
/* small helpers                                                       */
/* ------------------------------------------------------------------ */

static void
tab_state_free (TabState *tab)
{
  g_free (tab->path);
  g_free (tab->window_path);
  g_free (tab->title);
  g_free (tab);
}



static gboolean
conf_get_bool (const gchar *key,
               gboolean fallback)
{
  if (conf_channel == NULL)
    return fallback;
  return xfconf_channel_get_bool (conf_channel, key, fallback);
}



static guint
conf_get_uint (const gchar *key,
               guint fallback)
{
  if (conf_channel == NULL)
    return fallback;
  return xfconf_channel_get_uint (conf_channel, key, fallback);
}



static gint64
query_idle_ms (void)
{
  static XScreenSaverInfo *info = NULL;
  Display *dpy;

  dpy = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());
  if (dpy == NULL)
    return -1;

  if (info == NULL)
    {
      info = XScreenSaverAllocInfo ();
      if (info == NULL)
        return -1;
    }

  if (XScreenSaverQueryInfo (dpy, DefaultRootWindow (dpy), info) == 0)
    return -1;

  return (gint64) info->idle;
}



/* ------------------------------------------------------------------ */
/* bridge client                                                       */
/* ------------------------------------------------------------------ */

static GDBusProxy *
ensure_proxy (GHashTable *table,
              const gchar *path,
              const gchar *iface,
              const gchar *service)
{
  GDBusProxy *proxy;

  proxy = g_hash_table_lookup (table, path);
  if (proxy != NULL)
    return proxy;

  proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                         G_DBUS_PROXY_FLAGS_NONE,
                                         NULL, /* introspected on demand */
                                         service,
                                         path,
                                         iface,
                                         NULL, NULL);
  if (proxy == NULL)
    return NULL;

  g_hash_table_insert (table, g_strdup (path), proxy);
  return proxy;
}



static gchar *
proxy_get_str (GDBusProxy *proxy,
               const gchar *prop)
{
  GVariant *v;
  gchar *str;

  v = g_dbus_proxy_get_cached_property (proxy, prop);
  if (v == NULL)
    return NULL;
  str = g_variant_dup_string (v, NULL);
  g_variant_unref (v);
  return str;
}



static gboolean
proxy_get_bool (GDBusProxy *proxy,
                const gchar *prop)
{
  GVariant *v = g_dbus_proxy_get_cached_property (proxy, prop);
  gboolean b = v != NULL && g_variant_get_boolean (v);
  if (v != NULL)
    g_variant_unref (v);
  return b;
}



static gint64
proxy_get_int64 (GDBusProxy *proxy,
                 const gchar *prop)
{
  GVariant *v = g_dbus_proxy_get_cached_property (proxy, prop);
  gint64 x = v != NULL ? g_variant_get_int64 (v) : 0;
  if (v != NULL)
    g_variant_unref (v);
  return x;
}



static void
resolve_service (void)
{
  gint version;

  if (carousel.manager_service != NULL)
    return;

  for (version = CONTROL_SERVICE_MAX; version >= CONTROL_SERVICE_MIN; --version)
    {
      gchar *name = g_strdup_printf (CONTROL_SERVICE_FMT, version);
      GDBusProxy *probe = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                                         G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES
                                                         | G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
                                                         NULL, name,
                                                         CONTROL_PATH, CONTROL_MANAGER_IFACE,
                                                         NULL, NULL);
      if (probe != NULL)
        {
          GError *error = NULL;
          g_dbus_proxy_call_sync (probe, "ListWindows",
                                  g_variant_new ("()"), G_DBUS_CALL_FLAGS_NO_AUTO_START,
                                  500, NULL, &error);
          g_object_unref (probe);
          if (error == NULL)
            {
              carousel.manager_service = name;
              carousel.manager = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                                                G_DBUS_PROXY_FLAGS_NONE,
                                                                NULL, name,
                                                                CONTROL_PATH, CONTROL_MANAGER_IFACE,
                                                                NULL, NULL);
              g_debug ("found terminal control bridge on %s", name);
              return;
            }
          g_error_free (error);
        }
      g_free (name);
    }
}



static void
drop_stale (GHashTable *table,
            GHashTable *seen)
{
  GHashTableIter it;
  gpointer key;
  GPtrArray *stale = g_ptr_array_new_with_free_func (g_free);

  g_hash_table_iter_init (&it, table);
  while (g_hash_table_iter_next (&it, &key, NULL))
    if (!g_hash_table_contains (seen, key))
      g_ptr_array_add (stale, g_strdup (key));

  for (guint i = 0; i < stale->len; ++i)
    g_hash_table_remove (table, g_ptr_array_index (stale, i));

  g_ptr_array_unref (stale);
}



static gboolean
bridge_refresh (void)
{
  GVariant *reply;
  GVariant *array;
  GVariantIter witer;
  const gchar *win_path;
  GHashTable *seen_windows;
  GHashTable *seen_tabs;

  if (carousel.manager == NULL)
    return FALSE;

  reply = g_dbus_proxy_call_sync (carousel.manager, "ListWindows",
                                  g_variant_new ("()"), G_DBUS_CALL_FLAGS_NO_AUTO_START,
                                  1000, NULL, NULL);
  if (reply == NULL)
    {
      /* terminal went away; drop everything and re-resolve later */
      g_hash_table_remove_all (carousel.tabs);
      g_hash_table_remove_all (carousel.win_proxies);
      g_hash_table_remove_all (carousel.tab_proxies);
      g_clear_object (&carousel.manager);
      g_clear_pointer (&carousel.manager_service, g_free);
      return FALSE;
    }

  seen_windows = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  seen_tabs = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  array = g_variant_get_child_value (reply, 0);
  g_variant_iter_init (&witer, array);
  while (g_variant_iter_loop (&witer, "&o", &win_path))
    {
      GDBusProxy *wproxy;
      GVariant *tabs_var;
      GVariantIter titer;
      const gchar *tab_path;

      g_hash_table_add (seen_windows, g_strdup (win_path));

      wproxy = ensure_proxy (carousel.win_proxies, win_path, CONTROL_WINDOW_IFACE,
                             carousel.manager_service);
      if (wproxy == NULL)
        continue;

      if (proxy_get_bool (wproxy, "Dropdown"))
        continue;

      tabs_var = g_dbus_proxy_get_cached_property (wproxy, "Tabs");
      if (tabs_var == NULL)
        continue;

      g_variant_iter_init (&titer, tabs_var);
      while (g_variant_iter_loop (&titer, "&o", &tab_path))
        {
          GDBusProxy *tproxy;
          TabState *tab;

          tproxy = ensure_proxy (carousel.tab_proxies, tab_path, CONTROL_TAB_IFACE,
                                 carousel.manager_service);
          if (tproxy == NULL)
            continue;

          g_hash_table_add (seen_tabs, g_strdup (tab_path));

          tab = g_hash_table_lookup (carousel.tabs, tab_path);
          if (tab == NULL)
            {
              tab = g_new0 (TabState, 1);
              tab->path = g_strdup (tab_path);
              tab->window_path = g_strdup (win_path);
              g_hash_table_insert (carousel.tabs, tab->path, tab);
            }

          g_free (tab->title);
          tab->title = proxy_get_str (tproxy, "Title");
          tab->attention = proxy_get_bool (tproxy, "NeedsAttention");
          tab->foreground = proxy_get_bool (tproxy, "HasForegroundProcess");
          tab->last_output = proxy_get_int64 (tproxy, "LastOutput");
        }
      g_variant_unref (tabs_var);
    }
  g_variant_unref (array);
  g_variant_unref (reply);

  drop_stale (carousel.tabs, seen_tabs);
  drop_stale (carousel.tab_proxies, seen_tabs);
  drop_stale (carousel.win_proxies, seen_windows);

  g_hash_table_unref (seen_windows);
  g_hash_table_unref (seen_tabs);
  return TRUE;
}



/* ------------------------------------------------------------------ */
/* strip UI                                                            */
/* ------------------------------------------------------------------ */

static gboolean
strip_draw (GtkWidget *widget,
            cairo_t *cr,
            gpointer data)
{
  Carousel *c = data;
  GtkAllocation alloc;
  gdouble blink_alpha, first_dot_x;
  guint n;

  gtk_widget_get_allocation (widget, &alloc);

  cairo_set_source_rgba (cr, 0.08, 0.08, 0.10, 0.92);
  cairo_paint (cr);

  {
    gdouble phase = (g_get_monotonic_time () - c->blink_start)
                    % (2 * BLINK_INTERVAL_MS * 1000)
                    / (gdouble) (2 * BLINK_INTERVAL_MS * 1000);
    blink_alpha = phase < 0.5 ? phase * 2.0 : (1.0 - phase) * 2.0;
  }

  if (c->current < c->slides->len)
    {
      TabState *tab = g_ptr_array_index (c->slides, c->current);
      PangoLayout *layout = gtk_widget_create_pango_layout (widget, NULL);
      PangoAttrList *attrs = pango_attr_list_new ();
      pango_layout_set_ellipsize (layout, PANGO_ELLIPSIZE_END);
      pango_layout_set_width (layout, STRIP_TITLE_MAX_CHARS * PANGO_SCALE);
      pango_layout_set_text (layout, tab->title != NULL ? tab->title : "", -1);
      pango_attr_list_insert (attrs, pango_attr_scale_new (0.85));
      pango_attr_list_insert (attrs, pango_attr_foreground_new (57000, 57000, 57000));
      pango_layout_set_attributes (layout, attrs);
      pango_attr_list_unref (attrs);
      gtk_render_layout (gtk_widget_get_style_context (widget), cr,
                         8.0, (alloc.height - 14) / 2.0, layout);
      g_object_unref (layout);
    }

  first_dot_x = alloc.width - (c->slides->len * DOT_SPACING - (DOT_SPACING - 2 * DOT_RADIUS)) / 2.0;
  for (n = 0; n < c->slides->len; ++n)
    {
      TabState *tab = g_ptr_array_index (c->slides, n);
      gdouble cx = first_dot_x + n * DOT_SPACING;

      if (tab->attention)
        cairo_set_source_rgba (cr, 0.20, 0.95, 0.35, 0.25 + 0.75 * blink_alpha);
      else
        cairo_set_source_rgba (cr, 0.35, 0.55, 0.40, 0.65);
      cairo_arc (cr, cx, alloc.height / 2.0, DOT_RADIUS, 0.0, 2.0 * G_PI);
      cairo_fill (cr);

      if (n == c->current)
        {
          cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, 0.85);
          cairo_set_line_width (cr, 1.2);
          cairo_arc (cr, cx, alloc.height / 2.0, DOT_RADIUS + 2.5, 0.0, 2.0 * G_PI);
          cairo_stroke (cr);
        }
    }

  return FALSE;
}



static gboolean
strip_blink (gpointer data)
{
  Carousel *c = data;
  if (c->strip_area != NULL)
    gtk_widget_queue_draw (c->strip_area);
  return TRUE;
}



static void
carousel_stop (Carousel *c);

static gboolean
strip_button_press (GtkWidget *widget,
                    GdkEventButton *event,
                    gpointer data)
{
  Carousel *c = data;
  GtkAllocation alloc;
  gdouble first_dot_x;
  guint n;

  gtk_widget_get_allocation (widget, &alloc);
  first_dot_x = alloc.width - (c->slides->len * DOT_SPACING - (DOT_SPACING - 2 * DOT_RADIUS)) / 2.0;

  for (n = 0; n < c->slides->len; ++n)
    {
      gdouble cx = first_dot_x + n * DOT_SPACING;
      if (event->x >= cx - DOT_RADIUS - 4.0 && event->x <= cx + DOT_RADIUS + 4.0)
        {
          c->current = n;
          break;
        }
    }

  /* handing control back */
  carousel_stop (c);
  return TRUE;
}



static void
show_strip (Carousel *c)
{
  GdkMonitor *monitor;
  GdkRectangle geom;
  gint width;

  if (c->strip != NULL)
    return;

  c->strip = gtk_window_new (GTK_WINDOW_POPUP);
  gtk_window_set_skip_taskbar_hint (GTK_WINDOW (c->strip), TRUE);
  gtk_window_set_skip_pager_hint (GTK_WINDOW (c->strip), TRUE);
  gtk_window_set_accept_focus (GTK_WINDOW (c->strip), FALSE);
  gtk_widget_set_app_paintable (c->strip, TRUE);

  c->strip_area = gtk_drawing_area_new ();
  gtk_container_add (GTK_CONTAINER (c->strip), c->strip_area);
  g_signal_connect (c->strip_area, "draw", G_CALLBACK (strip_draw), c);
  g_signal_connect (c->strip_area, "button-press-event",
                    G_CALLBACK (strip_button_press), c);
  gtk_widget_add_events (c->strip_area, GDK_BUTTON_PRESS_MASK);

  width = STRIP_BASE_WIDTH + c->slides->len * DOT_SPACING;
  gtk_widget_set_size_request (c->strip_area, width, STRIP_HEIGHT);
  gtk_window_set_default_size (GTK_WINDOW (c->strip), width, STRIP_HEIGHT);
  gtk_widget_show_all (c->strip);

  monitor = gdk_display_get_primary_monitor (gdk_display_get_default ());
  if (monitor == NULL)
    monitor = gdk_display_get_monitor (gdk_display_get_default (), 0);
  gdk_monitor_get_geometry (monitor, &geom);
  gtk_window_move (GTK_WINDOW (c->strip),
                   geom.x + (geom.width - width) / 2, geom.y + 4);

  c->blink_start = g_get_monotonic_time ();
  c->blink_id = gdk_threads_add_timeout (BLINK_INTERVAL_MS, strip_blink, c);
}



/* ------------------------------------------------------------------ */
/* carousel state machine                                              */
/* ------------------------------------------------------------------ */

static void
carousel_stop (Carousel *c)
{
  if (c->slide_id != 0)
    {
      g_source_remove (c->slide_id);
      c->slide_id = 0;
    }

  if (c->blink_id != 0)
    {
      g_source_remove (c->blink_id);
      c->blink_id = 0;
    }

  if (c->strip != NULL)
    {
      gtk_widget_destroy (c->strip);
      c->strip = NULL;
      c->strip_area = NULL;
    }

  if (c->running)
    {
      g_debug ("carousel stopped: user is back");
      c->running = FALSE;
      g_ptr_array_set_size (c->slides, 0);
      c->current = 0;
    }
}



static void
show_slide (Carousel *c,
            guint index)
{
  TabState *tab;
  GDBusProxy *proxy;
  GError *error = NULL;

  if (index >= c->slides->len)
    return;

  c->current = index;
  tab = g_ptr_array_index (c->slides, index);

  proxy = g_hash_table_lookup (carousel.tab_proxies, tab->path);
  if (proxy != NULL)
    {
      g_dbus_proxy_call_sync (proxy, "Activate", g_variant_new ("()"),
                              G_DBUS_CALL_FLAGS_NO_AUTO_START, 1000, NULL, &error);
      if (error != NULL)
        {
          g_debug ("Activate failed: %s", error->message);
          g_error_free (error);
        }
    }

  c->last_switch = g_get_monotonic_time ();
  if (c->strip_area != NULL)
    gtk_widget_queue_draw (c->strip_area);
}



static gboolean
carousel_advance (gpointer data)
{
  Carousel *c = data;

  if (!c->running || c->slides->len == 0)
    return FALSE;

  show_slide (c, (c->current + 1) % c->slides->len);
  return TRUE;
}



static void
carousel_start (Carousel *c)
{
  guint interval;

  c->running = TRUE;
  g_debug ("carousel starting with %u active tabs", c->slides->len);
  show_strip (c);
  show_slide (c, 0);

  interval = conf_get_uint (CONF_SLIDE_INTERVAL, 6);
  c->slide_id = gdk_threads_add_timeout_seconds (MAX (2u, interval), carousel_advance, c);
}



static gboolean
poll_tick (gpointer data)
{
  Carousel *c = data;
  gboolean enabled;
  guint timeout, activity_min;
  gint64 idle_ms;
  GHashTableIter iter;
  gpointer key, value;

  enabled = conf_get_bool (CONF_ENABLED, TRUE);
  timeout = conf_get_uint (CONF_TIMEOUT, 60);
  activity_min = conf_get_uint (CONF_ACTIVITY_WINDOW, 10);

  if (!enabled)
    {
      carousel_stop (c);
      return TRUE;
    }

  resolve_service ();
  if (carousel.manager == NULL)
    {
      g_debug ("no terminal control bridge found");
      carousel_stop (c);
      return TRUE;
    }

  if (!bridge_refresh ())
    {
      g_debug ("bridge refresh failed");
      carousel_stop (c);
      return TRUE;
    }

  /* rebuild slide list */
  g_ptr_array_set_size (c->slides, 0);
  g_hash_table_iter_init (&iter, c->tabs);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      TabState *tab = value;
      gint64 activity_window_us = (gint64) activity_min * 60 * G_USEC_PER_SEC;

      if (tab->attention
          || (tab->last_output > 0
              && (g_get_monotonic_time () - tab->last_output) <= activity_window_us))
        g_ptr_array_add (c->slides, tab);
    }

  idle_ms = query_idle_ms ();
  if (idle_ms < 0)
    {
      g_debug ("no X11 idle backend (Wayland?)");
      carousel_stop (c);
      return TRUE;
    }

  g_debug ("poll: idle=%" G_GINT64_FORMAT "ms, %u active tabs", idle_ms, c->slides->len);

  if (c->running)
    {
      if (idle_ms < (gint64) timeout * 1000 - 250 || c->slides->len == 0)
        carousel_stop (c);
      /* slides refresh automatically on the next advance */
    }
  else if (idle_ms >= (gint64) timeout * 1000 && c->slides->len > 0)
    {
      carousel_start (c);
    }

  return TRUE;
}



/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

int
main (int argc,
      char **argv)
{
  GError *error = NULL;

  gtk_init (&argc, &argv);

  if (xfconf_init (&error))
    conf_channel = xfconf_channel_get (CONF_CHANNEL);
  else
    {
      g_debug ("xfconf unavailable (%s); using defaults", error->message);
      g_error_free (error);
    }

  carousel.tabs = g_hash_table_new_full (g_str_hash, g_str_equal, NULL,
                                         (GDestroyNotify) tab_state_free);
  carousel.win_proxies = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
                                                (GDestroyNotify) g_object_unref);
  carousel.tab_proxies = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
                                                (GDestroyNotify) g_object_unref);
  carousel.slides = g_ptr_array_new ();

  carousel.poll_id = gdk_threads_add_timeout (POLL_INTERVAL_MS, poll_tick, &carousel);

  gtk_main ();

  carousel_stop (&carousel);
  if (carousel.poll_id != 0)
    g_source_remove (carousel.poll_id);
  g_hash_table_unref (carousel.tabs);
  g_hash_table_unref (carousel.win_proxies);
  g_hash_table_unref (carousel.tab_proxies);
  g_ptr_array_unref (carousel.slides);
  g_clear_object (&carousel.manager);
  g_free (carousel.manager_service);

  if (conf_channel != NULL)
    g_clear_object (&conf_channel);
  xfconf_shutdown ();

  return 0;
}
