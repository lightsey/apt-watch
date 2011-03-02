// prefs-package-manager.h                        -*-c++-*-
//
//  Copyright 2004 Daniel Burrows
//
// Code to manage the "which package manager to use" settings.

#ifndef PREFS_PACKAGE_MANAGER_H
#define PREFS_PACKAGE_MANAGER_H

struct _PanelApplet;
typedef struct _PanelApplet PanelApplet;
struct _GladeXML;
typedef struct _GladeXML GladeXML;

void init_preferences_package_manager(PanelApplet *applet, GladeXML *xml);

#endif  // PREFS_PACKAGE_MANAGER_H
