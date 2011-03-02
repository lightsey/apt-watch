// prefs-download-upgrade.h                        -*-c++-*-
//
//  Copyright 2004 Daniel Burrows
//
// Code to manage the "what upgrades to download" portion of the preferences
// dialog.

#ifndef PREFS_DOWNLOAD_UPGRADE_H
#define PREFS_DOWNLOAD_UPGRADE_H

struct _PanelApplet;
typedef struct _PanelApplet PanelApplet;
struct _GladeXML;
typedef struct _GladeXML GladeXML;

void init_preferences_download_upgrades(PanelApplet *applet, GladeXML *xml);


#endif  // PREFS_DOWNLOAD_UPGRADE_H
