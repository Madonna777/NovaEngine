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


# ---------------------------------------------------------------------------
#  MeshTest
# ---------------------------------------------------------------------------
# WHY MeshTest LINKS Nova::Renderer: it checks the primitive generators, which
# are CPU geometry with no device involvement. Linking the whole module is what
# makes it possible to test geometry without a GPU - and it works because the
# generators return MeshData and never touch a resource. If a future change moves
# device work into a generator, this test fails to link, which is the correct
# outcome: it means the geometry is no longer independently testable.
#
# WHY IT IS NOT NAMED RendererTest: there is exactly one thing in Renderer worth
# testing without a device, and a name that claims to cover the module would be
# promising coverage that does not exist.
add_executable(MeshTest MeshTest/main.cpp)

nova_configure_target(MeshTest)
nova_apply_warnings(MeshTest)

target_link_libraries(MeshTest PRIVATE Nova::Math Nova::Renderer)

# ---------------------------------------------------------------------------
#  CTest registration
# ---------------------------------------------------------------------------
# WHY REGISTER AT ALL: enable_testing() in the root only creates the harness.
# An unregistered test executable is built by everyone and run by nobody, which
# is the worst of both - it costs CI time and gates nothing. `ctest --output-on-failure`
# becomes the single command that answers "is this machine still sane".
add_test(NAME MathTest COMMAND MathTest)
add_test(NAME MeshTest COMMAND MeshTest)