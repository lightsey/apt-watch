// fileutl.h -- high-level file operations.               -*-c++-*-
//
//  Copyright 2004 Daniel Burrows

#ifndef FILEUTL_H
#define FILEUTL_H

#include <string>

/** Copy the file "src" to a temporary file whose name is based on "dst" and
 *  which resides in the same directory.  The name is placed into the third
 *  argument of this function.
 */
bool copy_temp(const std::string &src, const std::string &dst,
	       std::string &name);

/** Copy the file "src" to "dst".  Behaves like "cp".  Returns \b true if
 *  the operation succeeded; otherwise sets errno.
 */
bool copy(const std::string &src, const std::string &dst);

/** Move the file "src" to "dst".  Tries to perform the move atomically,
 *  but that may not be possible.
 */
bool move(const std::string &src, const std::string &dst);

/** Copy a directory hierarchy, aborting and returning \b false if an
 *  error occurs.
 */
bool copy_recursive(const std::string &src, const std::string &dst);

/** Move files from one directory hierarchy to another, aborting and
 *  returning \b false if an error occurs.
 */
bool move_recursive(const std::string &src, const std::string &dst);

/** Copy a directory hierarchy, leaving newer files in place. */
bool copy_newer_recursive(const std::string &src, const std::string &dst);

/** Returns \b true if the given filename exists in the colon-separated PATH. */
bool in_path(const std::string &PATH, const std::string &fn);

/** Returns \b true if the given filename exists in the system PATH. */
bool in_path(const std::string &fn);

#endif // FILEUTL_H

