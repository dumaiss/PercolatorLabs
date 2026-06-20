# Single uniquely named V9958 emulator library target.
add_library(vdrip9958
  vDrip9958.c
  vDrip9958_render.c
  vDrip9958_commands.c
)

if (WIN32 AND BUILD_SHARED_LIBS)
  target_compile_definitions(vdrip9958 PRIVATE VDRIP9958_COMPILING_DLL)
endif()

# The source directory is the public include path for consumers.
# Only vDrip9958.h is supported public API; vDrip9958_internal.h is private.
target_include_directories(vdrip9958 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
