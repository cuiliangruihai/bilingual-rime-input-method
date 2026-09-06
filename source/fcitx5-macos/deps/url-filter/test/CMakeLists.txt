add_executable(testfilter testfilter.cpp)
target_link_libraries(testfilter url-filter)
target_compile_options(testfilter PRIVATE -UNDEBUG)
add_test(NAME testfilter COMMAND testfilter)

add_executable(testutil testutil.cpp)
target_link_libraries(testutil url-filter)
target_compile_options(testutil PRIVATE -UNDEBUG)
add_test(NAME testutil COMMAND testutil)
