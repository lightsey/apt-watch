// prefs-package-manager.h                        -*-c++-*-
//
//  Copyright 2004 Daniel Burrows
//  Copyright 2011 John Lightsey
//
// Code to manage the "which package manager to use" settings.

#include "prefs-package-manager.h"

#include "apt-watch-gnome.h"

#include <common/fileutl.h>

#include <cstring>
#include <cctype>
#include <string>

#include <gtk/gtk.h>
#include <panel-applet.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

using namespace std;

enum PackageManager {PM_UNSET=-2, PM_CUSTOM=-1,
		     PM_APT_GET, PM_APTITUDE, PM_GNOME_APT, PM_SYNAPTIC};

// The options are listed in *ORDER OF PREFERENCE* here.  I am assuming
// that gnome-apt sucks as much as it used to; correct me if I am wrong.
static GConfEnumStringPair package_manager_lookup_table[] = {
  {PM_SYNAPTIC, "synaptic"},
  {PM_APTITUDE, "aptitude"},
  {PM_APT_GET, "apt-get"},
  {PM_GNOME_APT, "gnome-apt"},
  {PM_CUSTOM, "custom"},
  {0, NULL}
};

// Indexed by enums (so it should be in a different order from the above)
static package_manager hardcoded_package_managers[] = {
  {"apt-get -u upgrade", true},
  {"aptitude", true},
  {"gnome-apt", false},
  {"synaptic", false}
};

static int get_package_manager_enum_loc(PanelApplet *applet)
{
  gint rval=PM_UNSET;

  string key=string(panel_applet_get_preferences_key(applet))+"/package_manager/package_manager";

  GError *err=NULL;

  gchar *stringval=gconf_client_get_string(confclient,
					   key.c_str(),
					   &err);

  if(err)
    g_error_free(err);

  if(stringval)
    {
      for(int i=0; package_manager_lookup_table[i].str; ++i)
	if(!strcmp(package_manager_lookup_table[i].str, stringval))
	  {
	    rval=i;
	    break;
	  }
    }
  else
    {
      for(int i=0; package_manager_lookup_table[i].str!=NULL; ++i)
	if(package_manager_lookup_table[i].enum_value>0 &&
	   in_path(package_manager_lookup_table[i].str))
	  {
	    rval=i;
	    break;
	  }
    }

  g_free(stringval);

  return rval;
}

static int get_package_manager_enum(PanelApplet *applet)
{
  int loc=get_package_manager_enum_loc(applet);

  if(loc<0)
    return PM_UNSET;
  else
    return package_manager_lookup_table[loc].enum_value;
}

const package_manager *get_package_manager(PanelApplet *applet)
{
  static package_manager custom_pm;

  const package_manager *rval=NULL;

  gint curval=get_package_manager_enum(applet);

  if(curval>=0)
    rval=hardcoded_package_managers+curval;
  else
    {
      gchar *customcmdkey=g_strdup_printf("%s/package_manager/custom_package_manager_command",
					  panel_applet_get_preferences_key(applet));
      gchar *runxtermkey=g_strdup_printf("%s/package_manager/custom_package_manager_run_in_xterm",
					 panel_applet_get_preferences_key(applet));

      GError *err=NULL;

      gchar *customstringval=gconf_client_get_string(confclient,
						     customcmdkey,
						     &err);

      if(err!=NULL)
	{
	  g_error_free(err);
	  err=NULL;
	}

      custom_pm.cmd=customstringval?customstringval:"";
      custom_pm.run_in_xterm=gconf_client_get_bool(confclient,
						   runxtermkey,
						   &err);

      if(err!=NULL)
	{
	  g_error_free(err);
	  err=NULL;
	}

      g_free(customstringval);
      g_free(runxtermkey);
      g_free(customcmdkey);

      rval=&custom_pm;
    }

  return rval;
}

/** Given an option menu, return the selected package manager or -1 for
 *  a custom package manager.
 */
static int get_selected_manager(GtkComboBox *menu)
{
  GtkTreeIter iter;
  GValue selected_value = {0};
  int selected_manager = PM_CUSTOM;
  if (!gtk_combo_box_get_active_iter(menu, &iter)) {
      return selected_manager;
  }

  GtkTreeModel *model = gtk_combo_box_get_model(menu);
  
  gtk_tree_model_get_value(model, &iter, 2, &selected_value);
  selected_manager = g_value_get_int(&selected_value);
  g_value_unset(&selected_value);

  g_object_unref(G_OBJECT(model));
  return selected_manager;
}

/** IF the "custom" entry of the GtkOptionMenu is selected, set the
 *  custom command based on the text in the corresponding GtkEntry.
 */
static void pm_command_edited(GtkWidget *w, GdkEventFocus *event,
			      gpointer userdata)
{
  GtkEntry *entry=GTK_ENTRY(w);

  GtkWidget *preferences_dialog=GTK_WIDGET(userdata);
  PanelApplet *applet=PANEL_APPLET(g_object_get_data(G_OBJECT(preferences_dialog), "applet"));
  GtkComboBox *menu=GTK_COMBO_BOX(g_object_get_data(G_OBJECT(preferences_dialog), "package_manager_menu"));

  if(get_selected_manager(menu)!=-1)
    return;

  string key=panel_applet_get_preferences_key(applet)+string("/package_manager/custom_package_manager_command");

  GError *err=NULL;

  gconf_client_set_string(confclient,
			  key.c_str(),
			  gtk_entry_get_text(entry),
			  &err);

  handle_gerror("Can't set the custom_package_manager_command key, is GConf working?\n\nError: %s", &err, false);
}

/** IF the "custom" entry of the GtkOptionMenu is selected, update the
 *  "run-in-xterm" setting based on the check button.
 */
static void pm_run_in_xterm_toggled(GtkToggleButton *button, gpointer userdata)
{
  GtkWidget *preferences_dialog=GTK_WIDGET(userdata);
  PanelApplet *applet=PANEL_APPLET(g_object_get_data(G_OBJECT(preferences_dialog), "applet"));
  GtkComboBox *menu=GTK_COMBO_BOX(g_object_get_data(G_OBJECT(preferences_dialog), "package_manager_menu"));

  if(get_selected_manager(menu)!=-1)
    return;

  string key=panel_applet_get_preferences_key(applet)+string("/package_manager/custom_package_manager_run_in_xterm");

  GError *err=NULL;

  gconf_client_set_bool(confclient,
			key.c_str(),
			gtk_toggle_button_get_active(button),
			&err);

  handle_gerror("Can't set the custom_package_manager_run_in_xterm key, is GConf working?\n\nError: %s", &err, false);
}

/** Handles updating the package manager that's selected in GConf based
 *  on the current setting of the option menu.
 */
static void pm_chosen(GtkComboBox *menu, gpointer userdata)
{
  GtkWidget *preferences_dialog=GTK_WIDGET(userdata);
  PanelApplet *applet=PANEL_APPLET(g_object_get_data(G_OBJECT(preferences_dialog), "applet"));

  string key=panel_applet_get_preferences_key(applet)+string("/package_manager/package_manager");

  GError *err=NULL;

  gconf_client_set_string(confclient,
			  key.c_str(),
			  gconf_enum_to_string(package_manager_lookup_table,
					       get_selected_manager(menu)),
			  &err);

  handle_gerror("Can't set the package_manager key, is GConf working?\n\nError: %s", &err, false);
}

/** Update the entry and check-button to reflect the state of the
 *  option menu.  Does not actually change any options.
 */
static void update_pm_widgets(GtkComboBox *optionmenu, gpointer userdata)
{
  GtkWidget *preferences_dialog=GTK_WIDGET(userdata);

  PanelApplet *applet=PANEL_APPLET(g_object_get_data(G_OBJECT(preferences_dialog),
						     "applet"));

  int selected_val=get_selected_manager(optionmenu);

  GtkEntry *command=GTK_ENTRY(g_object_get_data(G_OBJECT(preferences_dialog),
						"package_manager_command"));
  GtkToggleButton *run_in_xterm=GTK_TOGGLE_BUTTON(g_object_get_data(G_OBJECT(preferences_dialog), "package_manager_run_in_xterm"));

  if(selected_val==-1)
    {
      // The custom widget is selected.
      
      gchar *customcmdkey=g_strdup_printf("%s/package_manager/custom_package_manager_command",
					  panel_applet_get_preferences_key(applet));
      gchar *runxtermkey=g_strdup_printf("%s/package_manager/custom_package_manager_run_in_xterm",
					 panel_applet_get_preferences_key(applet));

      GError *err=NULL;

      gchar *customstringval=gconf_client_get_string(confclient,
						     customcmdkey,
						     &err);

      if(err)
	{
	  g_error_free(err);
	  err=NULL;
	}

      if(customstringval)
	gtk_entry_set_text(command, customstringval);
      else
	gtk_entry_set_text(command, "");

      gtk_toggle_button_set_active(run_in_xterm, gconf_client_get_bool(confclient,
								       runxtermkey,
								       &err));

      if(err)
	{
	  g_error_free(err);
	  err=NULL;
	}

      gtk_widget_set_sensitive(GTK_WIDGET(command), 1);
      gtk_widget_set_sensitive(GTK_WIDGET(run_in_xterm), 1);

      g_free(customstringval);
      g_free(runxtermkey);
      g_free(customcmdkey);
    }
  else
    {
      const package_manager *pm=hardcoded_package_managers+selected_val;

      gtk_widget_set_sensitive(GTK_WIDGET(command), 0);
      gtk_widget_set_sensitive(GTK_WIDGET(run_in_xterm), 0);

      gtk_entry_set_text(command, pm->cmd.c_str());
      gtk_toggle_button_set_active(run_in_xterm, pm->run_in_xterm);
    }
}

static void notify_pm_changed(GConfClient *client,
			      guint cnxn_id,
			      GConfEntry *entry,
			      gpointer userdata)
{
  GtkWidget *preferences_dialog=GTK_WIDGET(userdata);
  PanelApplet *applet=PANEL_APPLET(g_object_get_data(G_OBJECT(preferences_dialog), "applet"));
  GtkComboBox *menu=GTK_COMBO_BOX(g_object_get_data(G_OBJECT(preferences_dialog), "package_manager_menu"));

  // exploit the fact that the index in the lookup table is also the
  // index in the menu.
  int loc=get_package_manager_enum_loc(applet);

  g_return_if_fail(loc>0);

  gtk_combo_box_set_active(menu, loc);
}

static void notify_pm_custom_changed(GConfClient *client,
				     guint cnxn_id,
				     GConfEntry *entry,
				     gpointer userdata)
{
  GtkWidget *preferences_dialog=GTK_WIDGET(userdata);
  GtkComboBox *menu=GTK_COMBO_BOX(g_object_get_data(G_OBJECT(preferences_dialog), "package_manager_menu"));

  update_pm_widgets(menu, userdata);
}

void init_preferences_package_manager(PanelApplet *applet, GtkBuilder *builder)
{
  GtkWidget *preferences_dialog=GTK_WIDGET(gtk_builder_get_object(builder, "preferences_dialog"));

  // TODO: package_manager_menu is no longer a GtkOptionMenu
  GtkComboBox *optionmenu=GTK_COMBO_BOX(gtk_builder_get_object(builder, "package_manager_menu"));
  GtkEntry *command=GTK_ENTRY(gtk_builder_get_object(builder, "package_manager_command"));
  GtkCheckButton *run_in_xterm=GTK_CHECK_BUTTON(gtk_builder_get_object(builder, "package_manager_run_in_xterm"));

  g_object_set_data(G_OBJECT(preferences_dialog), "package_manager_menu", optionmenu);
  g_object_set_data(G_OBJECT(preferences_dialog), "package_manager_command", command);
  g_object_set_data(G_OBJECT(preferences_dialog), "package_manager_run_in_xterm", run_in_xterm);

  string pm_key=string(panel_applet_get_preferences_key(applet))+"/package_manager/package_manager";
  string pm_custom_cmd_key=string(panel_applet_get_preferences_key(applet))+"/package_manager/custom_package_manager_command";
  string pm_custom_xterm_key=string(panel_applet_get_preferences_key(applet))+"/package_manager/custom_package_manager_run_in_xterm";

  GError *err=NULL;

  guint pm_connection=gconf_client_notify_add(confclient, pm_key.c_str(),
					      notify_pm_changed,
					      preferences_dialog,
					      NULL, &err);

  handle_gerror("Can't monitor the package_manager key, is GConf working?\n\nError: %s", &err, false);

  guint pm_custom_cmd_connection=
    gconf_client_notify_add(confclient, pm_custom_cmd_key.c_str(),
			    notify_pm_custom_changed,
			    preferences_dialog,
			    NULL, &err);

  handle_gerror("Can't monitor the custom_package_manager_command key, is GConf working?\n\nError: %s", &err, false);

  guint pm_custom_xterm_connection=
    gconf_client_notify_add(confclient, pm_custom_xterm_key.c_str(),
			    notify_pm_custom_changed,
			    preferences_dialog,
			    NULL, &err);

  handle_gerror("Can't monitor the custom_package_manager_run_in_xterm key, is GConf working?\n\nError: %s", &err, false);

  GtkListStore *menu=gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_INT);
  
  // GtkMenuShell *menu=GTK_MENU_SHELL(gtk_menu_new());

  GtkTreeIter iter;
  GtkTreeIter active;

  int curr_enum_value=get_package_manager_enum(applet);

  // Build a list of installed package managers.
  for(int i=0; package_manager_lookup_table[i].str; ++i)
    {
      const GConfEnumStringPair * const pair=package_manager_lookup_table+i;

      // By an amazing coincidence, the string in the table is exactly
      // the command name of the program!  I wonder how that could be?
      string capitalized=pair->str;
      capitalized[0]=toupper(capitalized[0]);

      gtk_list_store_append( menu, &iter );
      
      gboolean visible = true;
      if(pair->enum_value!=-1 && !in_path(pair->str))
          visible = false;

      gtk_list_store_set( menu, &iter, 0, capitalized.c_str(), 1, visible, 2, pair->enum_value, -1 );

      if (pair->enum_value==curr_enum_value)
         active=iter;
    }

  gtk_combo_box_set_model(optionmenu,GTK_TREE_MODEL(menu));
  g_object_unref(G_OBJECT(menu));
  
  GtkCellRenderer *cell = gtk_cell_renderer_text_new();
  gtk_cell_layout_pack_start( GTK_CELL_LAYOUT( optionmenu ), cell, TRUE );
  gtk_cell_layout_set_attributes( GTK_CELL_LAYOUT( optionmenu ), cell, "text", 0, "sensitive", 1, NULL );
  g_object_unref(G_OBJECT(cell));
  
  gtk_combo_box_set_active_iter(optionmenu, &active);

  update_pm_widgets(optionmenu, preferences_dialog);
  g_signal_connect(G_OBJECT(optionmenu), "changed",
		   (GCallback) update_pm_widgets,
		   preferences_dialog);
  g_signal_connect(G_OBJECT(optionmenu), "changed",
		   (GCallback) pm_chosen,
		   preferences_dialog);
  g_signal_connect(G_OBJECT(command), "focus-out-event",
		   (GCallback) pm_command_edited,
		   preferences_dialog);
  g_signal_connect(G_OBJECT(run_in_xterm), "toggled",
		   (GCallback) pm_run_in_xterm_toggled,
		   preferences_dialog);

  g_signal_connect(G_OBJECT(preferences_dialog), "destroy",
		   (GCallback) do_notify_remove,
		   GINT_TO_POINTER(pm_connection));
  g_signal_connect(G_OBJECT(preferences_dialog), "destroy",
		   (GCallback) do_notify_remove,
		   GINT_TO_POINTER(pm_custom_cmd_connection));
  g_signal_connect(G_OBJECT(preferences_dialog), "destroy",
		   (GCallback) do_notify_remove,
		   GINT_TO_POINTER(pm_custom_xterm_connection));
}
