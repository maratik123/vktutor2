cmake_minimum_required(VERSION 3.5)

project(vktutor2 VERSION 0.1 LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 COMPONENTS Gui REQUIRED)
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Gui REQUIRED)

add_compile_definitions(
    QT_NO_CAST_FROM_ASCII
    QT_NO_CAST_TO_ASCII
    QT_NO_CAST_FROM_BYTEARRAY
    )

set(PROJECT_SOURCES
        main.cpp
        mainwindow.cpp mainwindow.h
        closeeventfilter.cpp closeeventfilter.h
        constlatin1string.h
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

target_link_libraries(vktutor2 PRIVATE Qt${QT_VERSION_MAJOR}::Gui)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(vktutor2)
endif()
