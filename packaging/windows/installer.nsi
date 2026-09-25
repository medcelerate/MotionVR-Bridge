; MotionVR Bridge Windows installer.
;
;   makensis /DSTAGE=<cmake --install prefix> /DVERSION=1.2.3 /DOUTFILE=setup.exe installer.nsi
;
; Add /DDRIVER_ONLY to build the installer that contains just the SteamVR driver.
; Both register the driver with SteamVR via register-driver.ps1.

Unicode true
!include "MUI2.nsh"
!include "LogicLib.nsh"

!ifndef STAGE
  !error "Pass /DSTAGE=<cmake --install prefix>"
!endif
!ifndef VERSION
  !define VERSION "0.0.0"
!endif
!ifndef OUTFILE
  !define OUTFILE "MotionVRBridge-setup.exe"
!endif

!ifdef DRIVER_ONLY
  !define PRODUCT "MotionVR Bridge SteamVR Driver"
  !define UNINSTALL_KEY "MotionVRBridgeDriver"
!else
  !define PRODUCT "MotionVR Bridge"
  !define UNINSTALL_KEY "MotionVRBridge"
!endif
!define UNINSTALL_REG "Software\Microsoft\Windows\CurrentVersion\Uninstall\${UNINSTALL_KEY}"

Name "${PRODUCT}"
OutFile "${OUTFILE}"
InstallDir "$PROGRAMFILES64\${PRODUCT}"
InstallDirRegKey HKLM "${UNINSTALL_REG}" "InstallLocation"
RequestExecutionLevel admin
VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "${PRODUCT}"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "CompanyName" "Virtualworx"
VIAddVersionKey "LegalCopyright" "Virtualworx"
VIAddVersionKey "FileDescription" "${PRODUCT} installer"

!insertmacro MUI_PAGE_LICENSE "${STAGE}\LICENSE"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!ifndef DRIVER_ONLY
  !define MUI_FINISHPAGE_RUN "$INSTDIR\MotionVRBridge.exe"
!endif
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Section "Install"
  SetShellVarContext all
  SetOutPath "$INSTDIR"
!ifndef DRIVER_ONLY
  File "${STAGE}\MotionVRBridge.exe"
  File /nonfatal "${STAGE}\*.dll"
  CreateShortCut "$SMPROGRAMS\MotionVR Bridge.lnk" "$INSTDIR\MotionVRBridge.exe"
!endif
  File "${STAGE}\register-driver.ps1"
  File "${STAGE}\LICENSE"
  File "${STAGE}\LICENSE-EXCEPTION"
  File "${STAGE}\THIRD_PARTY_NOTICES.md"
  SetOutPath "$INSTDIR\driver"
  File /r "${STAGE}\driver\motionvrbridge"

  WriteUninstaller "$INSTDIR\uninstall.exe"
  WriteRegStr HKLM "${UNINSTALL_REG}" "DisplayName" "${PRODUCT}"
  WriteRegStr HKLM "${UNINSTALL_REG}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "${UNINSTALL_REG}" "Publisher" "Virtualworx"
  WriteRegStr HKLM "${UNINSTALL_REG}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "${UNINSTALL_REG}" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegDWORD HKLM "${UNINSTALL_REG}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINSTALL_REG}" "NoRepair" 1

  DetailPrint "Registering the SteamVR driver..."
  nsExec::ExecToLog 'powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$INSTDIR\register-driver.ps1"'
  Pop $0
  ${If} $0 != 0
    MessageBox MB_ICONEXCLAMATION|MB_OK "The SteamVR driver could not be registered (SteamVR may not have been run yet).$\r$\n$\r$\nStart SteamVR once, then run register-driver.ps1 in:$\r$\n$INSTDIR"
  ${EndIf}
SectionEnd

Section "Uninstall"
  SetShellVarContext all
  nsExec::ExecToLog 'powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$INSTDIR\register-driver.ps1" -Remove'
  Pop $0

  Delete "$SMPROGRAMS\MotionVR Bridge.lnk"
  Delete "$INSTDIR\MotionVRBridge.exe"
  Delete "$INSTDIR\*.dll"
  Delete "$INSTDIR\register-driver.ps1"
  Delete "$INSTDIR\LICENSE"
  Delete "$INSTDIR\LICENSE-EXCEPTION"
  Delete "$INSTDIR\THIRD_PARTY_NOTICES.md"
  RMDir /r "$INSTDIR\driver"
  Delete "$INSTDIR\uninstall.exe"
  RMDir "$INSTDIR"
  DeleteRegKey HKLM "${UNINSTALL_REG}"
SectionEnd
