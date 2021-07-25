/*
 * Copyright (C) 1989 by Kenneth Almquist.  All rights reserved.
 * This file is part of ash, which is distributed under the terms specified
 * by the Ash General Public License.  See the file named LICENSE.
 */

#include <stdio.h>

char *commandname;


void
#ifdef __STDC__
error(char *msg, ...) {
#else
error(msg)
      char *msg;
      {
#endif

      fprintf(stderr, "%s: %s\n", commandname, msg);
      exit(2);
}
