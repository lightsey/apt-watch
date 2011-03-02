// prefs-check-freq.h                       -*-c++-*-
//
//  Copyright 2004 Daniel Burrows
//
// Routines for setting up the "check frequency" portion of a
// preferences dialog.

#ifndef PREFS_CHECK_FREQ_H
#define PREFS_CHECK_FREQ_H

struct _PanelApplet;
typedef struct _PanelApplet PanelApplet;
struct _GtkBuilder;
typedef struct _GtkBuilder GtkBuilder;

void init_preferences_check_freq(PanelApplet *applet, GtkBuilder *builder);

#endif // PREFS_CHECK_FREQ_H
