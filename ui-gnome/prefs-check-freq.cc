// prefs-check-freq.cc                           -*-c++-*-
//
//  Copyright 2004 Daniel Burrows
//
// This file contains routines which manage the "automatic check frequency"
// Preferences setting.

#include "prefs-check-freq.h"

#include "apt-watch-gnome.h"

#include <string>

#include <gtk/gtk.h>
#include <glade/glade-xml.h>
#include <panel-applet.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

using namespace std;

static GConfEnumStringPair check_lookup_table[] = {
  {CHECK_NEVER, "never"},
  {CHECK_DAILY, "daily"},
  {CHECK_WEEKLY, "weekly"},
  {0, NULL}
};

CheckFreq get_check_freq(PanelApplet *applet)
{
  gint curval=CHECK_DAILY;

  gchar *key=g_strdup_printf("%s/check/check_freq",
			     panel_applet_get_preferences_key(applet));

  GError *err=NULL;

  gchar *stringval=gconf_client_get_string(confclient,
					   key,
					   &err);

  // Drop errors on the floor
  if(err)
    g_error_free(err);

  if(stringval)
    gconf_string_to_enum(check_lookup_table,
			 stringval,
			 &curval);

  g_free(stringval);
  g_free(key);

  return (CheckFreq) curval;
}

static void update_check_freq(GtkToggleButton *togglebutton,
			      gpointer userdata)
{
  GtkWidget *preferences_dialog=GTK_WIDGET(userdata);
  PanelApplet *applet=PANEL_APPLET(g_object_get_data(G_OBJECT(preferences_dialog), "applet"));
  GError *err=NULL;

  if(gtk_toggle_button_get_active(togglebutton) &&
     !g_object_get_data(G_OBJECT(preferences_dialog), "suppress_check_freq_toggle"))
    {
      gint newval=GPOINTER_TO_INT(g_object_get_data(G_OBJECT(togglebutton), "freq"));

      gchar *key=g_strdup_printf("%s/check/check_freq",
				 panel_applet_get_preferences_key(applet));

      gconf_client_set_string(confclient,
			      key,
			      gconf_enum_to_string(check_lookup_table,
						   newval),
			      &err);

      handle_gerror("Can't set check_freq key, is GConf working?\n\nError: %s",
		    &err, false);

      g_free(key);
    }
}

static void notify_prefs_check_freq(GConfClient *client,
				    guint cnxn_id,
				    GConfEntry *entry,
				    gpointer userdata)
{
  GtkWidget *preferences_dialog=GTK_WIDGET(userdata);
  PanelApplet *applet=PANEL_APPLET(g_object_get_data(G_OBJECT(preferences_dialog), "applet"));
  GtkWidget *check_weekly=GTK_WIDGET(g_object_get_data(G_OBJECT(preferences_dialog), "check_weekly"));
  GtkWidget *check_daily=GTK_WIDGET(g_object_get_data(G_OBJECT(preferences_dialog), "check_daily"));
  GtkWidget *check_never=GTK_WIDGET(g_object_get_data(G_OBJECT(preferences_dialog), "check_never"));

  // Suppress the handler my own way (yes, a bit gross)
  g_object_set_data(G_OBJECT(preferences_dialog),
		    "suppress_check_freq_toggle", GINT_TO_POINTER(1));

  CheckFreq freq=get_check_freq(applet);

  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_weekly), freq==CHECK_WEEKLY);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_daily), freq==CHECK_DAILY);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_never), freq==CHECK_NEVER);

  g_object_set_data(G_OBJECT(preferences_dialog),
		    "suppress_check_freq_toggle", GINT_TO_POINTER(0));
}

void init_preferences_check_freq(PanelApplet *applet, GladeXML *xml)
{
  GtkWidget *preferences_dialog=glade_xml_get_widget(xml, "preferences_dialog");

  GtkWidget *check_never=glade_xml_get_widget(xml, "check_never");
  GtkWidget *check_daily=glade_xml_get_widget(xml, "check_daily");
  GtkWidget *check_weekly=glade_xml_get_widget(xml, "check_weekly");

  g_return_if_fail(check_never && check_daily && check_weekly);

  g_object_set_data(G_OBJECT(check_never), "freq", GINT_TO_POINTER(CHECK_NEVER));
  g_object_set_data(G_OBJECT(check_daily), "freq", GINT_TO_POINTER(CHECK_DAILY));
  g_object_set_data(G_OBJECT(check_weekly), "freq", GINT_TO_POINTER(CHECK_WEEKLY));

  g_object_set_data(G_OBJECT(preferences_dialog), "applet", applet);
  g_object_set_data(G_OBJECT(preferences_dialog),
		    "suppress_check_freq_toggle", GINT_TO_POINTER(0));
  g_object_set_data(G_OBJECT(preferences_dialog),
		    "check_weekly", check_weekly);
  g_object_set_data(G_OBJECT(preferences_dialog),
		    "check_daily", check_daily);
  g_object_set_data(G_OBJECT(preferences_dialog),
		    "check_never", check_never);

  const char *wname=NULL;

  // Start watching *before* reading the initial value, so there isn't
  // a race condition. (at worst there's an extra update)
  string key=string(panel_applet_get_preferences_key(applet))+"/check/check_freq";

  GError *err=NULL;

  guint connection=gconf_client_notify_add(confclient, key.c_str(),
					   notify_prefs_check_freq,
					   preferences_dialog,
					   NULL, &err);

  handle_gerror("Can't monitor the check_freq key, is GConf working?\n\nError: %s", &err, false);

  switch(get_check_freq(applet))
    {
    case CHECK_NEVER:
      wname="check_never";
      break;
    case CHECK_DAILY:
      wname="check_daily";
      break;
    case CHECK_WEEKLY:
      wname="check_weekly";
      break;
    }

  g_return_if_fail(wname!=NULL);

  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(glade_xml_get_widget(xml,
								      wname)),
			       TRUE);

  g_signal_connect(preferences_dialog,
		   "destroy",
		   (GCallback) do_notify_remove,
		   GINT_TO_POINTER(connection));

  g_signal_connect(check_never,
		   "toggled",
		   (GCallback) update_check_freq,
		   preferences_dialog),

  g_signal_connect(check_daily,
		   "toggled",
		   (GCallback) update_check_freq,
		   preferences_dialog);

  g_signal_connect(check_weekly,
		   "toggled",
		   (GCallback) update_check_freq,
		   preferences_dialog);

}
