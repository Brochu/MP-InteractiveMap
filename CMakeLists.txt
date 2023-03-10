cmake_minimum_required (VERSION 3.10)

project (MP-InteractiveMap)
set (CMAKE_EXPORT_COMPILE_COMMANDS ON)
set (CMAKE_CXX_STANDARD 20)

set (headers
    ${CMAKE_CURRENT_LIST_DIR}/include/d3dx12.h
    ${CMAKE_CURRENT_LIST_DIR}/include/stdafx.h

    ${CMAKE_CURRENT_LIST_DIR}/include/ImageIO.h
    ${CMAKE_CURRENT_LIST_DIR}/include/Utility.h
    ${CMAKE_CURRENT_LIST_DIR}/include/Win32Application.h
    ${CMAKE_CURRENT_LIST_DIR}/include/DXSample.h
    ${CMAKE_CURRENT_LIST_DIR}/include/DXSampleHelper.h
    ${CMAKE_CURRENT_LIST_DIR}/include/MapViewer.h
)
set (imgui_headers
#    ${CMAKE_CURRENT_LIST_DIR}/include/imgui/imconfig.h
#    ${CMAKE_CURRENT_LIST_DIR}/include/imgui/imgui.h
#    ${CMAKE_CURRENT_LIST_DIR}/include/imgui/imgui_impl_dx12.h
#    ${CMAKE_CURRENT_LIST_DIR}/include/imgui/imgui_impl_win32.h
#    ${CMAKE_CURRENT_LIST_DIR}/include/imgui/imgui_internal.h
#    ${CMAKE_CURRENT_LIST_DIR}/include/imgui/imstb_rectpack.h
#    ${CMAKE_CURRENT_LIST_DIR}/include/imgui/imstb_textedit.h
#    ${CMAKE_CURRENT_LIST_DIR}/include/imgui/imstb_truetype.h
)

set (source
    ${CMAKE_CURRENT_LIST_DIR}/src/Main.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/stdafx.cpp

    ${CMAKE_CURRENT_LIST_DIR}/src/ImageIO.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/Utility.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/Win32Application.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/DXSample.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/MapViewer.cpp
)
set (imgui_source
#    ${CMAKE_CURRENT_LIST_DIR}/src/imgui/imgui.cpp
#    ${CMAKE_CURRENT_LIST_DIR}/src/imgui/imgui_demo.cpp
#    ${CMAKE_CURRENT_LIST_DIR}/src/imgui/imgui_draw.cpp
#    ${CMAKE_CURRENT_LIST_DIR}/src/imgui/imgui_impl_dx12.cpp
#    ${CMAKE_CURRENT_LIST_DIR}/src/imgui/imgui_impl_win32.cpp
#    ${CMAKE_CURRENT_LIST_DIR}/src/imgui/imgui_tables.cpp
#    ${CMAKE_CURRENT_LIST_DIR}/src/imgui/imgui_widgets.cpp
)

set (libs
    d3d12.lib
    d3dcompiler.lib
    dxgi.lib
    dxguid.lib

    assimp-vc143-mt.lib
)

add_executable (MP-InteractiveMap WIN32
    ${source} ${imgui_source}
    ${headers} ${imgui_headers}
)

target_include_directories(MP-InteractiveMap
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/include
        ${CMAKE_CURRENT_LIST_DIR}/include/imgui
        ${CMAKE_CURRENT_LIST_DIR}/tracy/public/tracy
)
target_link_directories(MP-InteractiveMap
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/libs
)

target_link_libraries(MP-InteractiveMap ${libs})

set_target_properties(MP-InteractiveMap
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY       "${CMAKE_CURRENT_LIST_DIR}/"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_LIST_DIR}/"
)
