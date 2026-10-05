# ===========================================================================
#  Nova::Scene
# ===========================================================================
#  Scene-side types: cameras, transform hierarchies later. Depends on Core for
#  the Window/event surface and Math for poses and projection. Core never
#  depends back, so Scene cannot become a circular drag.
# ===========================================================================

nova_add_module(Scene
    SOURCES
        src/Camera.cpp
        src/FPSCameraController.cpp
    HEADERS
        Camera.h
        FPSCameraController.h
    DEPENDS
        Nova::Core
        Nova::Math
)
