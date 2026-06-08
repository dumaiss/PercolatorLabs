

add_library(vdrip_vdp vrEmuTms9918.c)
add_library(vdrip_vdp_util vrEmuTms9918Util.c)

if (WIN32)
  if (BUILD_SHARED_LIBS)
     add_definitions(-DVR_TMS9918_EMU_COMPILING_DLL)
   endif()
endif()

target_include_directories (vdrip_vdp INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(vdrip_vdp_util PUBLIC vdrip_vdp)
