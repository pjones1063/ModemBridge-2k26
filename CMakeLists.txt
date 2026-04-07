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

# --- MacOS Apple Silicon Homebrew Fix ---
if(APPLE)
    # Tell CMake to explicitly look in Homebrew's paths so it finds the arm64 binary
    list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew")
    list(APPEND CMAKE_LIBRARY_PATH "/opt/homebrew/lib")
    list(APPEND CMAKE_INCLUDE_PATH "/opt/homebrew/include")
endif()

# Find LibSSH natively (Works on Windows, Linux, and macOS)
find_package(libssh REQUIRED)

# --- ICON SETUP ---
# Updated path: Point to the icons subdirectory
set(MACOS_ICON "res/icons/app_icon.icns")

# Tell CMake to put the .icns file into the "Resources" directory of the macOS .app bundle
set_source_files_properties(${MACOS_ICON} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")

# Grab all our source files dynamically
file(GLOB_RECURSE PROJECT_SOURCES CONFIGURE_DEPENDS
    "src/*.cpp"
    "src/*.h"
    "res/*.qrc"
)

# --- PLATFORM SPECIFIC CONFIGURATIONS ---

if(APPLE)
    message(STATUS "Configuring for macOS (Apple Silicon / Intel)")
    # Append the icon to the source list so CMake knows to process it
    list(APPEND PROJECT_SOURCES ${MACOS_ICON})
    
elseif(WIN32)
    message(STATUS "Configuring for Windows")
    # We will eventually add a Windows Resource (.rc) file here 
    # so the .exe file itself has an icon in Task Manager.
elseif(UNIX AND NOT APPLE)
    message(STATUS "Configuring for Linux (Debian AMD/ARM)")
endif()

# --- END PLATFORM SPECIFIC ---

# Create the executable
add_executable(${PROJECT_NAME} WIN32 MACOSX_BUNDLE ${PROJECT_SOURCES})

# --- MACOS BUNDLE PROPERTIES ---
if(APPLE)
    # This automatically writes the CFBundleIconFile property into the generated Info.plist
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE_ICON_FILE "app_icon.icns"
        MACOSX_BUNDLE_BUNDLE_NAME "ModemBridge"
    )
endif()

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
    ssh    
)