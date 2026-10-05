# ===========================================================================
#  Tests
# ===========================================================================
#  Stands for plain executables built on top of engine modules: raw main()
#  checks with zero external test-framework overhead, one binary per module.
#  They are a sanity gate for a developer machine and work equally well as a
#  smoke run in CI, where gtest would buy us structure but cost a gtest
#  ingestion per check. MathTest is the shape of the future - the gtest-based
#  suite arrives with the scene graph and reflection serialisation, where
#  comparison assertions actually earn their keep.
# ===========================================================================

add_executable(MathTest MathTest/main.cpp)

nova_configure_target(MathTest)
nova_apply_warnings(MathTest)

target_link_libraries(MathTest PRIVATE Nova::Math)
