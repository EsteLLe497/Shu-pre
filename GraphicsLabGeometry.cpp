#include "main.h"
#include "GraphicsLabGeometry.h"
#include "MeshField.h"
#include <array>
#include <random>

namespace
{
    using Vertices = std::vector<VERTEX_3D>;
    using LabLandscape::Surface;

    constexpr XMFLOAT4 timber{0.28f, 0.20f, 0.13f, 1}, siding{0.62f, 0.56f, 0.43f, 1};
    constexpr XMFLOAT4 painted{0.48f, 0.45f, 0.32f, 1}, cement{0.42f, 0.44f, 0.43f, 1};
    constexpr XMFLOAT4 dark{0.021f, 0.026f, 0.027f, 1}, yellow{0.92f, 0.58f, 0.035f, 1};
    constexpr XMFLOAT4 steel{0.52f, 0.54f, 0.55f, 1}, rust{0.22f, 0.105f, 0.047f, 1};

    // 遠景の木を作る間だけ、枝の断面を粗くし、葉は枚数を減らして一枚を大きくする。
    struct DistantTreeDetail
    {
        bool active = false;
        float leafKeep = 0.34f;
        float leafScale = 1.65f;
    } distantTree;

    struct DistantTreeScope
    {
        DistantTreeScope() { distantTree.active = true; }
        ~DistantTreeScope() { distantTree.active = false; }
    };

    XMVECTOR load(XMFLOAT3 p) { return XMLoadFloat3(&p); }

    XMFLOAT3 xyz(FXMVECTOR v)
    {
        XMFLOAT3 p;
        XMStoreFloat3(&p, v);
        return p;
    }

    Vertices& batch(LabLandscape::Batches& batches, Surface surface)
    {
        return batches[static_cast<unsigned int>(surface)];
    }

    void triangle(Vertices& out, XMFLOAT3 a, XMFLOAT3 b, XMFLOAT3 c, XMFLOAT4 color)
    {
        const auto normal = xyz(XMVector3Normalize(XMVector3Cross(load(b) - load(a), load(c) - load(a))));
        for (const auto& p : {a, b, c}) out.push_back({p, normal, color, {}});
    }

    void quad(Vertices& out, XMFLOAT3 a, XMFLOAT3 b, XMFLOAT3 c, XMFLOAT3 d, XMFLOAT4 color)
    {
        triangle(out, a, b, c, color);
        triangle(out, a, c, d, color);
    }

    void hexahedron(Vertices& out, const XMFLOAT3 (&p)[8], XMFLOAT4 color)
    {
        constexpr int faces[][4] = {{0,3,2,1}, {4,5,6,7}, {0,4,7,3}, {1,2,6,5}, {0,1,5,4}, {3,7,6,2}};
        for (const auto& face : faces) quad(out, p[face[0]], p[face[1]], p[face[2]], p[face[3]], color);
    }

    void box(Vertices& out, XMFLOAT3 low, XMFLOAT3 high, XMFLOAT4 color)
    {
        const XMFLOAT3 p[] = {
            {low.x,low.y,low.z}, {high.x,low.y,low.z}, {high.x,high.y,low.z}, {low.x,high.y,low.z},
            {low.x,low.y,high.z}, {high.x,low.y,high.z}, {high.x,high.y,high.z}, {low.x,high.y,high.z}
        };
        hexahedron(out, p, color);
    }

    void tube(Vertices& out, XMFLOAT3 start, XMFLOAT3 end, float bottom, float top, XMFLOAT4 color, int sides = 9)
    {
        const auto a = load(start), b = load(end);
        const auto axis = XMVector3Normalize(b - a);
        const auto reference = std::abs(XMVectorGetY(axis)) > 0.90f ? XMVectorSet(1,0,0,0) : XMVectorSet(0,1,0,0);
        const auto u = XMVector3Normalize(XMVector3Cross(reference, axis));
        const auto v = XMVector3Cross(axis, u);
        for (int i = 0; i < sides; ++i)
        {
            const auto radial = [&](int index)
            {
                const float angle = XM_2PI * index / sides;
                return u * std::cos(angle) + v * std::sin(angle);
            };
            const auto n = radial(i), m = radial(i + 1);
            const VERTEX_3D p[] = {
                {xyz(a + n * bottom), xyz(n), color, {static_cast<float>(i) / sides, 0}},
                {xyz(a + m * bottom), xyz(m), color, {static_cast<float>(i + 1) / sides, 0}},
                {xyz(b + m * top), xyz(m), color, {static_cast<float>(i + 1) / sides, 1}},
                {xyz(b + n * top), xyz(n), color, {static_cast<float>(i) / sides, 1}}
            };
            for (int index : {0, 1, 2, 0, 2, 3}) out.push_back(p[index]);
            triangle(out, xyz(b), p[3].Position, p[2].Position, color);
            triangle(out, xyz(a), p[1].Position, p[0].Position, color);
        }
    }

    float groundAt(const MeshField& terrain, float x, float z)
    {
        float height = 0;
        terrain.tryGetHeight(x, z, height);
        return height;
    }

    float roadCenter(float z)
    {
        const float t = std::max(0.0f, std::min(1.0f, (z - 17.0f) / 46.0f));
        return 0.34f + 10.0f * t * t * (3.0f - 2.0f * t);
    }

    void station(LabLandscape::Batches& batches)
    {
        auto& wood = batch(batches, Surface::wood);
        auto& masonry = batch(batches, Surface::concrete);
        auto& roofing = batch(batches, Surface::roof);
        auto& solid = batch(batches, Surface::solid);
        auto& metal = batch(batches, Surface::steel);

        // 地方の片面ホームを観察し、張り出した庇、低い待合室、ベンチを一体で作る。
        box(masonry, {-13.0f,-0.08f,7.94f}, {-3.12f,0.52f,12.62f}, cement);
        box(masonry, {-13.06f,0.50f,7.86f}, {-3.06f,0.62f,12.71f}, {0.57f,0.55f,0.51f,1});
        for (int i = 0; i < 3; ++i)
            box(masonry, {-3.08f + i * 0.30f,-0.02f,9.4f}, {-2.78f + i * 0.30f,0.46f - i * 0.15f,10.75f}, cement);
        box(solid, {-12.93f,0.621f,7.99f}, {-3.17f,0.632f,8.10f}, {0.73f,0.53f,0.11f,1});
        for (int i = 0; i < 87; ++i)
        {
            const float x = -12.86f + i * 0.109f;
            box(solid, {x,0.631f,8.02f}, {x+0.042f,0.643f,8.07f}, yellow);
        }

        box(wood, {-12.22f,0.62f,10.86f}, {-6.14f,2.78f,12.09f}, siding);
        box(wood, {-12.22f,0.62f,8.98f}, {-11.94f,2.78f,11.06f}, siding);
        box(wood, {-6.41f,0.62f,8.98f}, {-6.14f,2.78f,11.06f}, siding);
        box(wood, {-12.02f,0.62f,8.94f}, {-6.26f,1.58f,9.08f}, siding);
        box(wood, {-12.02f,2.46f,8.94f}, {-6.26f,2.78f,9.09f}, siding);
        for (float x : {-11.96f,-10.95f,-9.18f,-6.28f})
            box(wood, {x-0.075f,0.62f,8.89f}, {x+0.075f,2.76f,9.07f}, timber);

        // 窓ガラスは濃い青い面と木枠で奥行きを出す。待合室の開口は面で塞がない。
        for (float x : {-11.72f, -10.78f})
        {
            box(solid, {x,1.69f,8.966f}, {x+0.74f,2.41f,8.981f}, {0.075f,0.13f,0.17f,1});
            for (float y : {1.68f,2.38f})
                box(wood, {x-0.04f,y,8.91f}, {x+0.78f,y+0.045f,8.98f}, painted);
            for (float offset : {0.0f,0.38f,0.74f})
                box(wood, {x+offset,1.69f,8.91f}, {x+offset+0.038f,2.41f,8.97f}, painted);
        }

        const XMFLOAT3 frontRoof[] = {
            {-12.78f,2.77f,8.21f}, {-3.64f,2.77f,8.21f}, {-3.64f,2.88f,8.21f}, {-12.78f,2.88f,8.21f},
            {-12.78f,3.40f,10.51f}, {-3.64f,3.40f,10.51f}, {-3.64f,3.51f,10.51f}, {-12.78f,3.51f,10.51f}
        };
        const XMFLOAT3 backRoof[] = {
            {-12.78f,3.40f,10.51f}, {-3.64f,3.40f,10.51f}, {-3.64f,3.51f,10.51f}, {-12.78f,3.51f,10.51f},
            {-12.78f,2.71f,12.64f}, {-3.64f,2.71f,12.64f}, {-3.64f,2.82f,12.64f}, {-12.78f,2.82f,12.64f}
        };
        hexahedron(roofing, frontRoof, {0.20f,0.25f,0.23f,1});
        hexahedron(roofing, backRoof, {0.20f,0.25f,0.23f,1});
        tube(metal, {-12.82f,3.51f,10.51f}, {-3.60f,3.51f,10.51f}, 0.058f, 0.058f, dark);
        tube(metal, {-12.83f,2.77f,8.17f}, {-3.57f,2.77f,8.17f}, 0.038f, 0.038f, dark);
        for (float x : {-12.38f,-8.10f,-4.03f})
        {
            box(wood, {x-0.086f,0.62f,8.43f}, {x+0.086f,2.83f,8.61f}, timber);
            box(masonry, {x-0.12f,0.62f,8.40f}, {x+0.12f,0.83f,8.64f}, cement);
            tube(wood, {x,2.35f,8.52f}, {x+0.50f,2.78f,8.52f}, 0.045f, 0.045f, timber, 4);
        }
        for (float x : {-5.57f,-4.41f})
            tube(metal, {x,0.63f,9.33f}, {x,1.05f,9.33f}, 0.045f,0.045f,dark,8);
        for (int i=0; i<4; ++i)
            box(wood, {-5.85f,1.02f,9.02f+i*0.095f}, {-4.16f,1.07f,9.10f+i*0.095f}, painted);
        for (int i=0; i<3; ++i)
            box(wood, {-5.85f,1.21f+i*0.094f,9.41f}, {-4.16f,1.29f+i*0.094f,9.46f}, painted);
        box(wood, {-10.20f,2.06f,8.88f}, {-8.66f,2.39f,8.95f}, timber);
        box(solid, {-10.16f,2.10f,8.866f}, {-8.70f,2.38f,8.882f}, {0.73f,0.71f,0.61f,1});
        // 架空の駅名板。テクスチャは表面一枚だけに貼る。
        auto& sign = batch(batches, Surface::stationSign);
        constexpr XMFLOAT3 front{0,0,-1};
        constexpr XMFLOAT4 white{1,1,1,1};
        const VERTEX_3D board[] = {
            {{-10.16f,2.38f,8.864f},front,white,{0,0}},
            {{ -8.70f,2.38f,8.864f},front,white,{1,0}},
            {{ -8.70f,2.10f,8.864f},front,white,{1,1}},
            {{-10.16f,2.10f,8.864f},front,white,{0,1}}
        };
        for (int i : {0,1,2,0,2,3}) sign.push_back(board[i]);
    }

    void railway(LabLandscape::Batches& batches)
    {
        auto& wood = batch(batches, Surface::wood);
        auto& gravel = batch(batches, Surface::ballast);
        auto& metal = batch(batches, Surface::steel);
        auto& solid = batch(batches, Surface::solid);
        auto& concrete = batch(batches, Surface::concrete);
        constexpr float left = -58.0f, right = 58.0f;
        quad(gravel, {left,0.013f,4.68f}, {left,0.12f,5.10f}, {right,0.12f,5.10f}, {right,0.013f,4.68f}, cement);
        quad(gravel, {left,0.12f,5.10f}, {left,0.12f,7.08f}, {right,0.12f,7.08f}, {right,0.12f,5.10f}, cement);
        quad(gravel, {left,0.12f,7.08f}, {left,0.013f,7.50f}, {right,0.013f,7.50f}, {right,0.12f,7.08f}, cement);
        for (int i = -88; i <= 88; ++i)
        {
            const float x = i * 0.65f;
            if (x > -2.44f && x < 3.20f) continue;
            const float tint = 0.82f + 0.16f * std::sin(i * 2.1f);
            box(wood, {x-0.11f,0.095f,5.05f}, {x+0.11f,0.183f,7.10f}, {tint*0.28f,tint*0.23f,tint*0.19f,1});
            for (float z : {5.43f,6.70f})
                box(metal, {x-0.065f,0.173f,z-0.10f}, {x+0.065f,0.206f,z+0.10f}, rust);
        }
        for (float z : {5.43f,6.70f})
        {
            box(metal, {left,0.17f,z-0.057f}, {right,0.197f,z+0.057f}, rust);
            box(metal, {left,0.197f,z-0.018f}, {right,0.26f,z+0.018f}, rust);
            box(metal, {left,0.254f,z-0.039f}, {right,0.280f,z+0.039f}, steel);
        }
        const XMFLOAT4 panels{0.105f,0.111f,0.109f,1};
        for (int i = 0; i < 10; ++i)
        {
            const float x = -2.21f + i * 0.515f;
            for (auto limits : {XMFLOAT2{5.08f,5.35f}, XMFLOAT2{5.51f,6.62f}, XMFLOAT2{6.78f,7.08f}})
                box(solid, {x,0.142f,limits.x}, {x+0.499f,0.266f,limits.y}, panels);
        }
        quad(concrete, {-2.23f,0.014f,4.12f},{-2.23f,0.266f,5.08f},
            {2.93f,0.266f,5.08f},{2.93f,0.014f,4.12f}, {0.30f,0.29f,0.26f,1});
        quad(concrete, {-2.23f,0.266f,7.08f},{-2.23f,0.014f,7.84f},
            {2.93f,0.014f,7.84f},{2.93f,0.266f,7.08f}, {0.30f,0.29f,0.26f,1});

        // 左右に警報機と遮断機を置き、黄黒の竿は開いた位置で固定する。
        auto& lenses = batch(batches, Surface::signalLens);
        for (float x : {-3.02f,3.84f})
        {
            const float inward = x < 0 ? 1.0f : -1.0f;
            const float z = x < 0 ? 4.04f : 7.49f;
            box(concrete, {x-0.20f,-0.025f,z-0.22f}, {x+0.20f,0.15f,z+0.22f}, cement);
            tube(metal, {x,0.11f,z}, {x,3.14f,z}, 0.055f,0.054f,dark,12);
            for (int i=0; i<7; ++i)
            {
                const float y = 0.16f + i*0.376f;
                tube(solid, {x,y,z}, {x,y+0.19f,z}, 0.057f,0.057f,yellow,12);
            }
            for (int side : {-1,1})
            {
                const auto a = XMVectorSet(x-side*0.49f,2.78f,z-0.085f,0);
                const auto b = XMVectorSet(x+side*0.49f,3.53f,z-0.085f,0);
                const auto width = XMVector3Normalize(XMVector3Cross(b-a,XMVectorSet(0,0,1,0))) * 0.079f;
                const XMFLOAT4 board{0.94f,0.66f,0.075f,1};
                quad(solid, xyz(a-width), xyz(b-width), xyz(b+width), xyz(a+width), board);
                for (int i=0; i<3; ++i)
                {
                    const auto lo = XMVectorLerp(a,b,0.12f+i*0.34f);
                    const auto hi = XMVectorLerp(a,b,0.19f+i*0.34f);
                    const auto cover = XMVectorSet(0,0,-0.002f,0);
                    quad(solid, xyz(lo-width+cover),xyz(hi-width+cover),xyz(hi+width+cover),xyz(lo+width+cover),dark);
                }
            }
            box(metal, {x-0.44f,2.21f,z-0.12f}, {x+0.44f,2.29f,z+0.075f}, dark);
            for (float dx : {-0.25f,0.25f})
            {
                tube(metal, {x+dx,2.27f,z+0.07f}, {x+dx,2.27f,z-0.19f}, 0.137f,0.14f,dark,16);
                tube(lenses, {x+dx,2.27f,z-0.191f}, {x+dx,2.27f,z-0.205f}, 0.112f,0.104f,
                    dx < 0 ? XMFLOAT4{0.42f,0.023f,0.017f,1} : XMFLOAT4{0.14f,0.007f,0.008f,1},16);
                for (int i=0; i<8; ++i)
                {
                    const float a = XM_PI*i/8, b = XM_PI*(i+1)/8;
                    quad(metal, {x+dx+std::cos(a)*0.14f,2.27f+std::sin(a)*0.14f,z-0.17f},
                        {x+dx+std::cos(b)*0.14f,2.27f+std::sin(b)*0.14f,z-0.17f},
                        {x+dx+std::cos(b)*0.14f,2.27f+std::sin(b)*0.14f,z-0.38f},
                        {x+dx+std::cos(a)*0.14f,2.27f+std::sin(a)*0.14f,z-0.38f},dark);
                }
            }

            const float gateX = x + inward * 0.30f;
            const float gateZ = z + (x < 0 ? -0.80f : 0.80f);
            box(concrete, {gateX-0.23f,-0.02f,gateZ-0.22f}, {gateX+0.23f,0.11f,gateZ+0.22f}, cement);
            box(solid, {gateX-0.15f,0.10f,gateZ-0.14f}, {gateX+0.15f,0.89f,gateZ+0.14f}, {0.31f,0.32f,0.28f,1});
            const auto hinge = XMVectorSet(gateX,0.84f,gateZ,0);
            const auto end = hinge + XMVectorSet(inward*0.76f,3.39f,0,0);
            for (int i=0; i<16; ++i)
                tube(solid, xyz(XMVectorLerp(hinge,end,i/16.0f)), xyz(XMVectorLerp(hinge,end,(i+1)/16.0f)),
                    0.050f-i*0.0013f,0.050f-(i+1)*0.0013f,(i%2) ? dark : yellow,8);
            tube(metal, {gateX,0.84f,gateZ-0.18f}, {gateX,0.84f,gateZ+0.18f}, 0.112f,0.112f,dark,12);
        }
    }

    struct Random
    {
        std::mt19937 engine{181};
        std::uniform_real_distribution<float> unit{0.0f, 1.0f};

        float operator()() { return unit(engine); }
        float between(float low, float high) { return low + (high - low) * (*this)(); }
    };

    XMFLOAT3 moved(XMFLOAT3 base, float x, float y, float z)
    {
        return {base.x+x, base.y+y, base.z+z};
    }

    XMFLOAT3 betweenPoints(XMFLOAT3 a, XMFLOAT3 b, float t)
    {
        return xyz(XMVectorLerp(load(a), load(b), t));
    }

    struct WoodyPath
    {
        XMFLOAT3 start, elbow, tip, sway;
        float phase = 0.0f;
        float tipSag = 0.0f;

        XMVECTOR sample(float t) const
        {
            const auto a = load(start), b = load(elbow), c = load(tip);
            const auto bend = (c-a)*0.42f;
            const auto center = t < 0.5f ? XMVectorHermite(a,(b-a)*0.73f,b,bend,t*2.0f)
                : XMVectorHermite(b,bend,c,(c-b)*0.77f,(t-0.5f)*2.0f);
            // 途中で曲がりの向きを反転させ、一本の弧や直線へ揃うのを避ける。
            const float blend = std::sin(t*XM_PI);
            // 片持ち梁の分布荷重を参考にした形。付け根の位置と角度を保って、先へ行くほど下がる。
            const float sag = tipSag*t*t*(6.0f+t*(-4.0f+t))/3.0f;
            return center + load(sway)*(blend*blend*std::sin(t*XM_2PI+phase))-XMVectorSet(0,sag,0,0);
        }

        XMFLOAT3 at(float t) const { return xyz(sample(t)); }
    };

    WoodyPath curvedBranch(XMFLOAT3 start, XMFLOAT3 elbow, XMFLOAT3 tip, float sway)
    {
        const auto axis = XMVector3Normalize(load(tip)-load(start));
        const auto reference = std::abs(XMVectorGetY(axis)) > 0.93f
            ? XMVectorSet(0,0,1,0) : XMVectorSet(0,1,0,0);
        return {start,elbow,tip,xyz(XMVector3Normalize(XMVector3Cross(reference,axis))*sway)};
    }

    WoodyPath curvedTrunk(XMFLOAT3 start, XMFLOAT3 elbow, XMFLOAT3 tip, float sway, Random& random)
    {
        // 幹の曲がりが同じ横方向や同じ高さに並ばないよう、曲がりの面と位置をずらす。
        const float bearing = random()*XM_2PI;
        return {start,elbow,tip,{std::sin(bearing)*sway,0,std::cos(bearing)*sway},random()*XM_2PI};
    }

    WoodyPath weightedBranch(XMFLOAT3 start, XMFLOAT3 elbow, XMFLOAT3 tip, float sway, float radius, float flexibility)
    {
        auto path = curvedBranch(start,elbow,tip,sway);
        const float dx = tip.x-start.x, dz = tip.z-start.z;
        const float reach = std::sqrt(dx*dx+dz*dz);
        // 横に長く張り出す枝や細い枝ほど垂れる。ほぼ直立する芯は樹種本来の向きを保つ。
        const float slenderness = reach/(reach+radius*24.0f);
        path.tipSag = reach*flexibility*slenderness;
        return path;
    }

    float taperedRadius(float base, float tip, float t)
    {
        return tip+(base-tip)*std::pow(1.0f-t,1.34f);
    }

    void branchPath(Vertices& wood, const WoodyPath& path,
        float radius, float tipRadius, XMFLOAT4 bark, float groundHeight = -100.0f)
    {
        // 曲線上で断面をつなぐ。樹皮の筋が、幹や枝が曲がる向きに沿って続く。
        const auto start = load(path.start), end = path.sample(1.0f);
        const auto axis = XMVector3Normalize(end-start);
        const auto reference = std::abs(XMVectorGetY(axis)) > 0.88f ? XMVectorSet(1,0,0,0) : XMVectorSet(0,1,0,0);
        const auto across = XMVector3Normalize(XMVector3Cross(reference,axis));
        int sides = radius > 0.07f ? 12 : (radius > 0.015f ? 7 : 5);
        int segments = radius > 0.07f ? 12 : (radius > 0.015f ? 6 : 3);
        if (distantTree.active)
        {
            // 数十メートル先では細い枝は1画素に満たない。幹と太枝だけを粗い断面で残す。
            if (radius < 0.035f) return;
            sides = radius > 0.12f ? 7 : 5;
            segments = radius > 0.12f ? 5 : 3;
        }
        std::array<VERTEX_3D,13> previous{}, next{};
        float distance = 0.0f;
        auto previousCenter = start;
        const float texturePhase = path.start.x*0.21f+path.start.z*0.19f;
        for (int ring=0; ring<=segments; ++ring)
        {
            const float t = static_cast<float>(ring)/segments;
            const auto p = path.sample(t);
            const auto along = XMVector3Normalize(path.sample(std::min(1.0f,t+0.004f))
                -path.sample(std::max(0.0f,t-0.004f)));
            const auto ringAxis = std::abs(XMVectorGetX(XMVector3Dot(across,along))) < 0.97f
                ? across : XMVector3Cross(axis,across);
            const auto u = XMVector3Normalize(ringAxis-along*XMVectorGetX(XMVector3Dot(ringAxis,along)));
            const auto v = XMVector3Cross(along,u);
            distance += XMVectorGetX(XMVector3Length(p-previousCenter));
            const float collar = path.tipSag > 0.0f ? radius*0.15f*std::max(1.0f-6.0f*t,0.0f) : 0.0f;
            const float taper = taperedRadius(radius,tipRadius,t)+collar;
            const float twist = 0.36f*std::sin(t*2.1f + texturePhase) + 0.18f*t;
            for (int i=0; i<=sides; ++i)
            {
                const float uv = static_cast<float>(i)/sides;
                const float angle = XM_2PI*uv + twist;
                const float ripple = 1.0f + 0.072f*std::cos(angle*3.0f+0.67f*t) + 0.042f*std::sin(angle*5.0f-1.6f*t);
                const auto n = u*std::cos(angle)+v*std::sin(angle);
                const auto vertex = xyz(p+n*(taper*ripple));
                // 地面に接する部分への天空光を弱める。色には焼かず、樹皮の環境光にだけ使う。
                const float exposed = std::max(0.0f,std::min(1.0f,(vertex.y-groundHeight)/0.53f));
                bark.w = 0.56f+0.44f*exposed;
                next[i] = {vertex,xyz(n),bark,{uv+texturePhase,distance+path.start.y}};
            }
            if (ring > 0)
                for (int i=0; i<sides; ++i)
                    for (const auto& vertex : {previous[i],previous[i+1],next[i+1],previous[i],next[i+1],next[i]})
                        wood.push_back(vertex);
            previous.swap(next);
            previousCenter = p;
        }
    }

    void roots(Vertices& bark, XMFLOAT3 root, float radius, XMFLOAT4 color, Random& random)
    {
        const float yaw = random()*XM_2PI;
        const int count = 3 + static_cast<int>(random()*4);
        for (int i=0; i<count; ++i)
        {
            const float a = yaw + i*XM_2PI/count + random.between(-0.34f,0.34f);
            const float length = radius*random.between(1.26f,2.75f);
            const float x = std::sin(a)*length, z = std::cos(a)*length;
            // 根張りを幹に食い込ませ、細い先端は地表の下へ沈める。
            const auto rootPath = curvedBranch(moved(root,x*0.12f,radius*0.98f,z*0.12f),
                moved(root,x*0.57f,radius*0.08f,z*0.57f),moved(root,x,-radius*0.31f,z),length*0.13f);
            branchPath(bark,rootPath,radius*random.between(0.32f,0.51f),radius*0.026f,color,root.y);
        }
    }

    void leafCard(Vertices& out, XMFLOAT3 center, FXMVECTOR axis, float length, float width,
        FXMVECTOR lightingNormal, XMFLOAT4 color, Random& random, float bend = 0.0f, bool visible = true)
    {
        const float tilt = random.between(-1.39f,1.39f);
        if (!visible) return;
        if (distantTree.active)
        {
            // 遠くでは葉の一枚一枚は見分けられない。枚数を減らし、残す葉を大きくして樹冠の量を保つ。
            if (random() > distantTree.leafKeep) return;
            length *= distantTree.leafScale;
            width *= distantTree.leafScale;
        }
        const auto up = XMVectorSet(0,1,0,0);
        const auto ref = std::abs(XMVectorGetY(axis)) > 0.92f ? XMVectorSet(0,0,1,0) : up;
        const auto side = XMVector3Normalize(XMVector3Cross(ref,axis));
        const auto cross = XMVector3Cross(axis,side);
        const auto across = side*std::cos(tilt)+cross*std::sin(tilt);
        const auto edge = across*width*0.5f;
        const auto tip = load(center)+axis*length*0.54f-up*bend;
        const auto start = load(center)-axis*length*0.46f;
        const auto addStrip = [&](FXMVECTOR a, FXMVECTOR b, float topV, float bottomV)
        {
            auto normal = XMVector3Normalize(XMVector3Cross(across,a-b));
            if (XMVectorGetY(normal) < 0.0f) normal = -normal;
            const auto n = xyz(XMVector3Normalize(lightingNormal*0.66f+normal*0.62f));
            const VERTEX_3D quad[] = {
                {xyz(a-edge),n,color,{0,topV}}, {xyz(a+edge),n,color,{1,topV}},
                {xyz(b+edge),n,color,{1,bottomV}}, {xyz(b-edge),n,color,{0,bottomV}}
            };
            for (int index : {0,1,2,0,2,3}) out.push_back(quad[index]);
        };
        if (bend > 0.0f)
        {
            // 近景の葉は先を少し下げ、表と裏の反射が平面一枚に揃わないようにする。
            const auto centerLine = (tip+start)*0.5f+up*(bend*0.34f);
            addStrip(tip,centerLine,0.0f,0.5f);
            addStrip(centerLine,start,0.5f,1.0f);
        }
        else addStrip(tip,start,0.0f,1.0f);
    }

    struct LeafPalette
    {
        XMFLOAT3 color;
        float cardWidth;
    };

    struct TreeSize
    {
        float height;
        float diameterAtBreastHeight;
    };

    void broadleafBough(Vertices& bark, Vertices& leaves, const WoodyPath& branch, float spread, float depth,
        int count, float leafScale, LeafPalette palette, XMFLOAT4 woodColor, bool detail, Random& random)
    {
        const auto axis = XMVector3Normalize(branch.sample(1.0f)-load(branch.start));
        const auto reference = std::abs(XMVectorGetY(axis)) > 0.999f ? XMVectorSet(1,0,0,0) : XMVectorSet(0,1,0,0);
        const auto side = XMVector3Normalize(XMVector3Cross(reference,axis));
        const float hue = random.between(-0.16f,0.16f);
        // 小枝を中心に房を分け、房と房の間は向こうの空が見える程度に空ける。
        constexpr int sprays = 5;
        for (int j=0; j<sprays; ++j)
        {
            const float t = (j+random.between(0.12f,0.93f))/sprays;
            const float sideSign = j%2 == 0 ? -1.0f : 1.0f;
            const auto shoot = XMVector3Normalize(axis*0.28f+side*(sideSign*random.between(0.43f,0.91f))
                +XMVectorSet(0,random.between(-0.19f,0.37f),0,0));
            const auto shootBase = branch.sample(0.36f+0.63f*t);
            const float shootLength = spread*random.between(0.59f,1.29f)*(1.0f-0.29f*t);
            const auto shoulder = shootBase+shoot*(shootLength*0.48f)+XMVectorSet(0,shootLength*0.14f,0,0);
            const auto shootEnd = shootBase+shoot*shootLength;
            const float twigRadius = std::min(0.019f,shootLength*0.012f);
            const auto twig = weightedBranch(xyz(shootBase),xyz(shoulder),xyz(shootEnd),0.0f,twigRadius,0.36f);
            // 房の付け根に実際の小枝を置く。遠い木も、同じ曲線上から葉を出す。
            if (detail) branchPath(bark,twig,twigRadius,twigRadius*0.08f,woodColor);
            const float light = random.between(0.83f,1.19f);
            // 房ごとに密度を変え、樹冠の中に光が通る隙間と影が重なる場所を作る。
            const float opening = std::sin(XMVectorGetX(shootBase)*1.4f+XMVectorGetZ(shootBase)*0.78f);
            for (int i=0; i<count/sprays; ++i)
            {
                const float angle = random()*XM_2PI;
                const float y = random.between(-1.0f,1.0f);
                const auto radial = XMVectorSet(std::cos(angle),y,std::sin(angle),0);
                const float along = random.between(0.20f,0.99f);
                const float width = (0.82f-0.44f*along)*std::sqrt(random());
                const auto tangent = XMVector3Normalize(twig.sample(std::min(along+0.06f,1.0f))-twig.sample(along-0.06f));
                const auto attachment = twig.sample(along)+radial*XMVectorSet(spread*0.38f,depth*0.36f,spread*0.38f,0)*width;
                const auto direction = XMVector3Normalize(tangent*0.66f+radial*0.32f
                    +XMVectorSet(0,random.between(-0.31f,0.16f),0,0));
                const auto normal = XMVector3Normalize(radial*0.52f+XMVectorSet(0,0.86f,0,0));
                const float value = light*random.between(0.73f,1.13f);
                const float size = leafScale*random.between(0.49f,1.10f);
                const auto center = attachment+direction*(size*0.33f);
                const float skyVisibility = 0.52f+0.32f*along+0.12f*y;
                const bool visible = (i%7) > (opening > 0.51f ? 2 : 0);
                leafCard(leaves,xyz(center),direction,size,size*palette.cardWidth,normal,
                    {palette.color.x*value*(1.0f+hue),palette.color.y*value,palette.color.z*value*(1.0f-hue),skyVisibility},
                    random,detail ? size*0.11f : 0.0f,visible);
            }
        }
    }

    void keyaki(Vertices& bark, Vertices& leaves, XMFLOAT3 root, TreeSize size, bool detail, Random& random)
    {
        // ケヤキ：灰色の幹が早い位置で数本に分かれ、主枝は先へ行くほど外と上に向かう。
        constexpr XMFLOAT4 color{0.46f,0.44f,0.38f,1};
        constexpr LeafPalette foliage{{0.068f,0.150f,0.036f},0.76f};
        const float height = size.height;
        const float flare = size.diameterAtBreastHeight*0.59f;
        const float crown = height*0.78f;
        const float sprayScale = std::min(height,12.0f);
        const float azimuth = random()*XM_2PI;
        const float lean = height*random.between(0.035f,0.082f);
        const float xLean = std::sin(azimuth)*lean, zLean = std::cos(azimuth)*lean;
        const auto base = moved(root,xLean*0.32f-zLean*0.40f,height*0.13f,zLean*0.32f+xLean*0.40f);
        const auto fork = moved(root,xLean,height*random.between(0.30f,0.39f),zLean);
        const auto trunk = curvedTrunk(moved(root,0,-0.12f,0),base,fork,height*0.020f,random);
        roots(bark,root,flare,color,random);
        branchPath(bark,trunk,flare,flare*0.48f,color,root.y);

        const int leaders = detail ? 6 : 4 + static_cast<int>(random()*3);
        for (int main=0; main<leaders; ++main)
        {
            const float angle = azimuth+main*2.399963f+random.between(-0.68f,0.68f);
            const float reach = crown*random.between(0.16f,0.43f)*(0.92f+0.22f*std::cos(angle-azimuth));
            const float upper = height*random.between(main == 0 ? 0.88f : 0.65f,1.0f);
            const float x = std::sin(angle)*reach, z = std::cos(angle)*reach;
            const float sideways = random.between(-0.30f,0.30f);
            const auto elbow = moved(root,xLean+x*0.46f+z*sideways,upper*random.between(0.68f,0.83f),zLean+z*0.46f-x*sideways);
            const auto stem = moved(root,x+xLean,upper,z+zLean);
            const auto connection = trunk.at(main == 0 ? 0.96f : random.between(0.61f,0.96f));
            const float leaderRadius = flare*(main == 0 ? 0.63f : random.between(0.34f,0.53f));
            const auto leader = weightedBranch(connection,elbow,stem,height*random.between(-0.055f,0.055f),
                leaderRadius,main == 0 ? 0.09f : 0.24f);
            branchPath(bark,leader,leaderRadius,flare*0.035f,color);

            const int twigs = 4+static_cast<int>(random()*5);
            for (int twig=0; twig<twigs; ++twig)
            {
                const float fraction = (twig+random.between(0.09f,0.91f))/twigs;
                const float attachment = 0.44f+0.54f*fraction;
                const auto branch = leader.at(attachment);
                const float side = angle + (twig%2 == 0 ? -1.0f : 1.0f)*random.between(0.33f,1.80f);
                const float length = crown*random.between(0.075f,0.19f)*(1.0f-0.48f*fraction);
                const auto end = moved(branch,std::sin(side)*length,length*random.between(0.06f,0.65f),std::cos(side)*length);
                const auto mid = moved(betweenPoints(branch,end,0.46f),0,length*random.between(0.07f,0.20f),0);
                const float twigRadius = std::min(flare*random.between(0.06f,0.14f),
                    taperedRadius(leaderRadius,flare*0.035f,attachment)*0.76f);
                const auto twigPath = weightedBranch(branch,mid,end,length*random.between(-0.16f,0.16f),twigRadius,0.42f);
                branchPath(bark,twigPath,twigRadius,twigRadius*0.13f,color);
                broadleafBough(bark,leaves,twigPath,crown*0.119f,sprayScale*0.088f,detail?126:76,
                    sprayScale*0.139f,foliage,color,detail,random);
            }
            broadleafBough(bark,leaves,leader,crown*0.141f,sprayScale*0.10f,detail?157:96,
                sprayScale*0.15f,foliage,color,detail,random);
        }
    }

    void konara(Vertices& bark, Vertices& leaves, XMFLOAT3 root, TreeSize size, bool detail, Random& random)
    {
        // コナラ：暗い灰褐色の幹と、向きも高さも不揃いな太い枝で丸みのある樹冠を作る。
        constexpr XMFLOAT4 color{0.29f,0.28f,0.24f,1};
        constexpr LeafPalette foliage{{0.057f,0.127f,0.037f},0.86f};
        const float height = size.height;
        const float flare = size.diameterAtBreastHeight*0.60f;
        const float crown = height*0.76f;
        const float sprayScale = std::min(height,11.2f);
        const float azimuth = random()*XM_2PI;
        const float lean = height*random.between(0.038f,0.144f);
        const float leanX = std::sin(azimuth)*lean, leanZ = std::cos(azimuth)*lean;
        const float bend = random.between(-0.34f,0.34f);
        const float lowerLean = random.between(0.19f,0.62f);
        const auto lower = moved(root,leanX*lowerLean+leanZ*bend,height*random.between(0.17f,0.27f),leanZ*lowerLean-leanX*bend);
        const auto junction = moved(root,leanX*0.60f,height*random.between(0.38f,0.54f),leanZ*0.60f);
        const auto upper = moved(root,-leanZ*0.34f+leanX,height*0.88f,leanX*0.34f+leanZ);
        roots(bark,root,flare,color,random);
        const auto trunkBase = moved(root,0,-0.09f,0);
        const float trunkSway = height*random.between(0.009f,0.035f);
        const auto trunk = curvedTrunk(trunkBase,lower,junction,trunkSway,random);
        branchPath(bark,trunk,flare,flare*0.61f,color,root.y);
        const auto upperTrunk = curvedTrunk(trunk.at(0.96f),
            moved(betweenPoints(junction,upper,0.49f),leanZ*0.37f,0,-leanX*0.37f),upper,height*0.024f,random);
        branchPath(bark,upperTrunk,flare*0.60f,flare*0.074f,color);

        const int limbs = 4+static_cast<int>(random()*(detail ? 5 : 4));
        for (int i=0; i<limbs; ++i)
        {
            const float level = i == 0 ? random.between(0.55f,0.81f) : random.between(0.64f,1.35f);
            const auto a = level < 1.0f ? trunk.at(level) : upperTrunk.at(level-1.0f);
            const float angle = azimuth+i*2.399963f+random.between(-0.89f,0.89f);
            const float reach = crown*(i == 0 ? random.between(0.37f,0.52f) : random.between(0.13f,0.43f));
            const float x = std::sin(angle)*reach, z = std::cos(angle)*reach;
            const float sideways = random.between(-0.29f,0.29f);
            const auto end = moved(root,x+leanX,height*(i == 0 ? random.between(0.48f,0.68f) : random.between(0.61f,0.90f)),z+leanZ);
            const auto mid = moved(betweenPoints(a,end,0.51f),z*sideways,reach*random.between(0.14f,0.25f),-x*sideways);
            const float limbRadius = flare*(i == 0 ? 0.65f : random.between(0.27f,0.49f));
            const auto limb = weightedBranch(a,mid,end,height*random.between(-0.047f,0.047f),limbRadius,0.35f);
            branchPath(bark,limb,limbRadius,flare*0.029f,color);
            const int forks = 4+static_cast<int>(random()*4);
            for (int j=0; j<forks; ++j)
            {
                const float fraction = (j+random.between(0.14f,0.84f))/forks;
                const float attachment = 0.40f+0.58f*fraction;
                const auto fork = limb.at(attachment);
                const float turn = angle+(j%2 == 0 ? -1.0f : 1.0f)*random.between(0.60f,1.82f);
                const float width = crown*random.between(0.055f,0.132f);
                const auto tip = moved(fork,std::sin(turn)*width,width*random.between(-0.05f,0.47f),std::cos(turn)*width);
                const auto bend = moved(betweenPoints(fork,tip,0.50f),0,width*0.15f,0);
                const float twigRadius = std::min(flare*random.between(0.062f,0.135f),
                    taperedRadius(limbRadius,flare*0.029f,attachment)*0.76f);
                const auto twigPath = weightedBranch(fork,bend,tip,width*random.between(-0.16f,0.16f),twigRadius,0.44f);
                branchPath(bark,twigPath,twigRadius,twigRadius*0.13f,color);
                const int density = static_cast<int>((detail ? 115 : 74)*random.between(0.67f,1.17f));
                broadleafBough(bark,leaves,twigPath,crown*0.137f,sprayScale*0.079f,density,
                    sprayScale*0.141f,foliage,color,detail,random);
            }
        }
        broadleafBough(bark,leaves,upperTrunk,crown*0.19f,sprayScale*0.11f,detail?208:120,
            sprayScale*0.157f,foliage,color,detail,random);
    }

    void sugi(Vertices& bark, Vertices& leaves, XMFLOAT3 root, TreeSize size, bool detail, Random& random)
    {
        // スギは一本の幹を梢まで残し、緩い偏りをつける。枝の輪の高さや長さは個体ごとにずらす。
        constexpr XMFLOAT4 color{0.39f,0.235f,0.17f,1};
        const float azimuth = random()*XM_2PI;
        const float height = size.height;
        const float radius = size.diameterAtBreastHeight*0.55f;
        const float sprayScale = std::min(height,15.0f);
        const float lean = height*random.between(0.012f,0.057f);
        const float x = std::sin(azimuth)*lean, z = std::cos(azimuth)*lean;
        const float trunkSway = height*random.between(0.006f,0.018f);
        const auto trunk = curvedTrunk(moved(root,0,-0.08f,0),moved(root,x*0.24f-z*0.40f,height*0.49f,z*0.24f+x*0.40f),
            moved(root,x,height,z),trunkSway,random);
        roots(bark,root,radius,color,random);
        branchPath(bark,trunk,radius,radius*0.035f,color,root.y);
        const float crownBase = random.between(0.29f,0.51f);
        const float crownWidth = random.between(0.092f,0.146f);
        const int levels = 12+static_cast<int>(random()*5);
        for (int level=0; level<levels; ++level)
        {
            const float t = (level+random.between(0.1f,0.78f))/levels;
            const float trunkLevel = crownBase+(0.98f-crownBase)*t;
            const float radiusAtLevel = height*crownWidth*std::pow(1.0f-t,0.77f)*(0.78f+0.22f*std::sin(t*XM_PI));
            // 幹沿いの短枝は主枝の輪の間にも生える。幹を露出させず、樹冠に不自然な横筋ができるのを抑える。
            for (int i=0; i<22; ++i)
            {
                const float turn = azimuth+level*0.88f+i*2.399963f;
                const float spread = radiusAtLevel*random.between(0.08f,0.62f);
                const float innerLevel = std::min(0.995f,trunkLevel+random.between(-0.029f,0.027f));
                const auto p = moved(trunk.at(innerLevel),std::sin(turn)*spread,-spread*0.11f,std::cos(turn)*spread);
                const auto direction = XMVector3Normalize(XMVectorSet(std::sin(turn),random.between(0.35f,0.91f),std::cos(turn),0));
                const float leafSize = std::max(sprayScale*0.076f,radiusAtLevel*0.62f)*random.between(0.76f,1.12f);
                const float green = random.between(0.090f,0.136f);
                leafCard(leaves,p,direction,leafSize,leafSize*0.80f,XMVectorSet(0,1,0,0),{green*0.32f,green,green*0.32f,0.45f},random);
            }
            const int arms = 3+static_cast<int>(random()*4);
            for (int arm=0; arm<arms; ++arm)
            {
                const float angle = azimuth+level*1.07f+arm*XM_2PI/arms+random.between(-0.36f,0.36f);
                const float length = radiusAtLevel*random.between(0.51f,1.34f)*(0.91f+0.16f*std::cos(angle-azimuth));
                const float bx = std::sin(angle)*length, bz = std::cos(angle)*length;
                const auto a = trunk.at(trunkLevel+random.between(-0.012f,0.012f));
                const auto middle = moved(a,bx*0.54f,length*random.between(0.04f,0.13f),bz*0.54f);
                const auto tip = moved(a,bx,length*random.between(0.02f,0.12f+t*0.24f),bz);
                const float limbRadius = radius*0.22f*(1.0f-0.58f*t);
                const auto limb = weightedBranch(a,middle,tip,length*random.between(-0.12f,0.12f),limbRadius,0.30f-0.18f*t);
                branchPath(bark,limb,limbRadius,limbRadius*0.16f,color);
                for (int spray=0; spray<(detail ? 29 : 24); ++spray)
                {
                    const float step = random.between(0.48f,0.99f);
                    const float spread = length*(1.05f-step)*random.between(-0.48f,0.48f);
                    const auto curveTangent = XMVector3Normalize(limb.sample(std::min(step+0.08f,1.0f))-limb.sample(step-0.08f));
                    const auto direction = XMVector3Normalize(curveTangent+XMVectorSet(
                        std::cos(angle)*spread,random.between(-0.22f,0.33f)+t*0.24f,-std::sin(angle)*spread,0));
                    const float leafSize = std::max(sprayScale*0.092f,length*0.78f)*random.between(0.58f,0.91f);
                    const float green = random.between(0.075f,0.125f);
                    const auto point = limb.sample(step) + direction*(leafSize*0.36f)
                        + XMVectorSet(std::cos(angle)*spread*0.62f,-std::abs(spread)*0.28f,-std::sin(angle)*spread*0.62f,0);
                    leafCard(leaves,xyz(point),direction,leafSize,leafSize*0.79f,XMVectorSet(0,1,0,0),
                        {green*0.33f,green,green*0.33f,0.43f+0.51f*step},random);
                }
            }
        }
        for (int i=0; i<22; ++i)
        {
            const float a=azimuth+i*2.39996f;
            const auto tip=moved(trunk.at(random.between(0.954f,0.992f)),std::sin(a)*height*0.008f,0,std::cos(a)*height*0.008f);
            const auto dir=XMVector3Normalize(XMVectorSet(std::sin(a)*0.26f,1,std::cos(a)*0.26f,0));
            leafCard(leaves,tip,dir,sprayScale*0.054f,sprayScale*0.027f,XMVectorSet(0,1,0,0),{0.036f,0.157f,0.052f,1},random);
        }
    }

    using TreeBuilder = void (*)(Vertices&, Vertices&, XMFLOAT3, TreeSize, bool, Random&);

    void plantTree(TreeBuilder grow, Vertices& bark, Vertices& leaves, XMFLOAT3 root, TreeSize size, bool detail, Random& random)
    {
        const auto firstBark = bark.size(), firstLeaf = leaves.size();
        grow(bark,leaves,root,size,detail,random);

        // 枝先の乱数で梢の位置が変わる。樹皮と葉をまとめて指定した樹高へ合わせる。
        float top = root.y;
        for (auto i=firstBark; i<bark.size(); ++i) top = std::max(top,bark[i].Position.y);
        for (auto i=firstLeaf; i<leaves.size(); ++i) top = std::max(top,leaves[i].Position.y);
        if (top <= root.y) return;
        const float verticalScale = size.height/(top-root.y);
        const auto fitHeight = [verticalScale,root](Vertices& vertices, size_t first)
        {
            for (auto i=first; i<vertices.size(); ++i)
            {
                auto& v = vertices[i];
                v.Position.y = root.y + (v.Position.y-root.y)*verticalScale;
                v.Normal.y /= verticalScale;
                v.Normal = xyz(XMVector3Normalize(load(v.Normal)));
            }
        };
        fitHeight(bark,firstBark);
        fitHeight(leaves,firstLeaf);
    }

    void planting(LabLandscape::Batches& batches, const MeshField& terrain)
    {
        auto& leaves = batch(batches,Surface::leaves);
        auto& zelkova = batch(batches,Surface::keyakiFoliage);
        auto& oaks = batch(batches,Surface::konaraFoliage);
        auto& cedars = batch(batches,Surface::sugiFoliage);
        auto& zelkovaBark = batch(batches,Surface::keyakiBark);
        auto& oakBark = batch(batches,Surface::bark);
        auto& cedarBark = batch(batches,Surface::sugiBark);
        Random random;
        Random placement{std::mt19937{2741}};
        std::mt19937 individualSeeds{497};
        const auto at = [&terrain](float x, float z) { return XMFLOAT3{x,groundAt(terrain,x,z),z}; };
        // 明るい駅辺、低い雑木林、奥の植林地を別々の群落として配置する。
        Random stationTree{std::mt19937{individualSeeds()}};
        Random rearTree{std::mt19937{individualSeeds()}};
        // 設定値はメートル。樹高と胸高直径は別に決め、葉や幹を一律には拡大しない。
        plantTree(keyaki,zelkovaBark,zelkova,at(-9.5f,2.9f),{22.0f,0.76f},true,stationTree);
        plantTree(keyaki,zelkovaBark,zelkova,at(-17.4f,17.3f),{17.0f,0.51f},false,rearTree);

        // 確認用の木を残し、その周囲は種がまとまって定着した場所と、その隙間を作る。
        std::vector<XMFLOAT2> planted{{-9.5f,2.9f},{-17.4f,17.3f},{8.9f,16.8f},{-8.7f,40.3f}};
        Random edgeOak{std::mt19937{individualSeeds()}};
        plantTree(konara,oakBark,oaks,at(8.9f,16.8f),{18.0f,0.54f},true,edgeOak);
        Random edgeCedar{std::mt19937{individualSeeds()}};
        plantTree(sugi,cedarBark,cedars,at(-8.7f,40.3f),{26.0f,0.41f},true,edgeCedar);

        struct Grove
        {
            XMFLOAT2 center, extent;
            int count;
            bool cedar;
        };
        constexpr Grove groves[] = {
            {{16.4f,16.8f},{8.2f,6.2f},6,false}, {{25.3f,27.1f},{10.2f,7.6f},7,false},
            {{25.4f,42.8f},{13.0f,9.0f},7,false}, {{-18.5f,26.4f},{7.3f,5.6f},4,false},
            {{-14.2f,44.0f},{7.7f,7.3f},9,true}, {{-27.0f,47.3f},{9.1f,6.8f},10,true}
        };
        for (const auto& grove : groves)
        {
            int count = 0;
            for (int attempt=0; attempt<grove.count*20 && count<grove.count; ++attempt)
            {
                const float angle = placement()*XM_2PI;
                const float reach = std::sqrt(placement());
                const float x = grove.center.x + grove.extent.x*std::cos(angle)*reach;
                const float z = grove.center.y + grove.extent.y*std::sin(angle)*reach;
                if (z < 10.4f || std::abs(x-roadCenter(z)) < 3.8f) continue;
                // 小さい木は近くても残せるが、大きい木同士は幹が重ならない距離を確保する。
                const bool young = placement() < (grove.cedar ? 0.20f : 0.30f);
                const float spacing = young ? 2.35f : (grove.cedar ? 3.85f : 5.0f);
                if (std::any_of(planted.begin(),planted.end(),[=](XMFLOAT2 p) {
                    const float dx = p.x-x, dz = p.y-z;
                    return dx*dx+dz*dz < spacing*spacing;
                })) continue;
                planted.emplace_back(x,z);
                ++count;
                Random individual{std::mt19937{individualSeeds()}};
                if (grove.cedar)
                {
                    const float height = young ? placement.between(9.0f,13.8f) : placement.between(20.0f,29.0f);
                    const float trunk = height*placement.between(0.012f,0.019f);
                    plantTree(sugi,cedarBark,cedars,at(x,z),{height,trunk},z<41.0f,individual);
                }
                else
                {
                    const float height = young ? placement.between(6.2f,10.0f) : placement.between(15.0f,20.0f);
                    const float trunk = height*placement.between(0.021f,0.034f);
                    plantTree(konara,oakBark,oaks,at(x,z),{height,trunk},z<26.0f,individual);
                }
            }
        }

        // 林縁には実生や低い草本がまだらに育つ。歩道や線路まで広がらないようにする。
        constexpr XMFLOAT2 thickets[] = {{7.1f,15.0f},{12.5f,9.9f},{15.2f,19.3f},{9.8f,22.3f},
            {16.2f,13.8f},{23.7f,24.3f},{18.3f,30.1f},{-14.1f,26.7f},{-19.3f,18.6f}};
        Random understory{std::mt19937{831}};
        for (const auto& p : thickets)
            for (int i=0;i<135;++i)
            {
                const float angle = understory()*XM_2PI;
                const float radius = std::sqrt(understory())*1.49f;
                const float x = p.x+std::cos(angle)*radius, z = p.y+std::sin(angle)*radius;
                if (std::abs(x-roadCenter(z)) < 2.9f) continue;
                const float spread = 1.0f-radius/1.7f;
                const float height = understory.between(0.10f,0.72f)*spread;
                const auto center = moved(at(x,z),0,height,0);
                const auto direction = XMVector3Normalize(XMVectorSet(std::cos(angle),understory.between(0.29f,0.86f),std::sin(angle),0));
                const float size = understory.between(0.25f,0.50f);
                const float green = understory.between(0.094f,0.16f);
                leafCard(oaks,center,direction,size,size*0.84f,XMVectorSet(0,1,0,0),
                    {green*0.38f,green,green*0.28f,0.72f},understory);
            }

        // 草は均一に散らさず、株が寄り集まる所とまばらな所を作る。
        const auto clumping = [](float x, float z)
        {
            const float a = std::sin(x*1.31f+std::sin(z*0.73f)*2.1f)*std::cos(z*1.07f+std::sin(x*0.61f)*1.7f);
            const float b = std::sin(x*0.29f-z*0.37f+1.3f);
            return std::max(0.0f, std::min(1.0f, 0.5f+0.38f*a+0.22f*b));
        };
        for (int i=0;i<34000;++i)
        {
            const float x=random.between(-34.0f,34.0f), z=random.between(-12.0f,55.0f);
            const float edge=std::abs(x-roadCenter(z));
            if (edge < 2.51f || (z>4.59f && z<7.55f)
                || (x>-13.3f && x<-2.12f && z>7.64f && z<12.84f)) continue;
            if (edge > 8.0f && std::sin(x*0.75f+z*0.14f)+std::sin(z*1.47f) < -0.95f) continue;
            const float clump = clumping(x,z);
            if (random() > 0.18f+0.82f*clump) continue;
            const float y=groundAt(terrain,x,z);
            const float tall = (edge<4.6f?0.46f:0.30f)*(0.65f+0.55f*clump);
            // 緑の株に、日に焼けて黄ばんだ株や枯れ葉色の株を少し混ぜる。
            const float dryness = std::pow(random(),2.4f)*0.85f;
            const float green = random.between(0.14f,0.25f);
            const XMFLOAT3 lush{green*random.between(0.36f,0.62f),green,green*random.between(0.18f,0.30f)};
            const XMFLOAT3 dry{0.16f,0.14f,0.062f};
            const XMFLOAT3 tuft{lush.x+(dry.x-lush.x)*dryness,lush.y+(dry.y-lush.y)*dryness,lush.z+(dry.z-lush.z)*dryness};
            const float angle=random()*XM_2PI;
            const int blades = 4 + static_cast<int>(random()*3.0f);
            for (int j=0;j<blades;++j)
            {
                const float a=angle+j*(XM_2PI/blades)+random.between(-0.4f,0.4f);
                const float height=tall*random.between(0.35f,1.0f);
                const float lean=height*random.between(0.15f,0.85f);
                const auto stride = XMVectorSet(std::cos(a),0,std::sin(a),0);
                const auto across = XMVectorSet(-std::sin(a),0,std::cos(a),0);
                const auto side = across*random.between(0.006f,0.016f);
                const auto base = XMVectorSet(x,y,z,0)+stride*random.between(0.0f,0.06f);
                const auto middle = base+stride*lean*0.27f+XMVectorSet(0,height*0.56f,0,0);
                const auto tip = base+stride*lean+XMVectorSet(0,height,0,0);
                // 葉の面の向きを法線に混ぜ、同じ株の中でも明るい葉と暗い葉ができるようにする。
                const float facing = random() < 0.5f ? -0.3f : 0.3f;
                const auto normal = xyz(XMVector3Normalize(across*facing+stride*0.25f+XMVectorSet(0,1,0,0)));
                const float shade = random.between(0.82f,1.12f);
                const auto colored = [&](float t)
                {
                    return XMFLOAT4{tuft.x*shade,tuft.y*shade,tuft.z*shade,t};
                };
                const VERTEX_3D b0{xyz(base-side),normal,colored(0.0f),{}}, b1{xyz(base+side),normal,colored(0.0f),{}};
                const VERTEX_3D m0{xyz(middle-side*0.62f),normal,colored(0.56f),{}};
                const VERTEX_3D m1{xyz(middle+side*0.62f),normal,colored(0.56f),{}};
                const VERTEX_3D t0{xyz(tip),normal,colored(1.0f),{}};
                for (const auto& v : {b0,b1,m1,b0,m1,m0,m0,m1,t0}) leaves.push_back(v);
            }
        }
    }

    constexpr int ridgeColumns = 86, ridgeRows = 14;
    constexpr float ridgeSpacing = 3.6f;

    float ridgeHeight(float x, float z, float start, float height, float phase)
    {
        const float t = (z - start) / (ridgeRows*ridgeSpacing);
        const float slope = std::pow(std::max(0.0f, std::sin(t*XM_PI)),0.76f);
        const float ridge = 0.57f + 0.22f*std::sin(x*0.029f+phase)
            + 0.14f*std::cos(x*0.067f-phase) + 0.07f*std::sin(x*0.115f+phase+z*0.062f);
        const float folds = std::cos(x*0.15f+z*0.10f+phase)*std::sin(x*0.08f-z*0.041f);
        return -1.6f + slope*height*(ridge+folds*0.11f);
    }

    // 手前の里山を本物の木で覆う。数十メートル先の山肌は、模様だけでは森に見えない。
    void hillForest(LabLandscape::Batches& batches, float start, float height, float phase)
    {
        auto& oaks = batch(batches,Surface::konaraFoliage);
        auto& oakBark = batch(batches,Surface::bark);
        auto& cedars = batch(batches,Surface::sugiFoliage);
        auto& cedarBark = batch(batches,Surface::sugiBark);
        Random placement{std::mt19937{9013}};
        std::mt19937 seeds{4421};
        const DistantTreeScope distant;
        constexpr float spacing = 5.2f;
        for (float z = start + 1.5f; z < start + ridgeRows*ridgeSpacing*0.62f; z += spacing)
            for (float x = -72.0f; x < 72.0f; x += spacing)
            {
                const float px = x + placement.between(-0.42f,0.42f)*spacing;
                const float pz = z + placement.between(-0.42f,0.42f)*spacing;
                const float y = ridgeHeight(px,pz,start,height,phase);
                if (y < 0.2f) continue;
                // 杉の植林地と雑木林を帯状に分ける。
                const bool cedar = std::sin(px*0.045f+1.0f)+std::cos(pz*0.07f-px*0.012f) > 0.55f;
                Random individual{std::mt19937{seeds()}};
                const float treeHeight = cedar ? placement.between(16.0f,24.0f) : placement.between(10.0f,16.0f);
                const float trunk = treeHeight*(cedar ? 0.016f : 0.026f);
                if (cedar)
                    plantTree(sugi,cedarBark,cedars,{px,y-0.4f,pz},{treeHeight,trunk},false,individual);
                else
                    plantTree(konara,oakBark,oaks,{px,y-0.4f,pz},{treeHeight,trunk},false,individual);
            }
    }

    // behind が真なら、同じ形の稜線を z の負側(駅の背後)へ折り返して置く。
    void mountainRange(Vertices& out, float start, float height, float phase, XMFLOAT4 color, bool behind = false)
    {
        constexpr int columns = ridgeColumns, rows = ridgeRows;
        constexpr float spacing = ridgeSpacing;
        const auto sample = [&](float x, float z) { return ridgeHeight(x,z,start,height,phase); };
        const auto vertex = [&](int column, int row)
        {
            const float x=(column-columns*0.5f)*spacing, z=start+row*spacing;
            const float y=sample(x,z);
            auto n=xyz(XMVector3Normalize(XMVectorSet(sample(x-1,z)-sample(x+1,z),2,
                sample(x,z-1)-sample(x,z+1),0)));
            const float s = 0.88f + 0.12f*std::sin(x*0.15f+z*0.26f)*std::cos(z*0.20f-x*0.21f);
            if (behind) n.z = -n.z;
            return VERTEX_3D{{x,y,behind ? -z : z},n,{color.x*s,color.y*s,color.z*s,1},{}};
        };
        for(int row=0;row<rows;++row)
            for(int col=0;col<columns;++col)
            {
                const auto a=vertex(col,row), b=vertex(col,row+1), c=vertex(col+1,row+1), d=vertex(col+1,row);
                for (auto p : {a,b,c,a,c,d}) out.push_back(p);
            }
    }
}

LabLandscape::Batches LabLandscape::build(const MeshField& terrain)
{
    Batches batches;
    station(batches);
    railway(batches);
    planting(batches,terrain);
    auto& hills = batch(batches,Surface::mountains);
    // 手前の里山と奥の稜線をずらし、霞でそれぞれ違う色に見せる。
    mountainRange(hills,54.0f,16.2f,0.8f,{0.071f,0.18f,0.11f,1});
    hillForest(batches,54.0f,16.2f,0.8f);
    mountainRange(hills,85.0f,23.5f,2.2f,{0.063f,0.14f,0.16f,1});
    mountainRange(hills,110.0f,36.0f,4.0f,{0.060f,0.13f,0.19f,1});
    // 駅の背後にも山並みを置き、どの視点からも地平線がむき出しにならないようにする。
    mountainRange(hills,64.0f,19.0f,5.3f,{0.071f,0.18f,0.11f,1},true);
    mountainRange(hills,100.0f,32.0f,6.9f,{0.060f,0.13f,0.19f,1},true);
    return batches;
}
