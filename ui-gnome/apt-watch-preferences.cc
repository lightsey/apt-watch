// apt-watch-preferences.cc
//
//  Copyright 2004 Daniel Burrows
//  Copyright 2011 John Lightsey
//
// We know about the following gconf preferences:
//
// <mydir>/check/check_freq : STRING
//
//   Controls the frequency of updates.  Acceptable values:
//
//   "never"  : never update
//   "daily"  : update at 24-hour intervals
//   "weekly" : update at 7-day intervals
//
//  In addiiton, the gnome-config mechanism is used to cache the most
//  recent update time (as an integer in UTC; ie, the return value of
//  time()) in ~.

#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <glib.h>

#include <string>

#include <panel-applet.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "apt-watch-gnome.h"
#include "prefs-check-freq.h"
#include "prefs-download-upgrade.h"
#include "prefs-package-manager.h"
#include "prefs-notify.h"

using namespace std;

const char *builder_file = DATADIR "/apt-watch/apt-watch.ui";

void do_notify_remove(GtkWidget *widget,
		      gpointer userdata)
{
  gconf_client_notify_remove(confclient, GPOINTER_TO_INT(userdata));
}

void do_preferences(gpointer data)
{
  PanelApplet *applet=(PanelApplet *) data;
  GtkBuilder *builder;
  GError *error=NULL;
  GtkWidget *preferences_dialog;

  builder=gtk_builder_new();
  
  if (!gtk_builder_add_from_file(builder, builder_file, &error)) {
      g_warning ("Couldn't load builder file: %s", error->message);
      g_error_free(error);
  }
  

  gtk_builder_connect_signals(builder, NULL);

  preferences_dialog=GTK_WIDGET(gtk_builder_get_object(builder, "preferences_dialog"));

  g_object_set_data(G_OBJECT(preferences_dialog), "applet", applet);

  init_preferences_check_freq(applet, builder);
  init_preferences_download_upgrades(applet, builder);
  init_preferences_package_manager(applet, builder);
  init_preferences_notify(applet, builder);
}
