; b1gMailServer Setup

;--------------------------------
;Include Modern UI

  !include "MUI.nsh"

;--------------------------------
; .NET

  !define SHORT_APP_NAME "b1gMailServer"
  !include "DotNetChecker.nsh"

;--------------------------------
; VC Redist

  !include "vcredist.nsh"

;--------------------------------
;General

  ;Name and file
  Name "b1gMailServer"
  OutFile "setup.exe"
  BrandingText "B1G Software"
  RequestExecutionLevel admin

  ;Get installation folder from registry if available
  InstallDirRegKey HKLM "Software\B1G Software\b1gMailServer" ""

;--------------------------------
;Interface Settings

  !define MUI_ICON ".\install.ico"
  !define MUI_UNICON ".\uninstall.ico"
  !define MUI_ABORTWARNING
  !define MUI_HEADERIMAGE
  !define MUI_HEADERIMAGE_RIGHT
  !define MUI_HEADERIMAGE_BITMAP ".\setup-head.bmp"
  !define MUI_WELCOMEFINISHPAGE_BITMAP ".\setup-wizard.bmp"
  !define MUI_UNWELCOMEFINISHPAGE_BITMAP ".\setup-wizard.bmp"
  !define MUI_FINISHPAGE_RUN_TEXT "b1gMailServer-Manager starten"
  !define MUI_FINISHPAGE_RUN "$INSTDIR\bin\BMSManager.exe"

;--------------------------------
;Pages

  !insertmacro MUI_PAGE_WELCOME
  !insertmacro MUI_PAGE_DIRECTORY
  !insertmacro MUI_PAGE_INSTFILES
  !insertmacro MUI_PAGE_FINISH

  !insertmacro MUI_UNPAGE_WELCOME
  !insertmacro MUI_UNPAGE_CONFIRM
  !insertmacro MUI_UNPAGE_INSTFILES
  !insertmacro MUI_UNPAGE_FINISH

;--------------------------------
;Languages

  !insertmacro MUI_LANGUAGE "German"


;--------------------------------
;Installer Sections

Section "b1gMailServer" Sec1gMailServer
  ; stop services
  IfFileExists "$INSTDIR\bin\BMSService.exe" 0 AfterStopServices
    ExecWait '"$INSTDIR\bin\BMSService.exe" --pop3 --stop'
    ExecWait '"$INSTDIR\bin\BMSService.exe" --imap --stop'
    ExecWait '"$INSTDIR\bin\BMSService.exe" --smtp --stop'
    ExecWait '"$INSTDIR\bin\BMSService.exe" --http --stop'
    ExecWait '"$INSTDIR\bin\BMSService.exe" --msgqueue --stop'
AfterStopServices:

  !insertmacro CheckNetFramework 40Client
  ${If} ${RunningX64}
	!insertmacro InstallVCRedist_64bit "$TEMP\b1gMailServerSetup"
  ${Else}
	!insertmacro InstallVCRedist_32bit "$TEMP\b1gMailServerSetup"
  ${EndIf}

  ; launcher
  SetOutPath "$INSTDIR"
  SetShellVarContext all
  File "..\OPENSSL_LICENSE"
  File "..\MARIADB_LICENSE"

  ; bin
  SetOutPath "$INSTDIR\bin"
  File "..\bin\b1gmailserver.exe"
  File "..\bin\BMSService.exe"
  File "..\bin\BMSManager.exe"
  File "..\bin\libmariadb.dll"
  File "..\bin\libcrypto*.dll"
  File "..\bin\libssl*.dll"

  ; plugins
  SetOutPath "$INSTDIR\plugins"
  File "..\bin\CLIQueueMgr.dll"

  ; queue
  CreateDirectory "$INSTDIR\queue"
  CreateDirectory "$INSTDIR\queue\0"
  CreateDirectory "$INSTDIR\queue\1"
  CreateDirectory "$INSTDIR\queue\2"
  CreateDirectory "$INSTDIR\queue\3"
  CreateDirectory "$INSTDIR\queue\4"
  CreateDirectory "$INSTDIR\queue\5"
  CreateDirectory "$INSTDIR\queue\6"
  CreateDirectory "$INSTDIR\queue\7"
  CreateDirectory "$INSTDIR\queue\8"
  CreateDirectory "$INSTDIR\queue\9"
  CreateDirectory "$INSTDIR\queue\A"
  CreateDirectory "$INSTDIR\queue\B"
  CreateDirectory "$INSTDIR\queue\C"
  CreateDirectory "$INSTDIR\queue\D"
  CreateDirectory "$INSTDIR\queue\E"
  CreateDirectory "$INSTDIR\queue\F"

  ; tls
  CreateDirectory "$INSTDIR\tls"

  ; reg keys
  ${If} ${RunningX64}
    SetRegView 64
  ${Else}
    SetRegView 32
  ${EndIf}
  WriteRegStr HKLM "Software\B1G Software\b1gMailServer" "Path" "$INSTDIR\bin"

  ; install services
  ExecWait '"$INSTDIR\bin\BMSService.exe" --pop3 --install'
  ExecWait '"$INSTDIR\bin\BMSService.exe" --imap --install'
  ExecWait '"$INSTDIR\bin\BMSService.exe" --smtp --install'
  ExecWait '"$INSTDIR\bin\BMSService.exe" --http --install'
  ExecWait '"$INSTDIR\bin\BMSService.exe" --msgqueue --install'

  ; launch configuration
  IfFileExists "$INSTDIR\b1gmailserver.cfg" ConfigExists 0
    ExecWait '"$INSTDIR\bin\BMSManager.exe" --firstrun'
ConfigExists:

  ; uninstaller
  WriteUninstaller "$INSTDIR\Uninstall.exe"

  ; shortcuts
  CreateDirectory "$SMPROGRAMS\b1gMailServer"
  CreateShortCut "$SMPROGRAMS\b1gMailServer\b1gMailServer-Manager.lnk" $INSTDIR\bin\BMSManager.exe
  CreateShortCut "$SMPROGRAMS\b1gMailServer\b1gMailServer deinstallieren.lnk" $INSTDIR\Uninstall.exe

  ; uninstaller registry
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\b1gMailServer" \
                 "DisplayName" "b1gMailServer"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\b1gMailServer" \
                 "UninstallString" "$INSTDIR\uninstall.exe"
SectionEnd


;--------------------------------
;Uninstaller Section

Section "Uninstall"
  SetShellVarContext all

  ; uninstall services
  ExecWait '"$INSTDIR\bin\BMSService.exe" --pop3 --uninstall'
  ExecWait '"$INSTDIR\bin\BMSService.exe" --imap --uninstall'
  ExecWait '"$INSTDIR\bin\BMSService.exe" --smtp --uninstall'
  ExecWait '"$INSTDIR\bin\BMSService.exe" --http --uninstall'
  ExecWait '"$INSTDIR\bin\BMSService.exe" --msgqueue --uninstall'

  ; delete shortcuts
  Delete "$SMPROGRAMS\b1gMailServer\b1gMailServer-Manager.lnk"
  Delete "$SMPROGRAMS\b1gMailServer\b1gMailServer deinstallieren.lnk"
  RMDir "$SMPROGRAMS\b1gMailServer"

  ; delete files
  Delete "$INSTDIR\Uninstall.exe"

  ; delete folder
  RMDir /R /REBOOTOK "$INSTDIR"

  ; delete registry keys
  ${If} ${RunningX64}
    SetRegView 64
  ${Else}
    SetRegView 32
  ${EndIf}
  ;DeleteRegKey HKLM "Software\B1G Software\b1gMailServer"
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\b1gMailServer"
SectionEnd
