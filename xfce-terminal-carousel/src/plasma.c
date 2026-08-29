/*-
 * Copyright (c) 2025 xfce4-terminal-carousel contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.
 *
 * Plasma screen (GTK4): a standalone fullscreen overlay that springs up
 * after the X11 idle threshold and shows a smooth card carousel of the
 * terminal tabs with active work. Feed is the Terminal Control D-Bus
 * bridge (same contract as the GTK3 strip daemon).
 *
 * Phase: slice 1/2 skeleton — bridge titles drive the cards; the card
 * body is a placeholder until the text mirror (slice 3) lands.
 */

#include <gtk/gtk.h>
#include <gdk/x11/gdkx.h>
#include <xfconf/xfconf.h>
#include <X11/extensions/scrnsaver.h>

#include <config.h>

#define POLL_INTERVAL_MS 2000
#define SLIDE_ANIM_MS 450

#define CARD_W 620
#define CARD_H 380
#define CARD_GAP 56
#define CARD_TITLE_H 30
#define BODY_LINES 9

/* Terminal Control bridge */
#define CONTROL_MANAGER_IFACE "org.xfce.Terminal.Control.Manager"
#define CONTROL_WINDOW_IFACE "org.xfce.Terminal.Control.Window"
#define CONTROL_TAB_IFACE "org.xfce.Terminal.Control.Tab"
#define CONTROL_PATH "/org/xfce/Terminal"
#define CONTROL_SERVICE_FMT "org.xfce.Terminal%d"
#define CONTROL_SERVICE_MIN 4
#define CONTROL_SERVICE_MAX 9

/* xfconf configuration (same channel as the strip daemon) */
#define CONF_CHANNEL "xfce4-terminal-carousel"
#define CONF_ENABLED "/enabled"
#define CONF_TIMEOUT "/timeout"
#define CONF_SLIDE_INTERVAL "/slide-interval"
#define CONF_ACTIVITY_WINDOW "/activity-window"

typedef struct
{
  gchar *path;
  gchar *title;
  gboolean attention;
  gint64 last_output; /* CLOCK_MONOTONIC usec */
} TabState;

typedef struct
{
  GHashTable *tabs;        /* path -> TabState* */
  GHashTable *win_proxies; /* window path -> GDBusProxy* */
  GHashTable *tab_proxies; /* tab path -> GDBusProxy* */
  gchar *manager_service;
  GDBusProxy *manager;

  guint poll_id;
  guint slide_id;

  /* carousel state */
  gboolean running;
  GPtrArray *slides; /* TabState* */
  guint current;
  gdouble rail;      /* continuous carousel position */

  /* overlay UI (GTK4) */
  GtkWidget *window;
  GtkWidget *area;
  guint tick_id;
  gint64 anim_start;  /* monotonic time of last slide advance */

  /* transient per-frame state for the draw func */
  guint32 frame;      /* frame counter while visible */
} Plasma;

static Plasma plasma = { NULL, };
static XfconfChannel *conf_channel = NULL;



/* ------------------------------------------------------------------ */
/* small helpers                                                       */
/* ------------------------------------------------------------------ */

static void
tab_state_free (TabState *tab)
{
  g_free (tab->path);
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
  Display *dpy = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());

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
/* bridge client (ported from the strip daemon)                        */
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
                                         G_DBUS_PROXY_FLAGS_NONE, NULL,
                                         service, path, iface, NULL, NULL);
  if (proxy == NULL)
    return NULL;

  g_hash_table_insert (table, g_strdup (path), proxy);
  return proxy;
}



static gchar *
proxy_get_str (GDBusProxy *proxy,
               const gchar *prop)
{
  GVariant *v = g_dbus_proxy_get_cached_property (proxy, prop);
  gchar *str;

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

  if (plasma.manager_service != NULL)
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
              plasma.manager_service = name;
              plasma.manager = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                                              G_DBUS_PROXY_FLAGS_NONE, NULL,
                                                              name, CONTROL_PATH,
                                                              CONTROL_MANAGER_IFACE,
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

  if (plasma.manager == NULL)
    return FALSE;

  reply = g_dbus_proxy_call_sync (plasma.manager, "ListWindows",
                                  g_variant_new ("()"), G_DBUS_CALL_FLAGS_NO_AUTO_START,
                                  1000, NULL, NULL);
  if (reply == NULL)
    {
      g_hash_table_remove_all (plasma.tabs);
      g_hash_table_remove_all (plasma.win_proxies);
      g_hash_table_remove_all (plasma.tab_proxies);
      g_clear_object (&plasma.manager);
      g_clear_pointer (&plasma.manager_service, g_free);
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

      wproxy = ensure_proxy (plasma.win_proxies, win_path, CONTROL_WINDOW_IFACE,
                             plasma.manager_service);
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

          tproxy = ensure_proxy (plasma.tab_proxies, tab_path, CONTROL_TAB_IFACE,
                                 plasma.manager_service);
          if (tproxy == NULL)
            continue;

          g_hash_table_add (seen_tabs, g_strdup (tab_path));

          tab = g_hash_table_lookup (plasma.tabs, tab_path);
          if (tab == NULL)
            {
              tab = g_new0 (TabState, 1);
              tab->path = g_strdup (tab_path);
              g_hash_table_insert (plasma.tabs, tab->path, tab);
            }

          g_free (tab->title);
          tab->title = proxy_get_str (tproxy, "Title");
          tab->attention = proxy_get_bool (tproxy, "NeedsAttention");
          tab->last_output = proxy_get_int64 (tproxy, "LastOutput");
        }
      g_variant_unref (tabs_var);
    }
  g_variant_unref (array);
  g_variant_unref (reply);

  drop_stale (plasma.tabs, seen_tabs);
  drop_stale (plasma.tab_proxies, seen_tabs);
  drop_stale (plasma.win_proxies, seen_windows);

  g_hash_table_unref (seen_windows);
  g_hash_table_unref (seen_tabs);
  return TRUE;
}



/* ------------------------------------------------------------------ */
/* GTK4 rendering                                                      */
/* ------------------------------------------------------------------ */

static void
rounded_rect (cairo_t *cr,
              gdouble x,
              gdouble y,
              gdouble w,
              gdouble h,
              gdouble r)
{
  cairo_new_sub_path (cr);
  cairo_arc (cr, x + w - r, y + r, r, -G_PI_2, 0);
  cairo_arc (cr, x + w - r, y + h - r, r, 0, G_PI_2);
  cairo_arc (cr, x + r, y + h - r, r, G_PI_2, G_PI);
  cairo_arc (cr, x + r, y + r, r, G_PI, 3 * G_PI_2);
  cairo_close_path (cr);
}



static void
draw_card (cairo_t *cr,
           gdouble cx,
           gdouble cy,
           gdouble scale,
           gdouble dim,
           TabState *tab)
{
  gdouble w = CARD_W, h = CARD_H;
  gdouble x = cx - w / 2.0, y = cy - h / 2.0;
  gdouble a = (1.0 - dim) * 0.94 + 0.06;
  PangoLayout *layout;

  /* shadow */
  cairo_save (cr);
  cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 0.35 * a * scale);
  rounded_rect (cr, x + 6, y + 10, w, h, 14.0);
  cairo_fill (cr);
  cairo_restore (cr);

  cairo_save (cr);
  cairo_translate (cr, cx, cy);
  cairo_scale (cr, scale, scale);
  cairo_translate (cr, -cx, -cy);

  /* card body */
  rounded_rect (cr, x, y, w, h, 14.0);
  cairo_set_source_rgba (cr, 0.10, 0.10, 0.13, 0.96 * a);
  cairo_fill_preserve (cr);
  cairo_set_source_rgba (cr, 0.5, 0.5, 0.6, 0.25 * a);
  cairo_set_line_width (cr, 1.0);
  cairo_stroke (cr);

  /* title bar */
  rounded_rect (cr, x + 1, y + 1, w - 2, CARD_TITLE_H, 14.0);
  cairo_set_source_rgba (cr, 0.16, 0.16, 0.20, 1.0 * a);
  cairo_fill (cr);
  cairo_set_source_rgba (cr, 0.5, 0.5, 0.6, 0.18 * a);
  cairo_rectangle (cr, x + 1, y + CARD_TITLE_H, w - 2, 1.0);
  cairo_fill (cr);

  /* attention dot in the title */
  if (tab->attention)
    {
      gdouble blink = 0.55 + 0.45 * sin (g_get_monotonic_time () / 300000.0);
      cairo_set_source_rgba (cr, 0.20, 0.95, 0.35, blink * a);
      cairo_arc (cr, x + 18, y + CARD_TITLE_H / 2.0 + 0.5, 4.5, 0.0, 2.0 * G_PI);
      cairo_fill (cr);
    }

  /* title text */
  layout = gtk_widget_create_pango_layout (plasma.area, NULL);
  pango_layout_set_width (layout, (w - 60) * PANGO_SCALE);
  pango_layout_set_ellipsize (layout, PANGO_ELLIPSIZE_END);
  pango_layout_set_text (layout, tab->title != NULL ? tab->title : "", -1);
  cairo_set_source_rgba (cr, 0.92, 0.92, 0.95, 0.95 * a);
  cairo_move_to (cr, x + 32, y + (CARD_TITLE_H - 14) / 2.0 - 1);
  pango_cairo_show_layout (cr, layout);
  g_object_unref (layout);

  /* placeholder terminal body: dim rows + cursor block
   * (slice 3 replaces this with live text mirror lines) */
  for (guint i = 0; i < BODY_LINES; ++i)
    {
      gdouble ry = y + CARD_TITLE_H + 14 + i * (h - CARD_TITLE_H - 34) / (BODY_LINES - 1);
      gdouble lw = (60.0 + 55.0 * ((i * 7) % 5) / 4.0) / 100.0 * (w - 56);
      gboolean cursor = (i == 0);

      cairo_set_source_rgba (cr, 0.62, 0.66, 0.72, (cursor ? 0.95 : 0.38) * a);
      cairo_rectangle (cr, x + 28, ry, lw, cursor ? 16.0 : 3.0);
      cairo_fill (cr);
      if (cursor)
        {
          cairo_set_source_rgba (cr, 0.35, 0.90, 0.45, 0.8 * a);
          cairo_rectangle (cr, x + 28 + lw + 6, ry, 8.0, 16.0);
          cairo_fill (cr);
        }
    }

  cairo_restore (cr);
}



static guint32 draw_count = 0;

static void
plasma_draw (GtkDrawingArea *area,
             cairo_t *cr,
             int width,
             int height,
             gpointer data)
{
  Plasma *p = data;
  gdouble cx = width / 2.0;
  gdouble cy = height / 2.0;

  draw_count++;
  if ((draw_count % 300) == 1)
    g_debug ("plasma draw #%u (rail=%.2f)", draw_count, p->rail);

  /* dim the desktop behind the overlay */
  cairo_set_source_rgba (cr, 0.04, 0.045, 0.06, 0.80);
  cairo_paint (cr);

  for (guint i = 0; i < p->slides->len; ++i)
    {
      gdouble dist = (gdouble) i - p->rail;
      gdouble x = cx + dist * (CARD_W + CARD_GAP);
      gdouble scale = CLAMP (1.0 - 0.14 * ABS (dist), 0.68, 1.0);
      gdouble dim = CLAMP (ABS (dist) * 0.28, 0.0, 0.72);

      if (x < -CARD_W || x > width + CARD_W)
        continue;

      draw_card (cr, x, cy, scale, dim, g_ptr_array_index (p->slides, i));
    }
}



/* tick callback: drives the rail toward the integer target (smooth
 * carousel) and proves animation via the frame counter */
static gboolean
plasma_tick (GtkWidget *widget,
             GdkFrameClock *clock,
             gpointer data)
{
  Plasma *p = data;
  gdouble target = p->current;

  if (p->running && p->slides->len > 0)
    {
      gdouble k = 1.0 - exp (-1.0 / 6.0); /* ~16fps settle toward target */
      p->rail += (target - p->rail) * k;
      if (ABS (p->rail - target) < 0.001)
        p->rail = target;
      /* queue the drawing AREA explicitly (queuing only the toplevel may
       * not reach child widgets in GTK4) plus the window */
      if (p->area != NULL)
        gtk_widget_queue_draw (p->area);
      gtk_widget_queue_draw (widget);
      p->frame++;
      if ((p->frame % 120) == 0)
        g_debug ("plasma frame %u (rail=%.2f)", p->frame, p->rail);
    }

  return G_SOURCE_CONTINUE;
}



/* ------------------------------------------------------------------ */
/* overlay lifecycle                                                   */
/* ------------------------------------------------------------------ */

static void plasma_stop (Plasma *p);



static void
plasma_hide_immediate (Plasma *p)
{
  if (p->window != NULL && gtk_widget_get_visible (p->window))
    gtk_widget_set_visible (p->window, FALSE);
  if (p->slide_id != 0)
    {
      g_source_remove (p->slide_id);
      p->slide_id = 0;
    }
  p->rail = p->current = 0;
  p->frame = 0;
  p->running = FALSE;
  g_ptr_array_set_size (p->slides, 0);
  g_debug ("plasma hidden (user is back)");
}



static void
on_key_pressed (GtkEventControllerKey *controller,
                guint keyval,
                guint keycode,
                GdkModifierType state,
                gpointer data)
{
  plasma_hide_immediate (data);
}



static void
on_click (GtkGestureClick *gesture,
          int n_press,
          double x,
          double y,
          gpointer data)
{
  Plasma *p = data;
  gint width;
  gdouble cx;

  if (!p->running)
    return;

  width = gtk_widget_get_width (p->area);
  cx = width / 2.0;

  for (guint i = 0; i < p->slides->len; ++i)
    {
      gdouble dist = (gdouble) i - p->rail;
      gdouble card_x = cx + dist * (CARD_W + CARD_GAP);

      if (x >= card_x - CARD_W / 2.0 && x <= card_x + CARD_W / 2.0)
        {
          p->current = i;
          plasma_hide_immediate (p);
          return;
        }
    }
}



static gboolean
plasma_advance (gpointer data)
{
  Plasma *p = data;

  if (!p->running || p->slides->len == 0)
    return FALSE;

  p->current = (p->current + 1) % p->slides->len;
  p->anim_start = g_get_monotonic_time ();
  g_debug ("plasma slide -> %u (%s)", p->current,
           ((TabState *) g_ptr_array_index (p->slides, p->current))->title);
  return TRUE;
}



static void
show_overlay (Plasma *p)
{
  GdkMonitor *monitor = NULL;
  GdkRectangle geom;

  if (p->window == NULL)
    {
      GListModel *monitors;

      p->window = gtk_window_new ();
      gtk_window_set_default_size (GTK_WINDOW (p->window), 1280, 800);
      gtk_window_set_title (GTK_WINDOW (p->window), "xfce4-terminal-plasma");
      gtk_window_set_decorated (GTK_WINDOW (p->window), FALSE);
      /* NOTE: GTK4 removed skip-taskbar/accept-focus window hints; a
       * layer-shell/WM-hint pass is deferred to the Wayland phase. */

      p->area = gtk_drawing_area_new ();
      gtk_widget_set_hexpand (p->area, TRUE);
      gtk_widget_set_vexpand (p->area, TRUE);
      gtk_window_set_child (GTK_WINDOW (p->window), p->area);
      gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (p->area),
                                      plasma_draw, p, NULL);

      GtkEventController *key = gtk_event_controller_key_new ();
      gtk_widget_add_controller (p->window, GTK_EVENT_CONTROLLER (key));
      g_signal_connect (key, "key-pressed",
                        G_CALLBACK (on_key_pressed), p);

      GtkGesture *click = gtk_gesture_click_new ();
      gtk_widget_add_controller (p->window, GTK_EVENT_CONTROLLER (click));
      g_signal_connect (click, "pressed", G_CALLBACK (on_click), p);

      monitors = gdk_display_get_monitors (gdk_display_get_default ());
      if (monitors != NULL && g_list_model_get_n_items (monitors) > 0)
        monitor = g_list_model_get_item (monitors, 0);
      if (monitor != NULL)
        {
          gdk_monitor_get_geometry (monitor, &geom);
          gtk_window_set_default_size (GTK_WINDOW (p->window),
                                       geom.width, geom.height);
        }

      p->tick_id = gtk_widget_add_tick_callback (p->window,
                                                 plasma_tick, p, NULL);
    }

  p->running = TRUE;
  p->rail = p->current = 0;
  p->anim_start = g_get_monotonic_time ();
  gtk_widget_set_visible (p->window, TRUE);
  gtk_window_present (GTK_WINDOW (p->window));

  g_debug ("plasma overlay shown (%u cards)", p->slides->len);
  p->slide_id = g_timeout_add_seconds (
      MAX (2u, conf_get_uint (CONF_SLIDE_INTERVAL, 6)),
      plasma_advance, p);
}



static void
plasma_stop (Plasma *p)
{
  /* full stop, equivalent to the strip daemon's carousel_stop */
  if (p->slide_id != 0)
    {
      g_source_remove (p->slide_id);
      p->slide_id = 0;
    }
  if (p->window != NULL && gtk_widget_get_visible (p->window))
    gtk_widget_set_visible (p->window, FALSE);
  p->rail = p->current = 0;
  p->frame = 0;
  p->running = FALSE;
  g_ptr_array_set_size (p->slides, 0);
}



/* ------------------------------------------------------------------ */
/* polling loop                                                        */
/* ------------------------------------------------------------------ */

static gboolean
poll_tick (gpointer data)
{
  Plasma *p = data;
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
      plasma_stop (p);
      return TRUE;
    }

  resolve_service ();
  if (plasma.manager == NULL)
    {
      g_debug ("no terminal control bridge found");
      plasma_stop (p);
      return TRUE;
    }

  if (!bridge_refresh ())
    {
      plasma_stop (p);
      return TRUE;
    }

  g_ptr_array_set_size (p->slides, 0);
  g_hash_table_iter_init (&iter, p->tabs);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      TabState *tab = value;
      gint64 window_us = (gint64) activity_min * 60 * G_USEC_PER_SEC;

      if (tab->attention
          || (tab->last_output > 0
              && (g_get_monotonic_time () - tab->last_output) <= window_us))
        g_ptr_array_add (p->slides, tab);
    }

  idle_ms = query_idle_ms ();
  if (idle_ms < 0)
    {
      g_debug ("no X11 idle backend");
      plasma_stop (p);
      return TRUE;
    }

  g_debug ("poll: idle=%" G_GINT64_FORMAT "ms, %u active tabs",
           idle_ms, p->slides->len);

  if (p->running)
    {
      if (idle_ms < (gint64) timeout * 1000 - 250 || p->slides->len == 0)
        plasma_stop (p);
    }
  else if (idle_ms >= (gint64) timeout * 1000 && p->slides->len > 0)
    {
      /* snap rail target to the slide we are about to show */
      show_overlay (p);
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
  GtkCssProvider *css;
  GMainLoop *loop;

  gtk_init ();

  /* transparent window background so only our painted content shows */
  css = gtk_css_provider_new ();
  gtk_css_provider_load_from_data (css,
      "window { background-color: rgba(0,0,0,0); }", -1);
  gtk_style_context_add_provider_for_display (
      gdk_display_get_default (), GTK_STYLE_PROVIDER (css),
      GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

  if (xfconf_init (&error))
    conf_channel = xfconf_channel_get (CONF_CHANNEL);
  else
    {
      g_debug ("xfconf unavailable (%s); using defaults", error->message);
      g_error_free (error);
    }

  plasma.tabs = g_hash_table_new_full (g_str_hash, g_str_equal, NULL,
                                       (GDestroyNotify) tab_state_free);
  plasma.win_proxies = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
                                              (GDestroyNotify) g_object_unref);
  plasma.tab_proxies = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
                                              (GDestroyNotify) g_object_unref);
  plasma.slides = g_ptr_array_new ();

  plasma.poll_id = g_timeout_add (POLL_INTERVAL_MS, poll_tick, &plasma);

  loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (loop);
  g_main_loop_unref (loop);

  plasma_stop (&plasma);
  if (plasma.poll_id != 0)
    g_source_remove (plasma.poll_id);
  if (plasma.tick_id != 0)
    gtk_widget_remove_tick_callback (plasma.window, plasma.tick_id);
  g_hash_table_unref (plasma.tabs);
  g_hash_table_unref (plasma.win_proxies);
  g_hash_table_unref (plasma.tab_proxies);
  g_ptr_array_unref (plasma.slides);
  g_clear_object (&plasma.manager);
  g_free (plasma.manager_service);

  if (conf_channel != NULL)
    g_clear_object (&conf_channel);
  xfconf_shutdown ();
  g_object_unref (css);

  return 0;
}