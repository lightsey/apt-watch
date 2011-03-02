// apt-watch-preferences.cc
//
//  Copyright 2004 Daniel Burrows
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

#include <glade/glade-xml.h>
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

const char *glade_file = DATADIR "/apt-watch/apt-watch.glade";

void do_notify_remove(GtkWidget *widget,
		      gpointer userdata)
{
  gconf_client_notify_remove(confclient, GPOINTER_TO_INT(userdata));
}

void do_preferences(gpointer data)
{
  PanelApplet *applet=(PanelApplet *) data;

  GladeXML *xml;

  xml = glade_xml_new(glade_file, "preferences_dialog", NULL);

  glade_xml_signal_autoconnect(xml);

  GtkWidget *preferences_dialog=glade_xml_get_widget(xml, "preferences_dialog");

  g_object_set_data(G_OBJECT(preferences_dialog), "applet", applet);

  init_preferences_check_freq(applet, xml);
  init_preferences_download_upgrades(applet, xml);
  init_preferences_package_manager(applet, xml);
  init_preferences_notify(applet, xml);
}
