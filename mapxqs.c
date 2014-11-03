/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is MapXQS.
 *
 * The Initial Developer of the Original Code is
 * Richard L. Walsh
 * Portions created by the Initial Developer are Copyright (C) 2010-2011
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *
 *
 * ***** END LICENSE BLOCK ***** */
/*****************************************************************************/
/*  mapxqs.c - v1.04a
 *
 *  MapXQS creates symbol files in the XQS format using mapfiles created
 *  by IBM, GCC, OpenWatcom, and Borland linkers.  By default, XQS files
 *  identify which module contains a given symbol (assuming the mapfile
 *  provided that info);  the 'M' commandline option will omit this info.
 *
 *  MapXQS can produce a listing of modules and symbols by using the 'L'
 *  option when creating the XQS file.  It can also create this listing
 *  from an existing XQS file using the 'D' option.
 *
 *  Note:  the demangler for gcc is statically linked to the exe while
 *  the vacpp demangler is contained in demangl.dll.  Unfortunately,
 *  the dll's functions have Optlink linkage which gcc 4.xx can't handle.
 *  As a workaround, Remap uses wrapper functions contained in a separate
 *  file, mapxqs_vac.c.  The author compiles it using VACPP 3.65 but it
 *  could probably be compiled with gcc 3.3.5.  Alternately, someone who
 *  understands gcc's assembler semantics could replace it with some
 *  inline assembly.
 *
 */
/*****************************************************************************/

#define USE_OS2_TOOLKIT_HEADERS

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <fcntl.h>
#include <io.h>
#include <sys\types.h>
#include <sys\stat.h>

#define INCL_DOS
#include <os2.h>

#include "mapxqs_demangle.h"

#define INCL_LOADEXCEPTQ
#include "exceptq.h"
#include "xqs.h"

/*****************************************************************************/

#define HFILE_NONE        ((HFILE)-1)
#define NULLCHAR          ((char)0)

#define OPT_NO_DEMANGLE   0x01
#define OPT_LIST          0x02
#define OPT_NOMOD         0x04
#define OPT_DUMP          0x08
#define OPT_GCC           0x10
#define OPT_VAC           0x20

#define REMAP_END         0
#define REMAP_MOD         0x0001
#define REMAP_OBJ         0x0002
#define REMAP_TYPE        0x000F

#define REMAP_VTABLE      0x01000
#define REMAP_THUNK       0x02000
#define REMAP_TYPEINFO    0x04000
#define REMAP_TYPENAME    0x08000
#define REMAP_GUARD       0x10000
#define REMAP_VTT         0x20000
#define REMAP_CONSTRUCT   0x40000
#define REMAP_VIRTTHUNK   0x80000

#define REMAP_ATTRMASK    0xFF000

#define REMAP_MASK        (REMAP_TYPE | REMAP_ATTRMASK)

#define REMAP_USED        0x20000000
#define REMAP_DUP2        0x40000000
#define REMAP_DUP         0x80000000

typedef struct _remap {
    struct _remap*  next;
    ULONG   type;
    ULONG   seg;
    ULONG   offs;
    struct _remap*  mod;
    ULONG   lth;
    char    text[1];
} REMAP;

/*****************************************************************************/

int     ParseArgs(int argc, char* argv[]);
int     Init(void);
int     LoadVacDemangler(void);

int     ParseInput(void);
char*   ParseModules(void);
int     ParseWatcom(void);
int     WatStorePublics(void);
int     WatParseModule(char* pData, REMAP* r);
int     ParseBorland(void);
int     BorStoreModules(void);
int     BorStorePublics(void);
int     ParseSyn(void);
int     ParseIBM(void);
int     IbmParseSegment(char* pData, ULONG* pSeg, ULONG* pOffs);
int     IbmStoreModule(char* pData, ULONG ulSeg, ULONG ulOffs);
int     IbmStorePublics(void);
int     IbmMarkDuplicateMods(char* pMods, int modCnt);
int     IbmDuplicateModSorter(const void* key, const void* element);
int     IbmMarkDuplicatePubs(char* pPublics, int pubCnt);
int     IbmDuplicatePubSorter(const void* key, const void* element);

char ** SeekToHdr(char** pSeek, char** pStop);
int     MatchArray(char** pArray, char* pText);
char *  Trim(char* pTrim, char** ppNext);
char *  TrimLine(char* pTrim);
char *  DecodeFlagName(ULONG flags);
char *  Demangle(char* pIn, char* pOut, ULONG cbOut, ULONG* pFlags);
void    DemangleCallback(const char* pSrc, size_t cbSrc, void* pv);
char*   DemangleVAC(char* pIn, char* pOut, ULONG* pFlags);

int     WriteOutput(void);
REMAP** SortByAddress(void);
int     AddressSorter(const void *key, const void *element);
int     PrintListing(REMAP** pr);
int     WriteHeader(REMAP** pArr);
int     WriteMods(REMAP** pArr, ULONG offs, ULONG offsEnd, ULONG padMods);
int     WriteSegs(REMAP** pArr);
int     WriteSyms(REMAP** pStart, REMAP** pStop, ULONG offsStrings,
                  ULONG padSym, ULONG padStrings);

int     DumpXQS(void);

/*****************************************************************************/

/** globals **/
FILE *  fi = 0;
FILE *  fo = 0;
char *  buffer = 0;
ULONG   cbBuffer = 0;
ULONG   cbFile = 0;

int     opts = 0;
int     lineNbr = 0;
int     isIbm = 0;
int     isWat = 0;
int     isBor = 0;
int     isSyn = 0;
int     cntMods = 0;
int     cbXQSYM = 0;

char *  pCur = 0;
int     recCnt = 0;

char    fIn[CCHMAXPATH] = "";
char    fOut[CCHMAXPATH] = "";
char    fList[CCHMAXPATH] = "";

char    bufIn[1024];
char    workBuf[1024];

/* these pointers are declared in remap_vac.c */
extern PFNDEMANGLE  pfnDemangle;
extern PFNKIND      pfnKind;
extern PFNTEXT     	pfnText;
extern PFNTEXT      pfnQualifier;
extern PFNTEXT      pfnFunctionName;
extern PFNERASE     pfnErase;

/*****************************************************************************/

/** Constants **/

char *  pszWS = " \t\r\n";
char *  pszTrouble = "$w$";
char *  pszModule = "Module:";

char    aPad[16] = {0};

char    szAtOffset[] = "at offset ";
int     cbAtOffset = sizeof(szAtOffset) - 1;

char    szVtable[] = "vtable for ";
int     cbVtable = sizeof(szVtable) - 1;
char    szThunk[] = "non-virtual thunk to ";
int     cbThunk = sizeof(szThunk) - 1;
char    szTypeInfo[] = "typeinfo for ";
int     cbTypeInfo = sizeof(szTypeInfo) - 1;
char    szTypeName[] = "typeinfo name for ";
int     cbTypeName = sizeof(szTypeName) - 1;
char    szGuard[] = "guard variable for ";
int     cbGuard = sizeof(szGuard) - 1;
char    szVTT[] = "VTT for ";
int     cbVTT = sizeof(szVTT) - 1;
char    szConstruct[] = "construction vtable for ";
int     cbConstruct = sizeof(szConstruct) - 1;
char    szVirtThunk[] = "virtual thunk to ";
int     cbVirtThunk = sizeof(szVirtThunk) - 1;

char    szVtableVAC[] = "::virtual-fn-table-ptr";
int     cbVtableVAC = sizeof(szVtableVAC) - 1;

char *  apszModules[] = {"Start", "Length", "Name", "Class", ""};
char *  apszGroups[] = {"Origin", "Group", ""};
char *  apszPubByName[] = {"Address", "Publics by Name", ""};
char *  apszPubByValue[] = {"Address", "Publics by Value", ""};

char *  apszWatSegments[] = {"Segment", "Class", "Group", "Address", "Size", ""};
char *  apszWatMemMap[] = {"Address", "Symbol", ""};

char *  apszBorSegments[] = {"Detailed map of segments", ""};

char *  pszSrcExt  = ".map";
char *  pszOutExt  = ".xqs";
char *  pszListExt = ".xql";

/*****************************************************************************/

char *  pszReportHdr =
        " MapXQS v1.04a - (C)2010-2011 R L Walsh\n\n"
        " Symbols%s included in %s\n"; 

char *  pszColumnHdr =
      "\n    Seg:Offset    Name\n"
        "   -------------  ------------------------\n";

/*****************************************************************************/

char *  pszHelp =
      "\n mapxqs v1.04a - (C)2010-2011  R L Walsh\n"
        " Creates .xqs symbol files from IBM, Watcom, and Borland .map files.\n\n"
        " Usage:  mapxqs [-options] [optional_files] mapfile[.map]\n"
        " General options:\n"
        "   -o  specify output file             (default: *.xqs)\n"
        "   -l  create a listing of symbols     (default: *.xql)\n"
        "   -m  omit module file names          (default: include module info)\n"
        " Demangler options:\n"
        "   -g  use builtin GCC demangler       (default)\n"
        "   -v  use VAC demangler               (requires demangl.dll)\n"
        "   -n  don't demangle symbols\n"
        " Other options:\n"
        "   -d  dump symbols in *.xqs to *.xql  (example: mapxqs -d file.xqs)\n"
        "       note: -o is the only option that can be used with -d\n"
        "\n";

/*****************************************************************************/

int     main(int argc, char* argv[])
{
  EXCEPTIONREGISTRATIONRECORD ExRegRec;
  int     xq;
  int     rtn = 1;

do {
  xq = LoadExceptq(&ExRegRec, "I", "MapXQS v1.04a");

  if (!ParseArgs(argc, argv))
    break;

  if (!Init()) {
    fprintf(stderr, "Init failed\n");
    break;
  }

  if (opts & OPT_DUMP) {
    if (DumpXQS())
      rtn = 0;
    break;
  }

  if (!ParseInput())
    break;

  if (WriteOutput())
    rtn = 0;

} while (0);

  if (buffer)
    free(buffer);

  if (xq)
    UninstallExceptq(&ExRegRec);

  return rtn;
}

/*****************************************************************************/
/* This lets options & files be specified on the commandline in almost any
 * order.  The only restriction is that optional files be specified in the
 * same order as the options that required them.
 */

int     ParseArgs(int argc, char* argv[])
{
  int     ctr;
  int     needInfile = 1;
  int     needOutfile = 0;
  char *  ptr;

  if (argc < 2) {
    fprintf(stderr, pszHelp);
    return 0;
  }

  for (ctr = 1; ctr < argc; ctr++) {

    if (*argv[ctr] == '-' || *argv[ctr] == '/') {
      ptr = argv[ctr];

      while (*(++ptr)) {
        switch(*ptr) {
          case 'l':
          case 'L':
            opts |= OPT_LIST;
            break;

          case 'm':
          case 'M':
            opts |= OPT_NOMOD;
            break;

          case 'n':
          case 'N':
            opts |= OPT_NO_DEMANGLE;
            break;

          case 'd':
          case 'D':
            opts |= OPT_DUMP;
            break;

          case 'g':
          case 'G':
            opts &= ~OPT_VAC;
            opts |= OPT_GCC;
            break;

          case 'v':
          case 'V':
            opts &= ~OPT_GCC;
            opts |= OPT_VAC;
            break;

          case 'o':
          case 'O':
            needOutfile = 1;
            break;

          case 'h':
          case 'H':
          case '?':
            fprintf(stderr, pszHelp);
            return 0;

          default:
            fprintf(stderr, "Invalid option '%c' ignored - continuing...\n", *ptr);
            break;

        } /* switch */
      } /* while */

      continue;
    } /* if */

    if (needOutfile) {
      strcpy(fOut, argv[ctr]);
      needOutfile = 0;
    } else
    if (needInfile) {
      strcpy(fIn, argv[ctr]);
      needInfile = 0;
    } else {
      fprintf(stderr, "Extra argument '%s'\n", argv[ctr]);
      return 0;
    }
  } /* for */

  if ((opts & OPT_DUMP) &&
      (opts & (OPT_LIST | OPT_NOMOD | OPT_NO_DEMANGLE | OPT_GCC | OPT_VAC))) {
    fprintf(stderr, "Option '-d' (dump) may only be combined with '-o' (output file)\n");
    return 0;
  }

  if (needInfile || needOutfile) {
    fprintf(stderr, "Missing argument for %s\n",
            (needOutfile ? "output file" : ((opts & OPT_DUMP) ? "xqs file" : "map file")));
    return 0;
  }

  if (!(opts & (OPT_GCC | OPT_VAC | OPT_DUMP)))
    opts |= OPT_GCC;

  return 1;
}

/*****************************************************************************/

int     Init(void)
{
  char *  ptr;
  char    szFile[CCHMAXPATH];

  /* Validate input filename. */
  if (!*fIn) {
    fprintf(stderr, "%s file not specified\n",
            (opts & OPT_DUMP) ? ".xqs" : ".map");
    return 0;
  }

  /* Add the appropriate extension if needed. */
  ptr = strrchr(fIn, '.');
  if (!ptr) {
    ptr = strchr(fIn, 0);
    strcpy(ptr, (opts & OPT_DUMP) ? pszOutExt : pszSrcExt);
  }

  /* Fully qualify the input file name. */
  if (DosQueryPathInfo(fIn, FIL_QUERYFULLNAME, szFile, sizeof(szFile))) {
    fprintf(stderr, "invalid input filename or path - '%s'\n", fIn);
    return 0;
  }
  strcpy(fIn, szFile);

  /* Create/validate output filename */
  if (!*fOut) {
    ptr = strrchr(fIn, '\\');
    if (!ptr)
      ptr = fIn - 1;
    ptr++;
    strcpy(fOut, ptr);

    ptr = strrchr(fOut, '.');
    if (!ptr)
      ptr = strchr(fOut, 0);
    strcpy(ptr, (opts & OPT_DUMP) ? pszListExt : pszOutExt);
  }

  /* Fully qualify the output file name. */
  if (DosQueryPathInfo(fOut, FIL_QUERYFULLNAME, szFile, sizeof(szFile))) {
    fprintf(stderr, "invalid output filename or path - '%s'\n", fOut);
    return 0;
  }
  strcpy(fOut, szFile);

  if (!stricmp(fIn, fOut)) {
    fprintf(stderr, "input and output files must have different names or paths\n");
    return 0;
  }

  /* create/validate listing filename */
  if (opts & OPT_LIST) {
    strcpy(fList, fOut);
    ptr = strrchr(fList, '.');
    if (!ptr)
      ptr = strchr(fList, 0);
    strcpy(ptr, pszListExt);

    if (!stricmp(fList, fOut) || !stricmp(fList, fIn)) {
      fprintf(stderr, "input, output, and list files must have different names or paths\n");
      return 0;
    }
  }

  /* load the VAC demangler if needed */
  if (!(opts & (OPT_NO_DEMANGLE | OPT_DUMP))) {
    if (opts & OPT_VAC) {
      if (!LoadVacDemangler()) {
        fprintf(stderr, "unable to load VAC demangler 'demangl.dll'\n");
        return 0;
      }
    }
  }

  /* confirm the input file exists & get its size */
  if (DosQueryPathInfo(fIn, FIL_STANDARD, szFile, sizeof(szFile))) {
    fprintf(stderr, "unable to find input file '%s'\n", fIn);
    return 0;
  }
  cbFile = ((FILESTATUS3*)szFile)->cbFile;
  cbBuffer = (cbFile * 3) / 2;

  /* allocate a buffer as large as the file to store entries */
  buffer = malloc(cbBuffer);
  if (!buffer) {
    fprintf(stderr, "malloc for main buffer failed - size= %ld\n", cbBuffer);
    return 0;
  }
  memset(buffer, 0, cbBuffer);
  pCur = buffer;

  return 1;
}

/*****************************************************************************/
/* This loads demangl.dll.  If it can't be found on the LIBPATH, it looks
   for it in the same directory as mapxqs.exe (which may not be the current
   directory.
*/

int     LoadVacDemangler(void)
{
  HMODULE   hmod = 0;
  PPIB      ppib;
  PTIB      ptib;
  char *    ptr;
  char      szFailName[16];

  *szFailName = 0;
  if (DosLoadModule(szFailName, sizeof(szFailName), "DEMANGL", &hmod)) {
    DosGetInfoBlocks(&ptib, &ppib);
    if (DosQueryModuleName(ppib->pib_hmte, CCHMAXPATH, workBuf) ||
        (ptr = strrchr(workBuf, '\\')) == 0)
      return 0;

    strcpy(&ptr[1], "DEMANGL.DLL");
    if (DosLoadModule(szFailName, sizeof(szFailName), workBuf, &hmod))
      return 0;
  }

  if (DosQueryProcAddr(hmod, 0, "demangle",     (PFN*)&pfnDemangle) ||
      DosQueryProcAddr(hmod, 0, "kind",         (PFN*)&pfnKind) ||
      DosQueryProcAddr(hmod, 0, "text",         (PFN*)&pfnText) ||
      DosQueryProcAddr(hmod, 0, "qualifier",    (PFN*)&pfnQualifier) ||
      DosQueryProcAddr(hmod, 0, "functionName", (PFN*)&pfnFunctionName) ||
      DosQueryProcAddr(hmod, 0, "erase",        (PFN*)&pfnErase)) {
    DosFreeModule(hmod);
    return 0;
  }

  return 1;
}

/*****************************************************************************/
/*  Input Processing                                                         */
/*****************************************************************************/

int     ParseInput(void)
{
  int     rtn = 0;
  char *  ptr;
  char ** pSeek;

  fi = fopen(fIn, "r");
  if (!fi) {
    fprintf(stderr, "unable to open input file '%s'\n", fIn);
    return 0;
  }

do {
  /* Look for either an IBM-style Modules header or Watcom's Segment header. */
  pSeek = SeekToHdr(apszModules, apszWatSegments);

  /* Found Watcom */
  if (pSeek == apszWatSegments) {
    isWat = 1;
    rtn = ParseWatcom();
    break;
  }

  /* If the Modules header wasn't found, we can't identify the format. */
  if (pSeek != apszModules) {
    fprintf(stderr, "Unable to identify mapfile format (missing modules header)\n");
    break;
  }

  /* Store the modules, stopping when an unrecognized line is encountered. */
  ptr = ParseModules();
  if (!ptr)
    break;

  /* Save the count of module records (which includes duplicates).  This
   * controls processing of mod info and is different than the OPT_NOMOD
   * flag which controls whether the shorter version of XQSYM without mod
   * info will be used.
   */
  cntMods = recCnt;

  /* If the line returned is a Groups header, this is an IBM-style map file. */
  if (MatchArray(apszGroups, ptr)) {
    isIbm = 1;
    rtn = ParseIBM();
    break;
  }

  /* If there was no mod info this may be a Borland map file or
   * it may be a "synthetic" mapfile that was created by one of
   * Steve Levine's scripts to be mapsym-compatible.
   */
  if (!cntMods) {
    if (MatchArray(apszPubByName, ptr))
      isBor = 1;
    else
    if (MatchArray(apszBorSegments, ptr))
      isBor = 2;

    if (isBor) {
      rtn = ParseBorland();
      break;
    }

    if (MatchArray(apszPubByValue, ptr) && IbmStorePublics()) {
      isSyn = 1;
      rtn = 1;
      break;
    }
  }

  /* Any other format is unknown. */
  fprintf(stderr, "Unable to identify mapfile format\n");

} while (0);

  fclose(fi);

  return rtn;
}

/*****************************************************************************/
/* This parses segment info and saves module info */

char*   ParseModules(void)
{
  ULONG   seg = 0;
  ULONG   offs = 0;
  char *  ptr;
  char *  pErr = "unexpected end of file";

  while (fgets(bufIn, sizeof(bufIn), fi)) {
    lineNbr++;

    ptr = bufIn + strspn(bufIn, pszWS);

    if (!*ptr)
      continue;

    /* If this is a segment entry, get its address then continue. */
    if (ptr[4] == ':' && ptr[13] == ' ' && ptr[23] == 'H') {
      if (!IbmParseSegment(ptr, &seg, &offs)) {
        pErr = "malformed segment header";
        break;
      }
      continue;
    }

    /* If this is a module entry, get the module info then continue. */
    if (!strncmp(ptr, szAtOffset, cbAtOffset)) {
      ptr += cbAtOffset;
      if (!IbmStoreModule(ptr, seg, offs)) {
        pErr = "malformed module listing";
        break;
      }
      continue;
    }

    /* If this is something else, return a ptr to it. */
    return ptr;
  }

  fprintf(stderr, "ParseModules failed at line %d:  %s\n", lineNbr, pErr);
  return 0;
}

/*****************************************************************************/
/*  Watcom                                                                   */
/*****************************************************************************/

int     ParseWatcom(void)
{
  if (!SeekToHdr(apszWatMemMap, 0)) {
    fprintf(stderr, "Watcom memory map header not found\n");
    return 0;
  }

  if (!WatStorePublics())
    return 0;

  return 1;
}

/*****************************************************************************/

int     WatStorePublics(void)
{
  int     startCnt = recCnt;
  int     blankOK = 1;
  char *  ptr;
  char *  pAddr;
  char *  pSym;
  REMAP * r;
  REMAP * rMod = 0;

  while (fgets(bufIn, sizeof(bufIn), fi)) {
    lineNbr++;

    r = (REMAP*)pCur;

    pAddr = Trim(bufIn, &pSym);
    if (!pAddr || *pAddr == '=') {
      if (blankOK)
        continue;
      break;
    }
    blankOK = 0;

    if (!strcmp(pAddr, "Module:")) {
      rMod = r;
      WatParseModule(pSym, r);
      continue;
    }

    r->seg = strtoul(pAddr, &ptr, 16);
    if (r->seg > 255 || *ptr != ':') {
      fprintf(stderr, "line %d:  invalid seg address\n", lineNbr);
      continue;
    }

    ptr++;
    ptr[8] = 0;
    r->offs = strtoul(ptr, 0, 16);

    if (!r->seg && !r->offs)
      continue;

    if (rMod && !rMod->seg && !rMod->offs) {
      rMod->seg  = r->seg;
      rMod->offs = r->offs;
    }

    pSym = Trim(pSym, 0);
    if (!pSym) {
      fprintf(stderr, "line %d:  symbol name not found\n", lineNbr);
      continue;
    }

    pSym = Demangle(pSym, workBuf, sizeof(workBuf), &r->type);
    if (!pSym) {
      fprintf(stderr, "line %d:  demangle failed for symbol name\n", lineNbr);
      continue;
    }

    strcpy(r->text, pSym);
    ptr = DecodeFlagName(r->type);
    if (*ptr)
      strcat(r->text, ptr);
    r->lth = strlen(r->text) + 1;
    pCur = &r->text[r->lth];

    r->mod = rMod;
    r->type |= REMAP_OBJ;
    r->next = (REMAP*)pCur;
    recCnt++;
  }

  return (recCnt > startCnt);
}

/*****************************************************************************/

int     WatParseModule(char* pData, REMAP* r)
{
  char *  pSrc;
  char *  ptr;

  pSrc = strchr(pData, '(');
  if (!pSrc)
    pSrc = "[unknown]";
  else {
    pSrc++;
    ptr = strchr(pSrc, ')');
    if (ptr)
      *ptr = 0;

    ptr = strrchr(pSrc, '\\');
    if (!ptr)
      ptr = strrchr(pSrc, '/');
    if (ptr)
      pSrc = ptr + 1;
  }

  r->type |= REMAP_MOD | REMAP_USED;
  r->seg  = 0;
  r->offs = 0;
  r->mod  = 0;

  r->lth  = strlen(pSrc) + 1;
  memcpy(r->text, pSrc, r->lth);
  pCur = &r->text[r->lth];

  r->next = (REMAP*)pCur;
  recCnt++;
  cntMods++;

  return 1;
}

/*****************************************************************************/
/*  Borland                                                                  */
/*****************************************************************************/

int     ParseBorland(void)
{
  if (isBor == 2) {
    if (!BorStoreModules()) {
      fprintf(stderr, "BorStorePublics failed\n");
      return 0;
    }
    cntMods = recCnt;

    if (!SeekToHdr(apszPubByName, 0)) {
      fprintf(stderr, "Unable to find 'Publics by Name' header in Borland mapfile\n");
      return 0;
    }
  }

  if (!BorStorePublics()) {
    fprintf(stderr, "BorStorePublics failed\n");
    return 0;
  }

  if (cntMods && !IbmMarkDuplicateMods(buffer, cntMods)) {
    fprintf(stderr, "IbmMarkDuplicateMods failed\n");
    return 0;
  }

  return 1;
}

/*****************************************************************************/

int     BorStoreModules(void)
{
  int     blankOK = 1;
  int     ctr;
  char *  ptr;
  char *  pEnd;
  REMAP * r;

  while (fgets(bufIn, sizeof(bufIn), fi)) {
    lineNbr++;

    r = (REMAP*)pCur;

    ptr = TrimLine(bufIn);
    if (!ptr) {
      if (!blankOK)
        break;

      blankOK = 0;
      continue;
    }
    blankOK = 0;

    /* get the segment & offset*/
    r->seg = strtoul(ptr, &pEnd, 16);
    if (r->seg > 255 || *pEnd != ':') {
      fprintf(stderr, "line %d:  invalid segment\n", lineNbr);
      continue;
    }
    r->offs = strtoul(&pEnd[1], &ptr, 16);

    /* ignore zero entries */
    if (!r->seg && !r->offs)
      continue;

    ptr = Trim(ptr, &pEnd);
    if (!ptr) {
      fprintf(stderr, "line %d:  malformed/unexpected entry\n", lineNbr);
      continue;
    }

    /* Segment length is zero, so there's no associated code or data. */
    if (!strtoul(ptr, 0, 16))
      continue;

    for (ctr = 0; ctr < 4 && ptr; ctr++)
      ptr = Trim(pEnd, &pEnd);

    if (!ptr || ptr[0] != 'M' || ptr[1] != '=') {
      fprintf(stderr, "line %d:  malformed/unexpected entry - %s\n",
              lineNbr, (ptr ? ptr : "null"));
      continue;
    }
    ptr += 2;

    pEnd = strrchr(ptr, '\\');
    if (pEnd)
      ptr = pEnd + 1;

    r->type |= REMAP_MOD;
    r->mod  = 0;

    r->lth  = strlen(ptr) + 1;
    memcpy(r->text, ptr, r->lth);
    pCur = &r->text[r->lth];

    r->next = (REMAP*)pCur;
    recCnt++;
  }

  return 1;
}

/*****************************************************************************/

int     BorStorePublics(void)
{
  int     blankOK = 1;
  int     ctr;
  char *  ptr;
  char *  pEnd;
  REMAP * r;

  while (fgets(bufIn, sizeof(bufIn), fi)) {
    lineNbr++;

    r = (REMAP*)pCur;

    ptr = TrimLine(bufIn);
    if (!ptr) {
      if (!blankOK)
        break;

      blankOK = 0;
      continue;
    }
    blankOK = 0;

    /* get the segment & offset*/
    r->seg = strtoul(ptr, &pEnd, 16);
    if (r->seg > 255 || *pEnd != ':') {
      fprintf(stderr, "line %d:  invalid segment\n", lineNbr);
      continue;
    }
    r->offs = strtoul(&pEnd[1], &ptr, 16);

    /* ignore zero entries */
    if (!r->seg && !r->offs)
      continue;

    /* skip over the flags(?) column */
    ptr += 6;
    ptr += strspn(ptr, pszWS);

    /* remove 'const' or 'volatile' at the end of a line */
    ctr = strlen(ptr);
    if (ptr[ctr-1] == 't' && ctr > 6 && !strcmp(&ptr[ctr-6], " const")) {
      ctr -= 6;
      ptr[ctr] = 0;
    }
    else
    if (ptr[ctr-1] == 'v' && ctr > 9 && !strcmp(&ptr[ctr-9], " volatile")) {
      ctr -= 9;
      ptr[ctr] = 0;
    }

    /* remove method arguments */
    if (ptr[ctr-1] == ')') {
      int cnt;
      for (pEnd = &ptr[ctr-2], cnt = 1; pEnd > ptr; pEnd--) {
        if (*pEnd == '(') {
          if (!(--cnt)) {
            *pEnd = 0;
            break;
          }
        }
        else
        if (*pEnd == ')')
          cnt++;
      }
    }

    strcpy(r->text, ptr);
    r->lth = strlen(r->text) + 1;
    pCur = &r->text[r->lth];

    r->mod = 0;
    r->type |= REMAP_OBJ;
    r->next = (REMAP*)pCur;
    recCnt++;
  }

  return 1;
}

/*****************************************************************************/
/*  IBM and IBM-style mapfiles                                               */
/*****************************************************************************/

int     ParseIBM(void)
{
  int     rtn = 1;
  char *  pPublics = pCur;

  if (!SeekToHdr(apszPubByName, 0)) {
    fprintf(stderr, "publics by name header not found\n");
    return 0;
  }

  if (!IbmStorePublics()) {
    fprintf(stderr, "IbmStorePublics (by name) failed\n");
    return 0;
  }

  /*  This compensates for a bug in ilink's handling of very large files
   *  which causes it to omit different symbols from Publics by Name and
   *  Publics by Value.  It reads both sections to create a listing of all
   *  available symbols then removes the duplicates.
   */
  if (recCnt - cntMods > 40000) {

    if (!SeekToHdr(apszPubByValue, 0)) {
      fprintf(stderr, "publics by value header not found\n");
      return 0;
    }

    if (!IbmStorePublics()) {
      fprintf(stderr, "IbmStorePublics (by value) failed\n");
      return 0;
    }

    if (!IbmMarkDuplicatePubs(pPublics, recCnt - cntMods)) {
      fprintf(stderr, "IbmMarkDuplicatePubs failed\n");
      return 0;
    }
  }

  /* Mark duplicate entries for each module as such so only one is used. */
  if (cntMods && !IbmMarkDuplicateMods(buffer, cntMods)) {
    fprintf(stderr, "IbmMarkDuplicateMods failed\n");
    return 0;
  }

  return rtn;
}

/*****************************************************************************/
/* Segment info isn't stored but it provides the seg nbr
 * and relative offset for each module.
 */

int     IbmParseSegment(char* pData, ULONG* pSeg, ULONG* pOffs)
{
  char *  pEnd;

  pData = Trim(pData, 0);
  if (!pData)
    return 0;

  *pSeg = strtoul(pData, &pEnd, 16);
  if (!*pSeg || *pSeg > 255 || *pEnd != ':')
    return 0;

  *pOffs = strtoul(&pEnd[1], 0, 16);

  return 1;
}

/*****************************************************************************/

int     IbmStoreModule(char* pData, ULONG ulSeg, ULONG ulOffs)
{
  ULONG   offs;
  char *  ptr;
  char *  pSrc;
  REMAP * r = (REMAP*)pCur;

  /* get the offset within the current segment */
  offs = strtoul(pData, &ptr, 16);
  if (*ptr != ' ') {
    fprintf(stderr, "line %d:  error getting offs - *ptr='%s'\n", lineNbr, ptr);
    return 0;
  }

  /* get the unqualified name of the module */
  pSrc = strchr(ptr, '(');
  if (!pSrc)
    pSrc = "[unknown]";
  else {
    pSrc++;
    ptr = strchr(pSrc, ')');
    if (ptr)
      *ptr = 0;

    ptr = strrchr(pSrc, '\\');
    if (!ptr)
      ptr = strrchr(pSrc, '/');
    if (ptr)
      pSrc = ptr + 1;
  }

  r->type |= REMAP_MOD;
  r->seg  = ulSeg;
  r->offs = ulOffs + offs;
  r->mod = 0;

  r->lth  = strlen(pSrc) + 1;
  memcpy(r->text, pSrc, r->lth);
  pCur = &r->text[r->lth];

  r->next = (REMAP*)pCur;
  recCnt++;

  return 1;
}

/*****************************************************************************/

int     IbmStorePublics(void)
{
  int     blankOK = 1;
  int     ctr;
  char *  ptr;
  char *  pEnd;
  char *  pSymbol;
  REMAP * r;

  while (fgets(bufIn, sizeof(bufIn), fi)) {
    lineNbr++;

    r = (REMAP*)pCur;

    ptr = bufIn + strspn(bufIn, pszWS);

    /* Some IBM linkers have a blank line after the header, some don't.
     * Thereafter, there are no blank lines until the end of the listing.
     * This allows one blank line if no data lines have been encountered.
     */
    if (!*ptr) {
      if (!blankOK)
        break;

      blankOK = 0;
      continue;
    }
    blankOK = 0;

    r->seg = strtoul(ptr, &pEnd, 16);
    if (r->seg > 255 || *pEnd != ':') {
      fprintf(stderr, "line %d:  invalid segment\n", lineNbr);
      continue;
    }

    r->offs = strtoul(&pEnd[1], &ptr, 16);

    /* skip entries whose seg & offset are both zero */
    if (!r->seg && !r->offs)
      continue;

    /* count the nbr of columns */
    for (pEnd = ptr, ctr = 0; pEnd && *pEnd; pEnd = strpbrk(pEnd, pszWS)) {
      pEnd += strspn(pEnd, pszWS);
      if (!*pEnd)
        break;
      ctr++;
    }

    /* skip over the flags column */
    if (ctr == 2) {
      pEnd = Trim(ptr, &ptr);
      if (strlen(pEnd) == 3)
        ctr--;
    }

    /* there should only be one column */
    if (ctr != 1) {
      fprintf(stderr, "line %d:  malformed/unexpected entry\n", lineNbr);
      continue;
    }

    pSymbol = Trim(ptr, 0);
    if (!pSymbol) {
      fprintf(stderr, "line %d:  symbol name not found\n", lineNbr);
      continue;
    }

    /* demangle the symbol - it may return either a demangled string
     * or the string that was passed in.
     */
    pSymbol = Demangle(pSymbol, workBuf, sizeof(workBuf), &r->type);
    if (!pSymbol) {
      fprintf(stderr, "line %d:  demangle failed for symbol name\n", lineNbr);
      continue;
    }

    /* append symbol type info, if any, to the demangled symbol */
    strcpy(r->text, pSymbol);
    ptr = DecodeFlagName(r->type);
    if (*ptr)
      strcat(r->text, ptr);
    r->lth = strlen(r->text) + 1;
    pCur = &r->text[r->lth];

    r->mod = 0;
    r->type |= REMAP_OBJ;
    r->next = (REMAP*)pCur;
    recCnt++;
  }

  return 1;
}

/*****************************************************************************/
/* IBM and Borland mapfiles may have multiple entries for the same module.
 * Sort them by name & address, then link the 2nd and subsequent entries
 * to the first entry.  See SortByAddress() for how this is used.
 */

int     IbmMarkDuplicateMods(char* pMods, int modCnt)
{
  int     ctr;
  REMAP** pr;
  REMAP** pArr;
  REMAP * pRec;
  REMAP * pMod;

  if (!modCnt) {
    fprintf(stderr, "no modules to sort\n");
    return 0;
  }

  pr = (REMAP**)malloc((modCnt + 1) * sizeof(REMAP*));
  if (!pr) {
    fprintf(stderr, "malloc failed for IbmMarkDuplicateMods - bytes= %d\n",
            modCnt * sizeof(REMAP*));
    return 0;
  }

  pRec = (REMAP*)pMods;
  pArr = pr;
  ctr = 0;
  /* note:  pRec->next == null identifies the first invalid entry */
  while (pRec->next && (pRec->type & REMAP_MOD)) {
    *pArr++ = pRec;
    pRec = pRec->next;
    ctr++;
  }
  *pArr = 0;

  if (ctr != modCnt) {
    fprintf(stderr, "invalid record count:  cnt= %d  pubCnt= %d\n",
            ctr, modCnt);
    return 0;
  }

  qsort(pr, modCnt, sizeof(REMAP*), IbmDuplicateModSorter);

  pArr = pr;
  pMod = *pArr;
  pArr++;
  pRec = *pArr;

  while (pRec) {
    if (strcmp(pMod->text, pRec->text))
      pMod = pRec;
    else
      pRec->mod = pMod;

    pArr++;
    pRec = *pArr;
  }

  free(pr);

  return 1;
}

/*****************************************************************************/
/* qsort callback for marking duplicate modules */

int     IbmDuplicateModSorter(const void* key, const void* element)
{
  int     res;
  ULONG   kType;
  ULONG   eType;

  kType = (*(REMAP**)key)->type;
  eType = (*(REMAP**)element)->type;

  res = (kType & REMAP_MASK) - (eType & REMAP_MASK);
  if (res) {
    fprintf(stderr, "ModSort:  key- %04lx:%08lx type=%lx  elem- %04lx:%08lx type=%lx\n",
            (*(REMAP**)key)->seg, (*(REMAP**)key)->offs, kType,
            (*(REMAP**)element)->seg, (*(REMAP**)element)->offs, eType);
    return res;
  }

  res = strcmp((*(REMAP**)key)->text, (*(REMAP**)element)->text);
  if (res)
    return res;

  res = (*(REMAP**)key)->seg - (*(REMAP**)element)->seg;
  if (res)
    return res;

  return (*(REMAP**)key)->offs - (*(REMAP**)element)->offs;
}

/*****************************************************************************/
/* If we read both Pubs by Name and Pubs by Value, there are many
 * duplicate entries.  If two entries are found to be identical
 * while sorting, mark one as a duplicate.
 */

int     IbmMarkDuplicatePubs(char* pPublics, int pubCnt)
{
  int     ctr;
  REMAP** pr;
  REMAP** pArr;
  REMAP * pRec;

  if (!pubCnt) {
    fprintf(stderr, "no public symbols to sort\n");
    return 0;
  }

  pr = (REMAP**)malloc((pubCnt + 1) * sizeof(REMAP*));
  if (!pr) {
    fprintf(stderr, "malloc failed for IbmMarkDuplicatePubs - bytes= %d\n",
            pubCnt * sizeof(REMAP*));
    return 0;
  }

  pRec = (REMAP*)pPublics;
  pArr = pr;
  ctr = 0;
  /* note:  pRec->next == null identifies the first invalid entry */
  while (pRec->next) {
    *pArr++ = pRec;
    pRec = pRec->next;
    ctr++;
  }
  *pArr = 0;

  if (ctr != pubCnt) {
    fprintf(stderr, "invalid record count:  cnt= %d  pubCnt= %d\n",
            ctr, pubCnt);
    return 0;
  }

  qsort(pr, pubCnt, sizeof(REMAP*), IbmDuplicatePubSorter);
  free(pr);

  return 1;
}

/*****************************************************************************/
/* qsort callback for marking duplicate publics */

int     IbmDuplicatePubSorter(const void* key, const void* element)
{
  int     res;
  ULONG   kType;
  ULONG   eType;

  res = (*(REMAP**)key)->seg - (*(REMAP**)element)->seg;
  if (res)
    return res;

  res = (*(REMAP**)key)->offs - (*(REMAP**)element)->offs;
  if (res)
    return res;

  kType = (*(REMAP**)key)->type;
  eType = (*(REMAP**)element)->type;

  res = (kType & REMAP_MASK) - (eType & REMAP_MASK);
  if (res) {
    fprintf(stderr, "PubSort:  key- %04lx:%08lx type=%lx  elem- %04lx:%08lx type=%lx\n",
            (*(REMAP**)key)->seg, (*(REMAP**)key)->offs, kType,
            (*(REMAP**)element)->seg, (*(REMAP**)element)->offs, eType);
    return res;
  }

  res = strcmp((*(REMAP**)key)->text, (*(REMAP**)element)->text);
  if (res)
    return res;

  if (!(kType & REMAP_DUP) && !(eType & REMAP_DUP))
    (*(REMAP**)key)->type |= REMAP_DUP;

  return 0;
}

/*****************************************************************************/
/*  Utility Functions                                                        */
/*****************************************************************************/
/* Read lines until either the "seek" or "stop" string is found */

char**  SeekToHdr(char** pSeek, char** pStop)
{
  char ** pRtn = 0;

  while (fgets(bufIn, sizeof(bufIn), fi)) {
    lineNbr++;

    if (MatchArray(pSeek, bufIn)) {
      pRtn = pSeek;
      break;
    }

    if (pStop) {
      if (MatchArray(pStop, bufIn)) {
        pRtn = pStop;
        break;
      }
    }
  }

  return pRtn;
}

/*****************************************************************************/
/* Match individual words in a string - this reduces the chance that a
 * string will be missed due to formatting variations.
 */

int     MatchArray(char** pArray, char* pText)
{
  while (**pArray) {
    pText = strstr(pText, *pArray);
    if (!pText)
      return 0;

    pText += strlen(*pArray);
    pArray++;
  }

  return 1;
}

/*****************************************************************************/
/* This trims words in-place.  It returns the first non-whitespace character
   in pTrim & puts a null after the last non-ws char.  If ppNext is supplied,
   it points to the character after the inserted null.
*/

char*   Trim(char* pTrim, char** ppNext)
{
  char *  ptr;

  if (ppNext)
    *ppNext = 0;

  if (!pTrim)
    return 0;

  pTrim += strspn(pTrim, pszWS);
  if (!*pTrim)
    return 0;

  ptr = strpbrk(pTrim, pszWS);
  if (!ptr)
    return pTrim;

  *ptr++ = 0;
  if (ppNext && *ptr)
    *ppNext = ptr;

  return pTrim;
}

/*****************************************************************************/
/* This trims a line in-place. */

char *  TrimLine(char* pTrim)
{
  char *  ptr;

  if (!pTrim)
    return 0;

  pTrim += strspn(pTrim, pszWS);
  if (!*pTrim)
    return 0;

  ptr = strchr(pTrim, 0) - 1;
  while (ptr > pTrim && strchr(pszWS, *ptr))
    ptr--;
  *(++ptr) = 0;

  return pTrim;
}

/*****************************************************************************/
/* Convert the flags generated when the function was demangled into
 * strings that will be appended to the method name.
 */

char *  DecodeFlagName(ULONG flags)
{
  if (flags & REMAP_VTABLE)
    return "::{vtable}";

  if (flags & REMAP_THUNK)
    return "::{thunk}";

  if (flags & REMAP_TYPEINFO)
    return "::{typeinfo}";

  if (flags & REMAP_TYPENAME)
    return "::{typename}";

  if (flags & REMAP_GUARD)
    return "::{guard_variable}";

  if (flags & REMAP_VTT)
    return "::{vtt}";

  if (flags & REMAP_VIRTTHUNK)
    return "::{virtual thunk}";

  if (flags & REMAP_CONSTRUCT)
    return "::{construction vtable}";

  return "";
}

/*****************************************************************************/
/* This demangles symbols for GCC using the builtin demangler. */

char *  Demangle(char* pIn, char* pOut, ULONG cbOut, ULONG* pFlags)
{
  int     ndx;
  char *  ptr;

  if (opts & OPT_NO_DEMANGLE)
    return pIn;

  if (opts & OPT_VAC)
    return DemangleVAC(pIn, pOut, pFlags);

  /* OPT_GCC */

  /* Determine if this is mangled & whether to remove the leading character. */
  if (!memcmp(pIn, "__Z", 3) || !memcmp(pIn, "@_Z", 3))
    ndx = 1;
  else
  if (!memcmp(pIn, "_Z", 2))
    ndx = 0;
  else
    return pIn;

  *pOut = 0;

  /* Mozilla (at least) appends a unique identifier to many symbols
   * that the demangler can't handle.  If the leading characters of
   * the identifier ("$w$") are found, remove the identifier.
   */
  ptr = strstr(pIn, pszTrouble);
  if (ptr)
    *ptr = 0;

  /* Call the gcc 3.x demangler. */
  *pOut = 0;
  if (!cplus_demangle_v3_callback(&pIn[ndx], 0, &DemangleCallback, &pOut))
    return pIn;

  /* Trim any trailing whitespace. */
  ptr = strchr(pOut, 0) - 1;
  while (ptr > pOut && strchr(pszWS, *ptr))
    ptr--;
  *(++ptr) = 0;

  /* Convert metadata generated by the demangler into flags,
   * then remove the metadata.
   */
  if (strchr(pOut, ' ')) {
    if (!strncmp(pOut, szVtable, cbVtable)) {
      *pFlags |= REMAP_VTABLE;
      strcpy(pOut, pOut + cbVtable);
    }
    else
    if (!strncmp(pOut, szThunk, cbThunk)) {
      *pFlags |= REMAP_THUNK;
      strcpy(pOut, pOut + cbThunk);

      /* remove the argument list that gets included for thunks */
      if ((ptr = strchr(pOut, '(')) != 0)
        *ptr = 0;
    }
    else
    if (!strncmp(pOut, szTypeInfo, cbTypeInfo)) {
      *pFlags |= REMAP_TYPEINFO;
      strcpy(pOut, pOut + cbTypeInfo);
    }
    else
    if (!strncmp(pOut, szTypeName, cbTypeName)) {
      *pFlags |= REMAP_TYPENAME;
      strcpy(pOut, pOut + cbTypeName);
    }
    else
    if (!strncmp(pOut, szGuard, cbGuard)) {
      *pFlags |= REMAP_GUARD;
      strcpy(pOut, pOut + cbGuard);
    }
    else
    if (!strncmp(pOut, szVTT, cbVTT)) {
      *pFlags |= REMAP_VTT;
      strcpy(pOut, pOut + cbVTT);
    }
    else
    if (!strncmp(pOut, szConstruct, cbConstruct)) {
      *pFlags |= REMAP_CONSTRUCT;
      strcpy(pOut, pOut + cbConstruct);
    }
    else
    if (!strncmp(pOut, szVirtThunk, cbVirtThunk)) {
      *pFlags |= REMAP_VIRTTHUNK;
      strcpy(pOut, pOut + cbVirtThunk);
    }
  }

  /* remove the arguments for templates */
  ptr = strchr(pOut, '<');
  while (ptr) {
    int     cnt;
    char *  pRt;

    for (pRt = ptr+1, cnt = 1; *pRt; pRt++) {
      if (*pRt == '<')
        cnt++;
      else
      if (*pRt == '>') {
        cnt--;
        if (!cnt) {
          strcpy(ptr, pRt+1);
          break;
        }
      }
    }

    /* If there are more '<' than '>', it's probably because this
     * line contains "operator<" or "operator<<".  If so, advance
     * past them and retry;  otherwise, leave it as-is.
     */
    if (cnt) {
      if (*ptr != '<')
        break;
      pRt = strstr(pOut, "operator<");
      if (!pRt || pRt+8 != ptr)
        break;
      ptr++;
      if (cnt > 1 && *ptr == '<')
        ptr++;
    }
    ptr = strchr(ptr, '<');
  }

  return pOut;
}

/*****************************************************************************/
/* Called by the GCC demangler one or more times to copy the pieces
 * of a demangled method to an output buffer.  pv is actually a ptr
 * to the pOut & cbOut arguments to Demangle() - cheesy, huh?
 */

void    DemangleCallback(const char* pSrc, size_t cbSrc, void* pv)
{
  struct ptr_lth {
    char *  pbuf;
    ULONG   cbbuf;
  } * p;
  size_t  cnt;

  p = (struct ptr_lth*)pv;
  cnt = strlen(p->pbuf);
  if (cnt + cbSrc + 1 < p->cbbuf)
    strcpy(p->pbuf + cnt, pSrc);

  return;
}

/*****************************************************************************/
/* This demangles symbols for VAC via demangl.dll.  The functions in
 * demangl.dll all use Optlink which gcc 4.x can't handle, so they're
 * invoked via wrapper functions in remap_vac.c.  Each wrapper has
 * '_vac' appended to the function's original name.
 */

char*   DemangleVAC(char* pIn, char* pOut, ULONG* pFlags)
{
  char *    ptr;
  char *    pEnd;
  Name *    nm;
  NameKind  nk;

  *pOut = 0;

  nm = demangle_vac(pIn, &ptr, (RegularNames | ClassNames | SpecialNames));
  if (!nm)
    return pIn;

  nk = kind_vac(nm);

  if (nk == MemberFunction || nk == Function) {

    if (nk == MemberFunction) {
      ptr = qualifier_vac(nm);
      if (ptr) {
        strcpy(pOut, ptr);
        strcat(pOut, "::");
      }
    }
    ptr = functionName_vac(nm);
    if (ptr)
      strcat(pOut, ptr);
  }
  else {
    ptr = text_vac(nm);
    if (!ptr)
      return pIn;
    strcpy(pOut, ptr);
  }

  if (nk == Special && (ptr = strstr(pOut, szVtableVAC)) != 0) {
    *pFlags |= REMAP_VTABLE;
    *ptr = 0;

    /* vtable entries for subclasses are formatted '{subclass}class';
     * this reformats it as 'class::subclass'
     */
    if (*pOut == '{' && (pEnd = strchr(pOut, '}')) != 0) {
      *pEnd++ = 0;
      *ptr++ = ':';
      *ptr++ = ':';
      strcpy(ptr, &pOut[1]);
      strcpy(pOut, pEnd);
    }
  }

  erase_vac(nm);

  return pOut;
}

/*****************************************************************************/
/*  Output Processing                                                        */
/*****************************************************************************/

int     WriteOutput(void)
{
  int     rtn = 0;
  REMAP** pArr = 0;

do {
  /* If no modules were found, set the OPT_NOMOD flag.  
   * Note that the flag & 'cntMods' serve somewhat different purposes.
   */
  if (!cntMods)
    opts |= OPT_NOMOD;

  /* set the size of each XQSYM entry */
  cbXQSYM = (opts & OPT_NOMOD) ? XQS_SYMSIZE_NOMOD : XQS_SYMSIZE_MOD;

  /* Sort module and symbol entries by address. */
  pArr = SortByAddress();
  if (!pArr)
    break;

  /* If requested, print a listing of modules & symbols. */
  if (opts & OPT_LIST)
    if (!PrintListing(pArr))
      break;

  /* Open the .xqs file. */
  fo = fopen(fOut, "wb");
  if (!fo) {
    fprintf(stderr, "unable to open output file '%s'\n", fOut);
    break;
  }

  /* Write the file header and module names.*/
  if (!WriteHeader(pArr))
    break;

  /* Write each segment's header, symbols, and strings. */
  rtn = WriteSegs(pArr);

} while (0);

  /* Clean up. */
  if (pArr)
    free(pArr);
  if (fo)
    fclose(fo);

  return rtn;
}

/*****************************************************************************/
/* Sort all entries by address. */

REMAP** SortByAddress(void)
{
  int     ctr;
  REMAP** pr;
  REMAP** pArr;
  REMAP * pRec;

  pr = (REMAP**)malloc((recCnt + 1) * sizeof(REMAP*));
  if (!pr) {
    fprintf(stderr, "malloc failed for SortByAddress - bytes= %d\n",
            recCnt * sizeof(REMAP*));
    return 0;
  }

  pRec = (REMAP*)buffer;
  pArr = pr;
  ctr  = 0;
  /* note:  the last valid pRec has a valid pRec->next; 
   * pRec->next == null identifies the first invalid entry
   */
  while (pRec->next) {
    if (!(pRec->type & REMAP_DUP)) {
      *pArr++ = pRec;
      ctr++;
    }
    pRec = pRec->next;
  }
  *pArr = 0;

  qsort(pr, ctr, sizeof(REMAP*), AddressSorter);

  /* Associate symbols with the preceding module entry (if any).
   * Also, set a flag on each module entry that is referenced by a symbol.
   * If there were multiple entries for a module, IbmMarkDuplicateMods()
   * set the 2nd and subsequent entries' pRec->mod to point at the first
   * entry.  Only that first one is referenced and marked as used;  the
   * rest remain unused & unmarked.
   */
  if (!isWat && cntMods) {
    REMAP * pMod = 0;

    pArr = pr;
    pRec = *pArr;
    while (pRec) {
      if (pRec->type & REMAP_MOD) {
        if (pRec->mod)
          pMod = pRec->mod;
        else
          pMod = pRec;
      }
      else {
        pRec->mod = pMod;
        if (pMod)
          pMod->type |= REMAP_USED;
      }

      pArr++;
      pRec = *pArr;
    }
  }

  return pr;
}

/*****************************************************************************/
/* qsort callback for sorting by address */

int     AddressSorter(const void *key, const void *element)
{
  int     res;
  ULONG   kType;
  ULONG   eType;

  res = (*(REMAP**)key)->seg - (*(REMAP**)element)->seg;
  if (res)
    return res;

  res = (*(REMAP**)key)->offs - (*(REMAP**)element)->offs;
  if (res)
    return res;

  kType = (*(REMAP**)key)->type;
  eType = (*(REMAP**)element)->type;

  res = (kType & REMAP_TYPE) - (eType & REMAP_TYPE);
  if (res)
    return res;

  return stricmp((*(REMAP**)key)->text, (*(REMAP**)element)->text);
}

/*****************************************************************************/
/* Print a listing of modules & symbols by address. */

int     PrintListing(REMAP** pr)
{
  FILE *  fl = 0;
  REMAP * r;
  REMAP * rMod = 0;
  int     modCnt = 0;
  int     symCnt = 0;

  fl = fopen(fList, "w");
  if (!fl) {
    fprintf(stderr, "unable to open list file '%s'\n", fList);
    return 0;
  }

  /* Print a report header & column header. */
  fprintf(fl, pszReportHdr, (opts & OPT_NOMOD) ? "" : " and source files", fOut);
  fputs(pszColumnHdr, fl);

  r = *pr;
  while (r) {

    switch (r->type & REMAP_TYPE) {
      case REMAP_MOD:
        /* Only count modules that are referenced. */
        if (r->type & REMAP_USED)
          modCnt++;
        break;

      case REMAP_OBJ:
        /* If the current entry points at a different module
         * than the previous one, print the new module name.
         */
        if (r->mod != rMod) {
          rMod = r->mod;
          fprintf(fl, "\n %s\n", (rMod && rMod->text) ? rMod->text : "[unknown]");
        }

        /* Print the entry & inc the symbol count. */
        fprintf(fl, "   %04lX:%08lX  %s\n", r->seg, r->offs, r->text);
        symCnt++;
        break;

      default:
        /* Ooops... what's this? */
        fprintf(fl, " ERROR:  unknown type= %lu\n", (r->type & REMAP_TYPE));
        break;
    }

    pr++;
    r = *pr;
  }

  /* Show the total number of modules & symbols. */
  fprintf(fl, "\n Modules= %d  Symbols= %d\n\n", modCnt, symCnt);
  fclose(fl);

  return 1;
}

/*****************************************************************************/

int     WriteHeader(REMAP** pArr)
{
  int     rtn = 1;
  ULONG   padMods = 0;
  XQFILE  xqFile;
  REMAP** pr;

  memset(&xqFile, 0, sizeof(XQFILE));
  xqFile.magic = XQFILE_MAGIC;
  xqFile.cbStruct = sizeof(XQFILE);
  xqFile.version = 1;
  xqFile.firstSeg = sizeof(XQFILE);

  /* If mod info will be included, put the mod names immediately after
   * this header and relocate the first segment header after the names.
   */
  if (!(opts & OPT_NOMOD)) {
    xqFile.offsMod = sizeof(XQFILE);

    for (pr = pArr; *pr; pr++) {
      if (((*pr)->type & (REMAP_MOD | REMAP_USED)) == (REMAP_MOD | REMAP_USED))
        xqFile.firstSeg += (*pr)->lth;
    }
    padMods = (0x10 - (xqFile.firstSeg & 0x0F)) & 0x0F;
    xqFile.firstSeg += padMods;
  }

  /* Write the file header. */
  if (fwrite(&xqFile, 1, sizeof(XQFILE), fo) != sizeof(XQFILE)) {
    fprintf(stderr, "error writing XQFILE to file - aborting\n");
    return 0;
  }

  /* If appropriate, write the module name strings. */
  if (!(opts & OPT_NOMOD))
    rtn = WriteMods(pArr, xqFile.offsMod, xqFile.firstSeg, padMods);

  return rtn;
}

/*****************************************************************************/

int     WriteMods(REMAP** pArr, ULONG offs, ULONG offsEnd, ULONG padMods)
{
  ULONG   cur;
  REMAP*  r;
  REMAP** pr;

  /* Write the strings associated with module entries referenced by symbols. */
  for (pr = pArr; *pr; pr++) {
    r = *pr;
    if ((r->type & (REMAP_MOD | REMAP_USED)) != (REMAP_MOD | REMAP_USED))
      continue;

    r->offs = offs;
    if (fwrite(r->text, 1, r->lth, fo) != r->lth) {
      fprintf(stderr, "error writing module name to file - aborting\n");
      return 0;
    }
    offs += r->lth;
  }

  /* If there should be padding after the strings, write it. */
  if (padMods) {
    if (fwrite(aPad, 1, padMods, fo) != padMods) {
      fprintf(stderr, "error writing module name padding to file - aborting\n");
      return 0;
    }
  }

  /* Confirm we're where we should be (offsEnd already includes any padding). */
  cur = ftell(fo);
  if (cur != offsEnd) {
    fprintf(stderr, "module array not expected length - aborting - expected= %ld  actual= %ld\n",
            offsEnd, cur);
    return 0;
  }

  return 1;
}

/*****************************************************************************/

int     WriteSegs(REMAP** pArr)
{
  ULONG   cbStrings;
  ULONG   offsStrings;
  ULONG   padStrings;
  ULONG   padSym;
  REMAP** pStart;
  REMAP** pStop;
  XQSEG   xqSeg;

  memset(&xqSeg, 0, sizeof(XQSEG));
  xqSeg.magic    = XQSEG_MAGIC;
  xqSeg.cbStruct = sizeof(XQSEG);
  xqSeg.cbXQSYM  = cbXQSYM;

  pStart = pArr;
  while (*pStart) {

    xqSeg.seg = (*pStart)->seg;

    cbStrings = 0;
    xqSeg.cntSym = 0;
    pStop = pStart;

    /* Count the number of symbols in this segment and calculate the
     * aggregate length of the strings associated with those symbols.
     */
    while (*pStop && (*pStop)->seg == xqSeg.seg) {
      if ((*pStop)->type & REMAP_OBJ) {
        cbStrings += (*pStop)->lth;
        xqSeg.cntSym++;
      }
      pStop++;
    }

    /* If this seg has no symbols, skip it. */
    if (!xqSeg.cntSym) {
      pStart = pStop;
      continue;
    }

    /* XQSYM entries start at the current pos + the sizeof XQSEG */
    xqSeg.offsSym = ftell(fo);
    if (xqSeg.offsSym < 0) {
      fprintf(stderr, "ftell() failed  - aborting\n");
      return 0;
    }
    xqSeg.offsSym += sizeof(XQSEG);

    /* Calc any padding needed after the XQSYM array,
     * then calc the position of the symbol's strings.
     */
    padSym = (0x10 - ((xqSeg.cntSym * cbXQSYM) & 0x0F)) & 0x0F;
    offsStrings = xqSeg.offsSym + (xqSeg.cntSym * cbXQSYM) + padSym;

    /* If this isn't the last segment, calc padding for the strings,
     * then calc the offset of the next XQSEG header.
     */
    if (*pStop) {
      padStrings = (0x10 - (cbStrings & 0x0F)) & 0x0F;
      xqSeg.offsNext = offsStrings + cbStrings + padStrings;
    }
    else {
      padStrings = 0;
      xqSeg.offsNext = 0;
    }

    /* Write the current seg's header, then its symbols & strings. */
    if (fwrite(&xqSeg, 1, sizeof(XQSEG), fo) != sizeof(XQSEG)) {
      fprintf(stderr, "error writing XQSEG to file - aborting\n");
      return 0;
    }
    if (!WriteSyms(pStart, pStop, offsStrings, padSym, padStrings))
      return 0;

    pStart = pStop;
  }

  return 1;
}

/*****************************************************************************/

int     WriteSyms(REMAP** pStart, REMAP** pStop, ULONG offsStrings,
                  ULONG padSym, ULONG padStrings)
{
  ULONG     pos;
  ULONG     cur;
  REMAP**   pr;
  REMAP *   r;
  XQSYM     xqs;

  memset(&xqs, 0, sizeof(xqs));
  pos = offsStrings;

  /* Write XQSYM entries, */
  for (pr = pStart; pr < pStop; pr++) {
    r = *pr;

    /* Ignore module entries. */
    if (!(r->type & REMAP_OBJ))
      continue;

    xqs.address  = r->offs;
    xqs.offsName = pos;
    xqs.cbName   = r->lth;
    pos += r->lth;

    /* Store module references if appropriate.  Note:  OPT_NOMOD may
     * be set by user request or because there was no module info.
     */
    if (!(opts & OPT_NOMOD)) {
      if (r->mod) {
        xqs.cbMod   = r->mod->lth;
        xqs.offsMod = r->mod->offs;
      }
      else {
        xqs.cbMod   = 0;
        xqs.offsMod = 0;
      }
    }

    if (fwrite(&xqs, 1, cbXQSYM, fo) != cbXQSYM) {
      fprintf(stderr, "error writing XQSYM to file - aborting\n");
      return 0;
    }
  }

  /* If there should be padding after the XQSYMs, write it. */
  if (padSym) {
    if (fwrite(aPad, 1, padSym, fo) != padSym) {
      fprintf(stderr, "error writing XQSYM padding to file - aborting\n");
      return 0;
    }
  }

  /* Confirm we're where we should be. */
  if (ftell(fo) != offsStrings) {
    fprintf(stderr, "XQSYM array not expected length  - aborting\n");
    return 0;
  }

  /* Write the symbols' strings. */
  for (pr = pStart; pr < pStop; pr++) {
    if (!((*pr)->type & REMAP_OBJ))
      continue;

    if (fwrite((*pr)->text, 1, (*pr)->lth, fo) != (*pr)->lth) {
      fprintf(stderr, "error writing symbol name to file - aborting\n");
      return 0;
    }
  }

  /* If there should be padding after the strings, write it. */
  if (padStrings) {
    if (fwrite(aPad, 1, padStrings, fo) != padStrings) {
      fprintf(stderr, "error writing symbol name padding to file - aborting\n");
      return 0;
    }
  }

  /* Confirm we're where we should be (pos doesn't include any padding). */
  cur = ftell(fo);
  if (cur != pos + padStrings) {
    fprintf(stderr, "name array not expected length - aborting - expected= %ld  actual= %ld\n",
            pos, cur);
    return 0;
  }

  return 1;
}

/*****************************************************************************/
/*  XQS to XQL                                                               */
/*****************************************************************************/
/* Create *.xql from *.xqs */

int     DumpXQS(void)
{
  int     hIn;
  int     rtn = 1;
  int     modCnt = 0;
  int     symCnt = 0;
  int     fMod;
  int     ctr;
  ULONG   offsSeg;
  ULONG   lastMod;
  ULONG   maxMod = 0;
  FILE *  fl = 0;
  XQFILE* xqFile;
  XQSEG * xqSeg;
  XQSYM * xqs;

  /* Open the xqs file. */
  hIn = open(fIn, O_RDONLY | O_BINARY, 0);
  if (hIn < 0) {
    fprintf(stderr, "unable to open input file '%s'\n", fIn);
    return 0;
  }

  /* Read the entire file into the buffer then close it. */
  rtn = read(hIn, buffer, cbBuffer);
  close(hIn);

  /* Ensure we got the entire file. */
  if (rtn != cbFile) {
    fprintf(stderr, "unable to read entire input file '%s'\n", fIn);
    return 0;
  }

  xqFile = (XQFILE*)buffer;

  /* Confirm this is a valid XQS file. */
  if (xqFile->magic != XQFILE_MAGIC) {
    fprintf(stderr, "input is not a valid XQS file - '%s'\n", fIn);
    return 0;
  }

  /* Open the listing file. */
  fl = fopen(fOut, "w");
  if (!fl) {
    fprintf(stderr, "unable to open list file '%s'\n", fOut);
    return 0;
  }

  /* Print a report header & column header. */
  fprintf(fl, pszReportHdr, (xqFile->offsMod) ? " and source files" : "", fIn);
  fputs(pszColumnHdr, fl);

  /* For each segment... */
  for (offsSeg = xqFile->firstSeg; offsSeg; offsSeg = xqSeg->offsNext) {

    /* Point at the XQSEG, then confirm it's valid. */
    xqSeg = (XQSEG*)(buffer + offsSeg);
    if (offsSeg > cbFile - sizeof(XQSEG) ||
        xqSeg->magic != XQSEG_MAGIC) {
      fprintf(stderr, "invalid segment offset %lx - aborting\n", offsSeg);
      rtn = 0;
      break;
    }

    fMod = xqSeg->cbXQSYM >= XQS_SYMSIZE_MOD;
    lastMod = 0;
    symCnt += xqSeg->cntSym;

    /* For each symbol entry in the segment... */
    for (ctr = xqSeg->cntSym, xqs = (XQSYM*)(buffer + xqSeg->offsSym);
         ctr;
         ctr--, xqs = (XQSYM*)((char*)xqs + xqSeg->cbXQSYM)) {

      /* If this seg has module info & the current entry's mod is different
       * than the previous one's, print the name provided it's valid.
       */
      if (fMod && lastMod != xqs->offsMod) {
        lastMod = xqs->offsMod;
        if (lastMod > maxMod)
          maxMod = lastMod;
        if (xqs->offsMod && xqs->cbMod)
          fprintf(fl, "\n %s\n", buffer + xqs->offsMod);
        else
          fprintf(fl, "\n [unknown]\n");
      }

      /* Print the symbol info. */
      fprintf(fl, "   %04hX:%08lX  %s\n", xqSeg->seg, xqs->address,
              (xqs->offsName && xqs->cbName) ? buffer + xqs->offsName : "[error]");
    }
  }
    
  /* Show the total number of modules & symbols. */
  if (rtn) {
    /* Count the number of module name strings. */
    if (xqFile->offsMod && maxMod) {
      char* ptr = buffer + xqFile->offsMod;
      while (*ptr && ptr <= &buffer[maxMod]) {
        modCnt++;
        ptr = strchr(ptr, 0) + 1;
      }
    }

    fprintf(fl, "\n Modules= %d  Symbols= %d\n\n", modCnt, symCnt);
  }

  fclose(fl);

  return rtn;
}

/*****************************************************************************/

