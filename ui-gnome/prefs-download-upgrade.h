// prefs-download-upgrade.h                        -*-c++-*-
//
//  Copyright 2004 Daniel Burrows
//  Copyright 2011 John Lightsey
//
// Code to manage the "what upgrades to download" portion of the preferences
// dialog.

#ifndef PREFS_DOWNLOAD_UPGRADE_H
#define PREFS_DOWNLOAD_UPGRADE_H

struct _PanelApplet;
typedef struct _PanelApplet PanelApplet;
struct _GtkBuilder;
typedef struct _GtkBuilder GtkBuilder;

void init_preferences_download_upgrades(PanelApplet *applet, GtkBuilder *builder);


#endif  // PREFS_DOWNLOAD_UPGRADE_H
