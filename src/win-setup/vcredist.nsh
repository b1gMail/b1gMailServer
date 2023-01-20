!include "x64.nsh"

!macro _VCRedist_SetStatus Status
    !ifmacrodef SetStatus
        !insertmacro SetStatus "${Status}"
    !else
        DetailPrint "${Status}"
    !endif
!macroend

!macro _VCRedist_MessageBoxIfError Message
    !ifmacrodef MessageBoxIfError
        !insertmacro MessageBoxIfError "${Message}"
    !endif
!macroend

!macro InstallVCRedist_32bit TempOutPath
	!insertmacro _VCRedist_SetStatus "Checking for Visual C++ Redistributable..."
	ReadRegStr $1 HKLM "Software\Microsoft\DevDiv\vc\Servicing\14.0\RuntimeMinimum" Install
	StrCmp $1 "1" InstallVCRedist_32bitFinish
	
	!insertmacro _VCRedist_SetStatus "Extracting Visual C++ Redistributable..."
	SetOutPath ${TempOutPath}
	File "..\bin\vc_redist.exe"
	
	!insertmacro _VCRedist_SetStatus "Installing Visual C++ Redistributable..."
	ClearErrors
	ExecWait '"${TempOutPath}\vc_redist.exe" /s /v" /qn"'
	!insertmacro _VCRedist_MessageBoxIfError "Failed to install Visual C++ Redistributable."
InstallVCRedist_32bitFinish:
!macroend

!macro InstallVCRedist_64bit TempOutPath
	!insertmacro _VCRedist_SetStatus "Checking for Visual C++ Redistributable..."
	SetRegView 64
	ReadRegStr $1 HKLM "Software\Microsoft\DevDiv\vc\Servicing\14.0\RuntimeMinimum" Install
	SetRegView lastused
	StrCmp $1 "1" InstallVCRedist_64bitFinish
	
	!insertmacro _VCRedist_SetStatus "Extracting Visual C++ Redistributable..."
	SetOutPath ${TempOutPath}
	File "..\bin\vc_redist.exe"
	
	!insertmacro _VCRedist_SetStatus "Installing Visual C++ Redistributable..."
	ClearErrors
	ExecWait '"${TempOutPath}\vc_redist.exe" /s /v" /qn"'
	!insertmacro _VCRedist_MessageBoxIfError "Failed to install Visual C++ Redistributable."
InstallVCRedist_64bitFinish:
!macroend
