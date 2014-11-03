/*****************************************************************************/
/*  mapxqs_demangle.h                                                        */
/*****************************************************************************/

#ifndef _mapxqs_demangle_h
#define _mapxqs_demangle_h

/*****************************************************************************/
/*  - used by mapxqs.c & mapxqs_vac.c                                        */
/*  - parts extracted from (ibmcxxo)\include\demangle.h - v3.65              */
/*****************************************************************************/

/* Name is an opaque structure (actually, a class)
 * containing the demangled data */
typedef struct _Name Name;

/* options passed to the demangler */
typedef enum { RegularNames = 0x1, ClassNames = 0x2, SpecialNames = 0x4,
               ParameterText = 0x8, QualifierText = 0x10 } DemanglingOptions;

/* the type of data a Name represents */
typedef enum { VirtualName, MemberVar, Function, MemberFunction, Class,
               Special, Long } NameKind;

/* typedefs for pointers to the _Optlink functions in demangl.dll */
typedef Name* _Optlink _DEMANGLE(char*, char**, unsigned long);
typedef _DEMANGLE*  PFNDEMANGLE;

typedef NameKind _Optlink _KIND(Name*);
typedef _KIND*  PFNKIND;

typedef char* _Optlink _TEXT(Name*);
typedef _TEXT*  PFNTEXT;

typedef void _Optlink _ERASE(Name*);
typedef _ERASE* PFNERASE;

/* _Cdecl wrappers for _Optlink functions - see mapxqs-vac.c */
Name* __cdecl   demangle_vac(char* name, char** rest, unsigned long options);
NameKind __cdecl kind_vac(Name* name);
char* __cdecl   text_vac(Name* name);
char* __cdecl   qualifier_vac(Name* name);
char* __cdecl   functionName_vac(Name* name);
void  __cdecl   erase_vac(Name* name);

/*****************************************************************************/
/*  - used by mapxqs.c only                                                  */
/*  - extracted from (gcc)\include\demangle.h - v4.x                         */
/*****************************************************************************/

#if __GNUC__

#define DMGL_NO_OPTS      0           /* For readability... */
#define DMGL_PARAMS       (1 << 0)    /* Include function args */
#define DMGL_ANSI         (1 << 1)    /* Include const, volatile, etc */
#define DMGL_JAVA         (1 << 2)    /* Demangle as Java rather than C++. */
#define DMGL_VERBOSE      (1 << 3)    /* Include implementation details.  */
#define DMGL_TYPES        (1 << 4)    /* Also try to demangle type encodings.  */
#define DMGL_RET_POSTFIX  (1 << 5)    /* Print function return types (when
                                         present) after function signature */
#define DMGL_GNU_V3       (1 << 14)

/* V3 ABI demangling entry points, defined in cp-demangle.c.  Callback
   variants return non-zero on success, zero on error.  char* variants
   return a string allocated by malloc on success, NULL on error.  */

/* Callback typedef for allocation-less demangler interfaces. */
typedef void (*demangle_callbackref) (const char *, size_t, void *);

extern int
cplus_demangle_v3_callback (const char *mangled, int options,
                            demangle_callbackref callback, void *opaque);

extern char*
cplus_demangle_v3 (const char *mangled, int options);

#endif /* __GNUC__ */

/*****************************************************************************/
/*  - used by mapxqs_vac.c only                                              */
/*  - extracted from (ibmcxxo)\include\demangle.h - v3.65                    */
/*****************************************************************************/

#if __IBMC__ || __IBMCPP__

#pragma pack(push)
#pragma pack(4)

typedef enum { False = 0, True = 1 } Boolean;

/*
 *     demangle. Given a valid C++ "name" and the address of a char pointer,
 * this function creates a "Name" instance and returns its address. A valid C++
 * name is one starting with an "_" or letter followed by a number of letters,
 * digits and "_"s. The name is assumed to start at the beginning of the
 * string, but there may be trailing characters not part of the mangled name.
 * A pointer into "name" at the first character not part of the mangled name
 * is returned in "rest".
 */

    Name* _Optlink demangle(char* name, char** rest, unsigned long options);

/*
 * Each of the following functions takes a pointer to a Name as its only
 * parameter.
 */

    NameKind _Optlink kind(Name* name);

    /* return the character representation of a given Name */
    char* _Optlink text(Name* name);

    /* return the probable type of a given LongName-type Name */
    NameKind _Optlink probableKind(Name* name);

    /* return the actual name of a given Var- or MemberVar-type Name */
    char* _Optlink varName(Name* name);

    /* return the qualifier text of the given Member-type Name */
    char* _Optlink qualifier(Name* name);

    /* return the actual name of a given Function- or MemberFunction- */
    /* type Name */
    char* _Optlink functionName(Name* name);

    /* returns whether the parameter information was maintained for a */
    /* particular Function- or MemberFunction- type Name. */
    Boolean _Optlink paramDataKept(Name* name);

    /* returns whether the qualifier information was maintained for a */
    /* particular Member- type Name. */
    Boolean _Optlink classDataKept(Name* name);

    /*
     * The next three functions require that option "ParameterText" was given
     * to Demangle.
     */

    /* return the number of arguments of a given Function- or Member- */
    /* Function type Name. */
    long _Optlink nArguments(Name* name);

    /* return the text of the argument list of a given Function- or Member- */
    /* Function- type Name. (char *)NULL is returned if the name wasn't     */
    /* demangled with option ParameterText, and "" is returned if the arg-  */
    /* ument list is empty. */
    char* _Optlink argumentsText(Name* name);

    /* return the text of the nth argument of a given Function- or Member- */
    /* Function- type Name. (char *)NULL is returned if the name wasn't    */
    /* demangled with option ParameterText, or the function doesn't have n */
    /* arguments. The arguments of a function are numbered from 0. */
    char* _Optlink argumentText(Name* name, int n);

    /*
     * The next three functions require that option "QualifierText" was given
     * to Demangle.
     */

    /* return the number of qualifiers of the given Member- type Name */
    unsigned long _Optlink nQualifiers(Name* name);

    /* return the text of the nth qualifier of a given Member- type Name. */
    /* (char *)NULL is returned if "n" is out of range. The qualifiers of */
    /* a name are numbered from the left starting at zero. */
    char* _Optlink qualifierText(Name* name, unsigned long n);

    /* return the text of the class name of the nth qualifier of a given    */
    /* Member- type Name. (char *)NULL is returned if "n" is out of range.  */
    /* This function will return a value different from the preceding func- */
    /* tion only if the class is a template class. The qualifiers of a name */
    /* are numbered from the left starting at zero. */
    char* _Optlink qualifierNameText(Name* name, unsigned long n);

    /* is a Member-type Name constant? */
    Boolean _Optlink isConstant(Name* name);

    /* is a Member-type Name static? */
    Boolean _Optlink isStatic(Name* name);

    /* is a Member-type Name volatile? */
    Boolean _Optlink isVolatile(Name* name);

    /* is a MemberFunction-type Name a contravariant function? */
    Boolean _Optlink isContravariant(Name* name);

    /* is a MemberFunction-type Name a tdisp thunk function? */
    Boolean _Optlink isTDispThunk(Name* name);

    /* is a MemberFunction-type Name __unaligned? */
    Boolean _Optlink isUnaligned(Name* name);

    /* delete the Name instance */
    void _Optlink erase(Name* name);

#pragma pack(pop)

#endif /* __IBMC__ || __IBMCPP__ */

/*****************************************************************************/

#endif /* _mapxqs_demangle_h */

/*****************************************************************************/

