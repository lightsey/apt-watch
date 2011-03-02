// fileutl.cc
//
//  Copyright 2004 Daniel Burrows

#include "fileutl.h"

#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <utime.h>

using namespace std;

bool in_path(const string &PATH, const string &fn)
{
  string::size_type start=0, end=PATH.find(':');

  if(access(fn.c_str(), X_OK)==0)
    return true;

  if(fn.find('/')!=fn.npos)
    return false;

  do
    {
      string component(PATH, start, end-start);

      if(access((component+"/"+fn).c_str(), X_OK)==0)
	return true;

      start=end+1;
      end=PATH.find(':', start);
    } while(end!=string::npos);

  return false;
}

bool in_path(const string &fn)
{

  const char *path=getenv("PATH");
  string PATH;

  if(path)
    PATH=path;
  else
    PATH="/usr/bin:/bin:/usr/local/bin:/usr/bin/X11:/usr/sbin:/sbin:/usr/local/sbin";

  return in_path(PATH, fn);
}

bool copy_temp(const string &src, const string &dst, string &name)
{
  struct stat buf;

  if(stat(src.c_str(), &buf)!=0)
    return false;

  if(S_ISLNK(buf.st_mode))
    {
      // silly hardcoded limit, but in the context this is used in
      // I don't expect symlinks at all, and anyone with a symlink
      // this long is nuts.
      char buf[1024];

      if(readlink(src.c_str(), buf, 1024)!=0)
	{
	  perror(("Can't read symlink "+src).c_str());
	  return false;
	}

      if(unlink(dst.c_str())!=0 ||
	 symlink(buf, dst.c_str())!=0)
	{
	  perror((string("Can't symlink ")+buf+" to "+dst).c_str());
	  return false;
	}

      return true;
    }

  int infd=open(src.c_str(), O_RDONLY);

  if(infd==-1)
    return false;

  char namebuf[512];

  if(snprintf(namebuf, 512, "%s.apt-watch-%u:%u", dst.c_str(), getpid(), rand())<0)
    {
      close(infd);

      return false;
    }

  name=namebuf;

  int outfd=open(namebuf, O_WRONLY|O_CREAT|O_EXCL, buf.st_mode);

  if(outfd==-1)
    {
      close(infd);

      return false;
    }

  char rdbuf[8192];

  int amt;

  do
    {
      amt=read(infd, rdbuf, sizeof(rdbuf));
      if(amt>0)
	write(outfd, rdbuf, amt);
    } while(amt>0);

  close(infd);
  close(outfd);

  if(amt<0)
    {
      unlink(namebuf);
      return false;
    }

  utimbuf timebuf;

  timebuf.actime=buf.st_atime;
  timebuf.modtime=buf.st_mtime;

  // Discard errors:
  utime(dst.c_str(), &timebuf);

  return true;
}

bool copy(const string &src, const string &dst)
{
  string tmpnam;

#ifdef DEBUG
  fprintf(stderr, "COPY %s -> %s\n", src.c_str(), dst.c_str());
#endif

  if(!copy_temp(src, dst, tmpnam))
    return false;

  if(rename(tmpnam.c_str(), dst.c_str())<0)
    {
      unlink(tmpnam.c_str());
      return false;
    }
  
  chown(dst.c_str(), geteuid(), getegid());
  return true;
}

bool move(const string &src, const string &dst)
{
#ifdef DEBUG
  fprintf(stderr, "MOVE %s -> %s\n", src.c_str(), dst.c_str());
#endif

  if(rename(src.c_str(), dst.c_str())==0) {
     chown(dst.c_str(), geteuid(), getegid());
    return true;
  }

  if(!copy(src, dst))
    return false;

  if(unlink(src.c_str())<0)
    return false;

  return true;
}

// These templates turned out to be overkill, I think.

/** Tries to apply the given action to each element of a directory; aborts
 *  if any subaction aborts.
 */
template<class actionT>
bool for_each_dir(const string &src, const string &dst,
		  actionT action)
{
  DIR *dir=opendir(src.c_str());
  struct stat buf;

  if(!dir)
    {
      perror(("Can't read entries of "+src).c_str());
      return false;
    }

  for(dirent *d=readdir(dir); d; d=readdir(dir))
    {
      if(!strcmp(d->d_name, ".") || !strcmp(d->d_name, ".."))
	continue;

      string srcname=src+"/"+d->d_name;
      string dstname=dst+"/"+d->d_name;

      if(lstat(srcname.c_str(), &buf)!=0)
	{
	  closedir(dir);
	  return false;
	}

      // descend.
      if(!action(srcname, dstname))
	{
	  closedir(dir);
	  return false;
	}
    }

  closedir(dir);

  return true;
}

/** Apply the given actions to all pairs of non-directories in the
 *  directory src, and corresponding names in dst.  Abort if action
 *  returns \b false.  eg, act_recursive(src, dst, copy) does what you
 *  expect.
 *
 *  Directories in "src" are descended into using diraction if they do
 *  not exist.  Errors cause the whole process to abort. (not terribly
 *  graceful)
 */
template<class fileactionT, class diractionT>
bool act_recursive(const string &src, const string &dst,
		   const fileactionT &fileaction,
		   const diractionT &diraction)
{
  struct stat buf;

  if(lstat(src.c_str(), &buf)!=0)
    {
      perror(("Can't stat "+src).c_str());
      return false;
    }

  if(S_ISDIR(buf.st_mode))
    return diraction(src, dst);
  else

    // Perform the specified action.
    if(!fileaction(src, dst))
      {
	perror((src+" -> "+dst).c_str());
	return false;
      }

  return true;
}

struct wrap_fn_action
{
  wrap_fn_action(bool (*action)(const string &, const string &)):myaction(action) {}

  bool operator()(const string &src, const string &dst) const
  {
    return myaction(src, dst);
  }

  bool (*myaction)(const string &, const string &);
};

template<class fileactionT, class diractionT>
struct curry_act_recursiveCLS
{
  curry_act_recursiveCLS(fileactionT faction, diractionT daction)
    :fileaction(faction), diraction(daction) {}

  bool operator()(const string &src, const string &dst) const
  {
    return act_recursive(src, dst, fileaction, diraction);
  }

  fileactionT fileaction;
  diractionT diraction;
};

// ow ow ow ow
template<class fileactionT, class diractionT>
inline curry_act_recursiveCLS<fileactionT, diractionT> curry_act_recursive(fileactionT faction, diractionT daction) {return curry_act_recursiveCLS<fileactionT, diractionT>(faction, daction);}

template<class recurT>
struct cp_dir_action
{
  cp_dir_action(recurT _recur):recur(_recur) {}

  bool operator()(const string &src, const string &dst) const
  {
    struct stat buf;

    if(stat(src.c_str(), &buf)!=0)
      {
	perror(("Can't stat "+src).c_str());
	return false;
      }

    // Blindly try to create a destination directory.
    if(mkdir(dst.c_str(), buf.st_mode)!=0 && errno!=EEXIST)
      {
	perror(("Can't create destination directory "+dst).c_str());
	return false;
      }

    return for_each_dir(src, dst,
			curry_act_recursive(recur, *this));
  }

  recurT recur;
};

bool copy_recursive(const string &src, const string &dst)
{
  return act_recursive(src, dst, wrap_fn_action(copy),
		       cp_dir_action<wrap_fn_action>(wrap_fn_action(copy)));
}

// tries to just immediately perform a rename; if that fails, it descends.
struct mv_dir_action
{
  mv_dir_action() {}

  bool operator()(const string &src, const string &dst) const
  {
    if(rename(src.c_str(), dst.c_str())==0) {
      chown(dst.c_str(), geteuid(), getegid());
      return true;
    }

    struct stat buf;

    if(stat(src.c_str(), &buf)!=0)
      {
	perror(("Can't stat "+src).c_str());
	return false;
      }

    // Blindly try to create a destination directory.
    if(mkdir(dst.c_str(), buf.st_mode)!=0 && errno!=EEXIST)
      {
	perror(("Can't create destination directory "+dst).c_str());
	return false;
      }

    if(!for_each_dir(src, dst,
		     curry_act_recursive(wrap_fn_action(move), mv_dir_action())))
      return false;

    if(rmdir(src.c_str())!=0)
      {
	perror(("Can't remove "+src).c_str());
	return false;
      }

    return true;
  }
};

bool move_recursive(const string &src, const string &dst)
{
  return act_recursive(src, dst, wrap_fn_action(move), mv_dir_action());
}

struct copy_newer_action
{
  bool operator()(const string &src, const string &dst) const
  {
    struct stat srcbuf, dstbuf;

    bool do_copy=true;

    if(lstat(src.c_str(), &srcbuf)!=0)
      {
	perror(("Can't stat "+src).c_str());
	return false;
      }

    if(lstat(dst.c_str(), &dstbuf)!=0)
      {
	if(errno!=ENOENT)
	  {
	    perror(("Can't stat "+dst).c_str());
	    return false;
	  }
      }
    else if(dstbuf.st_mtime>srcbuf.st_mtime)
      do_copy=false;

    if(do_copy)
      {
	if(!copy(src, dst))
	  {
	    perror(("Can't copy "+src+" to "+dst).c_str());
	    return false;
	  }
      }

    return true;
  }
};

bool copy_newer_recursive(const string &src, const string &dst)
{
  return act_recursive(src, dst,
		       copy_newer_action(),
		       cp_dir_action<copy_newer_action>(copy_newer_action()));
}
