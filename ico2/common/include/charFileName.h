/* charFileName.h -- the character-file id table's bound.
 *
 * charFileManager.c prints a message naming "commmon/include/charFileName.h"
 * and asking for MAX_CHARS to be raised whenever a character id reaches the
 * table's bound.
 */
#ifndef CHARFILENAME_H
#define CHARFILENAME_H
/* The European bound on every disc: the in-memory table is PAL's, and the
 * Japanese disc's ids stay below its own bound of 1621. */
#define MAX_CHARS 1637
/* the rows of the model-path table (modelData) ReadSkeltonFile and
   ReadCollisionFile search; the name is the one ReadSkeltonFile's
   "Too Many ModelDataID_NUMBER exists." message gives it */
#define ModelDataID_NUMBER 1474
#endif
