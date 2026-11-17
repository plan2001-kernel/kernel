#ifndef PLAN2001_PROGRAM_H
#define PLAN2001_PROGRAM_H
#include <stddef.h>
int p9_program_validate(const void *,size_t);
const char *p9_program_diagnostic(void);
int p9_program_register(const char *);
int p9_program_lookup(const char *); /* owned scoped executable Handle */
#endif
