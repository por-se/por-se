add_klee_unit_test(PseudoallocTest
  allocate.cpp
  randomtest.cpp
  sample.cpp
  stacktest.cpp)
target_compile_definitions(PseudoallocTest PUBLIC "-DPSEUDOALLOC_CHECKED" "-DUSE_PSEUDOALLOC" "-DUSE_GTEST_INSTEAD_OF_MAIN")
target_compile_definitions(PseudoallocTest PRIVATE ${KLEE_COMPONENT_CXX_DEFINES})
target_compile_options(PseudoallocTest PRIVATE ${KLEE_COMPONENT_CXX_FLAGS})
