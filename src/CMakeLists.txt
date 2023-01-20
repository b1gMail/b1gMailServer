cmake_minimum_required(VERSION 3.0)
project(b1gmailserver)

execute_process(
	COMMAND uname -m
	COMMAND tr -d '\n'
	OUTPUT_VARIABLE BMS_ARCHNAME
)
execute_process(
	COMMAND cat ${CMAKE_SOURCE_DIR}/buildno
	COMMAND tr -d '\n'
	OUTPUT_VARIABLE BMS_BUILD
)
execute_process(
	COMMAND date
	COMMAND tr -d '\n'
	OUTPUT_VARIABLE BMS_BUILD_DATE
)

set(BMS_VER_MAJOR		2)
set(BMS_VER_MINOR		8)
set(BMS_VERSION			${BMS_VER_MAJOR}.${BMS_VER_MINOR})

configure_file(build.h.in build.h)

set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
set(CMAKE_INSTALL_RPATH "/opt/b1gmailserver/libs")

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/CMake/Modules")

if(WIN32)
	set(CMAKE_CXX_FLAGS "/EHsc /DWIN32")
	set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MD")
	set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MDd")
else()
	add_definitions(-D_UNIX -D_UNIX_EXP)
endif()

add_definitions(-Wall -Wunused-value)

set(SOURCES
	io.cpp
	main.cpp
	core/blobstorage.cpp
	core/config.cpp
	core/dns.cpp
	core/exception.cpp
	core/md5.c
	core/mysql_db.cpp
	core/mysql_result.cpp
	core/process.cpp
	core/servicesession.cpp
	core/socket.cpp
	core/sqlite.cpp
	core/threadpool.cpp
	core/tls_dh.cpp
	core/utils.cpp
	core/win_compat.cpp
	http/http.cpp
	imap/auth.cpp
	imap/imap.cpp
	imap/imaphelper.cpp
	imap/mail.cpp
	imap/mailparser.cpp
	imap/nonauth.cpp
	imap/pattern.cpp
	imap/selected.cpp
	imap/tls.cpp
	msgqueue/apnsdispatcher.cpp
	msgqueue/bounce.cpp
	msgqueue/control.cpp
	msgqueue/deliveryrules.cpp
	msgqueue/enqueue.cpp
	msgqueue/inboundprocess.cpp
	msgqueue/inboundprocesspool.cpp
	msgqueue/msgqueue.cpp
	msgqueue/msgqueue_inbound.cpp
	msgqueue/msgqueue_outbound.cpp
	msgqueue/msgqueue_rule.cpp
	msgqueue/msgqueue_threadpool.cpp
	msgqueue/smtpsession.cpp
	msgqueue/smtpsessionpool.cpp
	plugin/plugin.cpp
	plugin/pluginmgr.cpp
	pop3/pop3.cpp
	smtp/smtp.cpp
	smtp/smtp_auth.cpp
	smtp/smtp_data.cpp
	smtp/smtp_peer.cpp
	smtp/spf.cpp
	smtp/milter.cpp
	sqlite3/sqlite3.c
)

if(WIN32)
	list(APPEND SOURCES
		win-pthread/pthread.cpp
	)
endif()

add_executable(${PROJECT_NAME} ${SOURCES})

find_package(ZLIB REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(PCRE REQUIRED)
find_package(mariadbclient REQUIRED)

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR} ${OPENSSL_INCLUDE_DIR} ${PCRE_INCLUDE_DIRS} ${MARIADBCLIENT_INCLUDE_DIRS} ${ZLIB_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} PRIVATE ${OPENSSL_LIBRARIES} ${ZLIB_LIBRARIES} ${PCRE_LIBRARIES} ${MARIADBCLIENT_LIBRARIES} dl pthread)

if(WIN32)
	target_link_libraries(${PROJECT_NAME} ws2_32.lib dnsapi.lib iphlpapi.lib)
	include_directories(${PROJECT_SOURCE_DIR}/win-pthread)
else()
	target_link_libraries(${PROJECT_NAME} PRIVATE resolv)
endif()

if(APPLE)
	target_link_libraries(${PROJECT_NAME} PRIVATE "-framework CoreFoundation -framework IOKit")
endif()

include_directories(${PROJECT_SOURCE_DIR})

install(TARGETS ${PROJECT_NAME} DESTINATION bin)

add_subdirectory(pluginsdk)

if(NOT WIN32)
	add_subdirectory(sendmail)
	add_subdirectory(msgqueue-supervise)
	add_subdirectory(linux-setup-v2)
	add_subdirectory(dist)
endif()
