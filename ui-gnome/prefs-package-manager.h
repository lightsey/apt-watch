// prefs-package-manager.h                        -*-c++-*-
//
//  Copyright 2004 Daniel Burrows
//  Copyright 2011 John Lightsey
//
// Code to manage the "which package manager to use" settings.

#ifndef PREFS_PACKAGE_MANAGER_H
#define PREFS_PACKAGE_MANAGER_H

struct _PanelApplet;
typedef struct _PanelApplet PanelApplet;
struct _GtkBuilder;
typedef struct _GtkBuilder GtkBuilder;

void init_preferences_package_manager(PanelApplet *applet, GtkBuilder *builder);

#endif  // PREFS_PACKAGE_MANAGER_H
