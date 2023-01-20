set(NAME "setup")

set(CMAKE_INSTALL_RPATH "./libs")

add_custom_command(
  OUTPUT            resdata.cpp
  COMMAND           php ${CMAKE_CURRENT_SOURCE_DIR}/rescc.php ${CMAKE_CURRENT_BINARY_DIR}/resdata.cpp
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  DEPENDS           res/
)

set(SOURCES
  HTTPServer.cpp
  Language.cpp
  main.cpp
  md5.c
  mysql_db.cpp
  mysql_result.cpp
  SetupServer_install.cpp
  SetupServer.cpp
  Template.cpp
  UI.cpp
  Utils.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/resdata.cpp
)

add_executable(${NAME} ${SOURCES})

find_package(dialog REQUIRED)

set(CURSES_NEED_NCURSES TRUE)
find_package(Curses REQUIRED)

target_include_directories(${NAME} PRIVATE ${CMAKE_BINARY_DIR} ${MARIADBCLIENT_INCLUDE_DIRS} ${DIALOG_INCLUDE_DIRS} ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(${NAME} PRIVATE ${DIALOG_LIBRARIES} ${MARIADBCLIENT_LIBRARIES} ${CURSES_LIBRARIES} ${OPENSSL_LIBRARIES} ${ZLIB_LIBRARIES})

install(TARGETS ${NAME} DESTINATION bin)
