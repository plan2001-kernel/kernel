#define TCC_VERSION "0.9.28-plan2001"
#define TCC_TARGET_X86_64 1
#define TCC_TARGET_PLAN2001 1
#define CONFIG_TCC_STATIC 1
#define CONFIG_TCC_BCHECK 0
#define CONFIG_TCC_BACKTRACE 0
#define CONFIG_TCC_SEMLOCK 0
#define CONFIG_TCC_PREDEFS 0
#define CONFIG_TCCDIR "/usr/lib/tcc"
#define CONFIG_TCC_SYSINCLUDEPATHS "/usr/lib/tcc/include:/usr/include"
#define CONFIG_TCC_LIBPATHS "/usr/lib:/usr/lib/tcc"
#define CONFIG_TCC_CRTPREFIX "/usr/lib"
#define CONFIG_TCC_SWITCHES "-static -Wl,-Ttext=0x200000"
