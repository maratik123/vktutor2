cmake_minimum_required(VERSION 3.21)

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
    cmake_path(SET current-shader-path "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SHADER}")
    cmake_path(SET current-output-path "${CMAKE_BINARY_DIR}/shaders/${SHADER}.spv")
    cmake_path(GET current-output-path PARENT_PATH current-output-dir)
    file(MAKE_DIRECTORY ${current-output-dir})
    add_custom_command(
        OUTPUT ${current-output-path}
        COMMAND ${Vulkan_GLSLC_EXECUTABLE} -O -o ${current-output-path} ${current-shader-path}
        DEPENDS ${current-shader-path}
        IMPLICIT_DEPENDS CXX ${current-shader-path}
        VERBATIM
    )
    set_source_files_properties(${current-output-path} PROPERTIES GENERATED TRUE)
    set(local_targets ${${TARGET}})
    list(APPEND local_targets ${current-output-path})
    set(${TARGET} ${local_targets} PARENT_SCOPE)
endfunction(add_shader)

function(add_asset TARGET ASSET)
    cmake_path(SET current-asset-path "${CMAKE_CURRENT_SOURCE_DIR}/${ASSET}")
    cmake_path(SET current-output-path "${CMAKE_BINARY_DIR}/${ASSET}")
    cmake_path(GET current-output-path PARENT_PATH current-output-dir)
    file(MAKE_DIRECTORY ${current-output-dir})
    add_custom_command(
        OUTPUT ${current-output-path}
        COMMAND ${CMAKE_COMMAND} -E copy ${current-asset-path} ${current-output-path}
        DEPENDS ${current-asset-path}
        IMPLICIT_DEPENDS CXX ${current-asset-path}
    )
    set_source_files_properties(${current-output-path} PROPERTIES
        GENERATED TRUE
        HEADER_FILE_ONLY TRUE
    )
    set(local_targets ${${TARGET}})
    list(APPEND local_targets ${current-output-path})
    set(${TARGET} ${local_targets} PARENT_SCOPE)
endfunction(add_asset)

function(add_resources RESOURCES TARGETS)
    set(local_resources ${${RESOURCES}})
    set(input_targets ${TARGETS})
    list(REMOVE_DUPLICATES input_targets)
    set(TARGET_DIRS)
    foreach(it ${input_targets})
        cmake_path(GET it PARENT_PATH TARGET_DIR)
        list(APPEND TARGET_DIRS ${TARGET_DIR})
    endforeach()
    list(REMOVE_DUPLICATES TARGET_DIRS)
    foreach(it ${TARGET_DIRS})
        set(LOCAL_TARGETS)
        foreach(it2 ${input_targets})
            cmake_path(GET it2 PARENT_PATH it2_path)
            if(it2_path STREQUAL it)
                list(APPEND LOCAL_TARGETS ${it2})
            endif()
        endforeach()
        cmake_path(SET qrc_name "${it}.qrc")

        if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
            add_custom_command(
                OUTPUT ${qrc_name}
                ${QT_CMAKE_EXPORT_NAMESPACE}::rcc
                ARGS -o ${qrc_name} --project
                WORKING_DIRECTORY ${it}
                DEPENDS ${LOCAL_TARGETS}
            )
        else()
            add_custom_command(
                OUTPUT ${qrc_name}
                COMMAND ${Qt5Core_RCC_EXECUTABLE}
                ARGS -o ${qrc_name} --project
                WORKING_DIRECTORY ${it}
                DEPENDS ${LOCAL_TARGETS}
            )
        endif()
        get_filename_component(qrc_filename ${qrc_name} NAME_WE)
        cmake_path(SET new_dir "${it}.rcc.dir")
        file(MAKE_DIRECTORY ${new_dir})
        set(NEW_LOCAL_TARGETS)
        foreach(it2 ${LOCAL_TARGETS})
            cmake_path(GET it2 FILENAME file_local_name)
            cmake_path(SET new_file "${new_dir}/${file_local_name}")
            add_custom_command(
                OUTPUT ${new_file}
                COMMAND ${CMAKE_COMMAND} -E copy ${it2} ${new_file}
                DEPENDS ${it2}
            )
            set_source_files_properties(${new_file} PROPERTIES
                GENERATED TRUE
            )
            list(APPEND NEW_LOCAL_TARGETS ${new_file})
        endforeach()
        cmake_path(SET qrc_new_file "${new_dir}/${qrc_filename}.qrc")
        add_custom_command(
            OUTPUT ${qrc_new_file}
            COMMAND ${CMAKE_COMMAND} -E copy ${qrc_name} ${qrc_new_file}
            DEPENDS ${qrc_name} ${NEW_LOCAL_TARGETS}
        )
        set_source_files_properties(${qrc_name} PROPERTIES
            GENERATED TRUE
            SKIP_AUTORCC ON
        )
        set_source_files_properties(${qrc_new_file} PROPERTIES
            GENERATED TRUE
            SKIP_AUTORCC ON
        )
        qt_add_resources(local_resources ${qrc_new_file} OPTIONS -root "/${qrc_filename}")
    endforeach()
    set(${RESOURCES} ${local_resources} PARENT_SCOPE)
endfunction(add_resources)

function(add_texture TARGET TEXTURE)
    add_asset(${TARGET} textures/${TEXTURE})
endfunction(add_texture)

function(add_model TARGET MODEL)
    add_asset(${TARGET} models/${MODEL})
endfunction(add_model)

find_package(QT NAMES Qt6 Qt5 COMPONENTS Gui VulkanSupport REQUIRED)
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Gui VulkanSupport REQUIRED)
find_package(Vulkan REQUIRED)
find_package(glm REQUIRED)

include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)


add_compile_definitions(
    QT_NO_CAST_FROM_ASCII
    QT_NO_CAST_TO_ASCII
    QT_NO_CAST_FROM_BYTEARRAY
)

set(resources)
add_shader(resources shader.vert)
add_shader(resources shader.frag)
add_asset(resources textures/texture.jpg)

set(PROJECT_SOURCES
    main.cpp
    mainwindow.cpp mainwindow.h
    closeeventfilter.cpp closeeventfilter.h
    constlatin1string.cpp constlatin1string.h
    vulkanrenderer.cpp vulkanrenderer.h
    queuefamilyindices.cpp queuefamilyindices.h
    utils.cpp utils.h
    externals/scope_guard/scope_guard.hpp
)

add_resources(GENERATED_SOURCES "${resources}")

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(vktutor2
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
        ${GENERATED_SOURCES}
    )
else()
    add_executable(vktutor2
        ${PROJECT_SOURCES}
        ${GENERATED_SOURCES}
    )
endif()

if( ipo_supported )
    message(STATUS "IPO / LTO enabled")
    set_property(TARGET vktutor2 PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
else()
    message(STATUS "IPO / LTO not supported: <${ipo_error}>")
endif()

target_link_libraries(vktutor2 PRIVATE
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${Qt_VERSION_MAJOR}::VulkanSupport
    Vulkan::Headers
    glm::glm
)

target_link_options(vktutor2 PRIVATE
    "LINKER:-O1"
    "LINKER:--as-needed"
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(vktutor2)
endif()
