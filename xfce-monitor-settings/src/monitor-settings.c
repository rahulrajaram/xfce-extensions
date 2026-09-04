#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <X11/extensions/Xrandr.h>
#include <xfconf/xfconf.h>
#include <math.h>

#define CONF_CHANNEL "xfce4-monitor-settings"

typedef struct
{
  gchar *name;
  RRCrtc crtc;
  GtkWidget *brightness;
  GtkWidget *night;
} Monitor;

static XfconfChannel *channel;
static GPtrArray *monitors;

static gdouble
setting (const gchar *output, const gchar *name, gdouble fallback)
{
  gchar *key = g_strdup_printf ("/%s/%s", output, name);
  gdouble value = xfconf_channel_get_double (channel, key, fallback);
  g_free (key);
  return value;
}

static void
set_setting (const gchar *output, const gchar *name, gdouble value)
{
  gchar *key = g_strdup_printf ("/%s/%s", output, name);
  xfconf_channel_set_double (channel, key, value);
  g_free (key);
}

static guint16
gamma_value (gdouble position, gdouble brightness, gdouble colour)
{
  gdouble value = position * brightness * colour;
  value = CLAMP (value, 0.0, 1.0);
  return (guint16) (value * G_MAXUINT16);
}

static void
apply_monitor (Monitor *monitor)
{
  Display *display = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());
  gint size = XRRGetCrtcGammaSize (display, monitor->crtc);
  gdouble brightness = gtk_range_get_value (GTK_RANGE (monitor->brightness));
  gdouble night = gtk_range_get_value (GTK_RANGE (monitor->night));
  XRRCrtcGamma *gamma;
  gint i;

  if (size <= 0)
    return;

  gamma = XRRAllocGamma (size);
  for (i = 0; i < size; ++i)
    {
      gdouble position = (gdouble) i / (gdouble) (size - 1);
      gamma->red[i] = gamma_value (position, brightness, 1.0);
      gamma->green[i] = gamma_value (position, brightness, 1.0 - night * 0.22);
      gamma->blue[i] = gamma_value (position, brightness, 1.0 - night * 0.55);
    }
  XRRSetCrtcGamma (display, monitor->crtc, gamma);
  XRRFreeGamma (gamma);
  XFlush (display);
}

static void
slider_changed (GtkRange *range, gpointer user_data)
{
  Monitor *monitor = user_data;
  const gchar *key = range == GTK_RANGE (monitor->brightness) ? "brightness" : "night-mode";
  set_setting (monitor->name, key, gtk_range_get_value (range));
  apply_monitor (monitor);
}

static void
monitor_free (Monitor *monitor)
{
  g_free (monitor->name);
  g_free (monitor);
}

static GtkWidget *
make_slider (const gchar *label, gdouble value, Monitor *monitor, gboolean night)
{
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
  GtkWidget *caption = gtk_label_new (label);
  GtkWidget *scale = gtk_scale_new_with_range (GTK_ORIENTATION_HORIZONTAL, 0.05, 1.0, 0.01);
  gtk_widget_set_hexpand (scale, TRUE);
  gtk_scale_set_draw_value (GTK_SCALE (scale), TRUE);
  gtk_range_set_value (GTK_RANGE (scale), value);
  gtk_box_pack_start (GTK_BOX (box), caption, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (box), scale, TRUE, TRUE, 0);
  if (night)
    monitor->night = scale;
  else
    monitor->brightness = scale;
  g_signal_connect (scale, "value-changed", G_CALLBACK (slider_changed), monitor);
  return box;
}

static void
add_monitors (GtkBox *list)
{
  Display *display = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());
  Window root = DefaultRootWindow (display);
  XRRScreenResources *resources = XRRGetScreenResourcesCurrent (display, root);
  guint i;

  for (i = 0; resources != NULL && i < resources->noutput; ++i)
    {
      XRROutputInfo *output = XRRGetOutputInfo (display, resources, resources->outputs[i]);
      XRRCrtcInfo *crtc;
      Monitor *monitor;
      GtkWidget *frame, *box, *title;
      gchar *text;

      if (output == NULL || output->connection != RR_Connected || output->crtc == None
          || g_str_has_prefix (output->name, "eDP"))
        {
          if (output != NULL) XRRFreeOutputInfo (output);
          continue;
        }
      crtc = XRRGetCrtcInfo (display, resources, output->crtc);
      if (crtc == NULL) { XRRFreeOutputInfo (output); continue; }

      monitor = g_new0 (Monitor, 1);
      monitor->name = g_strndup (output->name, output->nameLen);
      monitor->crtc = output->crtc;
      g_ptr_array_add (monitors, monitor);

      frame = gtk_frame_new (NULL);
      box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
      gtk_container_set_border_width (GTK_CONTAINER (box), 12);
      text = g_strdup_printf ("%s  (%ux%u)", monitor->name, crtc->width, crtc->height);
      title = gtk_label_new (text);
      gtk_widget_set_halign (title, GTK_ALIGN_START);
      gtk_frame_set_label_widget (GTK_FRAME (frame), title);
      gtk_box_pack_start (GTK_BOX (box), make_slider ("Brightness", setting (monitor->name, "brightness", 1.0), monitor, FALSE), FALSE, FALSE, 0);
      gtk_box_pack_start (GTK_BOX (box), make_slider ("Night mode", setting (monitor->name, "night-mode", 0.0), monitor, TRUE), FALSE, FALSE, 0);
      gtk_container_add (GTK_CONTAINER (frame), box);
      gtk_box_pack_start (list, frame, FALSE, FALSE, 0);
      apply_monitor (monitor);
      g_free (text);
      XRRFreeCrtcInfo (crtc);
      XRRFreeOutputInfo (output);
    }
  if (resources != NULL) XRRFreeScreenResources (resources);
}

int
main (int argc, char **argv)
{
  GtkWidget *window, *outer, *heading, *list, *note;
  GError *error = NULL;

  gtk_init (&argc, &argv);
  if (!xfconf_init (&error))
    { g_printerr ("Unable to initialize Xfconf: %s\n", error->message); g_clear_error (&error); return 1; }
  channel = xfconf_channel_get (CONF_CHANNEL);
  monitors = g_ptr_array_new_with_free_func ((GDestroyNotify) monitor_free);

  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title (GTK_WINDOW (window), "External Monitor Settings");
  gtk_window_set_default_size (GTK_WINDOW (window), 560, 360);
  g_signal_connect (window, "destroy", G_CALLBACK (gtk_main_quit), NULL);
  outer = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
  gtk_container_set_border_width (GTK_CONTAINER (outer), 18);
  heading = gtk_label_new ("External monitors");
  gtk_widget_set_halign (heading, GTK_ALIGN_START);
  gtk_style_context_add_class (gtk_widget_get_style_context (heading), "title");
  gtk_box_pack_start (GTK_BOX (outer), heading, FALSE, FALSE, 0);
  note = gtk_label_new ("These controls use software gamma ramps and work without DDC/CI hardware access.");
  gtk_widget_set_halign (note, GTK_ALIGN_START);
  gtk_box_pack_start (GTK_BOX (outer), note, FALSE, FALSE, 0);
  list = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
  gtk_box_pack_start (GTK_BOX (outer), list, TRUE, TRUE, 0);
  add_monitors (GTK_BOX (list));
  if (monitors->len == 0) gtk_box_pack_start (GTK_BOX (list), gtk_label_new ("No external monitors detected."), FALSE, FALSE, 0);
  gtk_container_add (GTK_CONTAINER (window), outer);
  gtk_widget_show_all (window);
  gtk_main ();
  g_ptr_array_unref (monitors);
  xfconf_shutdown (); 
  return 0;
}
