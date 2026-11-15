/* Deterministic bounded freestanding filename glob matcher.
 * ash uses fnmatch() to match shell words. This initial implementation
 * avoids libc allocation, object syscalls and implicit path authority.
 * Explicit unsupported pattern syntax returns FNM_NOMATCH, not a match.
 */
#include "fnmatch.h"
static unsigned char folded(unsigned char c, int flags)
{
    if ((flags & FNM_CASEFOLD) && c >= 'A' && c <= 'Z')
        return (unsigned char)(c + 'a' - 'A');
    return c;
}
static int leading_period(const char *s, const char *start, int flags)
{
    return (flags & FNM_PERIOD) && *s == '.' &&
           (s == start || ((flags & FNM_PATHNAME) && s[-1] == '/'));
}
/* On success advance to character after ], otherwise report invalid.
 * Range and negation use ASCII semantics; POSIX named classes and locale-
 * dependent collation are deliberately rejected by this initial adapter.
 */
static int bracket(const char **after, unsigned char value, int flags)
{
    const char *p = *after;
    unsigned char previous = 0;
    int invert = 0, matched = 0, first = 1, saw = 0;
    if (*p == '!' || *p == '^') { invert = 1; ++p; }
    if (*p == ']') {
        if (folded((unsigned char)*p, flags) == folded(value, flags))
            matched = 1;
        previous = (unsigned char)*p++;
        saw = 1;
    }
    while (*p && *p != ']') {
        unsigned char c;
        if (*p == '[' && p[1] == ':') return -1;
        if (*p == '\\' && !(flags & FNM_NOESCAPE)) {
            ++p;
            if (!*p) return -1;
        }
        c = folded((unsigned char)*p++, flags);
        if (c == '-' && !first && *p && *p != ']') {
            unsigned char last;
            if (*p == '\\' && !(flags & FNM_NOESCAPE)) {
                ++p;
                if (!*p) return -1;
            }
            last = folded((unsigned char)*p++, flags);
            if (previous > last) return -1;
            if (folded(value, flags) >= previous &&
                folded(value, flags) <= last)
                matched = 1;
            previous = 0; first = 1; saw = 1;
            continue;
        }
        if (c == folded(value, flags)) matched = 1;
        previous = c;
        first = 0; saw = 1;
    }
    if (*p != ']' || !saw) return -1;
    *after = p+1;
    return invert ? !matched : matched;
}
static int matches(const char *p, const char *s, const char *origin,
                   int flags, unsigned long *budget)
{
    if (!*budget) return 0;
    --*budget;
    while (*p) {
        unsigned char c = (unsigned char)*p++;
        if (c == '*') {
            const char *q = s;
            while (*p == '*') ++p;
            /* Do not consume a leading dot or a slash with '*' when
             * corresponding FNM flags prohibit it.
             */
            if (matches(p, q, origin, flags, budget)) return 1;
            while (*q) {
                if ((flags & FNM_PATHNAME) && *q == '/') break;
                if (leading_period(q, origin, flags)) break;
                ++q;
                if (matches(p, q, origin, flags, budget)) return 1;
            }
            return 0;
        }
        if (!*s) return 0;
        if (c == '?' || c == '[') {
            if (((flags & FNM_PATHNAME) && *s == '/') ||
                leading_period(s, origin, flags))
                return 0;
            if (c == '[') {
                int rc = bracket(&p, (unsigned char)*s, flags);
                if (rc != 1) return 0;
            }
            ++s;
            continue;
        }
        if (c == '\\' && !(flags & FNM_NOESCAPE)) {
            if (!*p) return 0;
            c = (unsigned char)*p++;
        }
        if (folded(c, flags) != folded((unsigned char)*s, flags))
            return 0;
        ++s;
    }
    return !*s || ((flags & FNM_LEADING_DIR) && *s == '/');
}
int fnmatch(const char *pattern, const char *string, int flags)
{
    unsigned long budget = 65536;
    if (!pattern || !string) return FNM_NOMATCH;
    return matches(pattern, string, string, flags, &budget) ?
           0 : FNM_NOMATCH;
}
