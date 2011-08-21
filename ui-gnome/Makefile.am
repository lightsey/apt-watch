libexec_PROGRAMS = apt-watch 

apt_watch_SOURCES = \
	apt-watch-gnome.cc apt-watch-gnome.h \
	apt-watch-preferences.cc prefs-check-freq.cc prefs-check-freq.h \
	prefs-download-upgrade.cc prefs-download-upgrade.h \
	prefs-package-manager.cc prefs-package-manager.h \
	prefs-notify.cc prefs-notify.h

pkgdata_DATA=apt-watch.ui

apt_watch_LDADD=@GNOME_LDADD@ ../common/libapt-watch-common.a

INCLUDES=@GNOME_INCLUDES@ -I../common

## Applet conf installation
appletdir       = $(LIBPANEL_APPLET_DIR)
applet_in_files = org.gnome.panel.Apt_Watch.panel-applet.in
applet_DATA     = $(applet_in_files:.panel-applet.in=.panel-applet)
org.gnome.panel.Apt_Watch.panel-applet: $(applet_in_files)
	$(AM_V_GEN)sed \
	-e "s|\@LIBEXECDIR\@|$(libexecdir)|" \
	-e "s|\@VERSION\@|$(PACKAGE_VERSION)|" \
	$< > $@

## Dbus service conf installation
servicedir       = $(datadir)/dbus-1/services
service_in_files = org.gnome.panel.applet.Apt_WatchFactory.service.in
service_DATA     = $(service_in_files:.service.in=.service)

org.gnome.panel.applet.Apt_WatchFactory.service: $(service_in_files)
	$(AM_V_GEN)sed \
	-e "s|\@LIBEXECDIR\@|$(libexecdir)|" \
	-e "s|\@VERSION\@|$(PACKAGE_VERSION)|" \
	$< > $@

uidir = $(datadir)/gnome-2.0/ui
# ui_DATA = apt-watch.xml

EXTRA_DIST = org.gnome.panel.Apt_Watch.panel-applet.in.in $(service_in_files) apt-watch.ui apt-watch.schemas

SUBDIRS=pixmaps

# From the gconf web site:
#
# RPM/deb packages should disable schema installation during the "make
# install" phase, as follows:
#
#    export GCONF_DISABLE_MAKEFILE_SCHEMA_INSTALL=1
#    make install
#    unset GCONF_DISABLE_MAKEFILE_SCHEMA_INSTALL
#
#  And then in the post-install script, run something like:
#
#    export GCONF_CONFIG_SOURCE=`gconftool-2 --get-default-source`
#    gconftool-2 --makefile-install-rule %{_sysconfdir}/gconf/schemas/metacity.schemas > /dev/null



schemadir   = @GCONF_SCHEMA_FILE_DIR@
schema_DATA = apt-watch.schemas

if GCONF_SCHEMAS_INSTALL
install-data-local:
	GCONF_CONFIG_SOURCE=$(GCONF_SCHEMA_CONFIG_SOURCE) gconftool-2 --makefile-install-rule $(srcdir)/$(schema_DATA)
endif
