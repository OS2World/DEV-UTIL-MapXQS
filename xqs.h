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
 * The Original Code is MapXQS header.
 *
 * The Initial Developer of the Original Code is
 * Richard L. Walsh
 * Portions created by the Initial Developer are Copyright (C) 2011
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *
 *
 * ***** END LICENSE BLOCK ***** */
/*****************************************************************************/
/*  mapxqs.h - v1.04a                                                        */
/*****************************************************************************/
/*
 * Magic numbers appear at the beginning of each header to uniquely
 * identify them.  Currently, only two are defined but others may be
 * added in future versions.
 */

#define XQFILE_MAGIC  ((ULONG)('x' | ('q' << 8) | ('s' << 16) | ('f' << 24)))
#define XQSEG_MAGIC   ((ULONG)('x' | ('q' << 8) | ('s' << 16) | ('s' << 24)))

/*
 * Data compression is not implemented currently but may be in some future
 * version.  If/When it is, it will operate at the segment level so that
 * some segments may be compressed while others aren't.  If compression is
 * used anywhere in the file, XQFLAG_ZIP will be set in XQFILE.flags.  If a
 * specific segment is compressed, XQFLAG_ZIP will be set in its XQSEG.flags.
 * XQFLAG_ZIP_MOD will be set in XQFILE.flags if module names are compressed.
 */

#define XQFLAG_ZIP        1
#define XQFLAG_ZIP_MOD    2

/*
 * XQFILE starts at byte 0 in the file and is the only header whose location
 * is guaranteed to be at a specific offset.  It will always be at least 32
 * bytes but may be larger by some multiple of 16 bytes.
 *
 * Note:  all offsets in all structures are absolute, i.e. they are relative
 *        to the beginning of the file.
 */

typedef struct _XQFILE {
  ULONG   magic;
  USHORT  cbStruct;
  USHORT  flags;
  USHORT  version;
  USHORT  unused;
  ULONG   firstSeg;
  ULONG   offsMod;
  ULONG   reserved[3];
} XQFILE;

/*
 * XQSEG currently appears immediately before the array of XQSYM structs
 * for a specific segment.  However, its position relative to them is not
 * guaranteed.  Some future version may position them elsewhere (e.g. in
 * a group at the beginning of the file).  It will always be at least 32
 * bytes but may be larger by some multiple of 16 bytes.
 */

typedef struct _XQSEG {
  ULONG   magic;
  USHORT  cbStruct;
  USHORT  flags;
  USHORT  cbXQSYM;
  USHORT  seg;
  ULONG   cntSym;
  ULONG   offsSym;
  ULONG   offsNext;
  ULONG   reserved[2];
} XQSEG;

/*
 * XQSYM is primarily a programming convenience since its size is not
 * guaranteed.  It will always be at least 10 bytes but may be 16 bytes
 * if module info is present.  It could be larger if other symbol info is
 * added in future versions.  XQSEG.cbXQSYM identifies its size for the
 * current segment's array, but that size may vary from segment to segment.
 *
 * Note:  The string lengths in cbName and cbMod include the trailing null.
 */

typedef struct _XQSYM {
  ULONG   address;
  ULONG   offsName;
  USHORT  cbName;
  USHORT  cbMod;
  ULONG   offsMod;
} XQSYM;

/*
 * These macros are provided to aid in determining whether to fetch module
 * names (and potentially, other as-yet undefined info).  For example:
 *   hasModNames = (XQSEG.cbXQSYM >= XQS_SYMSIZE_MOD);
 * XQS_SYMSIZE_NOMOD and XQS_SYMSIZE_MOD will not change in future versions.
 * XQS_SYMSIZE_ALL will change if other symbol info is added.
 */

#define XQS_SYMSIZE_NOMOD   10
#define XQS_SYMSIZE_MOD     16
#define XQS_SYMSIZE_ALL     sizeof(XQSYM)

/*****************************************************************************/

