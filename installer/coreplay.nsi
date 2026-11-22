; Coreplay installer (NSIS 3, Unicode). Built by build.ps1 -Installer:
;   makensis /DVERSION=<x.y.z> /DSRC=<dist\Coreplay> /DOUTFILE=<setup.exe> installer\coreplay.nsi
; Per-user install (no admin prompt): %LOCALAPPDATA%\Programs\Coreplay, HKCU registry only.
;
; File types: every listed type gets Coreplay under "Open with" and two right-click entries
; ("Play with Coreplay", "Add to Coreplay playlist"). The groups ticked on the Components
; page also make Coreplay the default. Since Windows 10 an installer cannot override a
; default the user already chose (UserChoice), so the Finish page offers Windows' own
; "Default apps" page for Coreplay. Uninstall restores each type's previous default.

Unicode true
SetCompressor /SOLID lzma
RequestExecutionLevel user

!ifndef VERSION
  !define VERSION "0.0.0"
!endif
!define APP "Coreplay"
!define EXE "coreplay.exe"
!define REG_APP "Software\Coreplay"
!define REG_UNINST "Software\Microsoft\Windows\CurrentVersion\Uninstall\Coreplay"

Name "${APP}"
OutFile "${OUTFILE}"
InstallDir "$LOCALAPPDATA\Programs\Coreplay"
InstallDirRegKey HKCU "${REG_APP}" "InstallDir"
BrandingText "${APP} ${VERSION}"

!include "MUI2.nsh"
!include "FileFunc.nsh"

!define MUI_ICON "..\res\coreplay.ico"
!define MUI_UNICON "..\res\coreplay.ico"
!define MUI_ABORTWARNING
!define MUI_COMPONENTSPAGE_SMALLDESC

; The first page: what Coreplay is, and the invitation to Core Studio (as in Help > About).
!define MUI_WELCOMEPAGE_TITLE "Welcome to Coreplay ${VERSION}"
!define MUI_WELCOMEPAGE_TEXT "A fast, free player for movies and image sequences.$\r$\n$\r$\nCoreplay is made by imCore Corp. as a companion to Core Studio, our professional AI film studio: from the first line of the script to the final frame, in one workspace.$\r$\n$\r$\nIf you enjoy working with Coreplay, you will feel at home in Core Studio. Explore it and go Pro at imcore.ai/core-studio.html$\r$\n$\r$\nClick Next to continue."
; The last page keeps a clickable link to Core Studio.
!define MUI_FINISHPAGE_LINK "Explore Core Studio and go Pro"
!define MUI_FINISHPAGE_LINK_LOCATION "https://imcore.ai/core-studio.html"

!define MUI_FINISHPAGE_RUN "$INSTDIR\${EXE}"
!define MUI_FINISHPAGE_RUN_TEXT "Start Coreplay"
!define MUI_FINISHPAGE_SHOWREADME ""
!define MUI_FINISHPAGE_SHOWREADME_TEXT "Choose Coreplay as default in Windows Settings"
!define MUI_FINISHPAGE_SHOWREADME_FUNCTION OpenDefaultApps

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "${APP}"
VIAddVersionKey "FileDescription" "${APP} installer"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "CompanyName" "imcore"
VIAddVersionKey "LegalCopyright" "imcore"

; ------------------------------------------------------------------ file-type macros

; Keys the installer CREATES are recorded, so uninstall can remove them again - only
; when still completely empty (no default, no values, no subkeys): another app may have
; written into them since.
!macro RecordIfMissing KEY
  ClearErrors
  EnumRegKey $9 HKCU "${KEY}" 0
  IfErrors 0 +2
    WriteRegStr HKCU "${REG_APP}\CreatedKeys" "${KEY}" "1"
!macroend

!macro DeleteIfCreated KEY
  ReadRegStr $9 HKCU "${REG_APP}\CreatedKeys" "${KEY}"
  StrCmp $9 "1" 0 +10
  ReadRegStr $8 HKCU "${KEY}" ""
  StrCmp $8 "" 0 +8
  ClearErrors
  EnumRegValue $8 HKCU "${KEY}" 0
  StrCmp $8 "" 0 +5
  EnumRegKey $8 HKCU "${KEY}" 0
  StrCmp $8 "" 0 +3
  DeleteRegKey HKCU "${KEY}"
  Nop
!macroend

; Coreplay is offered for this type: "Open with", Default-apps candidate, right-click entries.
!macro RegisterType EXT PROGID
  !insertmacro RecordIfMissing "Software\Classes\.${EXT}"
  !insertmacro RecordIfMissing "Software\Classes\.${EXT}\OpenWithProgids"
  !insertmacro RecordIfMissing "Software\Classes\SystemFileAssociations\.${EXT}"
  !insertmacro RecordIfMissing "Software\Classes\SystemFileAssociations\.${EXT}\shell"
  WriteRegStr HKCU "Software\Classes\.${EXT}\OpenWithProgids" "${PROGID}" ""
  WriteRegStr HKCU "${REG_APP}\Capabilities\FileAssociations" ".${EXT}" "${PROGID}"
  WriteRegStr HKCU "Software\Classes\Applications\${EXE}\SupportedTypes" ".${EXT}" ""
  WriteRegStr HKCU "Software\Classes\SystemFileAssociations\.${EXT}\shell\Coreplay.Play" "" "Play with Coreplay"
  WriteRegStr HKCU "Software\Classes\SystemFileAssociations\.${EXT}\shell\Coreplay.Play" "Icon" '"$INSTDIR\${EXE}",0'
  WriteRegStr HKCU "Software\Classes\SystemFileAssociations\.${EXT}\shell\Coreplay.Play\command" "" '"$INSTDIR\${EXE}" "%1"'
  WriteRegStr HKCU "Software\Classes\SystemFileAssociations\.${EXT}\shell\Coreplay.Add" "" "Add to Coreplay playlist"
  WriteRegStr HKCU "Software\Classes\SystemFileAssociations\.${EXT}\shell\Coreplay.Add" "Icon" '"$INSTDIR\${EXE}",0'
  WriteRegStr HKCU "Software\Classes\SystemFileAssociations\.${EXT}\shell\Coreplay.Add\command" "" '"$INSTDIR\${EXE}" --add "%1"'
!macroend

; Coreplay becomes the default (where the user has not chosen one; see the header).
; The previous default is kept so uninstall can restore it.
!macro MakeDefault EXT PROGID
  ReadRegStr $0 HKCU "Software\Classes\.${EXT}" ""
  StrCmp $0 "${PROGID}" +3 0
    WriteRegStr HKCU "${REG_APP}\PreviousDefaults" ".${EXT}" "$0"
    WriteRegStr HKCU "Software\Classes\.${EXT}" "" "${PROGID}"
!macroend

; Removes ONLY Coreplay's own values, and keys Coreplay created that are still empty.
; Never DeleteRegKey /ifempty on a shared key: it ignores values, and other apps keep
; theirs there (a first version of this installer wiped PotPlayer's entries that way).
!macro UnregisterType EXT PROGID
  DeleteRegValue HKCU "Software\Classes\.${EXT}\OpenWithProgids" "${PROGID}"
  DeleteRegKey HKCU "Software\Classes\SystemFileAssociations\.${EXT}\shell\Coreplay.Play"
  DeleteRegKey HKCU "Software\Classes\SystemFileAssociations\.${EXT}\shell\Coreplay.Add"
  ; The default, only where it is still ours: put the previous one back (or none).
  ReadRegStr $0 HKCU "Software\Classes\.${EXT}" ""
  StrCmp $0 "${PROGID}" 0 +6
    ReadRegStr $1 HKCU "${REG_APP}\PreviousDefaults" ".${EXT}"
    StrCmp $1 "" +3 0
    WriteRegStr HKCU "Software\Classes\.${EXT}" "" "$1"
    Goto +2
    DeleteRegValue HKCU "Software\Classes\.${EXT}" ""
  ; Keys this installer created, if still empty (deepest first)
  !insertmacro DeleteIfCreated "Software\Classes\SystemFileAssociations\.${EXT}\shell"
  !insertmacro DeleteIfCreated "Software\Classes\SystemFileAssociations\.${EXT}"
  !insertmacro DeleteIfCreated "Software\Classes\.${EXT}\OpenWithProgids"
  !insertmacro DeleteIfCreated "Software\Classes\.${EXT}"
!macroend

!macro WriteProgId PROGID DESC
  WriteRegStr HKCU "Software\Classes\${PROGID}" "" "${DESC}"
  WriteRegStr HKCU "Software\Classes\${PROGID}" "FriendlyTypeName" "${DESC}"
  WriteRegStr HKCU "Software\Classes\${PROGID}\DefaultIcon" "" '"$INSTDIR\${EXE}",0'
  WriteRegStr HKCU "Software\Classes\${PROGID}\shell" "" "open"
  WriteRegStr HKCU "Software\Classes\${PROGID}\shell\open" "" "Play with Coreplay"
  WriteRegStr HKCU "Software\Classes\${PROGID}\shell\open\command" "" '"$INSTDIR\${EXE}" "%1"'
  WriteRegStr HKCU "Software\Classes\${PROGID}\shell\enqueue" "" "Add to Coreplay playlist"
  WriteRegStr HKCU "Software\Classes\${PROGID}\shell\enqueue\command" "" '"$INSTDIR\${EXE}" --add "%1"'
!macroend

!include "filetypes.nsh"

; ------------------------------------------------------------------ install

Function .onInit
  Call CheckNotRunning
FunctionEnd

; A running Coreplay keeps its files locked: ask the user to close it. A silent install
; (an in-app update: Coreplay quits as it starts the installer) waits up to 30 s instead.
Function CheckNotRunning
  StrCpy $1 0
  retry:
  IfFileExists "$INSTDIR\${EXE}" 0 done
  ClearErrors
  FileOpen $0 "$INSTDIR\${EXE}" a
  IfErrors 0 closeit
  IfSilent 0 ask
    IntOp $1 $1 + 1
    IntCmp $1 60 +3 0 +3
    Sleep 500
    Goto retry
    Abort
  ask:
  MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "Coreplay is running. Close it, then click Retry." /SD IDCANCEL IDRETRY retry
  Abort
  closeit:
  FileClose $0
  done:
FunctionEnd

; The uninstaller removes the install folder, so it must be Coreplay's own folder:
; a location that does not end in \Coreplay (C:\Tools, say) is refused.
Function .onVerifyInstDir
  StrCpy $0 $INSTDIR "" -9
  StrCmp $0 "\Coreplay" +2 0
    Abort
FunctionEnd

; /RUN (in-app updates): start Coreplay again when the install is done.
Function .onInstSuccess
  ${GetParameters} $R0
  ClearErrors
  ${GetOptions} $R0 "/RUN" $R1
  IfErrors +2 0
    Exec '"$INSTDIR\${EXE}"'
FunctionEnd

Function OpenDefaultApps
  ExecShell "open" "ms-settings:defaultapps?registeredAppUser=Coreplay"
FunctionEnd

Section "Coreplay (required)" SecApp
  SectionIn RO
  SetOutPath "$INSTDIR"
  File /r "${SRC}\*.*"
  WriteUninstaller "$INSTDIR\Uninstall Coreplay.exe"

  CreateShortCut "$SMPROGRAMS\Coreplay.lnk" "$INSTDIR\${EXE}" "" "$INSTDIR\${EXE}" 0

  ; The application, its types and its Default-apps entry
  WriteRegStr HKCU "${REG_APP}" "InstallDir" "$INSTDIR"
  WriteRegStr HKCU "Software\Classes\Applications\${EXE}" "FriendlyAppName" "Coreplay"
  WriteRegStr HKCU "Software\Classes\Applications\${EXE}\DefaultIcon" "" '"$INSTDIR\${EXE}",0'
  WriteRegStr HKCU "Software\Classes\Applications\${EXE}\shell\open\command" "" '"$INSTDIR\${EXE}" "%1"'
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\App Paths\${EXE}" "" "$INSTDIR\${EXE}"
  WriteRegStr HKCU "${REG_APP}\Capabilities" "ApplicationName" "Coreplay"
  WriteRegStr HKCU "${REG_APP}\Capabilities" "ApplicationDescription" "Fast player for movies and image sequences"
  WriteRegStr HKCU "${REG_APP}\Capabilities" "ApplicationIcon" '"$INSTDIR\${EXE}",0'
  WriteRegStr HKCU "Software\RegisteredApplications" "Coreplay" "${REG_APP}\Capabilities"
  !insertmacro WriteProgId "${PROGID_Video}" "Video (Coreplay)"
  !insertmacro WriteProgId "${PROGID_Sequence}" "Image sequence (Coreplay)"
  !insertmacro WriteProgId "${PROGID_Playlist}" "Playlist (Coreplay)"
  !insertmacro WriteProgId "${PROGID_Audio}" "Audio (Coreplay)"
  !insertmacro WriteProgId "${PROGID_Image}" "Image (Coreplay)"
  !insertmacro RegisterVideo
  !insertmacro RegisterSequence
  !insertmacro RegisterPlaylist
  !insertmacro RegisterAudio
  !insertmacro RegisterImage

  ; "Apps & features"
  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  WriteRegStr HKCU "${REG_UNINST}" "DisplayName" "Coreplay"
  WriteRegStr HKCU "${REG_UNINST}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "${REG_UNINST}" "Publisher" "imcore"
  WriteRegStr HKCU "${REG_UNINST}" "DisplayIcon" '"$INSTDIR\${EXE}",0'
  WriteRegStr HKCU "${REG_UNINST}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${REG_UNINST}" "UninstallString" '"$INSTDIR\Uninstall Coreplay.exe"'
  WriteRegStr HKCU "${REG_UNINST}" "QuietUninstallString" '"$INSTDIR\Uninstall Coreplay.exe" /S'
  WriteRegDWORD HKCU "${REG_UNINST}" "EstimatedSize" $0
  WriteRegDWORD HKCU "${REG_UNINST}" "NoModify" 1
  WriteRegDWORD HKCU "${REG_UNINST}" "NoRepair" 1
SectionEnd

Section /o "Desktop shortcut" SecDesktop
  CreateShortCut "$DESKTOP\Coreplay.lnk" "$INSTDIR\${EXE}" "" "$INSTDIR\${EXE}" 0
SectionEnd

SectionGroup /e "Make Coreplay the default for" SecDefaults
  Section "${LABEL_Video}" SecVideo
    !insertmacro DefaultVideo
  SectionEnd
  Section "${LABEL_Sequence}" SecSequence
    !insertmacro DefaultSequence
  SectionEnd
  Section "${LABEL_Playlist}" SecPlaylist
    !insertmacro DefaultPlaylist
  SectionEnd
  Section /o "${LABEL_Audio}" SecAudio
    !insertmacro DefaultAudio
  SectionEnd
  Section /o "${LABEL_Image}" SecImage
    !insertmacro DefaultImage
  SectionEnd
SectionGroupEnd

Section "-Refresh"
  ; Tell Explorer the associations changed (SHCNE_ASSOCCHANGED).
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecApp} "Coreplay and its file-type registration: it appears under $\"Open with$\" for every supported type."
  !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} "A Coreplay icon on the desktop."
  !insertmacro MUI_DESCRIPTION_TEXT ${SecDefaults} "Double-clicking these files opens Coreplay. Windows keeps a default you chose before; change those in Settings > Default apps (offered at the end)."
!insertmacro MUI_FUNCTION_DESCRIPTION_END

; ------------------------------------------------------------------ uninstall

Function un.onInit
  IfFileExists "$INSTDIR\${EXE}" 0 done
  retry:
  ClearErrors
  FileOpen $0 "$INSTDIR\${EXE}" a
  IfErrors 0 closeit
  MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "Coreplay is running. Close it, then click Retry." /SD IDCANCEL IDRETRY retry
  Abort
  closeit:
  FileClose $0
  done:
FunctionEnd

Section "Uninstall"
  !insertmacro UnregisterVideo
  !insertmacro UnregisterSequence
  !insertmacro UnregisterPlaylist
  !insertmacro UnregisterAudio
  !insertmacro UnregisterImage
  DeleteRegKey HKCU "Software\Classes\${PROGID_Video}"
  DeleteRegKey HKCU "Software\Classes\${PROGID_Sequence}"
  DeleteRegKey HKCU "Software\Classes\${PROGID_Playlist}"
  DeleteRegKey HKCU "Software\Classes\${PROGID_Audio}"
  DeleteRegKey HKCU "Software\Classes\${PROGID_Image}"
  DeleteRegKey HKCU "Software\Classes\Applications\${EXE}"
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\App Paths\${EXE}"
  DeleteRegValue HKCU "Software\RegisteredApplications" "Coreplay"
  DeleteRegKey HKCU "${REG_APP}\Capabilities"
  DeleteRegKey HKCU "${REG_APP}\PreviousDefaults"
  DeleteRegKey HKCU "${REG_APP}\CreatedKeys"
  DeleteRegValue HKCU "${REG_APP}" "InstallDir"
  DeleteRegKey HKCU "${REG_UNINST}"
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'

  Delete "$SMPROGRAMS\Coreplay.lnk"
  Delete "$DESKTOP\Coreplay.lnk"
  RMDir /r "$INSTDIR"
SectionEnd
