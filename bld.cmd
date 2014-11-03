@echo off
rem ---------------------------------------------------------------------------
rem
rem   This is a simple batch file to build mapxqs using both VACPP & GCC
rem   (see mapxqs.c for an explanation of why both are required).
rem   It calls separate .cmd files to setup the environment for each
rem   compiler.  Modify or 'rem' the appropriate lines as needed.
rem   Note:  SET/ENDLOCAL doesn't handle BEGINLIBPATH, so we have to.
rem
rem ---------------------------------------------------------------------------
rem VACPP: No default libraries, Decorate external names, __cdecl linkage,
rem        Output mapxqs_vac.o
rem
SET BEGINSAVE=%BEGINLIBPATH%
SETLOCAL
call G:\Ibmcxxo\bin\setenv.cmd
@echo on
icc /Gn+ /Gy+ /Mc /O+ /Q /Sm /Ss /W3 /Fomapxqs_vac.o /C mapxqs_vac.c
@echo off
IF ERRORLEVEL 1 goto end
ENDLOCAL
rem
rem ---------------------------------------------------------------------------
rem GCC: OMF format, Optimized, No-strict-aliasing (to suppress a warning msg),
rem      Link in libiberty (the gcc3 demangler), Output mapxqs.exe
rem
SET BEGINLIBPATH=%BEGINSAVE%
SETLOCAL
call G:\MOZTOOLS\setmozenv.cmd > nul
@echo on
gcc -c -Wall -Zomf -O2 -fno-strict-aliasing mapxqs.c
@IF ERRORLEVEL 1 goto end
g++ -o mapxqs.exe -s -Zomf -Zmap -Zlinker /EXEPACK:2 mapxqs.o mapxqs_vac.o -llibiberty mapxqs.def
@IF ERRORLEVEL 1 goto end
mapxqs mapxqs
@rem
@rem --------------------------------------------------------------------------
:end
@echo off
ENDLOCAL
SET BEGINLIBPATH=%BEGINSAVE%
SET BEGINSAVE=
