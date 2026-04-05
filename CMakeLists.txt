cmake_minimum_required(VERSION 3.16)

project(ModemBridgeTray VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS 
    Core 
    Gui 
    Widgets 
    SerialPort 
    Network 
    WebSockets 
    WebChannel 
    HttpServer
    Xml
)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SSH REQUIRED libssh)


# Grab all our source files dynamically
file(GLOB_RECURSE PROJECT_SOURCES CONFIGURE_DEPENDS
    "src/*.cpp"
    "src/*.h"
    "res/*.qrc"
)

# --- PLATFORM SPECIFIC CONFIGURATIONS ---

if(APPLE)
    message(STATUS "Configuring for macOS (Apple Silicon / Intel)")
    # We will eventually add the .icns file and Info.plist properties here
    # to make it a proper Mac app bundle.
    
elseif(WIN32)
    message(STATUS "Configuring for Windows")
    # We will eventually add a Windows Resource (.rc) file here 
    # so the .exe file itself has an icon in Task Manager.
    
elseif(UNIX AND NOT APPLE)
    message(STATUS "Configuring for Linux (Debian AMD/ARM)")
    # Linux doesn't use bundles or WIN32 hiding, it just runs.
    # We might add install directives here later to put the executable in /usr/local/bin
endif()

# --- END PLATFORM SPECIFIC ---

# Create the executable
add_executable(${PROJECT_NAME} WIN32 MACOSX_BUNDLE ${PROJECT_SOURCES})

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::SerialPort
    Qt6::Network
    Qt6::Xml
    Qt6::WebSockets   
    Qt6::WebChannel    
    Qt6::HttpServer
    ${SSH_LIBRARIES}    
)

target_include_directories(${PROJECT_NAME} PRIVATE ${SSH_INCLUDE_DIRS})
