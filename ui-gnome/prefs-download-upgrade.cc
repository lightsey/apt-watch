// prefs-download-upgrade.cc
//
//  Copyright 2004 Daniel Burrows
//  Copyright 2011 John Lightsey
//
// Code to manage the "what upgrades to download" portion of the preferences
// dialog.

#include "prefs-download-upgrade.h"

#include "apt-watch-gnome.h"

#include <string>

#include <gtk/gtk.h>
#include <panel-applet.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

using namespace std;

static GConfEnumStringPair upgrade_lookup_table[] = {
  {DOWNLOAD_NONE, "none"},
  {DOWNLOAD_SECURITY, "security"},
  {DOWNLOAD_ALL, "all"},
  {0, NULL}
};

DownloadUpgrades get_download_upgrades(PanelApplet *applet)
{
  gint curval=DOWNLOAD_ALL;

  gchar *key=g_strdup_printf("%s/download/download_upgrades",
			     panel_applet_get_preferences_key(applet));

  GError *err=NULL;

  gchar *stringval=gconf_client_get_string(confclient,
					   key,
					   &err);

  if(err)
    g_error_free(err);

  if(stringval)
    gconf_string_to_enum(upgrade_lookup_table,
			 stringval,
			 &curval);

  g_free(stringval);
  g_free(key);

  return (DownloadUpgrades) curval;
}

void update_download_upgrades(GtkToggleButton *togglebutton,
			      gpointer userdata)
{
  GtkWidget *preferences_dialog=GTK_WIDGET(userdata);
  PanelApplet *applet=PANEL_APPLET(g_object_get_data(G_OBJECT(preferences_dialog), "applet"));

  if(gtk_toggle_button_get_active(togglebutton) &&
     !g_object_get_data(G_OBJECT(preferences_dialog), "suppress_download_upgrades_toggle"))
    {
      gint newval=GPOINTER_TO_INT(g_object_get_data(G_OBJECT(togglebutton), "download"));

      gchar *key=g_strdup_printf("%s/download/download_upgrades",
				 panel_applet_get_preferences_key(applet));

      GError *err=NULL; 

      gconf_client_set_string(confclient,
			      key,
			      gconf_enum_to_string(upgrade_lookup_table,
						   newval),
			      &err);

      handle_gerror("Can't set the download_upgrades key, is GConf working?\n\nError: %s", &err, false);

      g_free(key);
    }
}

static void notify_prefs_download_upgrades(GConfClient *client,
					   guint cnxn_id,
					   GConfEntry *entry,
					   gpointer userdata)
{
  GtkWidget *preferences_dialog=GTK_WIDGET(userdata);
  PanelApplet *applet=PANEL_APPLET(g_object_get_data(G_OBJECT(preferences_dialog), "applet"));

  GtkWidget *download_none=GTK_WIDGET(g_object_get_data(G_OBJECT(preferences_dialog), "download_none"));
  GtkWidget *download_security=GTK_WIDGET(g_object_get_data(G_OBJECT(preferences_dialog), "download_security"));
  GtkWidget *download_all=GTK_WIDGET(g_object_get_data(G_OBJECT(preferences_dialog), "download_all"));

  // Suppress the handler my own way (yes, a bit gross)
  g_object_set_data(G_OBJECT(preferences_dialog),
		    "suppress_download_upgrades_toggle", GINT_TO_POINTER(1));

  DownloadUpgrades download=get_download_upgrades(applet);

  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(download_none), download==DOWNLOAD_NONE);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(download_security), download==DOWNLOAD_SECURITY);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(download_all), download==DOWNLOAD_ALL);

  g_object_set_data(G_OBJECT(preferences_dialog),
		    "suppress_download_upgrades_toggle", GINT_TO_POINTER(0));
}

void init_preferences_download_upgrades(PanelApplet *applet, GtkBuilder *builder)
{
  GtkWidget *preferences_dialog=GTK_WIDGET(gtk_builder_get_object(builder, "preferences_dialog"));
  GtkWidget *download_none=GTK_WIDGET(gtk_builder_get_object(builder, "download_none"));
  GtkWidget *download_security=GTK_WIDGET(gtk_builder_get_object(builder, "download_security"));
  GtkWidget *download_all=GTK_WIDGET(gtk_builder_get_object(builder, "download_all"));

  g_return_if_fail(download_none && download_security && download_all);

  g_object_set_data(G_OBJECT(download_none), "download", GINT_TO_POINTER(DOWNLOAD_NONE));
  g_object_set_data(G_OBJECT(download_security), "download", GINT_TO_POINTER(DOWNLOAD_SECURITY));
  g_object_set_data(G_OBJECT(download_all), "download", GINT_TO_POINTER(DOWNLOAD_ALL));

  g_object_set_data(G_OBJECT(preferences_dialog),
		    "suppress_download_upgrades_toggle", GINT_TO_POINTER(0));

  g_object_set_data(G_OBJECT(preferences_dialog),
		    "download_none", download_none);
  g_object_set_data(G_OBJECT(preferences_dialog),
		    "download_security", download_security);
  g_object_set_data(G_OBJECT(preferences_dialog),
		    "download_all", download_all);

  const char *wname=NULL;

  string key=string(panel_applet_get_preferences_key(applet))+"/download/download_upgrades";

  GError *err=NULL;

  guint connection=gconf_client_notify_add(confclient, key.c_str(),
					   notify_prefs_download_upgrades,
					   preferences_dialog,
					   NULL, &err);

  handle_gerror("Can't monitor the download_upgrades key, is GConf working?\n\nError: %s", &err, false);

  switch(get_download_upgrades(applet))
    {
    case DOWNLOAD_NONE:
      wname="download_none";
      break;
    case DOWNLOAD_SECURITY:
      wname="download_security";
      break;
    case DOWNLOAD_ALL:
      wname="download_all";
      break;
    }

  g_return_if_fail(wname!=NULL);

  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gtk_builder_get_object(builder, wname)), TRUE);

  g_signal_connect(preferences_dialog,
		   "destroy",
		   (GCallback) do_notify_remove,
		   GINT_TO_POINTER(connection));

  g_signal_connect(download_none,
		   "toggled",
		   (GCallback) update_download_upgrades,
		   preferences_dialog),

  g_signal_connect(download_security,
		   "toggled",
		   (GCallback) update_download_upgrades,
		   preferences_dialog);

  g_signal_connect(download_all,
		   "toggled",
		   (GCallback) update_download_upgrades,
		   preferences_dialog);

}
