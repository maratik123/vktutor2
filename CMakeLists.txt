cmake_minimum_required(VERSION 3.5)

project(vktutor2 VERSION 0.1 LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN 1)

function(add_shader TARGET SHADER)
    find_program(GLSLC glslc)
    set(current-shader-path ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SHADER})
    set(current-output-path ${CMAKE_BINARY_DIR}/shaders/${SHADER}.spv)
    get_filename_component(current-output-dir ${current-output-path} DIRECTORY)
    file(MAKE_DIRECTORY ${current-output-dir})
    add_custom_command(
            OUTPUT ${current-output-path}
            COMMAND ${GLSLC} -O -o ${current-output-path} ${current-shader-path}
            DEPENDS ${current-shader-path}
            IMPLICIT_DEPENDS CXX ${current-shader-path}
            VERBATIM)
    set_source_files_properties(${current-output-path} PROPERTIES GENERATED TRUE)
    target_sources(${TARGET} PRIVATE ${current-output-path})
endfunction(add_shader)

function(add_asset TARGET ASSET)
    set(current-asset-path ${CMAKE_CURRENT_SOURCE_DIR}/${ASSET})
    set(current-output-path ${CMAKE_BINARY_DIR}/${ASSET})
    get_filename_component(current-output-dir ${current-output-path} DIRECTORY)
    file(MAKE_DIRECTORY ${current-output-dir})
    add_custom_command(
            OUTPUT ${current-output-path}
            COMMAND ${CMAKE_COMMAND} -E copy ${current-asset-path} ${current-output-path}
            DEPENDS ${current-asset-path}
            IMPLICIT_DEPENDS CXX ${current-asset-path})
    set_source_files_properties(${current-output-path} PROPERTIES
            GENERATED TRUE
            HEADER_FILE_ONLY TRUE)
    target_sources(${TARGET} PRIVATE ${current-output-path})
endfunction(add_asset)

function(add_texture TARGET TEXTURE)
    add_asset(${TARGET} textures/${TEXTURE})
endfunction(add_texture)

function(add_model TARGET MODEL)
    add_asset(${TARGET} models/${MODEL})
endfunction(add_model)

find_package(QT NAMES Qt6 Qt5 COMPONENTS Gui REQUIRED)
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Gui REQUIRED)
find_package(glm REQUIRED)

add_compile_definitions(
    QT_NO_CAST_FROM_ASCII
    QT_NO_CAST_TO_ASCII
    QT_NO_CAST_FROM_BYTEARRAY
    GLM_FORCE_MESSAGES
    GLM_FORCE_EXPLICIT_CTOR
    )

set(PROJECT_SOURCES
        main.cpp
        mainwindow.cpp mainwindow.h
        closeeventfilter.cpp closeeventfilter.h
        constlatin1string.cpp constlatin1string.h
        vulkanrenderer.cpp vulkanrenderer.h
        queuefamilyindices.cpp queuefamilyindices.h
        utils.cpp utils.h
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(vktutor2
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
else()
    add_executable(vktutor2
        ${PROJECT_SOURCES}
    )
endif()

add_shader(vktutor2 shader.vert)
add_shader(vktutor2 shader.frag)

target_link_libraries(vktutor2 PRIVATE Qt${QT_VERSION_MAJOR}::Gui glm::glm)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(vktutor2)
endif()
