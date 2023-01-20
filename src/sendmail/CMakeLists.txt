set(NAME "bms-sendmail")

set(SOURCES
  config.cpp
  context.cpp
  sendmail.cpp
  smtp.cpp
  socket.cpp
  utils.cpp
)

add_executable(${NAME} ${SOURCES})

target_include_directories(${NAME} PRIVATE ${PCRE_INCLUDE_DIRS})
target_link_libraries(${NAME} PRIVATE ${PCRE_LIBRARIES})

install(TARGETS ${NAME} DESTINATION bin)
