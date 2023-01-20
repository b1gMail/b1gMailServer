; b1gMailServer Setup x86 wrapper

Unicode true

!include "x64.nsh"

;--------------------------------
; x64 stuff
Function .onInit
	${If} ${RunningX64}
		MessageBox MB_OK|MB_ICONSTOP "Die 32-bit-Version von b1gMailServer kann nicht auf 64-bit-Systemen installiert werden."
		Abort
	${Else}
		Goto +1
	${EndIf}
FunctionEnd

InstallDir "$PROGRAMFILES32\B1G Software\b1gMailServer"

;--------------------------------
; original script
!include "setup.nsi"
