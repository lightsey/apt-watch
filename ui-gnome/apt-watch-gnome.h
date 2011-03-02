// Shared stuff for the apt-watch applet

#include <string>

#include <gconf/gconf-client.h>

struct _GtkWidget;
typedef struct _GtkWidget GtkWidget;

/** The possible frequencies with which to check for updates. */
enum CheckFreq {CHECK_NEVER, CHECK_DAILY, CHECK_WEEKLY};

/** The possible sets of upgrades to download. */
enum DownloadUpgrades {DOWNLOAD_NONE, DOWNLOAD_SECURITY, DOWNLOAD_ALL};

/** The possible types of notification. */
enum NotifyMessage {NOTIFY_MESSAGE_NEVER, NOTIFY_MESSAGE_SECURITY, NOTIFY_MESSAGE_ALL};

/** A data structure storing information about a hardwired package manager. */
struct package_manager
{
  std::string cmd;
  bool run_in_xterm;
};

// in apt-watch-preferences.cc
void do_preferences(gpointer data);
void do_notify_remove(GtkWidget *widget,
		      gpointer userdata);
CheckFreq get_check_freq(PanelApplet *applet);
DownloadUpgrades get_download_upgrades(PanelApplet *applet);
NotifyMessage get_notify_message(PanelApplet *applet);
const package_manager *get_package_manager(PanelApplet *applet);
extern const char *builder_file;

// in apt-watch.cc
extern GConfClient *confclient;

// in apt-watch-gnome.cc
bool handle_gerror(const char *msg,
		   GError **err,
		   bool show_dialog=true);
