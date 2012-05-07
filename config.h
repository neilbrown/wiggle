
/* Includes and defines for ccan files */

#include <endian.h>
#ifdef LITTLE_ENDIAN
 #define HAVE_LITTLE_ENDIAN 1
 #define HAVE_BIG_ENDIAN 0
#else
 #define HAVE_LITTLE_ENDIAN 0
 #define HAVE_BIG_ENDIAN 1
#endif

