# ===========================================================================
#  Nova::Math
# ===========================================================================
#  Hand-rolled math types used by Renderer, Scene and anything higher that
#  reasons about positions, transforms or viewports. Pure CPU, no OS or GPU
#  dependency - testable anywhere, no device needed.
#
#  API is GL::M-adjacent by request but PascalCase per house style:
#      glm::dot(a,b)       ->  a.Dot(b)
#      glm::normalize(v)   ->  v.Normalized()
#      glm::perspective()  ->  Mat4::Perspective()
#
#  Headers carry the implementations inline, with one compile-probe TU so the
#  umbrella header builds on its own.
# ===========================================================================

nova_add_module(Math
    SOURCES
        src/Math.cpp
    HEADERS
        Math.h
        Mat4.h
        Vec2.h
        Vec3.h
        Vec4.h
        Transform.h
        MathUtils.h
)
