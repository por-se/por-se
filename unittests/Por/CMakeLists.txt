add_klee_unit_test(PorTest
  csd.cpp
  thread_id.cpp
  unfolding.cpp)
target_link_libraries(PorTest PRIVATE kleeCore kleeFingerprint)
target_compile_definitions(PorTest PUBLIC "-DLIBPOR_CHECKED")
