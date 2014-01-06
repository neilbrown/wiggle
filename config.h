
/* Includes and defines for ccan files */

#if !defined(BYTE_ORDER) && !defined(LITTLE_ENDIAN) && !defined(BIG_ENDIAN)
 #if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
  #include <machine/endian.h>
 #else
  #include <endian.h>
 #endif
#endif
#if BYTE_ORDER == LITTLE_ENDIAN
 #define HAVE_LITTLE_ENDIAN 1
 #define HAVE_BIG_ENDIAN 0
#elif BYTE_ORDER == BIG_ENDIAN
 #define HAVE_LITTLE_ENDIAN 0
 #define HAVE_BIG_ENDIAN 1
#else
 #error Unknown endian
#endif

