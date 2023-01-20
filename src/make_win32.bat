@echo off

set PRGMFLS=c:\Program Files
if exist "c:\program files (x86)" set PRGMFLS=c:\program files (x86)

if "%AMD64%"=="1" set VSVARSPARAMS=amd64
if NOT "%AMD64%"=="1" set VSVARSPARAMS=x86

call "%PRGMFLS%\Microsoft Visual Studio 14.0\VC\vcvarsall.bat" %VSVARSPARAMS%

if "%AMD64%"=="1" set ARCH=win-x64
if NOT "%AMD64%"=="1" set ARCH=win-x32

if "%AMD64%"=="1" set BUILDENV=c:\buildenv\x64
if NOT "%AMD64%"=="1" set BUILDENV=c:\buildenv\x86

if "%AMD64%"=="1" set MACHINE=X64
if NOT "%AMD64%"=="1" set MACHINE=X86

if "%RELEASE%"=="1" set CLPARAMS=/Zi /DWIN32 /nologo /EHsc /I. /I.\win-pthread\ /I"%BUILDENV%\include" /I"%BUILDENV%\include\mariadb" /O2 /MD
if NOT "%RELEASE%"=="1" set CLPARAMS=/Zi /DWIN32 /nologo /EHsc /I. /I.\win-pthread\ /I"%BUILDENV%\include" /I"%BUILDENV%\include\mariadb" /O2 /DDEBUG /MD
if "%RELEASE%"=="1" set LINKPARAMS=/DEBUG /MAP /OPT:REF /OPT:ICF /LIBPATH:"%BUILDENV%\lib" /LIBPATH:"%BUILDENV%\lib\mariadb"
if NOT "%RELEASE%"=="1" set LINKPARAMS=/DEBUG /LIBPATH:"%BUILDENV%\lib" /LIBPATH:"%BUILDENV%\lib\mariadb"

echo ------------------------------------
if "%RELEASE%"=="1" echo Building b1gMailServer %ARCH% (release)...
if NOT "%RELEASE%"=="1" echo Building b1gMailServer %ARCH% (debug)...
echo ------------------------------------

if not exist pluginsdk\obj md pluginsdk\obj
if not exist obj md obj
if not exist bin md bin
del b1gmailserver-*.exe
del obj\*.obj
del bin\*.exe
del bin\*.dll

copy %BUILDENV%\bin\vc_redist.exe bin\
copy %BUILDENV%\bin\*.dll bin\
copy %BUILDENV%\lib\mariadb\*.dll bin\

cl.exe /nologo /DARCH="\"%ARCH%\"" build_pre.c
if ERRORLEVEL 1 exit /B 1
build_pre
if ERRORLEVEL 1 exit /B 1
del build_pre.exe
del build_pre.obj

setlocal
set BUILD=
for /f "tokens=*" %%a in ('type buildno 2^>NUL') do set BUILD=%%a

echo ------------------------------------
echo Compiling plugins
echo ------------------------------------

cl.exe /LD %CLPARAMS% /I.\pluginsdk\ /Fopluginsdk\obj\cliqueuemgr.obj		/c pluginsdk\cliqueuemgr.cpp
if ERRORLEVEL 1 exit /B 1

echo ------------------------------------
echo Linking plugins
echo ------------------------------------

link %LINKPARAMS% /nologo /DLL pluginsdk\obj\cliqueuemgr.obj /machine:%MACHINE% /out:bin\CLIQueueMgr.dll
if ERRORLEVEL 1 exit /B 1

echo ------------------------------------
echo Compiling core application...
echo ------------------------------------

cl.exe %CLPARAMS% /DWIN32 /nologo /EHsc /I. /Foobj\main.obj 			/c main.cpp
if ERRORLEVEL 1 exit /B 1
cl.exe %CLPARAMS% /Foobj\io.obj 			/c io.cpp
if ERRORLEVEL 1 exit /B 1
cl.exe %CLPARAMS% /Foobj\plugin.obj			/c plugin/plugin.cpp
if ERRORLEVEL 1 exit /B 1
cl.exe %CLPARAMS% /Foobj\pluginmgr.obj			/c plugin/pluginmgr.cpp
if ERRORLEVEL 1 exit /B 1
cl.exe %CLPARAMS% /Foobj\pthread.obj 		/c win-pthread/pthread.cpp
if ERRORLEVEL 1 exit /B 1
cl.exe %CLPARAMS% /Foobj\win_compat.obj 		/c core/win_compat.cpp
if ERRORLEVEL 1 exit /B 1
cl.exe %CLPARAMS% /Foobj\dns.obj 		/c core/dns.cpp
if ERRORLEVEL 1 exit /B 1
cl.exe %CLPARAMS% /Foobj\license.obj 		/c core/license.cpp
if ERRORLEVEL 1 exit /B 1
cl.exe %CLPARAMS% /Foobj\md5.obj 			/c core/md5.c
if ERRORLEVEL 1 exit /B 1
cl.exe %CLPARAMS% /Foobj\config.obj 			/c core/config.cpp
if ERRORLEVEL 1 exit /B 1
cl.exe %CLPARAMS% /Foobj\exception.obj 		/c core/exception.cpp
if ERRORLEVEL 1 exit /B 1
cl.exe %CLPARAMS% /Foobj\mysql_db.obj 		/c core/mysql_db.cpp
if ERRORLEVEL 1 exit /B 1
cl.exe %CLPARAMS% /Foobj\mysql_result.obj 		/c core/mysql_result.cpp
if ERRORLEVEL 1 exit /B 1
cl.exe %CLPARAMS% /Foobj\utils.obj 			/c core/utils.cpp
if ERRORLEVEL 1 exit /B 1
cl.exe %CLPARAMS% /Foobj\tls_dh.obj 			/c core/tls_dh.cpp
if ERRORLEVEL 1 exit /B 1
cl.exe %CLPARAMS% /Foobj\socket.obj 			/c core/socket.cpp
if ERRORLEVEL 1 exit /B 1
cl.exe %CLPARAMS% /Foobj\process.obj                    /c core/process.cpp
if ERRORLEVEL 1 exit /B 1
cl.exe %CLPARAMS% /Foobj\threadpool.obj                    /c core/threadpool.cpp
if ERRORLEVEL 1 exit /B 1
cl.exe %CLPARAMS% /Foobj\blobstorage.obj 		/c core/blobstorage.cpp
if ERRORLEVEL 1 exit /B 1
cl.exe %CLPARAMS% /Foobj\sqlite.obj 		/c core/sqlite.cpp
if ERRORLEVEL 1 exit /B 1
cl.exe %CLPARAMS% /Foobj\sqlite3.obj 		/c sqlite3/sqlite3.c
if ERRORLEVEL 1 exit /B 1
cl.exe %CLPARAMS% /Foobj\apnsdispatcher.obj 			/c msgqueue/apnsdispatcher.cpp
if ERRORLEVEL 1 exit /B 1
cl.exe %CLPARAMS% /Foobj\control.obj 			/c msgqueue/control.cpp
if ERRORLEVEL 1 exit /B 1
cl.exe %CLPARAMS% /Foobj\bounce.obj 			/c msgqueue/bounce.cpp
if ERRORLEVEL 1 exit /B 1
cl.exe %CLPARAMS% /Foobj\enqueue.obj 			/c msgqueue/enqueue.cpp
if ERRORLEVEL 1 exit /B 1
cl.exe %CLPARAMS% /Foobj\msgqueue_inbound.obj 			/c msgqueue/msgqueue_inbound.cpp
if ERRORLEVEL 1 exit /B 1
cl.exe %CLPARAMS% /Foobj\msgqueue_outbound.obj           /c msgqueue/msgqueue_outbound.cpp
if ERRORLEVEL 1 exit /B 1
cl.exe %CLPARAMS% /Foobj\msgqueue_rule.obj 			/c msgqueue/msgqueue_rule.cpp
if ERRORLEVEL 1 exit /B 1
cl.exe %CLPARAMS% /Foobj\msgqueue_threadpool.obj           /c msgqueue/msgqueue_threadpool.cpp
if ERRORLEVEL 1 exit /B 1
cl.exe %CLPARAMS% /Foobj\msgqueue.obj 			/c msgqueue/msgqueue.cpp
if ERRORLEVEL 1 exit /B 1
cl.exe %CLPARAMS% /Foobj\deliveryrules.obj 			/c msgqueue/deliveryrules.cpp
if ERRORLEVEL 1 exit /B 1
cl.exe %CLPARAMS% /Foobj\inboundprocess.obj 			/c msgqueue/inboundprocess.cpp
if ERRORLEVEL 1 exit /B 1
cl.exe %CLPARAMS% /Foobj\inboundprocesspool.obj 			/c msgqueue/inboundprocesspool.cpp
if ERRORLEVEL 1 exit /B 1
cl.exe %CLPARAMS% /Foobj\smtpsession.obj 			/c msgqueue/smtpsession.cpp
if ERRORLEVEL 1 exit /B 1
cl.exe %CLPARAMS% /Foobj\smtpsessionpool.obj 			/c msgqueue/smtpsessionpool.cpp
if ERRORLEVEL 1 exit /B 1
cl.exe %CLPARAMS% /Foobj\milter.obj 			/c smtp/milter.cpp
if ERRORLEVEL 1 exit /B 1
cl.exe %CLPARAMS% /Foobj\spf.obj 			/c smtp/spf.cpp
if ERRORLEVEL 1 exit /B 1
cl.exe %CLPARAMS% /Foobj\smtp.obj 			/c smtp/smtp.cpp
if ERRORLEVEL 1 exit /B 1
cl.exe %CLPARAMS% /Foobj\smtp_auth.obj 			/c smtp/smtp_auth.cpp
if ERRORLEVEL 1 exit /B 1
cl.exe %CLPARAMS% /Foobj\smtp_data.obj 			/c smtp/smtp_data.cpp
if ERRORLEVEL 1 exit /B 1
cl.exe %CLPARAMS% /Foobj\smtp_peer.obj 			/c smtp/smtp_peer.cpp
if ERRORLEVEL 1 exit /B 1
cl.exe %CLPARAMS% /Foobj\http.obj 			/c http/http.cpp
if ERRORLEVEL 1 exit /B 1
cl.exe %CLPARAMS% /Foobj\pop3.obj 			/c pop3/pop3.cpp
if ERRORLEVEL 1 exit /B 1
cl.exe %CLPARAMS% /Foobj\imap.obj 			/c imap/imap.cpp
if ERRORLEVEL 1 exit /B 1
cl.exe %CLPARAMS% /Foobj\tls.obj 			/c imap/tls.cpp
if ERRORLEVEL 1 exit /B 1
cl.exe %CLPARAMS% /Foobj\auth.obj 			/c imap/auth.cpp
if ERRORLEVEL 1 exit /B 1
cl.exe %CLPARAMS% /Foobj\imaphelper.obj 		/c imap/imaphelper.cpp
if ERRORLEVEL 1 exit /B 1
cl.exe %CLPARAMS% /Foobj\pattern.obj  		/c imap/pattern.cpp
if ERRORLEVEL 1 exit /B 1
cl.exe %CLPARAMS% /Foobj\mail.obj 			/c imap/mail.cpp
if ERRORLEVEL 1 exit /B 1
cl.exe %CLPARAMS% /Foobj\nonauth.obj 		/c imap/nonauth.cpp
if ERRORLEVEL 1 exit /B 1
cl.exe %CLPARAMS% /Foobj\selected.obj 		/c imap/selected.cpp
if ERRORLEVEL 1 exit /B 1
cl.exe %CLPARAMS% /Foobj\mailparser.obj 		/c imap/mailparser.cpp
if ERRORLEVEL 1 exit /B 1
rc resource.rc
if ERRORLEVEL 1 exit /B 1

echo ------------------------------------
echo Linking core application...
echo ------------------------------------

link %LINKPARAMS% /nologo winmm.lib wsock32.lib advapi32.lib kernel32.lib gdi32.lib user32.lib iphlpapi.lib libmariadb.lib libssl.lib libcrypto.lib ws2_32.lib pcre.lib zlib.lib dnsapi.lib resource.res obj\*.obj /machine:%MACHINE% /out:bin\b1gmailserver.exe
if ERRORLEVEL 1 exit /B 1

:BuildBMSService

echo ------------------------------------
echo Compiling service host...
echo ------------------------------------

cl.exe /Zi /nologo %CLPARAMS% /MD /Foobj\service_host.obj 	/c inetserver/main.cpp
if ERRORLEVEL 1 exit /B 1
rc inetserver\resource.rc
if ERRORLEVEL 1 exit /B 1
move inetserver\resource.res obj\service_host.res

echo ------------------------------------
echo Linking service host...
echo ------------------------------------

link %LINKPARAMS% /nologo kernel32.lib gdi32.lib user32.lib advapi32.lib ws2_32.lib obj/service_host.res obj/service_host.obj /machine:%MACHINE% /out:bin\BMSService.exe
if ERRORLEVEL 1 exit /B 1

del obj\service_host.obj
del obj\service_host.res

:BuildBMSManager

echo ------------------------------------
echo Compiling service manager...
echo ------------------------------------

cd BMSManager\BMSManager
if not exist obj md obj
if not exist obj\Release md obj\Release
if not exist bin md bin
if not exist bin\Release md bin\Release
resgen FormAbout.resx 			obj\Release\BMSManager.FormAbout.resources
if ERRORLEVEL 1 exit /B 1
resgen FormMain.resx 			obj\Release\BMSManager.FormMain.resources
if ERRORLEVEL 1 exit /B 1
resgen FormPrefs.resx 			obj\Release\BMSManager.FormPrefs.resources
if ERRORLEVEL 1 exit /B 1
resgen FormService.resx 		obj\Release\BMSManager.FormService.resources
if ERRORLEVEL 1 exit /B 1
resgen Properties\Resources.resx 	obj\Release\BMSManager.Properties.Resources.resources
if ERRORLEVEL 1 exit /B 1
set NETVER=v2.0.50727
Csc.exe /noconfig /nowarn:1701,1702 /errorreport:prompt /warn:4 /define:TRACE /reference:C:\Windows\Microsoft.NET\Framework\%NETVER%\System.Data.dll /reference:C:\Windows\Microsoft.NET\Framework\%NETVER%\System.Deployment.dll /reference:C:\Windows\Microsoft.NET\Framework\%NETVER%\System.Design.dll /reference:C:\Windows\Microsoft.NET\Framework\%NETVER%\System.dll /reference:C:\Windows\Microsoft.NET\Framework\%NETVER%\System.Drawing.dll /reference:C:\Windows\Microsoft.NET\Framework\%NETVER%\System.Management.dll /reference:C:\Windows\Microsoft.NET\Framework\%NETVER%\System.ServiceProcess.dll /reference:C:\Windows\Microsoft.NET\Framework\%NETVER%\System.Windows.Forms.dll /reference:C:\Windows\Microsoft.NET\Framework\%NETVER%\System.Xml.dll /debug:pdbonly /filealign:512 /optimize+ /out:bin\Release\BMSManager.exe /resource:obj\Release\BMSManager.FormAbout.resources /resource:obj\Release\BMSManager.FormMain.resources /resource:obj\Release\BMSManager.FormPrefs.resources /resource:obj\Release\BMSManager.FormService.resources /resource:obj\Release\BMSManager.Properties.Resources.resources /target:winexe /win32icon:"Email Configuration.ico" BMSConfig.cs BMSServiceController.cs FormAbout.cs FormAbout.Designer.cs FormMain.cs FormMain.Designer.cs FormPrefs.cs FormPrefs.Designer.cs FormService.cs FormService.Designer.cs Program.cs Properties\AssemblyInfo.cs Properties\Resources.Designer.cs Properties\Settings.Designer.cs
if ERRORLEVEL 1 exit /B 1
mt.exe -manifest BMSManager.exe.manifest -outputresource:"bin\Release\BMSManager.exe";#1
if ERRORLEVEL 1 exit /B 1
copy bin\Release\BMSManager.exe ..\..\bin\
cd ..\..

if NOT "%RELEASE%"=="1" exit /B

:BuildSetup

echo ------------------------------------
echo Creating setup...
echo ------------------------------------

cd win-setup
if "%AMD64%"=="1" set NSISSCRIPT=setup_x64.nsi
if NOT "%AMD64%"=="1" set NSISSCRIPT=setup_x86.nsi
"%PRGMFLS%\NSIS\makensis.exe" /V2 %NSISSCRIPT%
if ERRORLEVEL 1 exit /B 1
copy setup.exe ..\b1gmailserver-2.8.%BUILD%-%ARCH%.exe
cd ..

