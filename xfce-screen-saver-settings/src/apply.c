/*
 * xfce4-screen-saver-apply — apply the saved screen-saver/blanking state
 * to the running X server.
 *
 * Reads the xfconf channel "xfce4-screen-saver" and runs `xset s` so the
 * preference survives across sessions. Shipped as an autostart entry
 * (logon), harmless when xset is unavailable (e.g. Wayland/Xwayland-only).
 */

#include <xfconf/xfconf.h>
#include <glib.h>

#define CONF_CHANNEL "xfce4-screen-saver"
#define CONF_ENABLED "/enabled" /* bool, default TRUE */
#define CONF_TIMEOUT "/timeout" /* uint seconds, default 600 */

int
main (int argc, char **argv)
{
  GError *error = NULL;
  XfconfChannel *channel;
  gboolean enabled;
  guint timeout_sec;
  gchar *command;
  gint exit_status = 0;
  gboolean ok;

  (void) argc; (void) argv;

  if (!xfconf_init (&error))
    {
      g_printerr ("xfconf unavailable (%s); nothing applied\n",
                  error ? error->message : "unknown error");
      if (error != NULL)
        g_clear_error (&error);
      return 1;
    }

  channel = xfconf_channel_get (CONF_CHANNEL);
  enabled = xfconf_channel_get_bool (channel, CONF_ENABLED, TRUE);
  timeout_sec = xfconf_channel_get_uint (channel, CONF_TIMEOUT, 600);

  if (timeout_sec < 5)
    timeout_sec = 5;
  if (timeout_sec > 24 * 3600)
    timeout_sec = 24 * 3600;

  if (enabled)
    command = g_strdup_printf ("xset s %u %u", timeout_sec, timeout_sec);
  else
    command = g_strdup ("xset s off");

  ok = g_spawn_command_line_sync (command, NULL, NULL, &exit_status, NULL);
  g_printerr ("xfce4-screen-saver-apply: %s -> %s (rc=%d)\n",
              command,
              ok ? "ok" : "failed",
              exit_status);

  g_free (command);
  g_clear_object (&channel);
  xfconf_shutdown ();
  return ok ? 0 : 1;
}