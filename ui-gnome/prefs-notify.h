// prefs-notify.h                        -*-c++-*-
//
//  Copyright 2004 Daniel Burrows
//  Copyright 2011 John Lightsey
//
// Code to manage the "notifications about upgrades" dialog.

#ifndef PREFS_NOTIFY_H
#define PREFS_NOTIFY_H

struct _PanelApplet;
typedef struct _PanelApplet PanelApplet;
struct _GtkBuilder;
typedef struct _GtkBuilder GtkBuilder;

void init_preferences_notify(PanelApplet *applet, GtkBuilder *builder);


#endif  // PREFS_NOTIFY_H

