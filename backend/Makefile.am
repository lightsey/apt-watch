libexec_PROGRAMS = apt-watch-slave
noinst_PROGRAMS = apt-watch-auth-helper

apt_watch_slave_SOURCES = \
	apt-watch-slave.cc

apt_watch_auth_helper_SOURCES = \
	apt-watch-auth-helper.cc

INCLUDES="-I../common"

apt_watch_slave_LDADD=@FAM_LDADD@ ../common/libapt-watch-common.a -lapt-pkg
apt_watch_auth_helper_LDADD=../common/libapt-watch-common.a -lpam

install-exec-local:
	install -D -m 4755 apt-watch-auth-helper $(DESTDIR)$(libexecdir)/apt-watch-auth-helper
