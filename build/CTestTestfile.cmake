# CMake generated Testfile for 
# Source directory: /root/code/phoenix-shm
# Build directory: /root/code/phoenix-shm/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(unit_tests "/root/code/phoenix-shm/build/unit_tests")
set_tests_properties(unit_tests PROPERTIES  _BACKTRACE_TRIPLES "/root/code/phoenix-shm/CMakeLists.txt;115;add_test;/root/code/phoenix-shm/CMakeLists.txt;0;")
add_test(hello_world "/root/code/phoenix-shm/build/integration_hello_world")
set_tests_properties(hello_world PROPERTIES  _BACKTRACE_TRIPLES "/root/code/phoenix-shm/CMakeLists.txt;116;add_test;/root/code/phoenix-shm/CMakeLists.txt;0;")
add_test(stress_test "/root/code/phoenix-shm/build/stress_test")
set_tests_properties(stress_test PROPERTIES  _BACKTRACE_TRIPLES "/root/code/phoenix-shm/CMakeLists.txt;117;add_test;/root/code/phoenix-shm/CMakeLists.txt;0;")
