/* apt-watch-slave: the slave run as superuser (from su)
 *
 * Expects argv[1] to be an fd on which arguments are passed.
 */

#include <stdio.h>

#include <apt-pkg/acquire.h>
#include <apt-pkg/acquire-item.h>
#include <apt-pkg/cachefile.h>
#include <apt-pkg/clean.h>
#include <apt-pkg/configuration.h>
#include <apt-pkg/depcache.h>
#include <apt-pkg/error.h>
#include <apt-pkg/init.h>
#include <apt-pkg/pkgcache.h>
#include <apt-pkg/pkgsystem.h>
#include <apt-pkg/progress.h>
#include <apt-pkg/sourcelist.h>
#include <apt-pkg/strutl.h>

#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utime.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_LIBFAM
#include <fam.h>
#endif

#include <string>

#include "apt-watch-common.h"
#include "fileutl.h"

using namespace std;

pkgCacheFile *cache=NULL;

#ifdef HAVE_LIBFAM
FAMConnection famconn;
bool fam_available;
#endif

/** Stores the location of the system lists.
 */
string syslistdir;

/** Stores the location of the system archive directory.
 */
string sysarchivedir;

/** The time of the last change to the system package cache.
 *
 *  We reload 60 seconds after the last change (to avoid constant
 *  reloads when the user is installing or removing packages)
 *
 *  As a special case, this is 0 if there is no pending reload.
 */
time_t last_cache_change=0;

/** How long to wait before reloading the cache. */
const int RELOAD_DELAY=60;

/** The fd which is used to send messages to the auth helper. */
int to_authhelper_fd=-1;

/** The fd which is used to receive messages from the auth helper. */
int from_authhelper_fd=-1;

static void setup_archive_dir(int outfd);
static void setup_list_dir(int outfd);
static void write_progress_update(int fd, string Op, float Percent, bool MajorChange);

// Sends messages to the given fd for overall progress, compressed by 1/2.
//
// Do anything on Fail?
class slaveAcquireStatus:public pkgAcquireStatus
{
  bool needed_media_change;

  int fd;
public:
  slaveAcquireStatus(int _fd):needed_media_change(false), fd(_fd) {}

  // This should never happen for an update, but be robust if it does.
  //
  // TODO: support this.
  bool MediaChange(string Media, string drive)
  {
    needed_media_change=true;
    return true;
  }
#if 0
    // This code should be used in the parent to query if I find that
    // this can actually happen:
  {
    GtkWidget *dlg=gtk_message_dialog_new(NULL,
					  GTK_DIALOG_MODAL,
					  GTK_MESSAGE_INFO,
					  GTK_BUTTONS_OK,
					  "Please insert the disk labeled \"%s\" into the drive %s",
					  Media.c_str(), drive.c_str());

    gtk_window_set_position(GTK_WINDOW(dlg), GTK_WIN_POS_CENTER);
    gtk_label_set_line_wrap(GTK_LABEL((GTK_MESSAGE_DIALOG(dlg))->label),
			    TRUE);

    gtk_dialog_run(GTK_DIALOG(dlg));

    gtk_destroy(dlg);
    return true;
  }
#endif

  void update_progress()
  {
    if(fd!=-1)
      {
	float Percent;

	if(TotalBytes+TotalItems>0)
	  Percent=0.5*((100.0*(CurrentBytes+CurrentItems)))/(TotalBytes+TotalItems);
	else
	  Percent=0;

	char buf[512];
	if(TotalBytes==0)
	  snprintf(buf, sizeof(buf),
		   "%ld/%ld items",
		   CurrentItems, TotalItems);
	else
	  snprintf(buf, sizeof(buf),
		   "%sb/%sb",
		   SizeToStr(CurrentBytes).c_str(), SizeToStr(TotalBytes).c_str());

	string output=buf;

	if(CurrentCPS>0)
	  {
	    unsigned long ETA = (unsigned long)((TotalBytes - CurrentBytes)/CurrentCPS);

	    output=output+"; "+TimeToStr(ETA)+" remaining";
	  }

	write_progress_update(fd, output.c_str(), Percent, false);
      }
  }

  bool Pulse(pkgAcquire *Owner)
  {
    bool cancelled=false;

    pkgAcquireStatus::Pulse(Owner);

    // check for a cancel message.
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(0, &readfds);

    struct timeval tm;
    tm.tv_sec=0;
    tm.tv_usec=0;

    select(1, &readfds, NULL, NULL, &tm);

    if(FD_ISSET(0, &readfds))
      {
	unsigned char msgid;
	// assume EOF
	if(read(0, &msgid, sizeof(msgid))==sizeof(msgid) &&
	   msgid!=APPLET_CMD_ABORT_DOWNLOAD)
	  {
	    write_msg(1, APPLET_REPLY_FATALERROR, "Protocol error: received non-abort message during a download.");
	    exit(-1);
	  }

	cancelled=true;
      }

    if(needed_media_change)
      _error->Error("A media change was necessary and was not performed.  Please send a feature request for media change support.");

    update_progress();

    return !needed_media_change && !cancelled;
  }

  void IMSHit(pkgAcquire::ItemDesc&) {update_progress();}
  void Fetch(pkgAcquire::ItemDesc&) {update_progress();}
  void Done(pkgAcquire::ItemDesc&) {update_progress();}
  void Fail(pkgAcquire::ItemDesc&) {update_progress();}

  void Start() {update_progress();}

  void Stop()
  {
    if(fd!=-1)
      {
	unsigned char msgtype=APPLET_REPLY_PROGRESS_DONE;

	write(fd, &msgtype, sizeof(msgtype));
      }
  }
};

class my_cleaner:public pkgArchiveCleaner
{
protected:
  virtual void Erase(const char *file,
		     string pkg,
		     string ver,
		     struct stat &stat)
  {
    unlink(file);
  }
};

static void do_autoclean()
{
  if(_config->FindDir("Dir::Cache::archives")!=syslistdir)
    {
      my_cleaner cleaner;

      cleaner.Go(_config->FindDir("Dir::Cache::archives"), *cache);
      cleaner.Go(_config->FindDir("Dir::Cache::archives")+"partial/", *cache);
    }
}

static void write_progress_update(int fd, string Op, float Percent, bool MajorChange)
{
  unsigned char msgid=APPLET_REPLY_PROGRESS_UPDATE;

  write(fd, &msgid, sizeof(msgid));

  write_string(fd, Op);

  write(fd, &Percent, sizeof(Percent));
  write(fd, &MajorChange, sizeof(MajorChange));
}

class SlaveProgress:public OpProgress
{
  int fd;

  // HACK: How much of the space between 0 and 100 to reserve for a
  // PREVIOUS process when sending messages to the parent.
  //
  // This is a very special-purpose tweak to better support showing
  // the update progress.
  float reserve;
public:
  SlaveProgress(int _fd,
		float _reserve=0):fd(_fd), reserve(_reserve) {}

protected:
  void Update()
  {
    if(CheckChange(0.2))
      {
	float ActualPercent=Percent*(1-reserve)+reserve;

	write_progress_update(fd, Op, ActualPercent, MajorChange);
      }
  }

public:
  void Done()
  {
    unsigned char msgid=APPLET_REPLY_PROGRESS_DONE;
    write(fd, &msgid, sizeof(msgid));
  }
};

static void dump_errors(unsigned char msgid, int fd)
{
  string errs="";

  while(!_error->empty())
    {
      string msg;

      if(_error->PopMessage(msg))
	{
	  if(errs.empty())
	    errs=msg;
	  else
	    errs=errs+"\n"+msg;
	}
    }

  if(!errs.empty())
    write_msg(fd, msgid, errs);
}

/** Copy the global lists to our private list directory. */
void copy_lists()
{
  string mylistdir=_config->FindDir("Dir::State::Lists");

  if(mylistdir != syslistdir)
    copy_newer_recursive(syslistdir, mylistdir);
}

/** Tests whether a particular version is security-related.
 *
 *  \return \b true iff the given package version comes from security.d.o
 */
static bool version_is_security(const pkgCache::VerIterator &ver)
{
  for(pkgCache::VerFileIterator F=ver.FileList(); !F.end(); ++F)
    if(string(F.File().Site())=="security.debian.org")
      return true;

  return false;
}

/** Tests whether a package's candidate version is security-related.
 *  (convenience around the above)
 */
static bool upgrade_is_security(const pkgCache::PkgIterator &pkg)
{
  return !pkg.end() &&
    version_is_security((*cache)[pkg].CandidateVerIter(*cache));
}

/** Checks what sort of upgrades are available.
 *
 *  \return 0 if no upgrades, 1 for non-security upgrades, 2 for
 *    security upgrades
 */
static unsigned int upgrade_status()
{
  bool upgrades_exist=false;

  for(pkgCache::PkgIterator i=(*cache)->PkgBegin(); !i.end(); ++i)
    if(!i.CurrentVer().end() && (*cache)[i].Upgradable())
      {
	upgrades_exist=true;

	if(upgrade_is_security(i))
	  return 2;
      }

  return upgrades_exist?1:0;
}

static void write_cmd_reply(int outfd)
{
  unsigned char msgid;

  switch(upgrade_status())
    {
    case 0: msgid=APPLET_REPLY_CMD_COMPLETE_NOUPGRADES; break;
    case 1: msgid=APPLET_REPLY_CMD_COMPLETE_UPGRADES; break;
    case 2: msgid=APPLET_REPLY_CMD_COMPLETE_SECURITY_UPGRADES; break;
    default: msgid=APPLET_REPLY_CMD_COMPLETE_NOUPGRADES; break;
    }

  write_msgid(outfd, msgid);
}

static void write_init_reply(int outfd)
{
  unsigned char msgid;

  switch(upgrade_status())
    {
    case 0: msgid=APPLET_REPLY_INIT_OK_NOUPGRADES; break;
    case 1: msgid=APPLET_REPLY_INIT_OK_UPGRADES; break;
    case 2: msgid=APPLET_REPLY_INIT_OK_SECURITY_UPGRADES; break;
    default: msgid=APPLET_REPLY_INIT_OK_NOUPGRADES; break;
    }

  write_msgid(outfd, msgid);
}

static void do_update(int outfd)
{
  setup_list_dir(outfd);
  setup_archive_dir(outfd);

  SlaveProgress progress(outfd);

  copy_lists();

  slaveAcquireStatus log(outfd);
  pkgSourceList sources;

  if(sources.ReadMainList()==false || _error->PendingError())
    {
      dump_errors(APPLET_REPLY_FATALERROR, outfd);
      return;
    }

  FileFd lock;
  lock.Fd(GetLock(_config->FindDir("Dir::State::Lists")+"lock"));
  if(_error->PendingError())
    {
      dump_errors(APPLET_REPLY_FATALERROR, outfd);
      return;
    }

  pkgAcquire fetcher(&log);

  if(!sources.GetIndexes(&fetcher))
    {
      dump_errors(APPLET_REPLY_FATALERROR, outfd);
      return;
    }

  if(fetcher.Run()==pkgAcquire::Failed)
    {
      dump_errors(APPLET_REPLY_FATALERROR, outfd);
      return;
    }

  cache->Close();
  if(!cache->Open(progress, false))
    {
      dump_errors(APPLET_REPLY_FATALERROR, outfd);
      return;
    }

  if(!fetcher.Clean(_config->FindDir("Dir::State::lists")) ||
     !fetcher.Clean(_config->FindDir("Dir::State::lists")+"partial/"))
    {
      dump_errors(APPLET_REPLY_FATALERROR, outfd);
      return;
    }

  if(_error->PendingError())
    {
      dump_errors(APPLET_REPLY_FATALERROR, outfd);
      return;
    }

  do_autoclean();

  if(_error->PendingError())
    {
      dump_errors(APPLET_REPLY_FATALERROR, outfd);
      return;
    }
  else
    {
      write_cmd_reply(outfd);

      // Cancel any pending reload.
      last_cache_change=0;
    }
}

static void do_reload(int outfd)
{
  setup_list_dir(outfd);
  setup_archive_dir(outfd);
  
  SlaveProgress progress(outfd);

  copy_lists();

  cache->Close();

  if(!cache->Open(progress, false))
    dump_errors(APPLET_REPLY_FATALERROR, outfd);
  else
    write_cmd_reply(outfd);

  // Cancel any pending reload.
  last_cache_change=0;
}

static void do_su(int cmdfd, int outfd)
{
  bool run_xterm=0;
  read(cmdfd, &run_xterm, sizeof(run_xterm));

  char *cmdchstr=read_string(cmdfd);

  if(cmdchstr==NULL)
    {
      write_msg(outfd, APPLET_REPLY_AUTH_FAIL, "Protocol error: can't read the string which should be sent.");
      free(cmdchstr);

      return;
    }

  string cmd=cmdchstr;
  //free(cmdchstr);

  // Abort if we already have an auth helper running.
  //
  // NOTE: this is not terribly robust; it means that if the auth
  // helper stalls, we get stuck and can't execute a new one.  It might
  // be worthwhile to have a back-and-forth with the UI about whether to
  // kill off the helper...except that we probably (?) can't do that. hm.
  if(to_authhelper_fd!=-1)
    return;

  // Fork off the helper.
  int slavetohelper[2];
  int helpertoslave[2];

  if(pipe(slavetohelper)!=0 ||
     pipe(helpertoslave)!=0)
    {
      write_errno(outfd, APPLET_REPLY_AUTH_FAIL, "Couldn't open pipe to child process: %s");

      return;
    }

  switch(fork())
    {
    case 0:
      {
	char authhelper[]=LIBEXECDIR "/apt-watch-auth-helper";

	close(cmdfd);
	close(outfd);

	// Close the appropriate ends of the pipe
	close(slavetohelper[1]);
	close(helpertoslave[0]);

	dup2(slavetohelper[0], 0);
	dup2(helpertoslave[1], 1);

	close(slavetohelper[0]);
	close(helpertoslave[1]);

	if(run_xterm)
	  execl(authhelper, authhelper, "-x", cmd.c_str(), NULL);
	else
	  execl(authhelper, authhelper, cmd.c_str(), NULL);

	write_errno(helpertoslave[1], APPLET_REPLY_AUTH_FAIL, "Unable to execute \""+string(authhelper)+"\": %s");
	exit(-1);
      }
    case -1:
      close(slavetohelper[0]);
      close(slavetohelper[1]);

      close(helpertoslave[0]);
      close(helpertoslave[1]);

      write_errno(cmdfd, APPLET_REPLY_AUTH_FAIL, "Unable to fork: %s");
      return;

    default:
      close(slavetohelper[0]);
      close(helpertoslave[1]);

      to_authhelper_fd=slavetohelper[1];
      from_authhelper_fd=helpertoslave[0];

      break;
    }
}

static void do_auth_reply(int cmdfd, int outfd)
{
  char *s=read_string(cmdfd);

  if(!s)
    write_msg(outfd, APPLET_REPLY_AUTH_ERRORMSG, "Protocol error reading the command to execute.");
  else if(to_authhelper_fd!=-1)
    write_string(to_authhelper_fd, s);

  free(s);
}

static bool candidate_in_system_cache(pkgCache::PkgIterator &pkg)
{
  // This ASS U ME s that apt-pkg places things in
  // [Dir::Cache::archives]/name_version_arch.deb .  See
  // acquire_item.cc:376 (from apt 0.5.4)

  pkgCache::VerIterator candver=(*cache)[pkg].CandidateVerIter(*cache);

  if(candver.end())
    // hm
    return true;

  string fn=sysarchivedir +
    QuoteString(candver.ParentPkg().Name(),"_:") + '_' +
    QuoteString(candver.VerStr(),"_:") + '_' +
    QuoteString(candver.Arch(),"_:.") + ".deb";

  return access(fn.c_str(), F_OK)==0;
}

static void do_download(int cmdfd, int outfd)
{
  setup_archive_dir(outfd);

  bool download_all;
  read(cmdfd, &download_all, sizeof(download_all));

  slaveAcquireStatus log(outfd);
  pkgAcquire fetcher(&log);

  pkgSourceList sources;
  pkgRecords records(*cache);

  if(sources.ReadMainList()==false || _error->PendingError())
    {
      dump_errors(APPLET_REPLY_FATALERROR, outfd);
      return;
    }

  FileFd lock;
  lock.Fd(GetLock(_config->FindDir("Dir::State::Lists")+"lock"));
  if(_error->PendingError())
    {
      dump_errors(APPLET_REPLY_FATALERROR, outfd);
      return;
    }

  // A pox on undocumented APIs!
  //
  // The way this works is: the fetchers store *references* to their
  // final filenames in this array.  In order to have them do the
  // Right Thing (ie, default behavior), I need to generate a lot of
  // *distinct* strings here.
  vector<string *> filenames;

  // Download every upgradable file; include files that are depended upon.

  // Mark first without autoinst
  for(pkgCache::PkgIterator pkg=(*cache)->PkgBegin(); !pkg.end(); ++pkg)
    if(!pkg.CurrentVer().end() && (*cache)[pkg].Upgradable() &&	
       (download_all || upgrade_is_security(pkg)))
      (*cache)->MarkInstall(pkg);

  // Next with.
  for(pkgCache::PkgIterator pkg=(*cache)->PkgBegin(); !pkg.end(); ++pkg)
    if(!pkg.CurrentVer().end() && (*cache)[pkg].Upgradable() &&	
       (download_all || upgrade_is_security(pkg)))
      (*cache)->MarkInstall(pkg, true);

  // TODO: exclude files whose .deb already exists in the system directory.
  // TODO: exclude held files.
  for(pkgCache::PkgIterator pkg=(*cache)->PkgBegin(); !pkg.end(); ++pkg)
    if((*cache)[pkg].Install() &&
       !candidate_in_system_cache(pkg))
      {
	filenames.push_back(new string());

	new pkgAcqArchive(&fetcher, &sources, &records,
			  (*cache)[pkg].CandidateVerIter(*cache),
			  *(filenames.back()));
      }


  fetcher.Run();

  while(!filenames.empty())
    {
      delete filenames.back();
      filenames.pop_back();
    }

  // Nothing else to do right now: if the update failed, that's
  // Someone Else's Problem.

  if(_error->PendingError())
    dump_errors(APPLET_REPLY_FATALERROR, outfd);

  write_msgid(outfd, APPLET_REPLY_DOWNLOAD_COMPLETE);
}

const char *HOME;

/** If the archive directory is not writable, put archives in
 *  ~/.apt-watch/archives.
 */
static void setup_archive_dir(int outfd)
{
  if(sysarchivedir=="")
    sysarchivedir=_config->FindDir("Dir::Cache::archives");

  if(access(sysarchivedir.c_str(), R_OK|W_OK|X_OK)!=0)
    {
      if(!HOME)
	{
	  write_msg(outfd, APPLET_REPLY_FATALERROR,
		    "The HOME directory is not set, can't create a private cache directory.");
	  exit(-1);
	}

      string myarchivedir=string(HOME)+"/.apt-watch/archives";

      if(access(myarchivedir.c_str(), F_OK)!=0)
	mkdir(myarchivedir.c_str(), 0755);

      string partial=myarchivedir+"/partial";

      if(access(partial.c_str(), F_OK)!=0)
	mkdir(partial.c_str(), 0755);

      _config->Set("Dir::Cache::archives", myarchivedir.c_str());
    }
}

/** If the list directory is not writable, put lists in
 *  ~/.apt-watch/lists.
 */
static void setup_list_dir(int outfd)
{
  if(syslistdir=="")
    syslistdir=_config->FindDir("Dir::State::lists");

  if(access(syslistdir.c_str(), R_OK|W_OK|X_OK)!=0)
    {
      if(!HOME)
	{
	  write_msg(outfd, APPLET_REPLY_FATALERROR,
		    "The HOME directory variable is not set, can't create a private list directory.");
	  exit(-1);
	}

      string mylistdir=string(HOME)+"/.apt-watch/lists";

      if(access(mylistdir.c_str(), F_OK)!=0)
	mkdir(mylistdir.c_str(), 0755);

      string partial=mylistdir+"/partial";

      if(access(partial.c_str(), F_OK)!=0)
	mkdir(partial.c_str(), 0755);

      _config->Set("Dir::State::lists", mylistdir);
    }
}

static void shutdown_auth_helper()
{
  int Status = 0;
  close(to_authhelper_fd);
  close(from_authhelper_fd);

  to_authhelper_fd=-1;
  from_authhelper_fd=-1;

  // reap any dead children
  while (waitpid(-1, &Status, WNOHANG) > 0)
  ;
  
}

/** Returns \b true to terminate the program successfully. */
bool slave_handle_input(int cmdfd, int outfd)
{
  unsigned char c;

  if(read(cmdfd, &c, sizeof(c))<(int) sizeof(c))
    {
      // assume we should terminate.
      return true;
    }
  else
    switch(c)
      {
      case APPLET_CMD_UPDATE:
	do_update(outfd);
	break;
      case APPLET_CMD_RELOAD:
	do_reload(outfd);
	break;
      case APPLET_CMD_SU:
	do_su(cmdfd, outfd);
	break;
      case APPLET_CMD_AUTHREPLY:
	do_auth_reply(cmdfd, outfd);
	break;
      case APPLET_CMD_AUTHCANCEL:
	shutdown_auth_helper();
	break;
      case APPLET_CMD_DOWNLOAD:
	do_download(cmdfd, outfd);
	break;
      default:
	{
	  char s[1024];
	  snprintf(s, sizeof(s)-1, "Bad command ID %d", c);
	  write_msg(outfd, APPLET_REPLY_FATALERROR, s);
	}
	break;
      }

  return false;
}

static void slave_handle_auth_input(int outfd)
{
  unsigned char c;

  if(from_authhelper_fd==-1)
    {
      write_msg(outfd, APPLET_REPLY_AUTH_FAIL, "Internal error: I want to read an authentication message but the pipe is closed.");
      return;
    }

  if(read(from_authhelper_fd, &c, sizeof(c))<(int) sizeof(c))
    shutdown_auth_helper();
  else
    // Any APPLET_REPLY_AUTH* command is possible.
    //
    // We just forward them to our parent.  We do NOT splice the pipe
    // with our parent's pipe: while it is an idea, atomic write()s
    // are not currently used, which could cause problems.
    switch(c)
      {
	// These all pass a single string as an argument.
      case APPLET_REPLY_AUTH_PROMPT_NOECHO:
      case APPLET_REPLY_AUTH_PROMPT_ECHO:
      case APPLET_REPLY_AUTH_ERRORMSG:
      case APPLET_REPLY_AUTH_INFO:
      case APPLET_REPLY_AUTH_FAIL:
	{
	  char *s=read_string(from_authhelper_fd);

	  if(s==NULL)
	    {
	      write_errno(outfd, APPLET_REPLY_AUTH_FAIL, "Couldn't read a message from the authentication helper: %s");
	      shutdown_auth_helper();
	    }
	  else
	    write_msg(outfd, c, s);

	  free(s);
	  break;
	}

      case APPLET_REPLY_AUTH_OK:
	write_msgid(outfd, c);
	break;
	
      case APPLET_REPLY_AUTH_FINISHED:
	write_msgid(outfd, c);
	shutdown_auth_helper();
	break;
	
      default:
	write_msg(outfd, APPLET_REPLY_AUTH_FAIL, "Garbled reply from the authentication helper.");
	shutdown_auth_helper();
	break;
      }
}

#ifdef HAVE_LIBFAM
void slave_handle_fam()
{
  FAMEvent ev;

  while(FAMPending(&famconn))
    {
      FAMNextEvent(&famconn, &ev);

      // Ignore lockfiles.
      if(strcmp(ev.filename, "lock")==0)
	continue;

      switch(ev.code)
	{
	case FAMChanged:
	case FAMDeleted:
	case FAMCreated:
	  last_cache_change=time(0);
	  break;

      	default:
	  // Drop other events on the floor.
	  break;
	}
    }
}
#endif

int slave_main(int cmdfd, int outfd)
{
  // eternal loop.
  while(1)
    {
      fd_set readfds;
      int res;
      int highest=-1;
      int famfd=fam_available?FAMCONNECTION_GETFD(&famconn):-1;

      struct timeval tm;

      FD_ZERO(&readfds);
      FD_SET(cmdfd, &readfds);
      highest=max(highest, cmdfd);

#ifdef HAVE_LIBFAM
      if(fam_available)
	{
	  FD_SET(famfd, &readfds);
	  highest=max(highest, famfd);
	}
#endif

      if(from_authhelper_fd!=-1)
	{
	  FD_SET(from_authhelper_fd, &readfds);
	  highest=max(highest, from_authhelper_fd);
	}

      // See how long to select for.
      if(last_cache_change==0)
	res=select(highest+1, &readfds, NULL, NULL, NULL);
      else
	{
	  time_t curtime=time(0);

	  if(last_cache_change+RELOAD_DELAY<curtime)
	    tm.tv_sec=0;
	  else
	    tm.tv_sec=last_cache_change+RELOAD_DELAY-curtime;
	  tm.tv_usec=0;

	  res=select(highest+1, &readfds, NULL, NULL, &tm);
	}

      if(FD_ISSET(cmdfd, &readfds))
	if(slave_handle_input(cmdfd, outfd))
	  return 0;

#ifdef HAVE_LIBFAM
      if(fam_available && FD_ISSET(famfd, &readfds))
	slave_handle_fam();
#endif

      if(from_authhelper_fd!=-1 && FD_ISSET(from_authhelper_fd, &readfds))
	slave_handle_auth_input(outfd);

      // A slightly quirky way of doing this: if a reload is overdue,
      // we send a message to the applet requesting a reload command.
      // This roundabout approach is used in order to avoid any
      // potential race conditions (if a reload is inappropriate, the
      // applet will just drop the message on the floor -- the only
      // reason it can be inappropriate is if a reload or update is
      // already "under progress" -- meaning that we haven't read the
      // message requesting it yet).
      if(last_cache_change>0 &&
	 time(0)>last_cache_change &&
	 time(0)-last_cache_change>=RELOAD_DELAY)
	{
	  unsigned char msgtype=APPLET_REPLY_REQUEST_RELOAD;
	  write(outfd, &msgtype, 1);

	  last_cache_change=0;
	}
    }
}

int main(int argc, char **argv)
{
  int cmdfd=0;

  int outfd=1;

  int my_version=PROTOCOL_VERSION;
  int other_version;

  HOME=getenv("HOME");

  signal(SIGPIPE, SIG_IGN);

  write(outfd, &my_version, sizeof(my_version));
  read(cmdfd, &other_version, sizeof(other_version));

  // Compatibility logic goes here.
  if(my_version>other_version)
    {
      write_msg(outfd, APPLET_REPLY_FATALERROR, "Can't start the monitor: it speaks too new a version of the protocol.");
      return -1;
    }

  // Handle the SUID case
  if(getuid()!=0 && geteuid()==0)
    {
      clearenv();
    }
  else if(getuid()!=geteuid())
    {
      write_msg(outfd, APPLET_REPLY_AUTH_FAIL, "The SUID bit of the slave is set, but it is not SUID root; don't know what to do.");
      return -1;
    }
  else
    {
      // "dummy" authentication OK. (? -- just get rid of this here now?)
      unsigned char msg=APPLET_REPLY_AUTH_OK;
      write(1, &msg, sizeof(msg));

      if(HOME)
	{
	  // Make a private directory for later use
	  string mydir=string(HOME)+"/.apt-watch";

	  if(access(mydir.c_str(), F_OK)!=0)
	    mkdir(mydir.c_str(), 0755);
	}
    }

  if(pkgInitConfig(*_config))
    pkgInitSystem(*_config, _system);

  if(_error->PendingError())
    {
      dump_errors(APPLET_REPLY_INIT_FAILED, outfd);
      return -1;
    }

  setup_list_dir(outfd);
  setup_archive_dir(outfd);

  cache=new pkgCacheFile;
  SlaveProgress progress(outfd);

  if(!cache->Open(progress, false) || _error->PendingError())
    {
      dump_errors(APPLET_REPLY_INIT_FAILED, outfd);
      return -1;
    }

  write_init_reply(outfd);

#ifdef HAVE_LIBFAM
  // If opening the FAM connection fails, just don't bother.
  // TODO/FIXME: periodically reload the cache in that case.
  if(FAMOpen(&famconn)==0)
    {
      FAMRequest fr;

      FAMMonitorDirectory(&famconn, syslistdir.c_str(), &fr, NULL);
      FAMMonitorDirectory(&famconn, (syslistdir+"/partial").c_str(),
			  &fr, NULL);
      FAMMonitorDirectory(&famconn, _config->FindDir("Dir::Etc").c_str(),
			  &fr, NULL);
      FAMMonitorFile(&famconn, _config->FindFile("Dir::State::status").c_str(),
		     &fr, NULL);

      fam_available=true;
    }
  else
    fprintf(stderr, "Unable to initialize FAM.\n");
#endif

  int rval=slave_main(cmdfd, outfd);

#ifdef HAVE_LIBFAM
  FAMClose(&famconn);
#endif

  return rval;
}
