/* Initial freestanding POSIX filename-pattern subset for ash dependency work.
 * Flags follow the common libc fnmatch constants; no locale or UTF-8
 * character class handling yet. This is not a complete POSIX certification.
 */
#ifndef PLAN2001_UNIX_FNMATCH_H
#define PLAN2001_UNIX_FNMATCH_H
#define FNM_NOMATCH 1
#define FNM_PATHNAME 1
#define FNM_FILE_NAME FNM_PATHNAME
#define FNM_NOESCAPE 2
#define FNM_PERIOD 4
#define FNM_LEADING_DIR 8
#define FNM_CASEFOLD 16
int fnmatch(const char *pattern, const char *string, int flags);
#endif
