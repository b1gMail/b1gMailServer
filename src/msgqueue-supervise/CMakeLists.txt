set(NAME "bms-queue")

set(SOURCES
  main.cpp
)

add_executable(${NAME} ${SOURCES})

install(TARGETS ${NAME} DESTINATION bin)
