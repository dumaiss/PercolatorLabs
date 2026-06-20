# Unit 1 core smoke tests. No external test framework: each executable
# returns non-zero on the first failed check and is registered with CTest.

# Public-API smoke test: links only against the public library interface.
add_executable(test_core test_core.c)
target_link_libraries(test_core PRIVATE vdrip9958)
add_test(NAME core_public COMMAND test_core)

# Limited white-box smoke test: includes the private header to check
# separate VRAM ownership, register masks, and boundary addressing.
add_executable(test_core_internal test_core_internal.c)
target_link_libraries(test_core_internal PRIVATE vdrip9958)
target_include_directories(test_core_internal PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME core_internal COMMAND test_core_internal)

# Unit 2 rendering smoke test: public-API coverage of the render path.
add_executable(test_render test_render.c)
target_link_libraries(test_render PRIVATE vdrip9958)
add_test(NAME render COMMAND test_render)

# Unit 3 command-engine smoke test: public-API coverage of the command path.
add_executable(test_commands test_commands.c)
target_link_libraries(test_commands PRIVATE vdrip9958)
add_test(NAME commands COMMAND test_commands)

# Project static checks (naming, artifacts, dependencies, required files).
find_program(SH_PROGRAM sh)
if (SH_PROGRAM)
  add_test(NAME project_check
           COMMAND ${SH_PROGRAM} ${CMAKE_SOURCE_DIR}/tools/check_project.sh)
endif()
