; SPDX-License-Identifier: GPL-3.0-or-later
; Native regression fixture: no installer, SCM, driver or user settings access.
Unicode true
RequestExecutionLevel user
SilentInstall silent
Name "DeskPort checked process launch fixture"
OutFile "${OUTFILE}"
!include "FileFunc.nsh"
!include "${SOURCE}/winbuild/scripts/checked-exec.nsh"
Section
  ${GetParameters} $1
  ${GetOptions} $1 "/OUTPUT=" $2
  StrCmp $2 "" failed
  IfFileExists "$2" failed
  ; Seed a stale success result, just as the vulnerable uninstall path did.
  StrCpy $0 0
  !insertmacro CheckedExecWait '$\"$EXEDIR\missing-fixture-child.exe$\"'
  StrCmp $0 -1 +2
  Goto failed
  SetErrors
  !insertmacro CheckedExecWait '$\"$SYSDIR\cmd.exe$\" /d /c exit 0'
  StrCmp $0 0 +2
  Goto failed
  !insertmacro CheckedExecWait '$\"$SYSDIR\cmd.exe$\" /d /c exit 17'
  StrCmp $0 17 +2
  Goto failed
  FileOpen $3 "$2" w
  IfErrors failed
  FileWrite $3 "PASS missing child, stale exit code, stale error flag, success and nonzero exit$\r$\n"
  FileClose $3
  IfErrors failed
  SetErrorLevel 0
  Quit
failed:
  SetErrorLevel 1
SectionEnd
