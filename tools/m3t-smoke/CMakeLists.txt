# Proof-of-port smoke tool: consumes M3T strictly via find_package() from
# the vcpkg overlay port (vcpkg-overlay-ports/m3t, docs/m3t-port.md). The
# exported m3t target propagates C++17 and all transitive dependencies.
find_package(M3T CONFIG REQUIRED)

add_executable(m3t_port_smoke main.cpp)
target_link_libraries(m3t_port_smoke PRIVATE m3t)
