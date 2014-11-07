#ifndef VERSION_H
#define VERSION_H

#define _CSTR(x) #x
#define CSTR(x) _CSTR(x)

#define ED2KD_VER_MJR 17
#define ED2KD_VER_MNR 15

#define ED2KD_VER_STR CSTR(ED2KD_VER_MJR) "." CSTR(ED2KD_VER_MNR)

#endif // VERSION_H
