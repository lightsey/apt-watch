// prefs-notify.h                        -*-c++-*-
//
//  Copyright 2004 Daniel Burrows
//
// Code to manage the "notifications about upgrades" dialog.

#ifndef PREFS_NOTIFY_H
#define PREFS_NOTIFY_H

struct _PanelApplet;
typedef struct _PanelApplet PanelApplet;
struct _GladeXML;
typedef struct _GladeXML GladeXML;

void init_preferences_notify(PanelApplet *applet, GladeXML *xml);


#endif  // PREFS_NOTIFY_H

