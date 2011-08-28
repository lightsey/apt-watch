// apt-watch: lets you know when updates for your system are available. whee.

#include <string>

#include <gdk/gdk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gtk/gtk.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <panel-applet.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <assert.h>

#include <sys/resource.h>

#include <string>

#include "apt-watch-common.h"
#include "apt-watch-gnome.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

using namespace std;

typedef enum
  {
    NEED_SLAVE_START,
    PREINIT,
    ERROR,
    IDLE,
    UPDATING,
    DOWNLOADING,
  } applet_state;

// TODO: this must all be re-entrant, so it should be wrapped in a class or
// something and attached to the applet as properties.

applet_state state=NEED_SLAVE_START;
bool reloading;
bool can_upgrade;
bool security_upgrades_available;
bool pending_update=false, pending_reload=false, pending_notify=false;

// used for the progress stuff.
string progress_message;
float progress_percent;
bool progress_majorchange;

time_t last_timeout;
tm *last_tm;

GtkImage *icon;
GtkWidget *ebox;

GdkPixbuf *static_swirl;
GdkPixbufAnimation *rotating_swirl;

GConfClient *confclient;

gint to_slave, from_slave;
guint from_slave_input;

// Stores the timeout for the update.
guint update_timeout;

// Menu action group
GtkActionGroup *menu_action_group;


static gboolean do_update(gpointer data);
static gboolean do_update_timeout(gpointer data);
static gboolean do_reload(gpointer data);
static bool start_slave(PanelApplet *applet);
static void drop_slave(PanelApplet *);
static void set_state(applet_state new_state,
		      PanelApplet *applet);

#ifdef DEBUG
FILE *log;

static void start_log()
{
  if(log)
    return;

  gchar *fn=g_strdup_printf("/tmp/apt-watch.log");

  log=fopen(fn, "w");

  if(log==NULL)
    {
      gchar *message=g_strdup_printf("Couldn't open the debugging log file %s!",
				     fn);

      GtkWidget *dlg=gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL,
					    GTK_MESSAGE_ERROR,
					    GTK_BUTTONS_CLOSE,
					    message);

      gtk_dialog_run(GTK_DIALOG(dlg));

      gtk_widget_destroy(dlg);

      g_free(message);
    }

  g_free(fn);
}

void do_log(const char *format, ...)
{
  if(!log)
    return;

  va_list args;

  va_start(args, format);

  vfprintf(log, format, args);

  va_end(args);

  fflush(log);
}
#else
inline static void start_log() { }

inline void do_log(const char *format, ...) { }
#endif

// Handle a GError.
bool handle_gerror(const char *msg,
		   GError **err,
		   bool show_dialog)
{
  if((*err)!=NULL)
    {
      if(show_dialog)
	{
	  GtkWidget *w=gtk_message_dialog_new(NULL,
					      GTK_DIALOG_MODAL,
					      GTK_MESSAGE_ERROR,
					      GTK_BUTTONS_OK,
					      msg, (*err)->message);

	  gtk_dialog_run(GTK_DIALOG(w));

	  gtk_widget_destroy(w);
	}
      else
	do_log(msg, err);

      if(err!=NULL)
	g_error_free(*err);
      *err=NULL;

      return true;
    }

  return false;
}

// Handle an error code from a pipe.  Broken pipes are expected and
// will be ignored (they are handled programmatically as EOF), but
// other errors are displayed as a dialog box.
static bool handle_gio_error(const char *msg,
			     GError **err,
			     bool show_dialog=true)
{
  if((*err)!=NULL && (*err)->code==G_IO_CHANNEL_ERROR_PIPE)
    {
      g_error_free(*err);
      *err=NULL;
      return false;
    }
  else
    return handle_gerror(msg, err, show_dialog);
}

static void write_download_msg(int fd, bool update_all)
{
  write_msgid(fd, APPLET_CMD_DOWNLOAD);
  write(fd, &update_all, sizeof(update_all));
}

static void maybe_download(PanelApplet *applet)
{
  // Trigger downloading if applicable:
  if(state==IDLE && !reloading && can_upgrade)
    {
      DownloadUpgrades download=get_download_upgrades(applet);

      if(download == DOWNLOAD_ALL ||
	 (download == DOWNLOAD_SECURITY && security_upgrades_available))
	{
	  write_download_msg(to_slave, download==DOWNLOAD_ALL);
	  set_state(DOWNLOADING, applet);
	}
    }
}

void setup_update_timeout(PanelApplet *applet)
{
  if(update_timeout!=0)
    g_source_remove(update_timeout);

  CheckFreq freq=get_check_freq(applet);

  GError *err=NULL;

  string key=string(panel_applet_get_preferences_key(applet))+"/check/last_check";

  last_timeout=gconf_client_get_int(confclient,
                                    key.c_str(),
                                    &err);
  last_tm = localtime(&last_timeout);
  if(err!=NULL)
  {
    last_timeout=0;
    g_error_free(err);
    err=NULL;
  }

  if(freq!=CHECK_NEVER)
    {
      time_t next_timeout=last_timeout+(freq==CHECK_WEEKLY?7:1)*24*60*60;

      time_t curtime=time(0);

      if(next_timeout<=curtime)
	{
	  do_log("Last update was at %d seconds, scheduling immediate update\n",
		 last_timeout);
	  // Update right away if necessary.
	  do_update(applet);
	}
      else
	{
	  do_log("Scheduling update in %d seconds\n", next_timeout-curtime);
	  update_timeout=g_timeout_add((next_timeout-curtime)*1000,
				       do_update_timeout,
				       applet);
	}
    }
  else
    do_log("Automatic update is disabled.\n");
}

static void notify_check_freq(GConfClient *client,
			      guint cnxn_id,
			      GConfEntry *entry,
			      gpointer userdata)
{
  PanelApplet *applet=(PanelApplet *) userdata;

  setup_update_timeout(applet);
}

static void notify_download_upgrades(GConfClient *client,
				     guint cnxn_id,
				     GConfEntry *entry,
				     gpointer userdata)
{
  maybe_download((PanelApplet *) userdata);
}

static void bonobo_about(GtkAction       *action,
                         gpointer data)
{
  GtkBuilder *builder;
  GError *error=NULL;
  gchar *toplevel[] = {(gchar*)"about", NULL};

  builder = gtk_builder_new();
  if (!gtk_builder_add_objects_from_file(builder, builder_file, toplevel, &error)) {
      g_warning ("Couldn't load builder file: %s", error->message);
      g_error_free(error);
  }

  gtk_builder_connect_signals(builder, NULL);
}

static void bonobo_update(GtkAction       *action,
                         gpointer data)
{
  do_update(data);
}

static void bonobo_start(GtkAction       *action,
                         gpointer data)
{
  PanelApplet *applet=(PanelApplet *) data;

  start_slave(applet);
}

static void bonobo_prefs(GtkAction       *action,
                         gpointer data)
{
  do_preferences(data);
}

static void start_package_manager(PanelApplet *applet)
{
  const package_manager *manager=get_package_manager(applet);

  write_msgid(to_slave, APPLET_CMD_SU);
  write(to_slave, &manager->run_in_xterm, sizeof(bool));
  write_string(to_slave, manager->cmd);
}

static void bonobo_package_manager(GtkAction       *action,
                         gpointer data)
{
  if(state==IDLE)
    start_package_manager(PANEL_APPLET(data));
}

static void bonobo_download(GtkAction       *action,
                         gpointer data)
{
  if(state==IDLE)
    {
      PanelApplet *applet=(PanelApplet *) data;

      write_download_msg(to_slave, true);
      set_state(DOWNLOADING, applet);
    }
}

static void bonobo_cancel_download(GtkAction       *action,
                         gpointer data)
{
  if(state==DOWNLOADING || state==UPDATING)
    write_msgid(to_slave, APPLET_CMD_ABORT_DOWNLOAD);
}

static const GtkActionEntry my_verbs[]={
    { "Start", NULL, N_("Start"), NULL, NULL, G_CALLBACK (bonobo_start)},
    { "UpdateNow", NULL, N_("Update Now"), NULL, NULL, G_CALLBACK (bonobo_update)},
    { "PkgManager", NULL, N_("Open Package Manager"), NULL, NULL, G_CALLBACK (bonobo_package_manager)},
    { "Download", NULL, N_("Download"), NULL, NULL, G_CALLBACK (bonobo_download)},
    { "CancelDownload", NULL, N_("Cancel Download"), NULL, NULL, G_CALLBACK (bonobo_cancel_download)},
    { "Preferences", NULL, N_("Preferences"), NULL, NULL, G_CALLBACK (bonobo_prefs)},
    { "About", GTK_STOCK_ABOUT, N_("About"), NULL, NULL, G_CALLBACK (bonobo_about) }
};

const char my_menu[]=
    "<menuitem name=\"Start\" action=\"Start\" />"
    "<menuitem name=\"Update Now\" action=\"UpdateNow\" />"
    "<menuitem name=\"Open Package Manager\" action=\"PkgManager\" />"
    "<menuitem name=\"Download\" action=\"Download\" />"
    "<menuitem name=\"Cancel Download\" action=\"CancelDownload\" />"
    "<menuitem name=\"Preferences\" action=\"Preferences\" />"
    "<menuitem name=\"About\" action=\"About\" />";

static void set_menu_sensitive(PanelApplet *applet,
			       const string &item, bool sensitive)
{
    GtkAction *selected_action = gtk_action_group_get_action(menu_action_group, item.c_str());
    if (selected_action != NULL) {
        gtk_action_set_sensitive(selected_action, sensitive);
    }
    else {
        do_log("Failed to find GtkAction %s for set_menu_sensitive()", item.c_str());
    }
}

static void set_menu_visible(PanelApplet *applet,
			    const string &item, bool visible)
{
    GtkAction *selected_action = gtk_action_group_get_action(menu_action_group, item.c_str());
    if (selected_action != NULL) {
        gtk_action_set_visible(selected_action, visible);
    }
    else {
        do_log("Failed to find GtkAction %s for set_menu_visible()", item.c_str());
    }
}

static void set_state(applet_state new_state,
		      PanelApplet *applet)
{
  applet_state old_state=state;
  state=new_state;
  string msg;
  char tbuf[100];

  switch(state)
    {
    case NEED_SLAVE_START:
      msg="Not Watching";

      gtk_image_set_from_stock(icon, GTK_STOCK_STOP, GTK_ICON_SIZE_LARGE_TOOLBAR);

      break;
    case PREINIT:
      msg="Initializing";

      if(old_state!=UPDATING && old_state!=PREINIT && old_state!=DOWNLOADING)
	gtk_image_set_from_animation(icon, rotating_swirl);
      break;
    case ERROR:
      msg="Error";

      gtk_image_set_from_stock(icon, GTK_STOCK_DIALOG_ERROR, GTK_ICON_SIZE_LARGE_TOOLBAR);
      break;
    case IDLE:
      if(!can_upgrade) {
	msg="No upgrades available.\nChecked on ";
	strftime(tbuf, 100, "%m-%d at %l:%M %p", last_tm);
	msg += tbuf;
      } else if(!security_upgrades_available)
	msg="Upgrades available";
      else
	msg="Security upgrades available";

      if(!can_upgrade)
	gtk_image_set_from_pixbuf(icon, static_swirl);
      else if(!security_upgrades_available)
	gtk_image_set_from_stock(icon, GTK_STOCK_DIALOG_INFO, GTK_ICON_SIZE_LARGE_TOOLBAR);
      else
	gtk_image_set_from_stock(icon, GTK_STOCK_DIALOG_WARNING, GTK_ICON_SIZE_LARGE_TOOLBAR);
      break;
    case UPDATING:
      msg="Checking for new upgrades";

      if(old_state!=UPDATING && old_state!=PREINIT && old_state!=DOWNLOADING)
	gtk_image_set_from_animation(icon, rotating_swirl);
      break;
    case DOWNLOADING:
      msg="Downloading upgrades";

      if(old_state!=UPDATING && old_state!=PREINIT && old_state!=DOWNLOADING)
	gtk_image_set_from_animation(icon, rotating_swirl);
      break;
    default:
      msg="Internal error: bad state";

      gtk_image_set_from_stock(icon, GTK_STOCK_DIALOG_ERROR, GTK_ICON_SIZE_LARGE_TOOLBAR);
      break;
    }

  // Note that progress information from downloading package lists is useless
  // thanks to bug #168710.
  if(!progress_message.empty() && state!=UPDATING)
    {
      char buf[1024];

      // Percentage information is useless and confusing except for
      // downloading packages.  TODO: send that fact with the download
      // packet.
      if(state!=DOWNLOADING)
	snprintf(buf, 1024, "%s (%s)",
		 msg.c_str(), progress_message.c_str());
      else
	snprintf(buf, 1024, "%s (%s): %d%%",
		 msg.c_str(), progress_message.c_str(), (int) progress_percent);

      msg=buf;
    }

  gtk_widget_set_tooltip_text (ebox, msg.c_str());

  if((state==IDLE) &&
     pending_update)
    {
      pending_update=false;
      pending_reload=false;
      do_update(applet);
    }

  if((state==IDLE) && pending_reload)
    {
      pending_reload=false;
      do_reload(applet);
    }

  set_menu_visible(applet, "Start", state==NEED_SLAVE_START);
  set_menu_visible(applet, "CancelDownload", (state==DOWNLOADING || state==UPDATING));

  set_menu_sensitive(applet, "UpdateNow", state==IDLE);
  set_menu_sensitive(applet, "Download", state==IDLE);
  set_menu_sensitive(applet, "PkgManager", state==IDLE);
}

// Returns TRUE iff an update was actually carried out.
static gboolean do_update(gpointer data)
{
  if(state==IDLE && !reloading)
    {
      do_log("Updating\n");

      unsigned char msg=APPLET_CMD_UPDATE;
      PanelApplet *applet=(PanelApplet *) data;

      set_state(UPDATING, applet);
      write(to_slave, &msg, sizeof(msg));

      string key=string(panel_applet_get_preferences_key(applet))+"/check/last_check";

      GError *err=NULL;

      gconf_client_set_int(confclient, key.c_str(), time(0), &err);

      if(handle_gerror("Unable to store the current time: is GConf working?\n\nError: %s",
		       &err, false))
	{
	  // hmm, do I need to drop the slave connection here?
	  drop_slave(applet);
	  set_state(ERROR, applet);

	  return FALSE;
	}
      else
	{
	  setup_update_timeout(applet);

	  return TRUE;
	}
    }
  else
    {
      pending_update=true;

      return FALSE;
    }
}

static gboolean do_update_timeout(gpointer data)
{
  do_update(data);

  return FALSE;
}

static gboolean do_reload(gpointer data)
{
  if(state==IDLE && !reloading)
    {
      unsigned char msg=APPLET_CMD_RELOAD;

      reloading=true;
      set_state(state,
		(PanelApplet *) data);
      write(to_slave, &msg, sizeof(msg));
    }
  else
    pending_reload=true;

  return TRUE;
}

static void
report_failed_grab (const char *what)
{
  GtkWidget *err;

  err = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL,
			       GTK_MESSAGE_ERROR,
			       GTK_BUTTONS_CLOSE,
			       "Could not grab %s. "
			       "A malicious client may be eavesdropping "
			       "on your session.", what);
  gtk_window_set_position(GTK_WINDOW(err), GTK_WIN_POS_CENTER);

  gtk_dialog_run(GTK_DIALOG(err));

  gtk_widget_destroy(err);
}

static void root_slave_ok(GtkWidget *entry, gpointer dialog)
{
  gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
}

// some code "stolen" from openssh.  FIXME: probably I need to import a
// copyright notice or replace this or something?
static int ask_for_auth_info(const string &msg, bool echo)
{
  const char *failed;
  char *reply, *local;
  int result;

  GtkWidget *dialog, *entry;
  GdkGrabStatus status;

  dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL,
				  GTK_MESSAGE_QUESTION,
				  GTK_BUTTONS_OK_CANCEL,
				  "%s",
				  msg.c_str());

  entry = gtk_entry_new();
  gtk_box_pack_start(GTK_BOX(gtk_message_dialog_get_message_area(GTK_MESSAGE_DIALOG(dialog))), entry, FALSE,
		     FALSE, 0);
  if(!echo)
    gtk_entry_set_visibility(GTK_ENTRY(entry), FALSE);
  gtk_widget_grab_focus(entry);
  gtk_widget_show(entry);

  gtk_window_set_title(GTK_WINDOW(dialog), "Upgrade notification");
  gtk_window_set_position (GTK_WINDOW(dialog), GTK_WIN_POS_CENTER);

  /* Make <enter> close dialog */
  gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
  g_signal_connect(G_OBJECT(entry), "activate",
		   G_CALLBACK(root_slave_ok), dialog);

  /* Grab focus */
  gtk_widget_show_now(dialog);

  status = gdk_keyboard_grab(gtk_widget_get_parent_window(dialog), FALSE,
			     GDK_CURRENT_TIME);

  if (status != GDK_GRAB_SUCCESS)
    {
      failed = "keyboard";
      /* At least one grab failed - ungrab what we got, and report
	 the failure to the user.  Note that XGrabServer() cannot
	 fail.  */

      gdk_pointer_ungrab(GDK_CURRENT_TIME);

      gtk_widget_destroy(dialog);
        
      report_failed_grab(failed);
    }
  else
    {
      result = gtk_dialog_run(GTK_DIALOG(dialog));

      gdk_keyboard_ungrab(GDK_CURRENT_TIME);
      gdk_flush();

      /* Report reply if user selected OK */
      reply = g_strdup(gtk_entry_get_text(GTK_ENTRY(entry)));
      if (result == GTK_RESPONSE_OK) {
	string::size_type s=strlen(reply);

	GError *err=NULL;

	local = NULL;

	local = g_locale_from_utf8(reply, s,
				   NULL, NULL, &err);

	handle_gerror("Unable to convert input from UTF8: %s",
		      &err);

	if (local != NULL) {
	  s=strlen(local);
	  write_msgid(to_slave, APPLET_CMD_AUTHREPLY);
	  write(to_slave, &s, sizeof(s));
	  write(to_slave, local, s);

	  memset(local, '\0', strlen(local));
	  g_free(local);
	} else {
	  write_msgid(to_slave, APPLET_CMD_AUTHREPLY);
	  write(to_slave, &s, sizeof(s));
	  write(to_slave, reply, s);
	}

	s=0;
      }

      /* Zero reply in memory */
      memset(reply, '\b', strlen(reply));
      gtk_entry_set_text(GTK_ENTRY(entry), reply);
      memset(reply, '\0', strlen(reply));
      g_free(reply);
      gtk_widget_destroy(dialog);
      return (result == GTK_RESPONSE_OK ? 0 : -1);
    }

  return (-1);

}

// assumes the string is in the form specified in protocols.txt
//
// Returns as many chars as can be read, or NULL if there is an error (ie,
// no characters are available)
static string read_string(GIOChannel *source)
{
  string::size_type size;
  gsize amt_read;
  GError *err=NULL;

  g_io_channel_read_chars(source,
			  (gchar *) &size,
			  sizeof(size),
			  &amt_read,
			  &err);

  if(handle_gio_error("I can't read a string from the slave: %s", &err))
    return "";

  if(amt_read<sizeof(size))
    {
      do_log("I can't read a string from the slave!\n");

      return "";
    }

  char *buf=(char *) malloc(size+1);
  g_io_channel_read_chars(source, buf, size, &amt_read, &err);

  handle_gio_error("Unexpected error reading data from the slave: %s", &err);

  buf[amt_read]=0;

  do_log("Read \"%s\" from the slave.\n", buf);

  string rval=buf;

  g_free(buf);

  return rval;
}

// TODO: decide when to trigger this by wrapping accessors around
// the upgrade state.

/** Display a modal dialog box explaining that upgrades are available. */
static void do_notify(PanelApplet *applet)
{
  g_return_if_fail(security_upgrades_available || can_upgrade);

  g_return_if_fail(pending_notify);

  pending_notify=false;

  GtkBuilder *builder;
  GError *error=NULL;
  gchar *toplevel[] = {(gchar *)"upgrade_dialog", NULL};

  builder = gtk_builder_new();
  if (!gtk_builder_add_objects_from_file(builder, builder_file, toplevel, &error)) {
      g_warning ("Couldn't load builder file: %s", error->message);
      g_error_free(error);
      return;
  }

  gtk_builder_connect_signals(builder, NULL);

  string message;

  GtkWidget *dialog=GTK_WIDGET(gtk_builder_get_object(builder, "upgrade_dialog"));
  GtkWidget *label=GTK_WIDGET(gtk_builder_get_object(builder, "upgrade_message"));

  if(security_upgrades_available)
    message="<span weight=\"bold\" size=\"larger\">There are security upgrades available</span>";
  else
    message="<span weight=\"bold\" size=\"larger\">There are upgrades available</span>";

  // message+="  Click the button below to install them.";
  
  gtk_label_set_use_markup(GTK_LABEL(label), TRUE);
  gtk_label_set_label(GTK_LABEL(label), message.c_str());

  if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT)
    start_package_manager(applet);

  gtk_widget_destroy(dialog);
}

static gboolean handle_slave_msg(GIOChannel *source,
				 GIOCondition condition,
				 gpointer data)
{
  unsigned char msgtype;
  GtkWidget *dlg;
  string s;
  PanelApplet *applet=(PanelApplet *) data;
  GError *error=NULL;

  // Can I just read from the underlying fd?  I don't trust glib's io
  // channels.
  GIOStatus status=g_io_channel_read_chars(source,
					   (gchar *) &msgtype,
					   sizeof(msgtype),
					   NULL,
					   &error);

  do_log("Got message %d\n", msgtype);

  switch(status)
    {
    case G_IO_STATUS_ERROR:
      do_log("Error reading from the slave: %s\n",
	     error->message);

      drop_slave(applet);
      break;
    case G_IO_STATUS_EOF:
      do_log("Got EOF while reading from the slave.\n");

      drop_slave(applet);
      break;
    case G_IO_STATUS_AGAIN:
      break;
    case G_IO_STATUS_NORMAL:
      switch(msgtype)
	{
	case APPLET_REPLY_AUTH_PROMPT_NOECHO:
	case APPLET_REPLY_AUTH_PROMPT_ECHO:
	  s=read_string(source);

	  if(s.empty())
	    {
	      drop_slave(applet);

	      break;
	    }

	  if(ask_for_auth_info(s, msgtype==APPLET_REPLY_AUTH_PROMPT_ECHO)!=0)
	    write_msgid(to_slave, APPLET_CMD_AUTHCANCEL);
	  break;

	case APPLET_REPLY_PROGRESS_UPDATE:
	  {
	    string op;

	    float percent;

	    gsize amt_read;

	    bool majorchange;

	    GError *err=NULL;

	    op=read_string(source);

	    g_io_channel_read_chars(source,
				    (gchar*) &percent,
				    sizeof(percent),
				    &amt_read,
				    &err);

	    if(handle_gio_error("Protocol error: can't read the percentage for the progress indicator:\n%s", &err))
	      {
		drop_slave(applet);
		break;
	      }

	    g_io_channel_read_chars(source,
				    (gchar*) &majorchange,
				    sizeof(majorchange),
				    &amt_read,
				    &err);

	    if(handle_gio_error("Protocol error: can't read the major change flag for the progress indicator:\n%s", &err))
	      {
		drop_slave(applet);
		break;
	      }

	    if(amt_read==0)
	      {
		drop_slave(applet);
		break;
	      }

	    do_log("Progress: %s %d%%\n",
		   op.c_str(), (int) percent);

	    progress_message=op;
	    progress_percent=percent * 2;
	    progress_majorchange=majorchange;

	    set_state(state, applet);
	  }
	  break;

	case APPLET_REPLY_PROGRESS_DONE:
	  do_log("Progress done.\n");
	  progress_message="";
	  progress_percent=100;
	  progress_majorchange=true;

	  set_state(state, applet);
	  break;

	case APPLET_REPLY_AUTH_OK:
	  break;
	
	case APPLET_REPLY_AUTH_FINISHED:
	  do_reload(applet);
	  break;

	case APPLET_REPLY_CMD_COMPLETE_NOUPGRADES:
	case APPLET_REPLY_CMD_COMPLETE_UPGRADES:
	case APPLET_REPLY_CMD_COMPLETE_SECURITY_UPGRADES:
	  assert(state==UPDATING || (state==IDLE && reloading));

	  reloading=false;

	  // fallthrough

	case APPLET_REPLY_INIT_OK_NOUPGRADES:
	case APPLET_REPLY_INIT_OK_UPGRADES:
	case APPLET_REPLY_INIT_OK_SECURITY_UPGRADES:
	  {
	    bool old_can_upgrade=can_upgrade;
	    bool old_security_upgrades_available=security_upgrades_available;
	    NotifyMessage notify=get_notify_message(applet);

	    // hm
	    if(msgtype==APPLET_REPLY_INIT_OK_NOUPGRADES ||
	       msgtype==APPLET_REPLY_INIT_OK_UPGRADES ||
	       msgtype==APPLET_REPLY_INIT_OK_SECURITY_UPGRADES)
	      assert(state==PREINIT);

	    can_upgrade=(msgtype!=APPLET_REPLY_INIT_OK_NOUPGRADES &&
			 msgtype!=APPLET_REPLY_CMD_COMPLETE_NOUPGRADES);
	    security_upgrades_available=(msgtype==APPLET_REPLY_INIT_OK_SECURITY_UPGRADES ||
					 msgtype==APPLET_REPLY_CMD_COMPLETE_SECURITY_UPGRADES);

	    set_state(IDLE, applet);

	    maybe_download(applet);

	    if(notify==NOTIFY_MESSAGE_ALL &&
	       !old_can_upgrade && can_upgrade)
	      pending_notify=true;
	    else if(notify==NOTIFY_MESSAGE_SECURITY &&
		    !old_security_upgrades_available &&
		    security_upgrades_available)
	      pending_notify=true;

	    if(state == IDLE && pending_notify)
	      do_notify(applet);
	    break;
	  }

	case APPLET_REPLY_INIT_FAILED:
	  assert(state==PREINIT);
	case APPLET_REPLY_AUTH_FAIL:
	case APPLET_REPLY_AUTH_ERRORMSG:
	case APPLET_REPLY_AUTH_INFO:
	case APPLET_REPLY_FATALERROR:
	  s=read_string(source);

	  if(s.empty())
	    {
	      drop_slave(applet);

	      break;
	    }

	  dlg=gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL,
				     (msgtype==APPLET_REPLY_AUTH_INFO)?GTK_MESSAGE_INFO:GTK_MESSAGE_ERROR,
				     GTK_BUTTONS_CLOSE,
				     (msgtype==APPLET_REPLY_INIT_FAILED)?
				     "Failed to load the apt cache:\n%s":
				     "%s",
				     s.c_str());

	  gtk_window_set_position(GTK_WINDOW(dlg), GTK_WIN_POS_CENTER);

	  gtk_dialog_run(GTK_DIALOG(dlg));

	  gtk_widget_destroy(dlg);

	  if(msgtype==APPLET_REPLY_INIT_FAILED ||
	     msgtype==APPLET_REPLY_FATALERROR)
	    {
	      drop_slave(applet);	
	      set_state(ERROR, applet);
	    }
	  break;

	case APPLET_REPLY_REQUEST_RELOAD:
	  do_reload(applet);

	  break;

	case APPLET_REPLY_DOWNLOAD_COMPLETE:
	  {
	    set_state(IDLE, applet);

	    if(pending_notify)
	      do_notify(applet);

	    break;
	  }

	default:
	  dlg=gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL,
				     GTK_MESSAGE_ERROR,
				     GTK_BUTTONS_CLOSE,
				     "Garbled communication from slave: message code %d",
				     msgtype);

	  gtk_window_set_position(GTK_WINDOW(dlg), GTK_WIN_POS_CENTER);

	  gtk_dialog_run(GTK_DIALOG(dlg));

	  gtk_widget_destroy(dlg);

	  drop_slave(applet);
	}
      break;
    }

  if(error!=NULL)
    g_error_free(error);

  return TRUE;
}

static void drop_slave(PanelApplet *applet)
{
  do_log("Dropping the slave\n");

  if(from_slave_input)
    g_source_remove(from_slave_input);

  // from_slave should be closed by the above.
  close(to_slave);

  from_slave_input=0;
  from_slave=to_slave=0;

  set_state(NEED_SLAVE_START, applet);
}

static void run_error_dlg(const char *message)
{
  GtkWidget *dlg=gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL,
					GTK_MESSAGE_ERROR,
					GTK_BUTTONS_CLOSE,
					message);

  gtk_window_set_position(GTK_WINDOW(dlg), GTK_WIN_POS_CENTER);

  gtk_dialog_run(GTK_DIALOG(dlg));

  gtk_widget_destroy(dlg);
}

static bool start_slave(PanelApplet *applet)
{
  char *argv[]={const_cast<char *>(LIBEXECDIR "/apt-watch-slave"), NULL};

  // Clear out any messages lingering from a previous slave.
  progress_message="";
  progress_percent=0;
  progress_majorchange=false;

  do_log("Starting the slave.\n");

  GError *err=NULL;

  if(!g_spawn_async_with_pipes(NULL, argv, NULL,
			       (GSpawnFlags) 0,
			       NULL, NULL,
			       NULL,
			       &to_slave, &from_slave, NULL,
			       &err))
    {
      char *message=g_strdup_printf("Couldn't start the monitor process %s: %%s", LIBEXECDIR "/apt-watch-slave");

      if(err!=NULL)
	handle_gerror(message, &err);
      else
	run_error_dlg(message);

      g_free(message);

      return false;
    }

  int my_version=PROTOCOL_VERSION;
  int other_version;

  write(to_slave, &my_version, sizeof(my_version));
  read(from_slave, &other_version, sizeof(other_version));

  // Compatibility logic goes here:
  if(my_version>other_version)
    {
      run_error_dlg("Can't start the monitor: it speaks too old a version of the protocol.");

      drop_slave(applet);

      return false;
    }

  set_state(PREINIT, applet);

  GIOChannel *channel=g_io_channel_unix_new(from_slave);
  // glib uses UTF8 encoding by default, making it impossible to pass
  // binary data uncorrupted, a truly dreadful idea.
  g_io_channel_set_encoding(channel, NULL, &err);

  if(handle_gerror("Can't sanitize the slave channel encoding: %s",
		   &err))
    {
      g_io_channel_unref(channel);
      return false;
    }

  from_slave_input=g_io_add_watch(channel,
				  (GIOCondition) (G_IO_IN|G_IO_ERR|G_IO_HUP),
				  handle_slave_msg,
				  applet);

  g_io_channel_unref(channel);

  return true;
}

// Make sure every standard component of the path exists so we can find
// important programs.
const char *path_append[]= {
  "/bin", "/usr/bin", "/usr/local/bin",
  "/sbin", "/usr/sbin", "/usr/local/sbin",
  "/usr/bin/X11", NULL
};

static void munge_path()
{
  const char *path=getenv("PATH");

  if(!path)
    setenv("PATH", "/bin:/usr/bin:/usr/local/bin:/sbin:/usr/sbin:/usr/local/sbin:/usr/bin/X11", 1);
  else
    {
      string PATH=path;

      for(int i=0; path_append[i]; ++i)
	if(PATH.find(path_append[i])==string::npos)
	  PATH=PATH+":"+path_append[i];

      setenv("PATH", PATH.c_str(), 1);
    }
}

static gboolean
applet_button_pressed(GtkWidget *widget,
		      GdkEventButton *event,
		      gpointer userdata)
{
  PanelApplet *applet=PANEL_APPLET(userdata);

  if(event->type == GDK_2BUTTON_PRESS)
    {
      if(state==NEED_SLAVE_START)
	start_slave(applet);
      else if(state==IDLE && can_upgrade)
	start_package_manager(applet);
    }

  return FALSE;
}

static gboolean
apt_watch_applet_fill (PanelApplet *applet,
		       const gchar *iid,
		       gpointer     data)
{
  start_log();

  munge_path();

  do_log("Started with preferences key %s\n",
	 panel_applet_get_preferences_key(applet));

  confclient=gconf_client_get_default();

  signal(SIGPIPE, SIG_IGN);

  if (strcmp (iid, "Apt_WatchApplet") != 0)
    return FALSE;

  menu_action_group = gtk_action_group_new("Apt-Watch Applet Actions");
  gtk_action_group_add_actions(menu_action_group, my_verbs, G_N_ELEMENTS(my_verbs), applet);
  panel_applet_setup_menu(applet, my_menu, menu_action_group);

  ebox=gtk_event_box_new();
  g_signal_connect(G_OBJECT(ebox), "button-press-event",
		   (GCallback) applet_button_pressed, applet);

  GError *err=NULL;
  static_swirl=gdk_pixbuf_new_from_file(DATADIR "/apt-watch/pixmaps/gnome-debian-small.png", &err);
  handle_gerror("Unable to load the swirl pixmap from " DATADIR "/apt-watch/pixmaps/gnome-debian-small.png: %s", &err);

  rotating_swirl=gdk_pixbuf_animation_new_from_file(DATADIR "/apt-watch/pixmaps/debian-cycle-small.gif", &err);
  handle_gerror("Unable to load the rotating pixmap from " DATADIR "/apt-watch/pixmaps/debian-cycle-small.gif", &err);

  GtkWidget *vbox=gtk_vbox_new(TRUE, 2);
  gtk_container_add(GTK_CONTAINER(ebox), vbox);
  icon=GTK_IMAGE(gtk_image_new_from_animation(rotating_swirl));

  set_state(NEED_SLAVE_START, applet);

  // Don't try to abort, it causes more problems than it solves IMO
  start_slave(applet);

  gtk_container_set_border_width(GTK_CONTAINER(applet), 0);

  gtk_box_pack_end(GTK_BOX(vbox),
		   GTK_WIDGET(icon),
		   TRUE, TRUE, 0);

  gtk_container_add(GTK_CONTAINER(applet), ebox);

  gtk_widget_show_all(GTK_WIDGET(applet));

  setup_update_timeout(applet);

  string key=string(panel_applet_get_preferences_key(applet))+"/check/check_freq";

  gconf_client_notify_add(confclient, key.c_str(), notify_check_freq, applet,
			  NULL, &err);

  handle_gerror("Unable to monitor the check_freq key, is GConf working?\n\nError: %s", &err, false);

  key=string(panel_applet_get_preferences_key(applet))+"/download/download_upgrades";

  gconf_client_notify_add(confclient, key.c_str(), notify_download_upgrades, applet,
			  NULL, &err);

  handle_gerror("Unable to monitor the download_upgrades key, is GConf working?\n\nError: %s", &err, false);

  // Reload every 15 minutes? Laptop users would hate me.
  //gtk_timeout_add(1000*60*15, do_reload, NULL);

  return TRUE;
}

PANEL_APPLET_OUT_PROCESS_FACTORY ( "Apt_WatchApplet_Factory",
                                   PANEL_TYPE_APPLET,
                                   apt_watch_applet_fill,
                                   NULL);
