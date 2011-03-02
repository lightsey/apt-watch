// prefs-notify.h                        -*-c++-*-
//
//  Copyright 2004 Daniel Burrows
//  Copyright 2011 John Lightsey
//
// Code to manage the "notifications about upgrades" dialog.

#include "prefs-notify.h"

#include "apt-watch-gnome.h"

#include <string>

#include <gtk/gtk.h>
#include <panel-applet.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

using namespace std;

static GConfEnumStringPair notify_message_lookup_table[] = {
  {NOTIFY_MESSAGE_NEVER, "never"},
  {NOTIFY_MESSAGE_SECURITY, "security"},
  {NOTIFY_MESSAGE_ALL, "all"},
  {0, NULL}
};

NotifyMessage get_notify_message(PanelApplet *applet)
{
  gint curval=NOTIFY_MESSAGE_SECURITY;

  gchar *key=g_strdup_printf("%s/notify/notify_message",
			     panel_applet_get_preferences_key(applet));

  GError *err=NULL; 

  gchar *stringval=gconf_client_get_string(confclient,
					   key,
					   &err);

  if(err)
    g_error_free(err);

  if(stringval)
    gconf_string_to_enum(notify_message_lookup_table,
			 stringval,
			 &curval);

  g_free(stringval);
  g_free(key);

  return (NotifyMessage) curval;
}

static void update_notify_message(GtkToggleButton *togglebutton,
				  gpointer userdata)
{
  GtkWidget *preferences_dialog=GTK_WIDGET(userdata);
  PanelApplet *applet=PANEL_APPLET(g_object_get_data(G_OBJECT(preferences_dialog), "applet"));

  if(gtk_toggle_button_get_active(togglebutton))
    {
      gint newval=GPOINTER_TO_INT(g_object_get_data(G_OBJECT(togglebutton), "notify_message"));

      gchar *key=g_strdup_printf("%s/notify/notify_message",
				 panel_applet_get_preferences_key(applet));

      GError *err=NULL;

      gconf_client_set_string(confclient,
			      key,
			      gconf_enum_to_string(notify_message_lookup_table,
						   newval),
			      &err);

      handle_gerror("Can't set the notify_message key, is GConf working?\n\nError: %s", &err, false);

      g_free(key);
    }
}

static void notify_prefs_notify_message(GConfClient *client,
					guint cnxn_id,
					GConfEntry *entry,
					gpointer userdata)
{
  GtkWidget *preferences_dialog=GTK_WIDGET(userdata);
  PanelApplet *applet=PANEL_APPLET(g_object_get_data(G_OBJECT(preferences_dialog), "applet"));
  GtkWidget *notify_message_never=GTK_WIDGET(g_object_get_data(G_OBJECT(preferences_dialog), "notify_message_never"));
  GtkWidget *notify_message_security=GTK_WIDGET(g_object_get_data(G_OBJECT(preferences_dialog), "notify_message_security"));
  GtkWidget *notify_message_all=GTK_WIDGET(g_object_get_data(G_OBJECT(preferences_dialog), "notify_message_all"));

  NotifyMessage notify=get_notify_message(applet);

  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(notify_message_never), notify==NOTIFY_MESSAGE_NEVER);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(notify_message_security), notify==NOTIFY_MESSAGE_SECURITY);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(notify_message_all), notify==NOTIFY_MESSAGE_ALL);
}

void init_preferences_notify(PanelApplet *applet, GtkBuilder *builder)
{
  GtkWidget *preferences_dialog=GTK_WIDGET(gtk_builder_get_object(builder, "preferences_dialog"));

  GtkWidget *notify_message_all=GTK_WIDGET(gtk_builder_get_object(builder, "notify_message_all"));
  GtkWidget *notify_message_security=GTK_WIDGET(gtk_builder_get_object(builder, "notify_message_security"));
  GtkWidget *notify_message_never=GTK_WIDGET(gtk_builder_get_object(builder, "notify_message_never"));

  g_return_if_fail(notify_message_all && notify_message_security && notify_message_never);

  g_object_set_data(G_OBJECT(notify_message_all), "notify_message", GINT_TO_POINTER(NOTIFY_MESSAGE_ALL));
  g_object_set_data(G_OBJECT(notify_message_security), "notify_message", GINT_TO_POINTER(NOTIFY_MESSAGE_SECURITY));
  g_object_set_data(G_OBJECT(notify_message_never), "notify_message", GINT_TO_POINTER(NOTIFY_MESSAGE_NEVER));

  g_object_set_data(G_OBJECT(preferences_dialog), "applet", applet);
  g_object_set_data(G_OBJECT(preferences_dialog),
		    "notify_message_never", notify_message_never);
  g_object_set_data(G_OBJECT(preferences_dialog),
		    "notify_message_security", notify_message_security);
  g_object_set_data(G_OBJECT(preferences_dialog),
		    "notify_message_all", notify_message_all);

  const char *wname=NULL;

  // Start watching *before* reading the initial value, so there isn't
  // a race condition. (at worst there's an extra update)
  string key=string(panel_applet_get_preferences_key(applet))+"/notify/notify_message";

  GError *err=NULL;

  guint connection=gconf_client_notify_add(confclient, key.c_str(),
					   notify_prefs_notify_message,
					   preferences_dialog,
					   NULL, &err);

  handle_gerror("Can't monitor the notify_message key, is GConf working?\n\nError: %s", &err, false);

  switch(get_notify_message(applet))
    {
    case NOTIFY_MESSAGE_ALL:
      wname="notify_message_all";
      break;
    case NOTIFY_MESSAGE_SECURITY:
      wname="notify_message_security";
      break;
    case NOTIFY_MESSAGE_NEVER:
      wname="notify_message_never";
      break;
    }

  g_return_if_fail(wname!=NULL);

  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gtk_builder_get_object(builder, wname)), TRUE);

  g_signal_connect(preferences_dialog,
		   "destroy",
		   (GCallback) do_notify_remove,
		   GINT_TO_POINTER(connection));

  g_signal_connect(notify_message_all,
		   "toggled",
		   (GCallback) update_notify_message,
		   preferences_dialog),

  g_signal_connect(notify_message_security,
		   "toggled",
		   (GCallback) update_notify_message,
		   preferences_dialog);

  g_signal_connect(notify_message_never,
		   "toggled",
		   (GCallback) update_notify_message,
		   preferences_dialog);

}
