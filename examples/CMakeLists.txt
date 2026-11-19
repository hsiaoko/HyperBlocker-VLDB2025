# The hyperdeduce command-line driver.

add_executable(hyperdeduce_cli hyperdeduce.cc)
target_link_libraries(hyperdeduce_cli PRIVATE hyperdeduce gflags)
set_target_properties(hyperdeduce_cli PROPERTIES OUTPUT_NAME hyperdeduce)