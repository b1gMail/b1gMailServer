set(NAME "dist")

add_custom_target(${NAME}-copy)
add_custom_command(
  TARGET        ${NAME}-copy
  COMMAND       ${CMAKE_COMMAND} -E make_directory
    ${CMAKE_CURRENT_BINARY_DIR}/package/libs
)
add_custom_command(
  TARGET        ${NAME}-copy
  COMMAND       ${CMAKE_COMMAND} -E copy
    ${CMAKE_BINARY_DIR}/pluginsdk/CLIQueueMgr.so
    ${CMAKE_CURRENT_BINARY_DIR}/package/plugins/CLIQueueMgr.so
)
add_custom_command(
  TARGET        ${NAME}-copy
  COMMAND       ${CMAKE_COMMAND} -E copy
    ${CMAKE_BINARY_DIR}/b1gmailserver
    ${CMAKE_CURRENT_BINARY_DIR}/package/bin/b1gmailserver
)
add_custom_command(
  TARGET        ${NAME}-copy
  COMMAND       ${CMAKE_COMMAND} -E copy
    ${CMAKE_SOURCE_DIR}/init/bms-queue
    ${CMAKE_CURRENT_BINARY_DIR}/package/init/bms-queue
)
add_custom_command(
  TARGET        ${NAME}-copy
  COMMAND       ${CMAKE_COMMAND} -E copy
    ${CMAKE_BINARY_DIR}/msgqueue-supervise/bms-queue
    ${CMAKE_CURRENT_BINARY_DIR}/package/bin/bms-queue
)
add_custom_command(
  TARGET        ${NAME}-copy
  COMMAND       ${CMAKE_COMMAND} -E copy
    ${CMAKE_BINARY_DIR}/sendmail/bms-sendmail
    ${CMAKE_CURRENT_BINARY_DIR}/package/bin/bms-sendmail
)
add_custom_command(
  TARGET        ${NAME}-copy
  COMMAND       ${CMAKE_COMMAND} -E copy
    ${CMAKE_BINARY_DIR}/linux-setup-v2/setup
    ${CMAKE_CURRENT_BINARY_DIR}/package/setup
)
add_custom_command(
  TARGET        ${NAME}-copy
  COMMAND       ${CMAKE_COMMAND} -E copy
    ${CMAKE_SOURCE_DIR}/linux-setup-v2/run-setup
    ${CMAKE_CURRENT_BINARY_DIR}/package/run-setup
)
add_custom_command(
  TARGET        ${NAME}-copy
  COMMAND       ${CMAKE_COMMAND} -E copy
    ${CMAKE_SOURCE_DIR}/MARIADB_LICENSE
    ${CMAKE_CURRENT_BINARY_DIR}/package/MARIADB_LICENSE
)
add_custom_command(
  TARGET        ${NAME}-copy
  COMMAND       ${CMAKE_COMMAND} -E copy
    ${CMAKE_SOURCE_DIR}/bms-sendmail.conf
    ${CMAKE_CURRENT_BINARY_DIR}/package/conf/bms-sendmail.conf
)
add_custom_command(
  TARGET        ${NAME}-copy
  COMMAND       ${CMAKE_COMMAND} -E copy
    ${CMAKE_SOURCE_DIR}/OPENSSL_LICENSE
    ${CMAKE_CURRENT_BINARY_DIR}/package/OPENSSL_LICENSE
)
add_custom_command(
  TARGET        ${NAME}-copy
  COMMAND       ${CMAKE_COMMAND} -E copy
    ${CMAKE_SOURCE_DIR}/MARIADB_LICENSE
    ${CMAKE_CURRENT_BINARY_DIR}/package/MARIADB_LICENSE
)
add_custom_command(
  TARGET        ${NAME}-copy
  COMMAND       ${CMAKE_COMMAND} -E copy
    /buildenv/lib/libpcre.so.*.*.*
    ${CMAKE_CURRENT_BINARY_DIR}/package/libs/
)
add_custom_command(
  TARGET        ${NAME}-copy
  COMMAND       ${CMAKE_COMMAND} -E copy
    /buildenv/lib/mariadb/libmariadb.so.*
    ${CMAKE_CURRENT_BINARY_DIR}/package/libs/
)
add_custom_command(
  TARGET        ${NAME}-copy
  COMMAND       ${CMAKE_COMMAND} -E copy
    /buildenv/lib/libssl.so.*.*
    ${CMAKE_CURRENT_BINARY_DIR}/package/libs/
)
add_custom_command(
  TARGET        ${NAME}-copy
  COMMAND       ${CMAKE_COMMAND} -E copy
    /buildenv/lib/libcrypto.so.*.*
    ${CMAKE_CURRENT_BINARY_DIR}/package/libs/
)
add_dependencies(${NAME}-copy b1gmailserver bms-queue setup)

add_custom_target(${NAME})
add_custom_command(
  TARGET        ${NAME}
  COMMAND       ${CMAKE_SOURCE_DIR}/makeself-2.1.4/makeself.sh
    ${CMAKE_CURRENT_BINARY_DIR}/package/
    b1gmailserver-${BMS_VERSION}.${BMS_BUILD}-${BMS_ARCHNAME}.run
    "b1gMailServer ${BMS_VERSION}.${BMS_BUILD}"
    sh run-setup
)
add_dependencies(${NAME} ${NAME}-copy)
