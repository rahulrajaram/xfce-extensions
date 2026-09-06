/*
 * xfce4-screen-saver-settings — XFCE screen-saver / blanking settings.
 *
 * X11 has no locked-in screensaver UI in XFCE 4.18 (xfce4-screensaver is
 * not packaged in Debian 12); this dialog is the idiomatic replacement:
 *
 *   - XfceTitledDialog (libxfce4ui-2), registered with the settings
 *     manager (X-XfcePluggable, System category) like every stock dialog.
 *   - Preference state lives in the xfconf channel "xfce4-screen-saver"
 *     (survives sessions; xfce4-screen-saver-apply re-applies it at
 *     logon via autostart) and is written to the running X server live
 *     through `xset s`.
 *   - Shows the current X11 blanking state and whether XScreenSaver is
 *     installed, with a "Preview now" action and a shortcut to the
 *     XScreenSaver preferences when available.
 */

#include <gtk/gtk.h>
#include <libxfce4ui/libxfce4ui.h>
#include <xfconf/xfconf.h>

#include <string.h>

#define CONF_CHANNEL "xfce4-screen-saver"
#define CONF_ENABLED "/enabled" /* bool      default TRUE  */
#define CONF_TIMEOUT "/timeout" /* uint sec  default 600  */

#define TITLE "Screen Saver"
#define SUBTITLE "Screen blanking and screensaver settings (X11)"

static XfconfChannel *channel = NULL;
static gboolean syncing = FALSE;

static GtkWidget *sw_enabled = NULL;
static GtkWidget *spin_timeout = NULL;   /* minutes in the UI */
static GtkWidget *label_status = NULL;
static GtkWidget *btn_prefs = NULL;

/* ------------------------------------------------------------------ */

static guint
channel_get_uint (const gchar *key, guint fallback)
{
  if (channel == NULL)
    return fallback;
  return xfconf_channel_get_uint (channel, key, fallback);
}

static void
run_command (const gchar *command)
{
  GError *error = NULL;
  g_debug ("exec: %s", command);
  g_spawn_command_line_async (command, &error);
  if (error != NULL)
    {
      g_warning ("failed to run %s: %s", command, error->message);
      g_clear_error (&error);
    }
}

/* Apply the current preference to the live X server. */
static void
apply_to_x (gboolean enabled, guint timeout_sec)
{
  gchar *command = enabled
    ? g_strdup_printf ("xset s %u %u", timeout_sec, timeout_sec)
    : g_strdup ("xset s off");
  run_command (command);
  g_free (command);
}

static guint
current_blank_timeout (void)
{
  gchar *stdout_buf = NULL;
  gint exit_status = 0;
  GError *gerr = NULL;
  guint value = 0;

  if (g_spawn_command_line_sync ("xset q", &stdout_buf, NULL, &exit_status,
                                 &gerr)
      && exit_status == 0 && stdout_buf != NULL)
    {
      gchar *p = g_strrstr (stdout_buf, "timeout:");
      if (p != NULL)
        {
          p += strlen ("timeout:");
          while (*p == ' ' || *p == '\t')
            p++;
          gchar *end = p;
          while (g_ascii_isdigit (*end))
            end++;
          if (end > p)
            value = (guint) g_ascii_strtoull (p, NULL, 10);
        }
    }

  if (gerr != NULL)
    g_clear_error (&gerr);
  g_free (stdout_buf);
  return value;
}

static gboolean
dpms_enabled (void)
{
  gchar *stdout_buf = NULL;
  gint exit_status = 0;
  GError *gerr = NULL;
  gboolean on = FALSE;

  if (g_spawn_command_line_sync ("xset q", &stdout_buf, NULL, &exit_status,
                                 &gerr)
      && exit_status == 0 && stdout_buf != NULL)
    on = (g_strrstr (stdout_buf, "DPMS is Enabled") != NULL);
  else
    on = FALSE;

  if (gerr != NULL)
    g_clear_error (&gerr);
  g_free (stdout_buf);
  return on;
}

/* ------------------------------------------------------------------ */
/* XScreenSaver detection: tools installed, and is the daemon alive?  */

static gboolean
xss_tools_installed (void)
{
  return (g_find_program_in_path ("xscreensaver-command") != NULL
          || g_find_program_in_path ("xscreensaver-demo") != NULL);
}

static gboolean
xss_daemon_running (void)
{
  gchar *out = NULL;
  gint status = 0;
  GError *gerr = NULL;
  gboolean ok;

  if (g_find_program_in_path ("xscreensaver-command") == NULL)
    return FALSE;

  /* xscreensaver-command exits 0 only when a daemon responds. */
  ok = g_spawn_command_line_sync ("xscreensaver-command -time",
                                  &out, NULL, &status, &gerr);
  if (gerr != NULL)
    g_clear_error (&gerr);
  g_free (out);
  return ok && status == 0;
}

static void
refresh_status (void)
{
  gboolean installed = xss_tools_installed ();
  gboolean running = xss_daemon_running ();
  guint t = current_blank_timeout ();
  gboolean dpms = dpms_enabled ();
  gchar *xss_state;
  gchar *markup;

  if (installed && running)
    xss_state =
        "XScreenSaver is running — Preview and Preferences talk to the daemon.";
  else if (installed)
    xss_state =
        "XScreenSaver is installed but not running — Preview blanks the "
        "X screen directly; Preferences opens the standalone demo "
        "(`xscreensaver --no-splash &` runs the daemon in your session).";
  else
    xss_state =
        "XScreenSaver is not installed — run "
        "`sudo apt install xscreensaver xscreensaver-gl "
        "xscreensaver-gl-extra` for animated savers and locking.";

  markup = g_strdup_printf (
      "<span size=\"small\">Current: blanking %s, timeout %u s.\n"
      "DPMS is %s (monitor power-off is handled by Power Manager).\n"
      "%s</span>",
      t > 0 ? "on" : "off",
      t,
      dpms ? "enabled" : "disabled",
      xss_state);

  gtk_label_set_markup (GTK_LABEL (label_status), markup);
  gtk_widget_set_sensitive (btn_prefs, installed);
  g_free (markup);
}

/* ------------------------------------------------------------------ */
/* two-way sync: widgets <-> xfconf channel                           */

static void
sync_widgets_from_channel (void)
{
  if (channel == NULL)
    return;

  syncing = TRUE;
  gtk_switch_set_active (GTK_SWITCH (sw_enabled),
                         xfconf_channel_get_bool (channel, CONF_ENABLED, TRUE));
  gtk_spin_button_set_value (GTK_SPIN_BUTTON (spin_timeout),
                             channel_get_uint (CONF_TIMEOUT, 600) / 60);
  syncing = FALSE;
  refresh_status ();
}

static void
channel_property_changed (XfconfChannel *ch, gchar *prop, gchar *unused,
                          gpointer data)
{
  (void) ch; (void) prop; (void) unused; (void) data;
  sync_widgets_from_channel ();
}

static void
enabled_toggled (GtkSwitch *sw, gpointer unused)
{
  (void) unused;

  if (syncing || channel == NULL)
    return;

  gboolean on = gtk_switch_get_active (sw);
  guint timeout_sec =
      (guint) gtk_spin_button_get_value (GTK_SPIN_BUTTON (spin_timeout)) * 60;

  xfconf_channel_set_bool (channel, CONF_ENABLED, on);
  apply_to_x (on, timeout_sec);
  refresh_status ();
}

static void
timeout_changed (GtkSpinButton *spin, gpointer unused)
{
  (void) unused;

  if (syncing || channel == NULL)
    return;

  guint timeout_sec = (guint) gtk_spin_button_get_value (spin) * 60;
  xfconf_channel_set_uint (channel, CONF_TIMEOUT, timeout_sec);

  if (gtk_switch_get_active (GTK_SWITCH (sw_enabled)))
    {
      apply_to_x (TRUE, timeout_sec);
      refresh_status ();
    }
}

/* ------------------------------------------------------------------ */
/* dialog responses                                                    */

static gchar *
preview_command (void)
{
  if (xss_daemon_running ())
    return g_strdup ("xscreensaver-command -activate");
  return g_strdup ("xset s activate");
}

static gchar *
prefs_command (void)
{
  /* xscreensaver-demo both connects to a running daemon and opens the
   * configuration window standalone; Debian's xscreensaver-command has no
   * -prefs flag, so this is the only reliable path. */
  if (g_find_program_in_path ("xscreensaver-demo") != NULL)
    return g_strdup ("xscreensaver-demo");
  return NULL;
}

static void
dialog_response (GtkDialog *dlg, gint response, gpointer unused)
{
  (void) unused;

  switch (response)
    {
    case GTK_RESPONSE_APPLY:   /* "Preview now" */
      {
        gchar *cmd = preview_command ();
        g_debug ("action preview -> %s", cmd);
        run_command (cmd);
        g_free (cmd);
        return;                /* keep the dialog open */
      }
    case GTK_RESPONSE_HELP:    /* "XScreenSaver preferences…" */
      {
        gchar *cmd = prefs_command ();
        if (cmd != NULL)
          {
            g_debug ("action prefs -> %s", cmd);
            run_command (cmd);
            g_free (cmd);
          }
        else
          g_warning ("prefs requested but no XScreenSaver tool available");
        return;                /* keep the dialog open */
      }
    case GTK_RESPONSE_CLOSE:
    case GTK_RESPONSE_DELETE_EVENT:
    default:
      gtk_widget_destroy (GTK_WIDGET (dlg));
      break;
    }
}

/* ------------------------------------------------------------------ */

int
main (int argc, char **argv)
{
  GError *error = NULL;
  GtkWidget *dialog;

  gtk_init (&argc, &argv);

  if (xfconf_init (&error))
    channel = xfconf_channel_get (CONF_CHANNEL);
  else
    {
      g_warning ("xfconf unavailable (%s); dialog runs read-only",
                 error ? error->message : "unknown error");
      if (error != NULL)
        g_clear_error (&error);
    }

  dialog = xfce_titled_dialog_new ();
  xfce_titled_dialog_create_action_area (XFCE_TITLED_DIALOG (dialog));
  gtk_window_set_title (GTK_WINDOW (dialog), TITLE);
  xfce_titled_dialog_set_subtitle (XFCE_TITLED_DIALOG (dialog), SUBTITLE);
  gtk_window_set_icon_name (GTK_WINDOW (dialog), "xfce4-screen-saver");
  gtk_window_set_default_size (GTK_WINDOW (dialog), 460, -1);

  /* content: enable switch + timeout spin + status block */
  GtkWidget *grid = gtk_grid_new ();
  gtk_grid_set_row_spacing (GTK_GRID (grid), 6);
  gtk_grid_set_column_spacing (GTK_GRID (grid), 12);
  gtk_container_add (
      GTK_CONTAINER (gtk_dialog_get_content_area (GTK_DIALOG (dialog))),
      grid);

  GtkWidget *hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  GtkWidget *text = gtk_label_new ("Enable screen blanking after inactivity");
  gtk_label_set_xalign (GTK_LABEL (text), 0.0f);
  gtk_widget_set_hexpand (text, TRUE);
  sw_enabled = gtk_switch_new ();
  gtk_box_pack_start (GTK_BOX (hbox), text, TRUE, TRUE, 0);
  gtk_box_pack_end (GTK_BOX (hbox), sw_enabled, FALSE, FALSE, 0);
  gtk_grid_attach (GTK_GRID (grid), hbox, 0, 0, 1, 1);
  g_signal_connect (sw_enabled, "notify::active", G_CALLBACK (enabled_toggled), NULL);

  GtkWidget *timeout_row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  GtkWidget *timeout_text = gtk_label_new ("Blank after (minutes)");
  gtk_label_set_xalign (GTK_LABEL (timeout_text), 0.0f);
  gtk_widget_set_hexpand (timeout_text, TRUE);
  spin_timeout = gtk_spin_button_new_with_range (1, 240, 1);
  gtk_box_pack_start (GTK_BOX (timeout_row), timeout_text, TRUE, TRUE, 0);
  gtk_box_pack_end (GTK_BOX (timeout_row), spin_timeout, FALSE, FALSE, 0);
  gtk_grid_attach (GTK_GRID (grid), timeout_row, 0, 1, 1, 1);
  g_signal_connect (spin_timeout, "value-changed", G_CALLBACK (timeout_changed), NULL);

  label_status = gtk_label_new ("");
  gtk_label_set_xalign (GTK_LABEL (label_status), 0.0f);
  gtk_label_set_line_wrap (GTK_LABEL (label_status), TRUE);
  gtk_widget_set_margin_top (label_status, 14);
  gtk_grid_attach (GTK_GRID (grid), label_status, 0, 2, 1, 1);

  /* action area: preview + prefs + close */
  xfce_titled_dialog_add_button (XFCE_TITLED_DIALOG (dialog), "Preview now",
                                 GTK_RESPONSE_APPLY);
  btn_prefs = xfce_titled_dialog_add_button (XFCE_TITLED_DIALOG (dialog),
                                             "XScreenSaver preferences…",
                                             GTK_RESPONSE_HELP);
  xfce_titled_dialog_add_button (XFCE_TITLED_DIALOG (dialog), "Close",
                                 GTK_RESPONSE_CLOSE);
  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_CLOSE);

  g_signal_connect (dialog, "response", G_CALLBACK (dialog_response), NULL);
  g_signal_connect (dialog, "destroy", G_CALLBACK (gtk_main_quit), NULL);

  if (channel != NULL)
    g_signal_connect (channel, "property-changed",
                      G_CALLBACK (channel_property_changed), NULL);
  sync_widgets_from_channel ();

  gtk_widget_show_all (dialog);

  gtk_main ();

  if (channel != NULL)
    g_clear_object (&channel);
  xfconf_shutdown ();
  return 0;
}