#include <u.h>
#include <libc.h>
#include <avl.h>
#include <fcall.h>
#include "dat.h"

char Efs[]	= "internal error";
char Ecorrupt[] = "block contents corrupted";
char Eio[]	= "i/o error";
char Efull[]	= "file system full";
char Estuffed[]	= "emergency blocks exhausted";
char Enomem[]	= "out of memory";
