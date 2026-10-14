# The tool that makes src/app/qt/lightprobe/sky.ktx (README.md says how to run it).
#
# A project of its own, not part of the application's build: it needs Qt's
# private Quick3D modules and the IBL baker, which the application itself does
# not link, and it is run when the probe is to change - which is not often -
# not on every build.
cmake_minimum_required(VERSION 3.21)
project(cubeulator_lightprobe LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS
    Core Gui Quick3D Quick3DPrivate Quick3DHelpers Quick3DHelpersPrivate
    Quick3DIblBaker Quick3DIblBakerPrivate)

qt_standard_project_setup()

qt_add_executable(bake_probe bake_probe.cpp)
target_link_libraries(bake_probe PRIVATE
    Qt6::Core Qt6::Gui
    Qt6::Quick3D Qt6::Quick3DPrivate
    Qt6::Quick3DHelpers Qt6::Quick3DHelpersPrivate
    Qt6::Quick3DIblBaker Qt6::Quick3DIblBakerPrivate)
