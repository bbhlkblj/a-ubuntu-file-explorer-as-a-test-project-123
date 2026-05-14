cmake_minimum_required(VERSION 4.2)
project(fileExplorer)

set(CMAKE_CXX_STANDARD 20)

find_package(FLTK REQUIRED)
find_package(X11 REQUIRED)

add_executable(fileExplorer main.cpp
        SmartWindow.cpp
        SmartWindow.h
        RibbonBar.cpp
        RibbonBar.h
        ResizeDivider.cpp
        ResizeDivider.h
        SidePanel.cpp
        SidePanel.h
        ModernInput.cpp
        ModernInput.h
        ModernButton.cpp
        ModernButton.h
        FileView.cpp
        FileView.h
        X11Clipboard.cpp
        X11Clipboard.h
        PinStore.cpp
        PinStore.h
        StatusBar.cpp
        StatusBar.h
)

target_link_libraries(fileExplorer fltk fltk_images ${X11_LIBRARIES})
