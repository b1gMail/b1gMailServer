; b1gMailServer Setup x64 wrapper

Unicode true

!include "x64.nsh"

;--------------------------------
; x64 stuff
Function .onInit
	${If} ${RunningX64}
		Goto +1
	${Else}
		MessageBox MB_OK|MB_ICONSTOP "Die 64-bit-Version von b1gMailServer kann nicht auf 32-bit-Systemen installiert werden."
		Abort
	${EndIf}
FunctionEnd

InstallDir "$PROGRAMFILES64\B1G Software\b1gMailServer"

;--------------------------------
; original script
!include "setup.nsi"
