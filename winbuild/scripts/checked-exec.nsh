; SPDX-License-Identifier: GPL-3.0-or-later
!ifndef DESKPORT_CHECKED_EXEC_NSH
!define DESKPORT_CHECKED_EXEC_NSH
!include "LogicLib.nsh"
!macro CheckedExecWait Command
  ClearErrors
  ExecWait '${Command}' $0
  ${If} ${Errors}
    ; Distinguish a launch failure from a child that reported success. All
    ; callers gate nonzero results before modifying or deleting more state.
    StrCpy $0 -1
  ${EndIf}
!macroend
!endif
