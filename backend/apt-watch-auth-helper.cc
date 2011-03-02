// apt-watch-auth-helper.cc
//
// Called as apt-watch-auth-helper <cmd>.  First requires the root
// password from the parent, then copies lists/archives from
// ~/.apt-watch to the system directories, deletes any successfully
// copied archives, and executes the command.
//
// TODO: copy lists
// TODO: copy archives
// TODO: execute the command
//
// Copyright 2004 Daniel Burrows

#include "apt-watch-common.h"
#include "fileutl.h"

#include <security/pam_appl.h>

#include <cstdio>
#include <cstring>
#include <stdlib.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>


#include <string>

using namespace std;

/** The command to be executed with no arguments. */
string cmd;

/** If \b true, the command is run in an xterm (via xterm -e) */
bool run_xterm;

/** Handles PAM interaction. */
int my_conv(int num_msg,
	    const pam_message **msg,
	    pam_response **resp,
	    void *appdata_ptr)
{
  pam_response *reply=(pam_response *) malloc(num_msg*sizeof(pam_response));

  for(int i=0; i<num_msg; ++i)
    {
      unsigned char msgtype;
      std::string s;

      switch(msg[i]->msg_style)
	{
	case PAM_PROMPT_ECHO_OFF:
	  msgtype=APPLET_REPLY_AUTH_PROMPT_NOECHO;
	  break;
	case PAM_PROMPT_ECHO_ON:
	  msgtype=APPLET_REPLY_AUTH_PROMPT_ECHO;
	  break;
	case PAM_ERROR_MSG:
	  msgtype=APPLET_REPLY_AUTH_ERRORMSG;
	  break;
	case PAM_TEXT_INFO:
	  msgtype=APPLET_REPLY_AUTH_INFO;
	  break;
	default:
	  {
	    write_msg(APPLET_REPLY_AUTH_FAIL, 1,
		      "Unknown PAM query type");
	    exit(-1); // return a "failed" code instead?
	  }
	}

      char buf[1024];
      // Provide some protection against naive hijacking of the slave
      // by printing out the command which will be run, although if
      // the user's account is compromised there probably isn't any
      // way to save the situation perfectly.

      snprintf(buf, sizeof(buf), "Running \"%s\" as root.\n", cmd.c_str());

      s=string(buf)+msg[i]->msg;

      write_msg(1, msgtype, s);
    }

  for(int i=0; i<num_msg; ++i)
    {
      if(msg[i]->msg_style==PAM_PROMPT_ECHO_OFF ||
	 msg[i]->msg_style==PAM_PROMPT_ECHO_ON)
	{
	  string::size_type len;

	  if(read(0, &len, sizeof(len))<(int) sizeof(len))
	    // The user cancelled the interaction.
	    exit(0);

	  // Avoid DoS with a threshold that's much larger than necessary.
	  // (email me if you have a 1001-byte root password and you think
	  // this limit is unreasonable :) )
	  if(len>1000 || len>PAM_MAX_MSG_SIZE)
	    {
	      write_msg(APPLET_REPLY_AUTH_FAIL, 1,
			"Protocol error: absurd reply length");
	      exit(-1);
	    }

	  char *s=(char *) malloc(len+1);
	  read(0, s, len);
	  s[len]=0;

	  reply[i].resp=s;

	  reply[i].resp_retcode=0;
	}
      else
	{
	  reply[i].resp=NULL;
	  reply[i].resp_retcode=0;
	}
    }

  *resp=reply;

  return PAM_SUCCESS;
}

static const pam_conv pam_conversation=
  {
    my_conv,
    NULL
  };


int main(int argc, char **argv)
{
  int outfd=1;

  int rval;

  srand(time(0));

  if(argc<2)
    {
      write_msg(outfd, APPLET_REPLY_AUTH_FAIL, "Internal error: not enough arguments to the helper");
      return -1;
    }

  if(argc>2 && !strcmp(argv[1], "-x"))
    {
      for(int i=2; i<argc; ++i)
	argv[i-1]=argv[i];

      --argc;
      run_xterm=true;
    }

  // TODO: allow arguments?
  cmd=argv[1];

  // Die when a pipe is closed.
  signal(SIGPIPE, SIG_DFL);

  // Everything hinges on being able to run a program in X.
  if(!getenv("DISPLAY"))
    {
      write_msg(outfd, APPLET_REPLY_AUTH_FAIL,
		"No X session available for the package manager.");
      return -1;
    }

  //  Maybe I should clear everything but DISPLAY and HOME?  OTOH, if
  //  they have the root password, then it doesn't really matter what I do...
  //
  // clearenv();

  if(geteuid()!=0)
    {
      write_msg(outfd, APPLET_REPLY_AUTH_FAIL, "Internal error: the helper is not root");

      return -1;
    }

  if(getuid()!=0)
    {
      pam_handle_t *pam_handle;

      std::string display=getenv("DISPLAY");

      rval=pam_start("apt-watch-backend", "root", &pam_conversation, &pam_handle);

      if(rval!=PAM_SUCCESS)
	{
	  write_msg(APPLET_REPLY_AUTH_FAIL, outfd,
		    string("Unable to initialize PAM: ")+pam_strerror(pam_handle, rval));

	  pam_end(pam_handle, rval);

	  return -1;
	}

      if(rval==PAM_SUCCESS)
	rval=pam_authenticate(pam_handle, 0);

      if(rval==PAM_SUCCESS)
	rval=pam_acct_mgmt(pam_handle, 0);

      if(rval!=PAM_SUCCESS)
	{
	  write_msg(outfd, APPLET_REPLY_AUTH_FAIL,
		    pam_strerror(pam_handle, rval));

	  pam_end(pam_handle, rval);

	  return -1;
	}
      else
	{
	  if(pam_end(pam_handle, rval)!=PAM_SUCCESS)
	    {
	      write_msg(outfd, APPLET_REPLY_AUTH_FAIL,
			"Unable to shut down authentication!");
	      return -1;
	    }

	  write_msgid(outfd, APPLET_REPLY_AUTH_OK);
	}
    }

  const char *HOME=getenv("HOME");

  if(HOME)
    {
      string home=HOME;

      // Copy everything from ~/.apt-watch/lists to /var/lib/apt/lists;
      // everything from ~/.apt-watch/archives to /var/lib/apt/archives.
      //
      // TODO: read the apt configuration here?

      setegid(0);

      copy_recursive(home+"/.apt-watch/lists", "/var/lib/apt/lists");
      move_recursive(home+"/.apt-watch/archives", "/var/cache/apt/archives");
    }

  pid_t pmpid;
  int Status = 0;
  // Actually execute it.
  switch(pmpid = fork())
    {
    case 0:
      // Close all file descriptors.
      //for(int i=0; i<getdtablesize(); ++i)
      //  close(i);

      // ehhh, just close stdin and stdout
      close(0);
      close(1);

      // Really become root, or xterm and synaptic will have a fit
      setresgid(0,0,0);
      setresuid(0,0,0);

      // Run the appropriate program.  Uses the shell to do splitting,
      // etc (you have to know the root password to get here anyway,
      // so there's no danger of a privilege escalation)
      if(run_xterm)
	// assumes xterm is in the path
	execlp("xterm", "xterm", "-e", "sh", "-c", cmd.c_str(), NULL);
      else
	execlp("sh", cmd.c_str(), "-c", cmd.c_str(), NULL);

      // If the program is still alive, an error occured.
      //
      // (does it make sense to pass error codes back to the parent?)
      perror("apt-watch-auth-helper: exec");
      return -1;
    case -1:
      {
	write_errno(1, APPLET_REPLY_AUTH_FAIL, "fork() failed: %s");

	return -1;
      }
    default:
      waitpid(pmpid,&Status,0);
      break;
    }
    
	write_msgid(outfd, APPLET_REPLY_AUTH_FINISHED);
	return 0;
}
