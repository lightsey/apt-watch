// apt-watch-common.cc
//
// Copyright 2004 Daniel Burrows

#include "apt-watch-common.h"

#include <errno.h>

using namespace std;

void write_msgid(int fd, unsigned char msgid)
{
  write(fd, &msgid, sizeof(msgid));
}

void write_string(int fd, const string &str)
{
  string::size_type sz=str.size();

  write(fd, &sz, sizeof(sz));
  write(fd, str.c_str(), sz);
}

void write_msg(int fd, unsigned char msgid, const std::string &str)
{
  write_msgid(fd, msgid);
  write_string(fd, str);
}

// Collect this in one spot so I can do it right with mostly-proper
// error checking.
void write_errno(int fd, unsigned char msgid, const std::string &str, int nr)
{
  int size=512;
  char *errstr=(char *) malloc(size);

  int ok=1;

  while(size<(1<<16) && errstr!=NULL &&
	((ok=strerror_r(nr, errstr, size)!=0) || errno!=EINVAL))
    {
      size*=2;
      free(errstr);
      errstr=(char *)malloc(size);
    }

  if(errstr==NULL)
    write_msg(fd, msgid, "Couldn't allocate memory to report an error.");
  else if(ok!=0)
    // Should never happen.
    write_msg(fd, msgid, "An error occured, but reporting it requires too large a buffer.");
  else
    {
      size_t outlen=strlen(errstr)+str.size()+10;
      char *outbuf=(char *) malloc(outlen);

      snprintf(outbuf, outlen, str.c_str(), errstr);

      write_msg(fd, msgid, outbuf);

      free(outbuf);
    }

  free(errstr);
}

void write_errno(int fd, unsigned char msgid, const std::string &str)
{
  write_errno(fd, msgid, str, errno);
}

char *read_string(int fd)
{
  size_t amt;

  if(read(fd, &amt, sizeof(amt))<(int) sizeof(amt))
    return NULL;

  char *rval=(char *) malloc(amt+1);

  read(fd, rval, amt);

  rval[amt]=0;

  return rval;
}
