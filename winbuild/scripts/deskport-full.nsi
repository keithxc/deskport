; DeskPort Windows x64 offline installer.
; Built with NSIS on Linux; every payload file is embedded in the installer.

; Export the exact generated uninstaller for offline PE auditing.
!ifdef AUDIT_UNINSTALLER
  !uninstfinalize 'cp "%1" "${AUDIT_UNINSTALLER}"' = 0
!endif

Unicode true
SetCompressor /SOLID lzma

!ifndef VERSION
  !define VERSION "0.0.0"
!endif
!ifndef PAYLOAD
  !error "PAYLOAD must be defined"
!endif
!ifndef OUTFILE
  !error "OUTFILE must be defined"
!endif

!define APPNAME "DeskPort"
!define PUBLISHER "DeskPort contributors"
!define REGKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}"

Name "${APPNAME} ${VERSION} (x64)"
OutFile "${OUTFILE}"
InstallDir "$PROGRAMFILES64\${APPNAME}"
InstallDirRegKey HKLM "Software\${APPNAME}" "InstallDir"
RequestExecutionLevel admin
ShowInstDetails show
ShowUninstDetails show
BrandingText "${APPNAME} ${VERSION} x64"

!include "MUI2.nsh"
!include "x64.nsh"
!include "LogicLib.nsh"

!define MUI_ABORTWARNING
!define MUI_ICON "${PAYLOAD}\deskport.ico"
!define MUI_UNICON "${PAYLOAD}\deskport.ico"

!insertmacro MUI_PAGE_LICENSE "${PAYLOAD}\LICENSE.txt"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN "$INSTDIR\DeskPort.exe"
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "SimpChinese"

Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP "This package requires 64-bit Windows (x64)." /SD IDOK
    SetErrorLevel 1
    Abort
  ${EndIf}
  SetShellVarContext all
  SetRegView 64
FunctionEnd

Function un.onInit
  SetShellVarContext all
  SetRegView 64
FunctionEnd

; Defender or another security product can quarantine a bundled PE while the
; installer is running. Keep the uninstaller available, fail the install, and
; direct the user to Windows Security instead of weakening protection.
Function VerifyPayload
  IfFileExists "$INSTDIR\DeskPort.exe" +1 payloadMissing
  IfFileExists "$INSTDIR\deskport-maintenance.exe" +1 payloadMissing
  IfFileExists "$INSTDIR\deskport-driver-setup.exe" +1 payloadMissing
  IfFileExists "$INSTDIR\host\deskport-host.exe" +1 payloadMissing
  IfFileExists "$INSTDIR\host\deskport-display.exe" +1 payloadMissing
  IfFileExists "$INSTDIR\host\deskport-display-recovery.exe" +1 payloadMissing
  IfFileExists "$INSTDIR\driver\MttVDD.inf" +1 payloadMissing
  IfFileExists "$INSTDIR\driver\MttVDD.dll" +1 payloadMissing
  Push 0
  Return
payloadMissing:
  MessageBox MB_ICONSTOP|MB_OK "DeskPort installation is incomplete: a required component is missing. Windows Security or another security product may have quarantined it. Review Windows Security protection history, then repair or uninstall this installation. Do not disable protection." /SD IDOK
  Push 1
FunctionEnd

Section "DeskPort" SecMain
  SectionIn RO
  InitPluginsDir
  SetOutPath "$PLUGINSDIR"
  File "${PAYLOAD}\deskport-maintenance.exe"
  ExecWait '$\"$PLUGINSDIR\deskport-maintenance.exe$\" stop $\"$INSTDIR$\"' $0
  ${If} $0 != 0
    MessageBox MB_ICONSTOP "DeskPort could not close this installation safely. Close its windows and tray process, then retry." /SD IDOK
    SetErrorLevel 1
    Abort
  ${EndIf}
  SetOutPath "$INSTDIR"
  File /r "${PAYLOAD}\*.*"

  ; Write this before the first integrity gate so a quarantined install keeps
  ; an executable path for cleanup and repair.
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  Call VerifyPayload
  Pop $0
  ${If} $0 != 0
    SetErrorLevel 1
    Abort
  ${EndIf}

  WriteRegStr HKLM "Software\${APPNAME}" "InstallDir" "$INSTDIR"
  WriteRegStr HKLM "Software\${APPNAME}" "Version" "${VERSION}"

  ExecWait '$\"$INSTDIR\deskport-maintenance.exe$\" register-recovery $\"$INSTDIR$\"' $0
  ${If} $0 != 0
    MessageBox MB_ICONSTOP "DeskPort could not register its owned display recovery task (error $0). An unrelated task was not replaced." /SD IDOK
    SetErrorLevel 1
    Abort
  ${EndIf}
  WriteRegStr HKLM "${REGKEY}" "DisplayName" "${APPNAME} ${VERSION} (x64)"
  WriteRegStr HKLM "${REGKEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "${REGKEY}" "Publisher" "${PUBLISHER}"
  WriteRegStr HKLM "${REGKEY}" "DisplayIcon" "$INSTDIR\DeskPort.exe"
  WriteRegStr HKLM "${REGKEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "${REGKEY}" "UninstallString" "$\"$INSTDIR\Uninstall.exe$\""
  WriteRegStr HKLM "${REGKEY}" "QuietUninstallString" "$\"$INSTDIR\Uninstall.exe$\" /S"
  WriteRegDWORD HKLM "${REGKEY}" "NoModify" 1
  WriteRegDWORD HKLM "${REGKEY}" "NoRepair" 1

  ; Windows performs normal catalog verification and publisher consent. No
  ; certificate is imported and no signature policy is weakened.
  ExecWait '$\"$INSTDIR\deskport-driver-setup.exe$\" stage $\"$INSTDIR\driver\MttVDD.inf$\"' $0
  ${If} $0 != 0
  ${AndIf} $0 != 3010
    MessageBox MB_ICONSTOP "Windows rejected or canceled staging the signed display driver (error $0). The installed uninstaller can remove the incomplete installation." /SD IDOK
    SetErrorLevel 1
    Abort
  ${EndIf}
  ExecWait '$\"$INSTDIR\deskport-driver-setup.exe$\" install $\"$INSTDIR\driver\MttVDD.inf$\"' $0
  ${If} $0 == 3010
    SetRebootFlag true
  ${ElseIf} $0 != 0
    MessageBox MB_ICONSTOP "Windows could not install the signed DeskPort virtual display (error $0). No signing policy was changed. The installation is incomplete; the installed uninstaller can remove its files." /SD IDOK
    SetErrorLevel 1
    Abort
  ${EndIf}

  nsExec::ExecToLog 'netsh advfirewall firewall delete rule name="DeskPort Full Host TCP" program="$INSTDIR\host\deskport-host.exe"'
  Pop $0
  nsExec::ExecToLog 'netsh advfirewall firewall delete rule name="DeskPort Full Host UDP" program="$INSTDIR\host\deskport-host.exe"'
  Pop $0
  nsExec::ExecToLog 'netsh advfirewall firewall delete rule name="DeskPort Full Binding" program="$INSTDIR\DeskPort.exe"'
  Pop $0
  nsExec::ExecToLog 'netsh advfirewall firewall add rule name="DeskPort Full Host TCP" dir=in action=allow program="$INSTDIR\host\deskport-host.exe" protocol=TCP localport=48984,48989,49010,49084,49089,49110,49184,49189,49210,49284,49289,49310,49384,49389,49410,49484,49489,49510,49584,49589,49610,49684,49689,49710,49784,49789,49810,49884,49889,49910,49984,49989,50010,50084,50089,50110,50184,50189,50210,50284,50289,50310,50384,50389,50410,50484,50489,50510,50584,50589,50610,50684,50689,50710,50784,50789,50810,50884,50889,50910 profile=any remoteip=LocalSubnet'
  Pop $0
  ${If} $0 != 0
    MessageBox MB_ICONSTOP "Windows could not create the DeskPort firewall rule. Installation is incomplete (error $0)." /SD IDOK
    SetErrorLevel 1
    Abort
  ${EndIf}
  nsExec::ExecToLog 'netsh advfirewall firewall add rule name="DeskPort Full Host UDP" dir=in action=allow program="$INSTDIR\host\deskport-host.exe" protocol=UDP localport=48998,48999,49000,49002,49098,49099,49100,49102,49198,49199,49200,49202,49298,49299,49300,49302,49398,49399,49400,49402,49498,49499,49500,49502,49598,49599,49600,49602,49698,49699,49700,49702,49798,49799,49800,49802,49898,49899,49900,49902,49998,49999,50000,50002,50098,50099,50100,50102,50198,50199,50200,50202,50298,50299,50300,50302,50398,50399,50400,50402,50498,50499,50500,50502,50598,50599,50600,50602,50698,50699,50700,50702,50798,50799,50800,50802,50898,50899,50900,50902 profile=any remoteip=LocalSubnet'
  Pop $0
  ${If} $0 != 0
    MessageBox MB_ICONSTOP "Windows could not create the DeskPort firewall rule. Installation is incomplete (error $0)." /SD IDOK
    SetErrorLevel 1
    Abort
  ${EndIf}
  nsExec::ExecToLog 'netsh advfirewall firewall add rule name="DeskPort Full Binding" dir=in action=allow program="$INSTDIR\DeskPort.exe" protocol=TCP profile=any remoteip=LocalSubnet'
  Pop $0
  ${If} $0 != 0
    MessageBox MB_ICONSTOP "Windows could not create the DeskPort firewall rule. Installation is incomplete (error $0)." /SD IDOK
    SetErrorLevel 1
    Abort
  ${EndIf}

  ; Security software can quarantine a file after extraction while setup is
  ; still running, so verify once more before allowing the finish page.
  Call VerifyPayload
  Pop $0
  ${If} $0 != 0
    SetErrorLevel 1
    Abort
  ${EndIf}



  CreateDirectory "$SMPROGRAMS\${APPNAME}"
  CreateShortCut "$SMPROGRAMS\${APPNAME}\${APPNAME}.lnk" "$INSTDIR\DeskPort.exe"
  CreateShortCut "$SMPROGRAMS\${APPNAME}\Uninstall ${APPNAME}.lnk" "$INSTDIR\Uninstall.exe"
SectionEnd

Section "Desktop shortcut" SecDesktop
  CreateShortCut "$DESKTOP\${APPNAME}.lnk" "$INSTDIR\DeskPort.exe"
SectionEnd

Section "Uninstall"
  ExecWait '$\"$INSTDIR\deskport-maintenance.exe$\" stop $\"$INSTDIR$\"' $0
  ${If} $0 != 0
    MessageBox MB_ICONSTOP "Close this DeskPort installation before uninstalling." /SD IDOK
    SetErrorLevel 1
    Abort
  ${EndIf}
  ExecWait '$\"$INSTDIR\deskport-maintenance.exe$\" remove-recovery $\"$INSTDIR$\"' $0
  ${If} $0 != 0
    MessageBox MB_ICONSTOP "DeskPort could not safely remove its recovery task (error $0)." /SD IDOK
    SetErrorLevel 1
    Abort
  ${EndIf}
  ExecWait '$\"$INSTDIR\deskport-driver-setup.exe$\" remove' $0
  ${If} $0 != 0
  ${AndIf} $0 != 3010
    MessageBox MB_ICONSTOP "The DeskPort virtual display could not be removed. Its files and ownership record have been retained." /SD IDOK
    SetErrorLevel 1
    Abort
  ${EndIf}
  nsExec::ExecToLog 'netsh advfirewall firewall delete rule name="DeskPort Full Host TCP" program="$INSTDIR\host\deskport-host.exe"'
  Pop $0
  nsExec::ExecToLog 'netsh advfirewall firewall delete rule name="DeskPort Full Host UDP" program="$INSTDIR\host\deskport-host.exe"'
  Pop $0
  nsExec::ExecToLog 'netsh advfirewall firewall delete rule name="DeskPort Full Binding" program="$INSTDIR\DeskPort.exe"'
  Pop $0
  ExecWait '$\"$INSTDIR\deskport-driver-setup.exe$\" restore-config' $0
  ${If} $0 != 0
    MessageBox MB_ICONSTOP "The virtual display configuration changed outside DeskPort. Its files were retained (error $0)." /SD IDOK
    SetErrorLevel 1
    Abort
  ${EndIf}
  ExecWait '$\"$INSTDIR\deskport-driver-setup.exe$\" remove-package' $0
  ${If} $0 != 0
    MessageBox MB_ICONSTOP "Windows retained the DeskPort driver package (error $0). The ownership record and uninstaller remain available for retry." /SD IDOK
    SetErrorLevel 1
    Abort
  ${EndIf}
  DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "io.github.keithxc.DeskPort"
  Delete "$INSTDIR\deskport-maintenance.exe"
  Delete "$INSTDIR\deskport-driver-setup.exe"
  RMDir /r "$INSTDIR\host"
  RMDir /r "$INSTDIR\driver"
  Delete "$DESKTOP\${APPNAME}.lnk"
  Delete "$SMPROGRAMS\${APPNAME}\${APPNAME}.lnk"
  Delete "$SMPROGRAMS\${APPNAME}\Uninstall ${APPNAME}.lnk"
  RMDir "$SMPROGRAMS\${APPNAME}"

  Delete "$INSTDIR\Uninstall.exe"
  Delete "$INSTDIR\DeskPort.exe"
  Delete "$INSTDIR\gamecontrollerdb.txt"
  Delete "$INSTDIR\deskport.ico"
  Delete "$INSTDIR\LICENSE.txt"
  Delete "$INSTDIR\BUILD-INFO.txt"
  Delete "$INSTDIR\THIRD-PARTY-NOTICES.txt"
  RMDir /r "$INSTDIR\licenses"
  RMDir "$INSTDIR"

  DeleteRegKey HKLM "${REGKEY}"
  DeleteRegKey HKLM "Software\${APPNAME}"
SectionEnd
