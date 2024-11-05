add_executable(test_parity test_parity.cpp ../parity.cpp)
target_include_directories(test_parity PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(test_parity PRIVATE pthread PkgConfig::LOG4C)

install(TARGETS test_parity DESTINATION bin)
