// apt-watch-common.h          -*-c++-*-

#include <string>

#define PROTOCOL_VERSION 1

#define APPLET_CMD_UPDATE 0
#define APPLET_CMD_RELOAD 1
#define APPLET_CMD_SU     2
#define APPLET_CMD_AUTHREPLY 3
#define APPLET_CMD_AUTHCANCEL 4
#define APPLET_CMD_DOWNLOAD 5

// Only valid during an update or a download.
#define APPLET_CMD_ABORT_DOWNLOAD 6

#define APPLET_REPLY_AUTH_PROMPT_NOECHO 64
#define APPLET_REPLY_AUTH_PROMPT_ECHO 65
#define APPLET_REPLY_AUTH_ERRORMSG 66
#define APPLET_REPLY_AUTH_INFO 67

#define APPLET_REPLY_PROGRESS_UPDATE 68
#define APPLET_REPLY_PROGRESS_DONE 69

#define APPLET_REPLY_AUTH_FAIL 128
#define APPLET_REPLY_AUTH_OK 129

#define APPLET_REPLY_INIT_OK_NOUPGRADES 130
#define APPLET_REPLY_INIT_OK_UPGRADES 131
#define APPLET_REPLY_INIT_OK_SECURITY_UPGRADES 132
#define APPLET_REPLY_INIT_FAILED 133

#define APPLET_REPLY_CMD_COMPLETE_NOUPGRADES 134
#define APPLET_REPLY_CMD_COMPLETE_UPGRADES 135
#define APPLET_REPLY_CMD_COMPLETE_SECURITY_UPGRADES 136

#define APPLET_REPLY_FATALERROR 137

#define APPLET_REPLY_REQUEST_RELOAD 138

#define APPLET_REPLY_DOWNLOAD_COMPLETE 139

#define APPLET_REPLY_AUTH_FINISHED 140

// TODO: protocol marshalling/demarshalling functions.

/** Write a string to the given fd */
void write_string(int fd, const std::string &str);

/** Convenience for messages that have a single string; writes the
 *  message header and then the string.
 */
void write_msg(int fd, unsigned char msgid, const std::string &str);

/** Write a message header to the given fd.  (could be inline/a macro?) */
void write_msgid(int fd, unsigned char msgid);

/** Format and write a message based on the given error code to the
 *  given fd.  %s will be replaced with a string describing the error.
 */
void write_errno(int fd, unsigned char msgid, const std::string &str, int nr);

/** As above, but uses the current value of errno. */
void write_errno(int fd, unsigned char msgid, const std::string &str);

/** Reads a string from a file descriptor, returning the \b newly
 *  \b allocated string or NULL if one could not be read.
 */
char* read_string(int fd);
