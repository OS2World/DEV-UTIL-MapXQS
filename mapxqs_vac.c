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
/*  mapxqs_vac.c - v1.04a                                                    */
/*****************************************************************************/

#include "mapxqs_demangle.h"

/*****************************************************************************/

PFNDEMANGLE pfnDemangle;
PFNKIND     pfnKind;
PFNTEXT     pfnText;
PFNTEXT     pfnQualifier;
PFNTEXT     pfnFunctionName;
PFNERASE    pfnErase;

/*****************************************************************************/
/*  gcc 4.x doesn't handle _Optlink correctly, so these functions have
 *  to be wrapped and the wrapper has to be compiled using VAC or some
 *  other compiler that can handle it.
 */

Name* __cdecl   demangle_vac(char* name, char** rest, unsigned long options)
{
  return pfnDemangle(name, rest, options);
}

NameKind __cdecl kind_vac(Name* name)
{
  return pfnKind(name);
}

char* __cdecl   text_vac(Name* name)
{
  return pfnText(name);
}

char* __cdecl   qualifier_vac(Name* name)
{
  return pfnQualifier(name);
}

char* __cdecl   functionName_vac(Name* name)
{
  return pfnFunctionName(name);
}

void  __cdecl   erase_vac(Name* name)
{
  pfnErase(name);
}

/*****************************************************************************/

