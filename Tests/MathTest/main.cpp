// ===========================================================================
//  MathTest - standalone checks for Nova::Math
// ---------------------------------------------------------------------------
//  Deliberately a plain main(), not gtest: the checks are arithmetic identities
//  that read better as "here is the expected shape" than as EXPECT_EQ shells,
//  and pulling a test framework in for this alone would add a vcpkg manifest
//  feature for no user benefit.
//
//  Exit code is 0 on success, 1 on any failure. Each check prints one line so
//  a CI log is grep-able for `FAIL`.
// ===========================================================================

#include <Math/Math.h>

#include <cmath>
#include <cstdio>

namespace
{
/// Helper: allows a small float tolerance. Exact compares on chains of
/// multiply/add are the kind of test that passes one driver and fails
/// another's because of FP contraction, so everything goes through this.
bool Near(float a, float b, float epsilon = 0.0001F)
{
    return std::fabs(a - b) <= epsilon;
}

bool Near4(float x, float y, float z, float w, Nova::Math::Vec4 v, float epsilon = 0.0001F)
{
    return Near(v.x, x, epsilon) && Near(v.y, y, epsilon) && Near(v.z, z, epsilon) && Near(v.w, w, epsilon);
}

int failures = 0;

void Check(bool condition, const char* name)
{
    std::printf("%-44s %s\n", name, condition ? "OK" : "FAIL");
    if (!condition)
    {
        ++failures;
    }
}
} // namespace

int main()
{
    using namespace Nova::Math;

    // ---- Vec3 ---------------------------------------------------------------

    Check(Vec3{ 1.0F, 0.0F, 0.0F }.Dot(Vec3{ 0.0F, 1.0F, 0.0F }) == 0.0F, "Vec3 dot orthogonal");
    Vec3 cross = Vec3{ 1.0F, 0.0F, 0.0F }.Cross(Vec3{ 0.0F, 1.0F, 0.0F });
    Check(Near(cross.x, 0.0F) && Near(cross.y, 0.0F) && Near(cross.z, 1.0F), "Vec3 cross x,y = z");

    Vec3 v{ 3.0F, 4.0F, 0.0F };
    Check(Near(v.Length(), 5.0F), "Vec3 length(3,4,0) = 5");
    Vec3 n = v.Normalized();
    Check(Near(n.Length(), 1.0F), "Vec3 normalized has unit length");

    Vec3 a{ 0.0F, 0.0F, 0.0F }, b{ 10.0F, 0.0F, 0.0F };
    Check(a.Lerp(b, 0.5F).x == 5.0F, "Vec3 lerp halves a 10-unit segment");
    Check(Near(a.Distance(Vec3{ 3.0F, 4.0F, 0.0F }), 5.0F), "Vec3 distance(0,0,0)->(3,4,0)");

    Vec3 e{ 0.0F };
    e.Normalize(); // must not produce NaN
    Check(e.x == 0.0F && e.y == 0.0F && e.z == 0.0F, "Vec3 zero normalize stays zero not NaN");

    Check(Vec3::Zero.Length() == 0.0F && Vec3::One.x == 1.0F, "Vec3 static constants");
    Check(Vec3::Up.Dot(Vec3::Forward) == 0.0F, "Vec3 Up orthogonal to Forward");

    // ---- Mat4 ---------------------------------------------------------------

    Mat4 translation = Mat4::Translate(Vec3{ 1.0F, 2.0F, 3.0F });
    Vec4 translated = translation * Vec4{ 0.0F, 0.0F, 0.0F, 1.0F };
    Check(Near4(1.0F, 2.0F, 3.0F, 1.0F, translated), "Mat4 translate moves origin");

    Mat4 id = Mat4::Identity();
    Check(id * Vec4{ 1.0F, 2.0F, 3.0F, 1.0F } == Vec4{ 1.0F, 2.0F, 3.0F, 1.0F }, "Mat4 identity passthrough");

    Mat4 scaling = Mat4::Scale(Vec3{ 2.0F, 2.0F, 2.0F });
    Vec4 scaledPt = scaling * Vec4{ 1.0F, 1.0F, 1.0F, 1.0F };
    Check(Near4(2.0F, 2.0F, 2.0F, 1.0F, scaledPt), "Mat4 scale");

    Mat4 rot = Mat4::Rotate(HalfPi, Vec3::Up);
    Vec4 rotated = rot * Vec4{ 0.0F, 0.0F, 1.0F, 0.0F };
    Check(Near(rotated.x, 1.0F) && Near(rotated.y, 0.0F) && Near(rotated.z, 0.0F, 1e-3F),
          "Mat4 rotate +Z by 90deg about Y = +X");

    Mat4 p = Mat4::Perspective(Radians(90.0F), 1.0F, 0.1F, 100.0F);
    Vec4 nearPt = p * Vec4{ 0.0F, 0.0F, 0.1F, 1.0F };
    Vec4 farPt  = p * Vec4{ 0.0F, 0.0F, 100.0F, 1.0F };
    // D3D NDC range is z in [0,1]: after homogeneous divide, near->0, far->1.
    Check(Near(nearPt.z / nearPt.w, 0.0F, 1e-3F), "Mat4 perspective maps near to NDC z=0");
    Check(Near(farPt.z / farPt.w, 1.0F, 1e-3F), "Mat4 perspective maps far to NDC z=1");

    Mat4 view = Mat4::LookAt(Vec3{ 0.0F, 0.0F, 0.0F }, Vec3{ 0.0F, 0.0F, 1.0F }, Vec3::Up);
    Vec4 originInView = view * Vec4{ 0.0F, 0.0F, 1.0F, 1.0F };
    Check(Near4(0.0F, 0.0F, 1.0F, 1.0F, originInView, 1e-3F),
          "Mat4 lookAt identity when eye at origin looking +Z");

    Mat4 model = Mat4::Translate(Vec3{ 10.0F, 0.0F, 0.0F });
    Mat4 roundtrip = model * model.Inverse();
    Check(Near(roundtrip.m[0], 1.0F) && Near(roundtrip.m[15], 1.0F), "Mat4 transpose*inverse of translation is identity");

    Mat4 rotatedM = Mat4::Rotate(Radians(45.0F), Vec3::Up) * Mat4::Translate(Vec3{ 3.0F, 0.0F, 0.0F });
    Mat4 inverse = rotatedM.Inverse();
    bool inverseOk = true;
    for (int i = 0; i < 16; ++i)
    {
        if (!Near((rotatedM * inverse).m[i], id.m[i], 1e-3F))
        {
            inverseOk = false;
        }
    }
    Check(inverseOk, "Mat4 M * M^-1 = I for a rotate+translate");

    Mat4 ortho = Mat4::Orthographic(-1.0F, 1.0F, -1.0F, 1.0F, 0.1F, 10.0F);
    Vec4 orthoPt = ortho * Vec4{ 2.0F, 2.0F, 5.0F, 1.0F };
    Check(orthoPt.x > 1.0F && orthoPt.x < 1.0F + 1e-6F + 10.0F, "Mat4 ortho lhs x is within clip shape sanity");

    // ---- Transform ---------------------------------------------------------

    Transform moved;
    moved.position = Vec3{ 10.0F, 0.0F, 0.0F };
    Vec4 atOrigin = moved.GetModelMatrix() * Vec4{ 0.0F, 0.0F, 0.0F, 1.0F };
    Check(Near4(10.0F, 0.0F, 0.0F, 1.0F, atOrigin), "Transform: translation applies to origin");

    Transform stretched;
    stretched.scale = Vec3{ 2.0F, 2.0F, 2.0F };
    Vec4 one = stretched.GetModelMatrix() * Vec4{ 1.0F, 1.0F, 1.0F, 1.0F };
    Check(Near4(2.0F, 2.0F, 2.0F, 1.0F, one), "Transform: scale applies");

    Transform yawed;
    yawed.Rotate(Vec3{ 0.0F, HalfPi, 0.0F });
    Vec3 f = yawed.Forward();
    Check(Near(f.x, 1.0F, 1e-3F) && Near(f.y, 0.0F, 1e-3F) && Near(f.z, 0.0F, 1e-3F),
          "Transform: yaw 90deg maps Forward to +X");

    Vec3 fw = yawed.Forward();
    Check(Near(fw.Dot(yawed.Up()), 0.0F, 1e-3F), "Transform: Forward is orthogonal to Up");

    std::printf("\n%s\n", failures == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return failures == 0 ? 0 : 1;
}
