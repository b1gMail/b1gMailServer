!macro CheckNetFramework FrameworkVersion
	Var /GLOBAL dotNetUrl
	Var /GLOBAL dotNetReadableVersion

	!define DOTNET45_URL 	    "http://go.microsoft.com/fwlink/?LinkId=225702"
	!define DOTNET40Full_URL 	"http://www.microsoft.com/downloads/info.aspx?na=41&srcfamilyid=0a391abd-25c1-4fc0-919f-b21f31ab88b7&srcdisplaylang=en&u=http%3a%2f%2fdownload.microsoft.com%2fdownload%2f9%2f5%2fA%2f95A9616B-7A37-4AF6-BC36-D6EA96C8DAAE%2fdotNetFx40_Full_x86_x64.exe"
	!define DOTNET40Client_URL	"http://www.microsoft.com/downloads/info.aspx?na=41&srcfamilyid=e5ad0459-cbcc-4b4f-97b6-fb17111cf544&srcdisplaylang=en&u=http%3a%2f%2fdownload.microsoft.com%2fdownload%2f5%2f6%2f2%2f562A10F9-C9F4-4313-A044-9C94E0A8FAC8%2fdotNetFx40_Client_x86_x64.exe"
	!define DOTNET35_URL		"http://download.microsoft.com/download/2/0/e/20e90413-712f-438c-988e-fdaa79a8ac3d/dotnetfx35.exe"
	!define DOTNET30_URL		"http://download.microsoft.com/download/2/0/e/20e90413-712f-438c-988e-fdaa79a8ac3d/dotnetfx35.exe"
	!define DOTNET20_URL		"http://www.microsoft.com/downloads/info.aspx?na=41&srcfamilyid=0856eacb-4362-4b0d-8edd-aab15c5e04f5&srcdisplaylang=en&u=http%3a%2f%2fdownload.microsoft.com%2fdownload%2f5%2f6%2f7%2f567758a3-759e-473e-bf8f-52154438565a%2fdotnetfx.exe"
	!define DOTNET11_URL		"http://www.microsoft.com/downloads/info.aspx?na=41&srcfamilyid=262d25e3-f589-4842-8157-034d1e7cf3a3&srcdisplaylang=en&u=http%3a%2f%2fdownload.microsoft.com%2fdownload%2fa%2fa%2fc%2faac39226-8825-44ce-90e3-bf8203e74006%2fdotnetfx.exe"
	!define DOTNET10_URL		"http://www.microsoft.com/downloads/info.aspx?na=41&srcfamilyid=262d25e3-f589-4842-8157-034d1e7cf3a3&srcdisplaylang=en&u=http%3a%2f%2fdownload.microsoft.com%2fdownload%2fa%2fa%2fc%2faac39226-8825-44ce-90e3-bf8203e74006%2fdotnetfx.exe"

	${If} ${FrameworkVersion} == "45"
		StrCpy $dotNetUrl ${DOTNET45_URL}
		StrCpy $dotNetReadableVersion "4.5"
	${ElseIf} ${FrameworkVersion} == "40Full"
		StrCpy $dotNetUrl ${DOTNET40Full_URL}
		StrCpy $dotNetReadableVersion "4.0 Full"
	${ElseIf} ${FrameworkVersion} == "40Client"
		StrCpy $dotNetUrl ${DOTNET40Client_URL}
		StrCpy $dotNetReadableVersion "4.0 Client"
	${ElseIf} ${FrameworkVersion} == "35"
		StrCpy $dotNetUrl ${DOTNET35_URL}
		StrCpy $dotNetReadableVersion "3.5"
	${ElseIf} ${FrameworkVersion} == "30"
		StrCpy $dotNetUrl ${DOTNET30_URL}
		StrCpy $dotNetReadableVersion "3.0"
	${ElseIf} ${FrameworkVersion} == "20"
		StrCpy $dotNetUrl ${DOTNET20_URL}
		StrCpy $dotNetReadableVersion "2.0"
	${ElseIf} ${FrameworkVersion} == "11"
		StrCpy $dotNetUrl ${DOTNET11_URL}
		StrCpy $dotNetReadableVersion "1.1"
	${ElseIf} ${FrameworkVersion} == "10"
		StrCpy $dotNetUrl ${DOTNET10_URL}
		StrCpy $dotNetReadableVersion "1.0"
	${EndIf}
	
	DetailPrint "Bestimme .NET-Framework-Version..."

	Push $0
	Push $1
	Push $2
	Push $3
	Push $4
	Push $5
	Push $6
	Push $7

	DotNetChecker::IsDotNet${FrameworkVersion}Installed
	Pop $0
	
	${If} $0 == "f"
		DetailPrint ".NET-Framework $dotNetReadableVersion nicht gefunden."
		Goto NoDotNET
	${Else}
		DetailPrint ".NET-Framework $dotNetReadableVersion gefunden."
		Goto NewDotNET
	${EndIf}

NoDotNET:
	MessageBox MB_YESNOCANCEL|MB_ICONEXCLAMATION \
	"Das .NET-Framework wurde nicht gefunden. Erforderliche Version: $dotNetReadableVersion.$\n.NET-Framework $dotNetReadableVersion von www.microsoft.com herunterladen?" \
	/SD IDYES IDYES DownloadDotNET IDNO NewDotNET
	goto GiveUpDotNET ;IDCANCEL

DownloadDotNET:
	DetailPrint "Download von .NET-Framework $dotNetReadableVersion beginnt."
	NSISDL::download $dotNetUrl "$TEMP\dotnetfx.exe"
	DetailPrint "Download abgeschlossen."

	Pop $0
	${If} $0 == "cancel"
		MessageBox MB_YESNO|MB_ICONEXCLAMATION \
		"Download abgebrochen. Installation dennoch fortfahren?" \
		IDYES NewDotNET IDNO GiveUpDotNET
	${ElseIf} $0 != "success"
		MessageBox MB_YESNO|MB_ICONEXCLAMATION \
		"Download fehlgeschlagen:$\n$0$\n$\nInstallation dennoch fortfahren?" \
		IDYES NewDotNET IDNO GiveUpDotNET
	${EndIf}

	DetailPrint "Installation wird bis zur Fertigstellung der Installation des .NET-Frameworks pausiert."
	ExecWait '$TEMP\dotnetfx.exe /q /c:"install /q"'

	DetailPrint ".NET-Framework wurde installiert/aktualisiert. Entferne Installationspaket."
	Delete "$TEMP\dotnetfx.exe"
	DetailPrint ".NET Framework installer removed."
	goto NewDotNet

GiveUpDotNET:
	Abort "Installation durch Benutzer abgebrochen."

NewDotNET:
	DetailPrint "Installation wird fortgesetzt."
	Pop $0
	Pop $1
	Pop $2
	Pop $3
	Pop $4
	Pop $5
	Pop $6
	Pop $7

!macroend