#include "main.h"
#include "GraphicsLab.h"
#include "renderer.h"
#include "GraphicsLabGeometry.h"
#include "MeshField.h"
#include "HeroineAnimationAssets.h"
#include "ToonPanel.h"
#include "ToonShader.h"
#include "Wave.h"
#include "animationModel.h"
#include "manager.h"
#include "imgui/imgui.h"
#include <d3dcompiler.h>
#include <shlobj.h>
#include <wrl/client.h>
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "shell32.lib")

using Microsoft::WRL::ComPtr;

namespace
{
    // 画質の段階ごとのシャドウマップの解像度。高・中・低の順。
    constexpr UINT shadowSizes[] = {4096, 2048, 1024};
    constexpr UINT lightingSlot = 12;
    constexpr UINT surfaceSlot = 13;
    constexpr UINT shadowResourceSlot = 10;
    constexpr UINT imageSlot = 11;
    constexpr UINT materialTextureSlot = 12;
    constexpr UINT groveDepthSlot = 15;
    constexpr UINT groveImageSlot = 16;
    constexpr UINT aoSlot = 17;
    constexpr UINT bloomSlot = 18;
    constexpr UINT postSamplerSlot = 5;
    constexpr UINT bloomLevels = 6;
    constexpr UINT captureWidth = 5120, captureHeight = 2880;
    constexpr UINT pngWidth = 3840, pngHeight = 2160;
    constexpr float leafAlphaCutoff = 0.43f;
    constexpr float cameraNear = 0.24f;
    constexpr float cameraFar = 190.0f;
    constexpr float clearColor[] = { 0.16f, 0.33f, 0.58f, 1.0f };

    using LabLandscape::Surface;
    constexpr auto surfaceCount = static_cast<unsigned int>(Surface::count);

    struct SurfaceConstants
    {
        XMFLOAT4 material;
        XMFLOAT4 maps;
    };

    struct FrameConstants
    {
        XMFLOAT4 sunDirection, sunColor, ambientColor, fogColor, fog;
        XMFLOAT3 shadow;
        float characterMaterialMode;
        XMFLOAT4 post;
        XMFLOAT4X4 sunViewProjection, inverseViewProjection;
        XMFLOAT4 characterDetail;
        XMFLOAT4 groveLight;
        XMFLOAT4X4 viewProjection;
        XMFLOAT4 ao, bloom, grade;
        XMFLOAT4 haze;
    };

    // 画面効果の中間バッファ。ウィンドウ用と撮影用で解像度が違うため描画先ごとに持つ。
    struct PostTargets
    {
        ComPtr<ID3D11RenderTargetView> aoColor, aoBlurColor, bloomColor;
        ComPtr<ID3D11ShaderResourceView> aoImage, aoBlurImage, bloomImage;
    };

    struct RenderDestination
    {
        UINT width, height;
        ID3D11RenderTargetView* sceneColor;
        ID3D11ShaderResourceView* sceneImage;
        ID3D11RenderTargetView* finalColor;
        ID3D11DepthStencilView* depth;
        ID3D11DepthStencilView* waterDepth = nullptr;
        ID3D11ShaderResourceView* depthImage = nullptr;
        ID3D11RenderTargetView* groveColor = nullptr;
        ID3D11ShaderResourceView* groveImage = nullptr;
        const PostTargets* post = nullptr;
    };

    struct Settings
    {
        float yaw = -146.0f, pitch = 43.0f;
        float sun[3] = { 1.0f, 0.90f, 0.73f };
        float ambient[3] = { 0.52f, 0.70f, 1.0f };
        float fog[3] = { 0.47f, 0.60f, 0.74f };
        float sunPower = 3.9f, ambientPower = 0.54f;
        float shadowStrength = 0.86f, shadowSoftness = 1.5f;
        float roughness = 0.54f, metallic = 0.0f;
        float fogStart = 24.0f, fogDensity = 0.0085f;
        float exposure = -0.69f, saturation = 0.90f, contrast = 1.06f;
        bool ambientOcclusion = true, bloom = true, grading = true;
        float aoStrength = 0.85f, aoRadius = 0.95f, aoDistance = 70.0f;
        float bloomStrength = 0.20f, bloomThreshold = 0.85f;
        float splitTone = 0.65f, vignette = 0.32f, olive = 0.45f;
        bool aerialHaze = true;
        float hazeFalloff = 0.03f, hazeSun = 1.0f, hazeBrightness = 1.0f;
        // 0: 高, 1: 中, 2: 低。RTX 4050 級を想定し、見た目がほぼ同じで軽い「中」を標準にする。
        int quality = 1;
        bool sunEnabled = true, ambientEnabled = true, shadows = true, pbr = true, toon = true;
        bool atmosphere = true, fogEnabled = true, hdr = true, water = true, plants = true;
        bool characterMaterials = true, showMaterialIds = false;
        float clothDetail = 1.0f, skinTone = 0.85f, eyeReflection = 1.0f;
        float skinSoftness = 0.72f;
        float groveStrength = 0.65f, leafTransmission = 1.0f;
        bool groveRays = true;
        int camera = 0;
    };

    // GPU の処理時間を区間ごとに測る。結果は数フレーム遅れて読み出し、描画を待たせない。
    class GpuTimer
    {
    public:
        enum Section { shadow, landscape, characterAndWater, post, sectionCount };
        static constexpr const char* names[sectionCount] = {u8"影", u8"背景", u8"人物と水", u8"画面効果"};

        bool init()
        {
            auto* device = Renderer::GetDevice();
            D3D11_QUERY_DESC disjointDesc{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
            D3D11_QUERY_DESC stampDesc{D3D11_QUERY_TIMESTAMP, 0};
            for (auto& slot : slots)
            {
                if (FAILED(device->CreateQuery(&disjointDesc, &slot.disjoint))) return false;
                for (auto& stamp : slot.stamps)
                    if (FAILED(device->CreateQuery(&stampDesc, &stamp))) return false;
            }
            return true;
        }

        void begin()
        {
            auto& slot = slots[current];
            Renderer::GetDeviceContext()->Begin(slot.disjoint.Get());
            Renderer::GetDeviceContext()->End(slot.stamps[0].Get());
            recording = true;
        }

        void mark(Section finished)
        {
            if (recording) Renderer::GetDeviceContext()->End(slots[current].stamps[finished + 1].Get());
        }

        // 新しい計測値が読めたら true を返す。
        bool end()
        {
            if (!recording) return false;
            Renderer::GetDeviceContext()->End(slots[current].disjoint.Get());
            slots[current].pending = true;
            recording = false;
            current = (current + 1) % slotCount;
            return collect(slots[current]);
        }

        float latest[sectionCount]{};
        float smoothed[sectionCount]{};

    private:
        static constexpr int slotCount = 5;
        struct Slot
        {
            ComPtr<ID3D11Query> disjoint;
            std::array<ComPtr<ID3D11Query>, sectionCount + 1> stamps;
            bool pending = false;
        };
        std::array<Slot, slotCount> slots;
        int current = 0;
        bool recording = false, primed = false;

        bool collect(Slot& slot)
        {
            if (!slot.pending) return false;
            auto* context = Renderer::GetDeviceContext();
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint{};
            if (context->GetData(slot.disjoint.Get(), &disjoint, sizeof(disjoint), 0) != S_OK) return false;
            slot.pending = false;
            if (disjoint.Disjoint || disjoint.Frequency == 0) return false;
            UINT64 stamps[sectionCount + 1]{};
            for (int i = 0; i <= sectionCount; ++i)
                if (context->GetData(slot.stamps[i].Get(), &stamps[i], sizeof(UINT64), 0) != S_OK) return false;
            for (int i = 0; i < sectionCount; ++i)
            {
                latest[i] = static_cast<float>(double(stamps[i + 1] - stamps[i]) * 1000.0 / double(disjoint.Frequency));
                smoothed[i] = primed ? smoothed[i] * 0.93f + latest[i] * 0.07f : latest[i];
            }
            primed = true;
            return true;
        }
    };

    struct Mesh
    {
        ComPtr<ID3D11Buffer> vertices;
        UINT count = 0;
        void draw() const
        {
            if (!count) return;
            auto* context = Renderer::GetDeviceContext();
            UINT stride = sizeof(VERTEX_3D), offset = 0;
            context->IASetVertexBuffers(0, 1, vertices.GetAddressOf(), &stride, &offset);
            context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            context->Draw(count, 0);
        }
    };

    bool createBuffer(ComPtr<ID3D11Buffer>& result, UINT size, UINT flags, const void* data = nullptr)
    {
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = size;
        desc.Usage = data ? D3D11_USAGE_IMMUTABLE : D3D11_USAGE_DEFAULT;
        desc.BindFlags = flags;
        D3D11_SUBRESOURCE_DATA initial{};
        initial.pSysMem = data;
        return SUCCEEDED(Renderer::GetDevice()->CreateBuffer(&desc, data ? &initial : nullptr, &result));
    }

    bool uploadMesh(Mesh& mesh, const std::vector<VERTEX_3D>& vertices)
    {
        mesh.count = static_cast<UINT>(vertices.size());
        return createBuffer(mesh.vertices, mesh.count * sizeof(VERTEX_3D), D3D11_BIND_VERTEX_BUFFER, vertices.data());
    }

    struct SurfaceTextures
    {
        std::array<ComPtr<ID3D11ShaderResourceView>,3> maps;

        bool loadMap(const std::wstring& path, unsigned int index, bool alphaCutout = false)
        {
            ScratchImage source, mipmaps;
            if (FAILED(LoadFromWICFile(path.c_str(), WIC_FLAGS_IGNORE_SRGB, nullptr, source))) return false;
            const auto filter = index == 0 ? TEX_FILTER_BOX | TEX_FILTER_SRGB : TEX_FILTER_BOX;
            const bool hasMipmaps = SUCCEEDED(GenerateMipMaps(source.GetImages(), source.GetImageCount(),
                source.GetMetadata(), filter, 0, mipmaps));
            // 細い針葉や鋸歯が縮小時に一斉に消えないよう、ミップ間の被覆率を揃える。
            if (hasMipmaps && alphaCutout)
                ScaleMipMapsAlphaForCoverage(source.GetImages(), source.GetImageCount(), source.GetMetadata(),
                    0, leafAlphaCutoff, mipmaps);
            const auto& image = hasMipmaps ? mipmaps : source;
            const auto encoding = index == 0 ? CREATETEX_FORCE_SRGB : CREATETEX_IGNORE_SRGB;
            return SUCCEEDED(CreateShaderResourceViewEx(Renderer::GetDevice(), image.GetImages(), image.GetImageCount(),
                image.GetMetadata(), D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0, encoding, &maps[index]));
        }

        bool load(const wchar_t* name)
        {
            constexpr const wchar_t* suffix[] = {L"-diff.jpg",L"-nor_dx.jpg",L"-rough.jpg"};
            for (unsigned int i = 0; i < maps.size(); ++i)
                if (!loadMap(std::wstring(L"asset/graphics-lab/") + name + suffix[i], i)) return false;
            return true;
        }

        bool loadBark(const wchar_t* species)
        {
            const auto prefix = std::wstring(L"asset/graphics-lab/") + species + L"-bark";
            return loadMap(prefix + L"-color.png", 0) && loadMap(prefix + L"-relief.png", 1);
        }

        bool bind(unsigned int requiredMaps) const
        {
            std::array<ID3D11ShaderResourceView*,3> views{};
            const bool complete = std::all_of(maps.begin(), maps.begin() + requiredMaps,
                [](const auto& map) { return map.Get() != nullptr; });
            if (complete)
                for (unsigned int i=0; i<views.size(); ++i) views[i]=maps[i].Get();
            Renderer::GetDeviceContext()->PSSetShaderResources(materialTextureSlot,
                static_cast<UINT>(views.size()), views.data());
            return complete;
        }
    };

    struct Landscape
    {
        std::array<Mesh,surfaceCount> meshes;
        std::array<SurfaceTextures,surfaceCount> textures;

        bool init(const MeshField& terrain, std::string& warning)
        {
            const auto batches = LabLandscape::build(terrain);
            for(unsigned int i=0;i<meshes.size();++i)
                if(!batches[i].empty() && !uploadMesh(meshes[i], batches[i])) return false;

            // 実写ベースの色と凹凸を使い、ファイルが無ければ頂点色に戻す。
            const struct { Surface kind; const wchar_t* name; } materials[] = {
                {Surface::ground,L"gravel"}, {Surface::ballast,L"ballast"},
                {Surface::wood,L"siding"},
                {Surface::roof,L"roof"}, {Surface::concrete,L"concrete"}
            };
            for(const auto& entry : materials)
                if(!textures[static_cast<unsigned int>(entry.kind)].load(entry.name))
                    warning = "Some landscape textures could not be loaded (asset/graphics-lab).";
            const struct { Surface kind; const wchar_t* species; } bark[] = {
                {Surface::keyakiBark,L"keyaki"}, {Surface::bark,L"konara"}, {Surface::sugiBark,L"sugi"}
            };
            for (const auto& entry : bark)
                if (!textures[static_cast<unsigned int>(entry.kind)].loadBark(entry.species))
                    warning = "Some tree bark textures could not be loaded (asset/graphics-lab).";
            if (!textures[static_cast<unsigned int>(Surface::stationSign)].loadMap(L"asset/graphics-lab/station-sign.png", 0))
                warning = "The station sign texture could not be loaded (asset/graphics-lab).";
            const struct { Surface kind; const wchar_t* path; } leaves[] = {
                {Surface::keyakiFoliage,L"asset/graphics-lab/keyaki-leaves.png"},
                {Surface::konaraFoliage,L"asset/graphics-lab/konara-leaves.png"},
                {Surface::sugiFoliage,L"asset/graphics-lab/sugi-leaves.png"}
            };
            for (const auto& entry : leaves)
                if (!textures[static_cast<unsigned int>(entry.kind)].loadMap(entry.path, 0, true))
                    warning = "Some tree foliage textures could not be loaded (asset/graphics-lab).";
            return true;
        }
    };

    void resetCharacterLook(ToonSettings& toon)
    {
        toon = {};
        // 細い髪やレースの上に輪郭を重ねず、素材の色と柔らかい影で形を出す。
        toon.outlineEnabled = false;
        toon.shadowBrightness = 0.78f;
        toon.edgeSoftness = 0.08f;
        toon.rimStrength = 0.065f;
    }

    struct CameraShot
    {
        Vector3 eye, target;
        float fov;
    };

    const CameraShot cameraShots[] = {
        {{-1.05f, 2.26f, -8.9f}, {-2.95f, 1.92f, 8.5f}, 0.85f},
        {{-0.6f, 0.62f, -6.0f}, {1.1f, 0.98f, 4.5f}, 0.95f},
        {{1.62f, 1.42f, -0.69f}, {0.15f, 1.28f, 3.92f}, 0.91f},
        {{-4.2f, 1.85f, -2.0f}, {1.35f, 2.16f, 13.0f}, 0.84f},
        {{1.22f, 1.52f, 0.95f}, {0.99f, 1.48f, 2.72f}, 0.50f},
        {{-4.73f, 2.19f, 5.1f}, {-9.14f, 1.94f, 10.30f}, 0.79f},
        {{-2.3f, 2.13f, 12.1f}, {8.9f, 4.35f, 47.0f}, 0.78f},
        {{3.8f, 2.35f, -25.0f}, {-7.35f, 9.4f, 2.9f}, 0.84f},
        {{2.4f, 2.25f, 2.1f}, {12.0f, 7.8f, 20.0f}, 0.95f},
        {{2.6f, 3.22f, 11.0f}, {-15.0f, 11.8f, 43.8f}, 0.91f},
        {{-10.8f, 1.66f, -0.12f}, {-9.5f, 1.24f, 2.9f}, 0.63f},
        {{7.1f, 1.57f, 13.7f}, {8.9f, 1.43f, 16.8f}, 0.66f},
        {{-10.7f, 1.75f, 36.4f}, {-8.7f, 1.53f, 40.3f}, 0.68f},
        {{18.6f, 2.00f, 25.9f}, {10.4f, 3.30f, 16.1f}, 0.86f}
    };

    constexpr const wchar_t* shotFileNames[] = {L"railway-crossing", L"puddle", L"station-character", L"work-view",
        L"character", L"station", L"forest", L"keyaki", L"konara", L"sugi", L"keyaki-bark", L"konara-bark", L"sugi-bark",
        L"komorebi"};
    static_assert(IM_ARRAYSIZE(shotFileNames) == IM_ARRAYSIZE(cameraShots), "Camera preset count mismatch.");

    std::wstring screenshotDirectory()
    {
        PWSTR pictures = nullptr;
        const HRESULT result = SHGetKnownFolderPath(FOLDERID_Pictures,KF_FLAG_DEFAULT,nullptr,&pictures);
        std::wstring folder;
        if (SUCCEEDED(result)) folder = std::wstring(pictures) + L"\\Shu-pre-Graphics";
        CoTaskMemFree(pictures);
        if (folder.empty()) return {};
        if (CreateDirectoryW(folder.c_str(),nullptr)) return folder;
        const DWORD attributes = GetFileAttributesW(folder.c_str());
        return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) ? folder : std::wstring{};
    }

    class LabCamera
    {
    public:
        void setShot(const CameraShot& shot)
        {
            fov = shot.fov;
            setPose(shot.eye, shot.target);
            drag = Drag::none;
            followingCharacter = false;
        }

        void update(const Vector3& character, float dt)
        {
            const auto& io = ImGui::GetIO();
            const bool overScene = !io.WantCaptureMouse && !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow)
                && ImGui::IsMousePosValid() && io.MousePos.x >= 0 && io.MousePos.x < io.DisplaySize.x
                && io.MousePos.y >= 0 && io.MousePos.y < io.DisplaySize.y;
            const Vector3 face = character + Vector3{0, 1.46f, 0};

            if ((drag == Drag::orbit && !ImGui::IsMouseDown(ImGuiMouseButton_Right))
                || (drag == Drag::pan && !ImGui::IsMouseDown(ImGuiMouseButton_Middle)))
                drag = Drag::none;

            bool dragStarted = false;
            if (overScene && drag == Drag::none)
            {
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
                    drag = Drag::orbit;
                else if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
                    drag = Drag::pan;
                dragStarted = drag != Drag::none;

                // 景色のプリセットからでも、ホイールを回した瞬間に人物へ注視点を移す。
                if (io.MouseWheel != 0.0f && !dragStarted)
                {
                    if (!followingCharacter) setPose(position(), face);
                    followingCharacter = true;
                    distance = std::max(0.65f, std::min(70.0f, distance * std::pow(0.87f, io.MouseWheel)));
                }
            }

            if (!io.WantCaptureKeyboard && ImGui::IsKeyPressed(ImGuiKey_F, false))
            {
                if (!followingCharacter) setPose(position(), face);
                target = face;
                distance = 2.2f;
                followingCharacter = true;
            }

            if (followingCharacter) target = face;
            if (!dragStarted && drag == Drag::orbit)
            {
                yaw = std::remainder(yaw - io.MouseDelta.x * 0.008f, XM_2PI);
                pitch = boundedPitch(pitch + io.MouseDelta.y * 0.008f);
            }
            else if (!dragStarted && drag == Drag::pan && io.DisplaySize.y > 0)
            {
                const Vector3 right{ -std::cos(yaw), 0, std::sin(yaw) };
                const Vector3 up = crossProduct(right, orbitDirection());
                const float worldPerPixel = 2.0f * distance * std::tan(fov * 0.5f) / io.DisplaySize.y;
                translate((up * io.MouseDelta.y - right * io.MouseDelta.x) * worldPerPixel);
            }

            if (!io.WantCaptureKeyboard)
            {
                const Vector3 forward{-std::sin(yaw), 0, -std::cos(yaw)};
                const Vector3 right{-std::cos(yaw), 0, std::sin(yaw)};
                const float speed = io.KeyShift ? 10.0f : 3.4f;
                const auto axis = [](ImGuiKey positive, ImGuiKey negative)
                {
                    return static_cast<float>(ImGui::IsKeyDown(positive)) - static_cast<float>(ImGui::IsKeyDown(negative));
                };
                const Vector3 motion = forward * axis(ImGuiKey_W, ImGuiKey_S)
                    + right * axis(ImGuiKey_D, ImGuiKey_A) + Vector3{0, axis(ImGuiKey_E, ImGuiKey_Q), 0};
                translate(motion * (speed * dt));
            }
        }

        Vector3 position() const
        {
            Vector3 eye = target + orbitDirection() * distance;
            eye.y = std::max(0.28f, eye.y);
            return eye;
        }

        Vector3 focus() const { return target; }
        float fieldOfView() const { return fov; }

    private:
        enum class Drag { none, orbit, pan };

        Vector3 target{0, 0, 0};
        float yaw = 0.0f, pitch = 0.0f, distance = 1.0f, fov = 0.83f;
        Drag drag = Drag::none;
        bool followingCharacter = false;

        static float boundedPitch(float angle)
        {
            return std::max(XMConvertToRadians(-68.0f), std::min(XMConvertToRadians(78.0f), angle));
        }

        Vector3 orbitDirection() const
        {
            const float horizontal = std::cos(pitch);
            return { std::sin(yaw) * horizontal, std::sin(pitch), std::cos(yaw) * horizontal };
        }

        void setPose(const Vector3& eye, const Vector3& aim)
        {
            const Vector3 offset = eye - aim;
            const float radius = offset.lenght();
            if (radius >= 0.01f)
            {
                yaw = std::atan2(offset.x, offset.z);
                pitch = boundedPitch(std::asin(std::max(-1.0f, std::min(1.0f, offset.y / radius))));
            }
            target = aim;
            distance = std::max(0.65f, radius);
        }

        void translate(const Vector3& motion)
        {
            if (motion.x == 0 && motion.y == 0 && motion.z == 0) return;
            followingCharacter = false;
            target = target + motion;
            target.y = std::max(0.28f, target.y);
        }
    };

    // 画質の段階。高: 4096 の影と全効果、中: 影を 2048 に、低: 影を 1024 にして画面効果も省く。
    void applyQuality(Settings& settings, int quality)
    {
        settings.quality = std::max(0, std::min(2, quality));
        const bool effects = settings.quality < 2;
        settings.ambientOcclusion = effects;
        settings.groveRays = effects;
        settings.bloom = effects;
    }

    bool drawControls(Settings& settings, ToonSettings& toon, const std::string& error,
        bool ready, bool& capture, const char* captureStatus)
    {
        ImGui::SetNextWindowPos({12, 12}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({308, SCREEN_HEIGHT - 24.0f}, ImGuiCond_Always);
        ImGui::Begin(u8"描画検証", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
        if (!error.empty()) ImGui::TextWrapped("%s", error.c_str());
        const char* shots[] = {u8"踏切と駅舎", u8"水たまり", u8"人物と庇", u8"作業用の固定画面", u8"顔と髪の近景",
            u8"駅舎と線路", u8"林と山並み", u8"ケヤキ（駅のそば）", u8"コナラ（雑木林）", u8"スギ（植林帯）",
            u8"ケヤキの樹皮", u8"コナラの樹皮", u8"スギの樹皮", u8"林の木漏れ日"};
        static_assert(IM_ARRAYSIZE(shots) == IM_ARRAYSIZE(cameraShots), "Camera preset count mismatch.");
        bool resetCamera = ImGui::Combo(u8"視点", &settings.camera, shots, IM_ARRAYSIZE(shots), IM_ARRAYSIZE(shots));
        const char* qualities[] = {u8"高 (影4096・全効果)", u8"中 (影2048)", u8"低 (影1024・効果なし)"};
        int quality = settings.quality;
        if (ImGui::Combo(u8"画質", &quality, qualities, IM_ARRAYSIZE(qualities))) applyQuality(settings, quality);
        ImGui::TextWrapped(u8"ホイール: 人物に寄る / 引く  F: 人物を中心に");
        ImGui::TextWrapped(u8"右ドラッグ: 回転  中ドラッグ: 平行移動");
        ImGui::TextWrapped(u8"WASD: 移動  Q/E: 上下");
        if (ready)
        {
            capture = ImGui::Button(u8"4K PNG を保存");
            ImGui::SameLine();
            if (ImGui::Button(u8"保存先を開く"))
            {
                const auto directory = screenshotDirectory();
                if (!directory.empty()) ShellExecuteW(nullptr,L"open",directory.c_str(),nullptr,nullptr,SW_SHOWNORMAL);
            }
            if (captureStatus) ImGui::TextWrapped("%s",captureStatus);
        }
        if (ImGui::Button(u8"初期設定に戻す"))
        {
            settings = {};
            resetCharacterLook(toon);
            resetCamera = true;
        }
        if (ImGui::CollapsingHeader(u8"照明と影", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Checkbox(u8"太陽光", &settings.sunEnabled);
            ImGui::SliderFloat(u8"方位", &settings.yaw, -180, 180, "%.0f deg");
            ImGui::SliderFloat(u8"仰角", &settings.pitch, 8, 85, "%.0f deg");
            ImGui::ColorEdit3(u8"太陽色", settings.sun, ImGuiColorEditFlags_NoInputs);
            ImGui::SliderFloat(u8"太陽強度", &settings.sunPower, 0, 9);
            ImGui::Checkbox(u8"環境光", &settings.ambientEnabled);
            ImGui::ColorEdit3(u8"環境光色", settings.ambient, ImGuiColorEditFlags_NoInputs);
            ImGui::SliderFloat(u8"環境光強度", &settings.ambientPower, 0, 2);
            ImGui::Checkbox(u8"シャドウマップ", &settings.shadows);
            ImGui::SliderFloat(u8"影の強度", &settings.shadowStrength, 0, 1);
            ImGui::SliderFloat(u8"影の柔らかさ", &settings.shadowSoftness, 0, 4);
            ImGui::SliderFloat(u8"葉の透過光", &settings.leafTransmission, 0, 1.65f);
            ImGui::Checkbox(u8"木漏れ日の光芒", &settings.groveRays);
            if (settings.groveRays)
                ImGui::SliderFloat(u8"光芒の強さ", &settings.groveStrength, 0, 1.5f);
        }
        if (ImGui::CollapsingHeader(u8"背景と人物", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Checkbox("PBR", &settings.pbr);
            ImGui::SliderFloat("Roughness", &settings.roughness, 0.08f, 1);
            ImGui::SliderFloat("Metallic", &settings.metallic, 0, 1);
            ImGui::Checkbox(u8"トゥーン", &settings.toon);
            ImGui::Checkbox(u8"人物を材質別に描画", &settings.characterMaterials);
            if (settings.characterMaterials)
                ImGui::Checkbox(u8"人物の材質を色分け", &settings.showMaterialIds);
            if (settings.characterMaterials && settings.showMaterialIds)
                ImGui::TextWrapped(u8"橙: 肌  青: 髪  緑: 服  水色: 虹彩  黄: 光点");
            if (settings.characterMaterials && !settings.showMaterialIds)
            {
                ImGui::SliderFloat(u8"布の繊維感", &settings.clothDetail, 0, 2.0f);
                ImGui::SliderFloat(u8"肌の血色", &settings.skinTone, 0, 1.5f);
                ImGui::SliderFloat(u8"肌のやわらかさ", &settings.skinSoftness, 0, 1);
                ImGui::SliderFloat(u8"瞳の光", &settings.eyeReflection, 0, 2.0f);
            }
            ImGui::SliderFloat(u8"陰影境界", &toon.lightThreshold, 0.01f, 0.95f);
            ImGui::SliderFloat(u8"リムライト", &toon.rimStrength, 0, 1.5f);
            ImGui::Checkbox(u8"草木", &settings.plants);
            ImGui::SameLine();
            ImGui::Checkbox(u8"水たまり", &settings.water);
        }
        if (ImGui::CollapsingHeader(u8"空・フォグ・色調整", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Checkbox(u8"青空と雲", &settings.atmosphere);
            ImGui::Checkbox(u8"距離フォグ", &settings.fogEnabled);
            ImGui::ColorEdit3(u8"フォグ色", settings.fog, ImGuiColorEditFlags_NoInputs);
            ImGui::SliderFloat(u8"フォグ開始距離", &settings.fogStart, 0, 85);
            ImGui::SliderFloat(u8"フォグ密度", &settings.fogDensity, 0, 0.12f, "%.3f");
            ImGui::Checkbox(u8"高さで薄まる霞", &settings.aerialHaze);
            if (settings.aerialHaze)
            {
                ImGui::SliderFloat(u8"霞の高さ減衰", &settings.hazeFalloff, 0.002f, 0.15f, "%.3f /m");
                ImGui::SliderFloat(u8"太陽側の霞の明るさ", &settings.hazeSun, 0, 3);
                ImGui::SliderFloat(u8"霞の明るさ", &settings.hazeBrightness, 0.3f, 2);
            }
            ImGui::Checkbox(u8"HDRと色調整", &settings.hdr);
            ImGui::SliderFloat("Exposure", &settings.exposure, -3, 3, "%.2f EV");
            ImGui::SliderFloat(u8"彩度", &settings.saturation, 0, 2);
            ImGui::SliderFloat(u8"コントラスト", &settings.contrast, 0.4f, 1.7f);
        }
        if (ImGui::CollapsingHeader(u8"画面効果", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Checkbox(u8"アンビエントオクルージョン", &settings.ambientOcclusion);
            if (settings.ambientOcclusion)
            {
                ImGui::SliderFloat(u8"遮蔽の強さ", &settings.aoStrength, 0, 1.5f);
                ImGui::SliderFloat(u8"遮蔽の半径", &settings.aoRadius, 0.2f, 3.0f, "%.2f m");
                ImGui::SliderFloat(u8"遮蔽が消える距離", &settings.aoDistance, 10, 150, "%.0f m");
            }
            ImGui::Checkbox(u8"ブルーム", &settings.bloom);
            if (settings.bloom)
            {
                ImGui::SliderFloat(u8"にじみの強さ", &settings.bloomStrength, 0, 1);
                ImGui::SliderFloat(u8"にじみのしきい値", &settings.bloomThreshold, 0.2f, 3.0f);
            }
            ImGui::Checkbox(u8"色調補正", &settings.grading);
            if (settings.grading)
            {
                ImGui::SliderFloat(u8"日向と日陰の色分け", &settings.splitTone, 0, 1.5f);
                ImGui::SliderFloat(u8"緑のくすみ", &settings.olive, 0, 1);
                ImGui::SliderFloat(u8"周辺減光", &settings.vignette, 0, 0.8f);
            }
            if (!settings.hdr && settings.bloom)
                ImGui::TextWrapped(u8"ブルームは HDR が有効なときだけ働きます。");
        }
        ImGui::End();
        return resetCamera;
    }
}

struct GraphicsLab::State
{
    Settings settings;
    LabCamera camera;
    MeshField terrain;
    Wave wave;
    Wave::Layout pond;
    GameObject actor;
    AnimationModel character{&actor};
    ToonShader toon;
    Landscape landscape;
    ComPtr<ID3D11VertexShader> surfaceVS, screenVS;
    ComPtr<ID3D11PixelShader> surfacePS, shadowPS, leafShadowPS, skyPS, grovePS, postPS;
    ComPtr<ID3D11PixelShader> ssaoPS, aoBlurPS, bloomPrefilterPS;
    PostTargets windowPost;
    GpuTimer gpuTimer;
    bool timerReady = false, timerSampled = false;
    ComPtr<ID3D11InputLayout> inputLayout;
    ComPtr<ID3D11Buffer> lightingBuffer, surfaceBuffer;
    ComPtr<ID3D11RenderTargetView> hdrTarget, ldrTarget;
    ComPtr<ID3D11ShaderResourceView> hdrImage, ldrImage, shadowImage;
    ComPtr<ID3D11RenderTargetView> groveTarget;
    ComPtr<ID3D11ShaderResourceView> groveImage;
    ComPtr<ID3D11DepthStencilView> shadowDepth;
    ComPtr<ID3D11DepthStencilState> noDepth;
    ComPtr<ID3D11RasterizerState> twoSided, shadowRasterizer, sceneRasterizer;
    ComPtr<ID3D11SamplerState> shadowSampler, postSampler, surfaceSampler;
    std::string error;
    const char* captureStatus = nullptr;
    bool terrainReady = false, characterReady = false, ready = false;
    float time = 0.0f;

    ~State()
    {
        character.Uninit();
        if (terrainReady) terrain.Uninit();
    }

    bool createShaders()
    {
        auto* device = Renderer::GetDevice();
        const auto compile = [this](const char* entry, const char* target)
        {
            ComPtr<ID3DBlob> blob, output;
            const HRESULT hr = D3DCompileFromFile(L"shader/GraphicsLab.hlsl", nullptr,
                D3D_COMPILE_STANDARD_FILE_INCLUDE, entry, target, D3DCOMPILE_ENABLE_STRICTNESS, 0, &blob, &output);
            if (FAILED(hr))
            {
                error = entry;
                error += ": ";
                if (output) error.append(static_cast<const char*>(output->GetBufferPointer()), output->GetBufferSize() - 1);
                else error += "cannot open or compile shader/GraphicsLab.hlsl";
            }
            return blob;
        };
        auto vertex = compile("surfaceVS", "vs_5_0");
        if (!vertex || FAILED(device->CreateVertexShader(vertex->GetBufferPointer(), vertex->GetBufferSize(), nullptr, &surfaceVS)))
            return false;
        const D3D11_INPUT_ELEMENT_DESC layout[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(VERTEX_3D, Position), D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(VERTEX_3D, Normal), D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(VERTEX_3D, Diffuse), D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(VERTEX_3D, TexCoord), D3D11_INPUT_PER_VERTEX_DATA, 0}
        };
        if (FAILED(device->CreateInputLayout(layout, ARRAYSIZE(layout), vertex->GetBufferPointer(), vertex->GetBufferSize(), &inputLayout)))
            return false;

        auto screen = compile("screenVS", "vs_5_0");
        if (!screen || FAILED(device->CreateVertexShader(screen->GetBufferPointer(), screen->GetBufferSize(), nullptr, &screenVS)))
            return false;
        const auto pixel = [&](const char* entry, ComPtr<ID3D11PixelShader>& shader)
        {
            auto blob = compile(entry, "ps_5_0");
            return blob && SUCCEEDED(device->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &shader));
        };
        return pixel("surfacePS", surfacePS) && pixel("shadowPS", shadowPS) && pixel("leafShadowPS", leafShadowPS)
            && pixel("skyPS", skyPS) && pixel("grovePS", grovePS) && pixel("postPS", postPS)
            && pixel("ssaoPS", ssaoPS) && pixel("aoBlurPS", aoBlurPS) && pixel("bloomPrefilterPS", bloomPrefilterPS);
    }

    bool createColor(UINT width, UINT height, DXGI_FORMAT format, ID3D11RenderTargetView** target,
        ID3D11ShaderResourceView** image = nullptr)
    {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
        desc.Format = format;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | (image ? D3D11_BIND_SHADER_RESOURCE : 0);
        auto* device = Renderer::GetDevice();
        ComPtr<ID3D11Texture2D> texture;
        return SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &texture))
            && SUCCEEDED(device->CreateRenderTargetView(texture.Get(), nullptr, target))
            && (!image || SUCCEEDED(device->CreateShaderResourceView(texture.Get(), nullptr, image)));
    }

    bool createDepth(UINT width, UINT height, ID3D11DepthStencilView** target, ID3D11ShaderResourceView** image,
        ID3D11DepthStencilView** readOnly = nullptr)
    {
        auto* device = Renderer::GetDevice();
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
        desc.Format = DXGI_FORMAT_R32_TYPELESS;
        desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
        ComPtr<ID3D11Texture2D> texture;
        if (FAILED(device->CreateTexture2D(&desc, nullptr, &texture))) return false;

        D3D11_DEPTH_STENCIL_VIEW_DESC depth{};
        depth.Format = DXGI_FORMAT_D32_FLOAT;
        depth.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        D3D11_SHADER_RESOURCE_VIEW_DESC view{};
        view.Format = DXGI_FORMAT_R32_FLOAT;
        view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        view.Texture2D.MipLevels = 1;
        if (FAILED(device->CreateDepthStencilView(texture.Get(), &depth, target))
            || FAILED(device->CreateShaderResourceView(texture.Get(), &view, image))) return false;
        depth.Flags = D3D11_DSV_READ_ONLY_DEPTH;
        return !readOnly || SUCCEEDED(device->CreateDepthStencilView(texture.Get(), &depth, readOnly));
    }

    bool createPostTargets(UINT width, UINT height, PostTargets& post)
    {
        const UINT halfWidth = std::max(width/2,1u), halfHeight = std::max(height/2,1u);
        if (!createColor(halfWidth,halfHeight,DXGI_FORMAT_R16G16_FLOAT,&post.aoColor,&post.aoImage)
            || !createColor(halfWidth,halfHeight,DXGI_FORMAT_R16G16_FLOAT,&post.aoBlurColor,&post.aoBlurImage))
            return false;

        // ブルームは縮小段を GenerateMips で作るため、ミップ付きの描画先にする。
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = halfWidth;
        desc.Height = halfHeight;
        desc.MipLevels = bloomLevels;
        desc.ArraySize = desc.SampleDesc.Count = 1;
        desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
        auto* device = Renderer::GetDevice();
        ComPtr<ID3D11Texture2D> texture;
        D3D11_RENDER_TARGET_VIEW_DESC target{};
        target.Format = desc.Format;
        target.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        return SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &texture))
            && SUCCEEDED(device->CreateRenderTargetView(texture.Get(), &target, &post.bloomColor))
            && SUCCEEDED(device->CreateShaderResourceView(texture.Get(), nullptr, &post.bloomImage));
    }

    bool createTargets()
    {
        return createColor(SCREEN_WIDTH,SCREEN_HEIGHT,DXGI_FORMAT_R16G16B16A16_FLOAT,&hdrTarget,&hdrImage)
            && createColor(SCREEN_WIDTH,SCREEN_HEIGHT,DXGI_FORMAT_R8G8B8A8_UNORM,&ldrTarget,&ldrImage)
            && createColor(SCREEN_WIDTH/2,SCREEN_HEIGHT/2,DXGI_FORMAT_R16G16B16A16_FLOAT,&groveTarget,&groveImage)
            && createShadowMap()
            && createPostTargets(SCREEN_WIDTH,SCREEN_HEIGHT,windowPost);
    }

    UINT shadowResolution = 0;

    // 画質の段階に合わせてシャドウマップを作り直す。
    bool createShadowMap()
    {
        const UINT wanted = shadowSizes[settings.quality];
        if (wanted == shadowResolution && shadowDepth) return true;
        shadowDepth.Reset();
        shadowImage.Reset();
        if (!createDepth(wanted,wanted,&shadowDepth,&shadowImage)) return false;
        shadowResolution = wanted;
        return true;
    }

    bool createStates()
    {
        auto* device = Renderer::GetDevice();
        Renderer::GetDeviceContext()->RSGetState(&sceneRasterizer);
        D3D11_RASTERIZER_DESC raster{};
        raster.FillMode = D3D11_FILL_SOLID;
        raster.CullMode = D3D11_CULL_NONE;
        raster.DepthClipEnable = TRUE;
        if (FAILED(device->CreateRasterizerState(&raster, &twoSided))) return false;
        raster.DepthBias = 50;
        raster.SlopeScaledDepthBias = 1.3f;
        if (FAILED(device->CreateRasterizerState(&raster, &shadowRasterizer))) return false;
        D3D11_DEPTH_STENCIL_DESC depth{};
        depth.DepthFunc = D3D11_COMPARISON_ALWAYS;
        if (FAILED(device->CreateDepthStencilState(&depth, &noDepth))) return false;
        D3D11_SAMPLER_DESC sampler{};
        sampler.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
        std::fill_n(sampler.BorderColor, 4, 1.0f);
        sampler.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
        sampler.MaxAnisotropy = 1;
        sampler.MaxLOD = D3D11_FLOAT32_MAX;
        if (FAILED(device->CreateSamplerState(&sampler, &shadowSampler))) return false;
        D3D11_SAMPLER_DESC clamp{};
        clamp.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        clamp.AddressU = clamp.AddressV = clamp.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        clamp.ComparisonFunc = D3D11_COMPARISON_NEVER;
        clamp.MaxLOD = D3D11_FLOAT32_MAX;
        if (FAILED(device->CreateSamplerState(&clamp, &postSampler))) return false;
        // 地面を浅い角度で見ても砂利や土の粒がつぶれないよう、異方性を最大にする。
        D3D11_SAMPLER_DESC surface{};
        surface.Filter = D3D11_FILTER_ANISOTROPIC;
        surface.AddressU = surface.AddressV = surface.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        surface.MaxAnisotropy = 16;
        surface.ComparisonFunc = D3D11_COMPARISON_NEVER;
        surface.MaxLOD = D3D11_FLOAT32_MAX;
        return SUCCEEDED(device->CreateSamplerState(&surface, &surfaceSampler));
    }

    bool initialize()
    {
        if (!createShaders() || !createTargets() || !createStates()
            || !createBuffer(lightingBuffer, sizeof(FrameConstants), D3D11_BIND_CONSTANT_BUFFER)
            || !createBuffer(surfaceBuffer, sizeof(SurfaceConstants), D3D11_BIND_CONSTANT_BUFFER)) return false;
        timerReady = gpuTimer.init();

        terrain.Init();
        terrainReady = true;
        const auto grid = terrain.worldGrid();
        MeshField::Heights heights{};
        for (int row = 0; row < MeshField::GridSize; ++row)
        {
            for (int col = 0; col < MeshField::GridSize; ++col)
            {
                const float x = grid.x + col * grid.z, z = grid.y + row * grid.w;
                const float edge = std::min(1.0f, std::max(0.0f, (std::abs(x) - 10.0f) / 21.0f));
                heights[row * MeshField::GridSize + col] = edge * (0.52f + 0.47f * std::sin(x * 0.19f + z * 0.17f));
            }
        }
        terrain.setHeights(heights);
        if (!landscape.init(terrain, error)) return false;

        pond.pondHeight = 0.048f;
        pond.opacity = 0.73f;
        pond.reflection = 0.92f;
        pond.paintPond(1.25f, -2.9f, 1.75f, true);
        pond.paintPond(0.10f, -3.3f, 1.22f, true);
        pond.paintPond(1.20f, -4.25f, 1.05f, true);

        actor.SetPosition({1.14f, 0.0f, 2.72f});
        actor.SetRotation({0, -0.19f, 0});
        toon.init();
        resetCharacterLook(toon.settings());
        character.enableTextureMipmaps();
        character.enableCharacterMaterials();
        characterReady = HeroineAnimationAssets::loadCharacter(character, HeroineAnimationAssets::previewModel)
            != HeroineAnimationAssets::LoadResult::modelFailed;
        if (!characterReady)
            characterReady = HeroineAnimationAssets::loadCharacter(character, HeroineAnimationAssets::model)
                != HeroineAnimationAssets::LoadResult::modelFailed;
        if (characterReady)
        {
            HeroineAnimationAssets::loadMotion(character, "Idle");
        }
        else
            error = "Character FBX could not be loaded.";
        return true;
    }

    static void viewport(UINT width, UINT height)
    {
        const D3D11_VIEWPORT view{0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1};
        Renderer::GetDeviceContext()->RSSetViewports(1, &view);
    }

    void screen(ID3D11PixelShader* pixel) const
    {
        auto* context = Renderer::GetDeviceContext();
        context->OMSetDepthStencilState(noDepth.Get(), 0);
        context->RSSetState(twoSided.Get());
        context->IASetInputLayout(nullptr);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(screenVS.Get(), nullptr, 0);
        context->PSSetShader(pixel, nullptr, 0);
        context->Draw(3, 0);
    }

    void drawSurface(bool shadowPass)
    {
        auto* context = Renderer::GetDeviceContext();
        context->IASetInputLayout(inputLayout.Get());
        context->VSSetShader(surfaceVS.Get(), nullptr, 0);
        context->PSSetShader(shadowPass ? nullptr : surfacePS.Get(), nullptr, 0);
        context->PSSetConstantBuffers(surfaceSlot, 1, surfaceBuffer.GetAddressOf());

        const auto bindMaterial = [&](Surface kind)
        {
            const bool foliage = LabLandscape::isTreeFoliage(kind);
            if (shadowPass && !foliage)
            {
                context->PSSetShader(nullptr, nullptr, 0);
                return true;
            }

            const bool bark = LabLandscape::isTreeBark(kind);
            const unsigned int mapCount = kind == Surface::stationSign || foliage ? 1 : (bark ? 2 : 3);
            const bool hasMaps = landscape.textures[static_cast<unsigned int>(kind)].bind(mapCount);
            if (foliage && !hasMaps) return false;
            if (shadowPass) context->PSSetShader(leafShadowPS.Get(), nullptr, 0);

            float roughness = settings.roughness;
            float metallic = settings.metallic;
            if (bark) metallic = 0.0f;
            if (kind == Surface::steel || kind == Surface::roof) metallic = std::max(0.32f, metallic);
            if (kind == Surface::signalLens) roughness = std::max(0.16f, roughness * 0.42f);
            if (LabLandscape::isVegetation(kind) || kind == Surface::mountains) roughness = 0.91f;
            const SurfaceConstants params{
                {roughness, metallic, static_cast<float>(kind), settings.pbr ? 1.0f : 0.0f},
                {hasMaps ? 1.0f : 0.0f, foliage ? 1.0f : 0.0f, leafAlphaCutoff, 0}
            };
            context->UpdateSubresource(surfaceBuffer.Get(), 0, nullptr, &params, 0, 0);
            return true;
        };

        bindMaterial(Surface::ground);
        terrain.drawGeometry();
        Renderer::SetWorldMatrix(XMMatrixIdentity());
        for (unsigned int i=1; i<landscape.meshes.size(); ++i)
        {
            const auto kind = static_cast<Surface>(i);
            if (!settings.plants && LabLandscape::isVegetation(kind)) continue;
            // 山は影の範囲外が大半で、草の葉の影はシャドウマップの解像度では見えない。どちらも影に描かない。
            if (shadowPass && (kind == Surface::mountains || kind == Surface::leaves)) continue;
            if (bindMaterial(kind)) landscape.meshes[i].draw();
        }
        ID3D11ShaderResourceView* empty[3]{};
        context->PSSetShaderResources(materialTextureSlot, ARRAYSIZE(empty), empty);
    }

    FrameConstants makeFrame(FXMVECTOR sun, FXMMATRIX view, CXMMATRIX projection, CXMMATRIX lightViewProjection) const
    {
        FrameConstants frame{};
        XMStoreFloat4(&frame.sunDirection, sun);
        frame.sunColor = {settings.sun[0], settings.sun[1], settings.sun[2], settings.sunEnabled ? settings.sunPower : 0.0f};
        frame.ambientColor = {settings.ambient[0], settings.ambient[1], settings.ambient[2],
            settings.ambientEnabled ? settings.ambientPower : 0.0f};
        frame.fogColor = {settings.fog[0], settings.fog[1], settings.fog[2], 0};
        frame.fog = {settings.fogStart, settings.fogEnabled ? settings.fogDensity : 0.0f, 1,
            settings.atmosphere ? 1.0f : 0.0f};
        frame.shadow = {settings.shadows && settings.sunEnabled ? settings.shadowStrength : 0.0f,
            settings.shadowSoftness / shadowResolution, 0.00025f};
        // 人物の比較方式は影の設定とは独立させ、影を切っても切り替えられるようにする。
        frame.characterMaterialMode = settings.characterMaterials ? (settings.showMaterialIds ? 2.0f : 1.0f) : 0.0f;
        frame.characterDetail = {settings.clothDetail, settings.skinTone, settings.eyeReflection, settings.skinSoftness};
        const bool lightShafts = settings.groveRays && settings.plants && frame.shadow.x > 0.0f;
        frame.groveLight = {lightShafts ? settings.groveStrength : 0.0f,settings.leafTransmission,cameraNear,cameraFar};
        frame.post = {settings.exposure, settings.saturation, settings.contrast, settings.hdr ? 1.0f : 0.0f};
        frame.ao = {settings.aoStrength, settings.aoRadius, settings.aoDistance, settings.ambientOcclusion ? 1.0f : 0.0f};
        frame.bloom = {settings.bloomStrength, settings.bloomThreshold, settings.bloomThreshold * 0.5f,
            settings.bloom && settings.hdr ? 1.0f : 0.0f};
        frame.grade = {settings.splitTone, settings.vignette, settings.olive, settings.grading ? 1.0f : 0.0f};
        frame.haze = {settings.hazeFalloff, settings.hazeSun, settings.hazeBrightness, settings.aerialHaze ? 1.0f : 0.0f};
        XMStoreFloat4x4(&frame.viewProjection, XMMatrixTranspose(view * projection));
        XMStoreFloat4x4(&frame.sunViewProjection, XMMatrixTranspose(lightViewProjection));
        XMStoreFloat4x4(&frame.inverseViewProjection, XMMatrixTranspose(XMMatrixInverse(nullptr, view * projection)));
        return frame;
    }

    void render(const RenderDestination& destination, float dt)
    {
        createShadowMap();
        const Vector3 eye = camera.position();
        const Vector3 focus = camera.focus();
        const auto up = XMVectorSet(0, 1, 0, 0);
        const auto view = XMMatrixLookAtLH(XMVectorSet(eye.x, eye.y, eye.z, 0),
            XMVectorSet(focus.x, focus.y, focus.z, 0), up);
        const auto projection = XMMatrixPerspectiveFovLH(camera.fieldOfView(),
            destination.width / static_cast<float>(destination.height), cameraNear, cameraFar);
        const float yaw = XMConvertToRadians(settings.yaw), pitch = XMConvertToRadians(settings.pitch);
        const auto sun = XMVectorSet(std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch), 0);
        // 高さ約29mの梢から地面までを含め、背の高い木でも枝が影の範囲外に出ないようにする。
        // 手前の里山に植えた木まで含め、山裾の林床にも木漏れ日の影を落とす。
        const auto center = XMVectorSet(-2, 17, 33, 0);
        const auto lightView = XMMatrixLookAtLH(center + sun * 80, center, up);
        const auto bounds = XMVectorSet(52, 24, 56, 0);
        const auto lightAxes = XMMatrixTranspose(lightView);
        const auto halfX = XMVectorGetX(XMVector3Dot(bounds,XMVectorAbs(lightAxes.r[0])));
        const auto halfY = XMVectorGetX(XMVector3Dot(bounds,XMVectorAbs(lightAxes.r[1])));
        const auto halfZ = XMVectorGetX(XMVector3Dot(bounds,XMVectorAbs(lightAxes.r[2])));
        const auto lightProjection = XMMatrixOrthographicLH(2*halfX, 2*halfY, 80-halfZ, 80+halfZ);
        auto frame = makeFrame(sun, view, projection, lightView * lightProjection);
        auto* context = Renderer::GetDeviceContext();
        context->UpdateSubresource(lightingBuffer.Get(), 0, nullptr, &frame, 0, 0);
        context->PSSetConstantBuffers(lightingSlot, 1, lightingBuffer.GetAddressOf());
        ComPtr<ID3D11SamplerState> previousSampler;
        context->PSGetSamplers(0, 1, &previousSampler);
        context->PSSetSamplers(0, 1, surfaceSampler.GetAddressOf());
        Renderer::SetCameraPosition(eye);
        Renderer::SetObjectOpacity(1.0f);
        LIGHT waterLight{};
        waterLight.Enable = frame.sunColor.w > 0.0f;
        XMStoreFloat4(&waterLight.Direction, -sun);
        waterLight.Diffuse = frame.sunColor;
        waterLight.Ambient = frame.ambientColor;
        Renderer::SetLight(waterLight);

        time += dt;
        if (characterReady)
            character.sampleClip("Idle", time, AnimationModel::RootTravel::lockTranslation);

        // 影を書き込む間は、同じテクスチャを参照するスロットを外す。
        ID3D11ShaderResourceView* empty = nullptr;
        context->PSSetShaderResources(shadowResourceSlot, 1, &empty);
        // 撮影時の巨大な描画先は測らず、ウィンドウ表示の負荷だけを記録する。
        const bool timed = timerReady && destination.post == &windowPost;
        if (timed) gpuTimer.begin();
        if (frame.shadow.x > 0)
        {
            viewport(shadowResolution, shadowResolution);
            context->OMSetRenderTargets(0, nullptr, shadowDepth.Get());
            context->ClearDepthStencilView(shadowDepth.Get(), D3D11_CLEAR_DEPTH, 1, 0);
            Renderer::SetDepthEnable(true);
            context->RSSetState(shadowRasterizer.Get());
            Renderer::SetViewMatrix(lightView);
            Renderer::SetProjectionMatrix(lightProjection);
            drawSurface(true);
            if (characterReady)
            {
                context->PSSetShader(shadowPS.Get(), nullptr, 0);
                character.drawOutline();
            }
        }

        if (timed) gpuTimer.mark(GpuTimer::shadow);
        context->OMSetRenderTargets(1, &destination.sceneColor, destination.depth);
        context->ClearRenderTargetView(destination.sceneColor, clearColor);
        if (destination.depthImage)
            context->ClearDepthStencilView(destination.depth, D3D11_CLEAR_DEPTH, 1, 0);
        viewport(destination.width, destination.height);
        if (settings.atmosphere) screen(skyPS.Get());

        Renderer::SetDepthEnable(true);
        context->RSSetState(twoSided.Get());
        Renderer::SetViewMatrix(view);
        Renderer::SetProjectionMatrix(projection);
        context->PSSetShaderResources(shadowResourceSlot, 1, shadowImage.GetAddressOf());
        context->PSSetSamplers(4, 1, shadowSampler.GetAddressOf());
        drawSurface(false);
        ID3D11Buffer* noSurface = nullptr;
        context->PSSetConstantBuffers(surfaceSlot,1,&noSurface);
        context->RSSetState(sceneRasterizer.Get());
        if (timed) gpuTimer.mark(GpuTimer::landscape);

        if (characterReady)
        {
            toon.settings().shadingMode = settings.toon ? 0 : 1;
            toon.bindSurface();
            character.Draw();
            if (settings.toon) toon.drawOutline(character);
        }

        if (settings.water)
            wave.draw(pond, terrain, eye, view * projection, dt, destination.waterDepth, destination.depthImage);
        if (timed) gpuTimer.mark(GpuTimer::characterAndWater);

        auto* back = Renderer::backBuffer();
        // 後処理の各パスは不透明で書き、終わったら元の合成設定へ戻す。
        ComPtr<ID3D11BlendState> previousBlend;
        float blendFactor[4]{};
        UINT sampleMask = 0;
        context->OMGetBlendState(&previousBlend,blendFactor,&sampleMask);
        context->OMSetBlendState(nullptr,nullptr,0xffffffff);
        context->PSSetSamplers(postSamplerSlot, 1, postSampler.GetAddressOf());
        const PostTargets* post = destination.post;
        if (!post && (frame.ao.w > 0.0f || frame.bloom.w > 0.0f))
        {
            frame.ao.w = frame.bloom.w = 0.0f;
            context->UpdateSubresource(lightingBuffer.Get(), 0, nullptr, &frame, 0, 0);
        }
        // 深度を読み込めるよう出力から外す。
        auto* depthImage = destination.depthImage ? destination.depthImage : Renderer::sceneDepth();
        context->OMSetRenderTargets(0,nullptr,nullptr);
        context->PSSetShaderResources(groveDepthSlot,1,&depthImage);
        const UINT halfWidth = std::max(destination.width/2,1u), halfHeight = std::max(destination.height/2,1u);
        if (frame.groveLight.x > 0.0f)
        {
            // 低解像度で林内の光の散乱を計算する。
            context->OMSetRenderTargets(1,&destination.groveColor,nullptr);
            viewport(halfWidth,halfHeight);
            screen(grovePS.Get());
        }
        if (post && frame.ao.w > 0.0f)
        {
            // 遮蔽を求めてから、奥行きをまたがないようにぼかす。
            context->OMSetRenderTargets(1,post->aoColor.GetAddressOf(),nullptr);
            viewport(halfWidth,halfHeight);
            screen(ssaoPS.Get());
            context->OMSetRenderTargets(1,post->aoBlurColor.GetAddressOf(),nullptr);
            context->PSSetShaderResources(aoSlot,1,post->aoImage.GetAddressOf());
            screen(aoBlurPS.Get());
            context->OMSetRenderTargets(0,nullptr,nullptr);
            context->PSSetShaderResources(aoSlot,1,post->aoBlurImage.GetAddressOf());
        }
        if (post && frame.bloom.w > 0.0f)
        {
            // 明るい部分を半解像度へ抜き出し、ミップで段階的に広げる。
            context->OMSetRenderTargets(1,post->bloomColor.GetAddressOf(),nullptr);
            viewport(halfWidth,halfHeight);
            context->PSSetShaderResources(imageSlot, 1, &destination.sceneImage);
            screen(bloomPrefilterPS.Get());
            context->OMSetRenderTargets(0,nullptr,nullptr);
            context->GenerateMips(post->bloomImage.Get());
            context->PSSetShaderResources(bloomSlot,1,post->bloomImage.GetAddressOf());
        }
        context->OMSetRenderTargets(1, &destination.finalColor, nullptr);
        viewport(destination.width,destination.height);
        context->PSSetShaderResources(imageSlot, 1, &destination.sceneImage);
        if (frame.groveLight.x > 0.0f) context->PSSetShaderResources(groveImageSlot,1,&destination.groveImage);
        screen(postPS.Get());
        context->OMSetBlendState(previousBlend.Get(),blendFactor,sampleMask);
        if (timed)
        {
            gpuTimer.mark(GpuTimer::post);
            timerSampled = gpuTimer.end();
        }

        // ImGui はトーンマッピング後に描画する。次のフレームへ専用設定を残さない。
        context->PSSetShaderResources(imageSlot, 1, &empty);
        context->PSSetShaderResources(shadowResourceSlot, 1, &empty);
        context->PSSetShaderResources(groveDepthSlot,1,&empty);
        context->PSSetShaderResources(groveImageSlot,1,&empty);
        context->PSSetShaderResources(aoSlot,1,&empty);
        context->PSSetShaderResources(bloomSlot,1,&empty);
        context->PSSetSamplers(0, 1, previousSampler.GetAddressOf());
        ID3D11Buffer* noBuffer = nullptr;
        context->PSSetConstantBuffers(lightingSlot, 1, &noBuffer);
        context->OMSetRenderTargets(1, &back, Renderer::depthTarget());
        viewport(SCREEN_WIDTH, SCREEN_HEIGHT);
        Renderer::SetDepthEnable(true);
        context->RSSetState(sceneRasterizer.Get());
    }

    void renderWindow(float dt)
    {
        render({SCREEN_WIDTH,SCREEN_HEIGHT,settings.hdr ? hdrTarget.Get() : ldrTarget.Get(),
            settings.hdr ? hdrImage.Get() : ldrImage.Get(),Renderer::backBuffer(),Renderer::depthTarget(),
            nullptr,nullptr,groveTarget.Get(),groveImage.Get(),&windowPost},dt);
    }

    const char* saveScreenshot(const std::wstring& folderOverride = {}, UINT outputWidth = pngWidth,
        UINT outputHeight = pngHeight, bool fixedName = false)
    {
        const auto folder = folderOverride.empty() ? screenshotDirectory() : folderOverride;
        if (folder.empty()) return u8"画像の保存先を作成できませんでした。";

        ComPtr<ID3D11RenderTargetView> sceneColor, finalColor;
        ComPtr<ID3D11ShaderResourceView> sceneImage, depthImage;
        ComPtr<ID3D11DepthStencilView> depth, waterDepth;
        ComPtr<ID3D11RenderTargetView> captureGrove;
        ComPtr<ID3D11ShaderResourceView> captureGroveImage;
        PostTargets capturePost;
        const auto sceneFormat = settings.hdr ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM;
        if (!createColor(captureWidth,captureHeight,sceneFormat,&sceneColor,&sceneImage)
            || !createColor(captureWidth,captureHeight,DXGI_FORMAT_R8G8B8A8_UNORM,&finalColor)
            || !createColor(captureWidth/2,captureHeight/2,DXGI_FORMAT_R16G16B16A16_FLOAT,&captureGrove,&captureGroveImage)
            || !createDepth(captureWidth,captureHeight,&depth,&depthImage,&waterDepth)
            || !createPostTargets(captureWidth,captureHeight,capturePost))
            return u8"撮影用の描画バッファを作成できませんでした。";

        // ウィンドウを拡大せず、今の視点を撮影専用バッファに描く。アニメーションは進めない。
        render({captureWidth,captureHeight,sceneColor.Get(),sceneImage.Get(),finalColor.Get(),depth.Get(),
            waterDepth.Get(),depthImage.Get(),captureGrove.Get(),captureGroveImage.Get(),&capturePost},0.0f);
        ComPtr<ID3D11Resource> source;
        finalColor->GetResource(&source);
        ScratchImage rendered, image;
        if (FAILED(CaptureTexture(Renderer::GetDevice(),Renderer::GetDeviceContext(),source.Get(),rendered)))
            return u8"描画結果を読み出せませんでした。";

        // ガンマを戻してから縮小し、細い枝葉の輪郭に出るジャギーを抑える。
        if (FAILED(Resize(*rendered.GetImage(0,0,0),outputWidth,outputHeight,TEX_FILTER_TRIANGLE | TEX_FILTER_SRGB,image)))
            return u8"撮影画像を4Kに変換できませんでした。";

        SYSTEMTIME now{};
        GetLocalTime(&now);
        wchar_t fileName[96];
        if (fixedName)
            swprintf_s(fileName,L"%ls.png",shotFileNames[settings.camera]);
        else
            swprintf_s(fileName,L"%ls-%04u%02u%02u-%02u%02u%02u-%03u.png",shotFileNames[settings.camera],
                now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute,now.wSecond,now.wMilliseconds);
        const auto filePath = folder + L"\\" + fileName;
        if (FAILED(SaveToWICFile(*image.GetImage(0,0,0),WIC_FLAGS_FORCE_SRGB,GetWICCodec(WIC_CODEC_PNG),filePath.c_str())))
            return u8"PNGを書き込めませんでした。";
        return u8"4K PNGを保存しました。";
    }

    // --lab-capture <フォルダ> で起動すると、全視点を撮影して終了する。画作りの前後比較に使う。
    struct BatchCapture
    {
        std::wstring folder;
        std::vector<int> shots;
        int warmupFrames = 4;
        // 撮影の前に、各視点をウィンドウ解像度で描いて GPU 時間を平均する。
        size_t timingShot = 0;
        int timingFrame = 0, samples = 0;
        std::array<double, GpuTimer::sectionCount> sum{};
        std::vector<std::array<float, GpuTimer::sectionCount>> timings;
        bool timingDone = false;
    } batch;

    // 計測中なら true を返す。
    bool measureBatchTiming()
    {
        if (batch.timingDone || !timerReady) return false;
        constexpr int settleFrames = 12, measuredFrames = 60;
        if (batch.timingFrame >= settleFrames && timerSampled)
        {
            for (int i = 0; i < GpuTimer::sectionCount; ++i) batch.sum[i] += gpuTimer.latest[i];
            ++batch.samples;
        }
        if (batch.timingFrame > 0 && batch.timingFrame >= settleFrames + measuredFrames)
        {
            std::array<float, GpuTimer::sectionCount> average{};
            for (int i = 0; i < GpuTimer::sectionCount; ++i)
                average[i] = batch.samples ? static_cast<float>(batch.sum[i] / batch.samples) : 0.0f;
            batch.timings.push_back(average);
            batch.sum = {};
            batch.samples = 0;
            batch.timingFrame = 0;
            ++batch.timingShot;
        }
        if (batch.timingShot >= batch.shots.size())
        {
            batch.timingDone = true;
            return false;
        }
        if (batch.timingFrame == 0)
        {
            const int shot = batch.shots[batch.timingShot];
            settings.camera = shot;
            camera.setShot(cameraShots[shot]);
        }
        ++batch.timingFrame;
        return true;
    }

    void readCommandLine()
    {
        int count = 0;
        wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &count);
        if (!arguments) return;
        for (int i = 1; i + 1 < count; ++i)
        {
            const std::wstring name = arguments[i], value = arguments[i + 1];
            if (name == L"--lab-capture") batch.folder = value;
            else if (name == L"--lab-quality")
                applyQuality(settings, value == L"low" ? 2 : (value == L"medium" ? 1 : 0));
            else if (name == L"--lab-shots")
            {
                for (size_t start = 0; start <= value.size();)
                {
                    const size_t end = std::min(value.find(L',', start), value.size());
                    if (end > start) batch.shots.push_back(std::stoi(value.substr(start, end - start)));
                    start = end + 1;
                }
            }
            else if (name == L"--lab-off")
            {
                // 比較用に、指定した効果だけを切る。
                const auto has = [&](const wchar_t* key) { return value.find(key) != std::wstring::npos; };
                if (has(L"ao")) settings.ambientOcclusion = false;
                if (has(L"bloom")) settings.bloom = false;
                if (has(L"grade")) settings.grading = false;
                if (has(L"grove")) settings.groveRays = false;
                if (has(L"haze")) settings.aerialHaze = false;
            }
        }
        LocalFree(arguments);
        if (!batch.folder.empty() && batch.shots.empty())
            for (int i = 0; i < IM_ARRAYSIZE(cameraShots); ++i) batch.shots.push_back(i);
    }

    bool runBatchCapture()
    {
        if (batch.folder.empty()) return false;
        if (batch.warmupFrames-- > 0) return true;
        if (measureBatchTiming()) return true;
        CreateDirectoryW(batch.folder.c_str(), nullptr);
        // 描画負荷の目安として、材質ごとの頂点数を書き出す。
        if (FILE* stats = _wfopen((batch.folder + L"\\stats.txt").c_str(), L"w"))
        {
            UINT total = 0;
            for (unsigned int i = 0; i < landscape.meshes.size(); ++i)
            {
                fprintf(stats, "surface %u: %u vertices\n", i, landscape.meshes[i].count);
                total += landscape.meshes[i].count;
            }
            fprintf(stats, "total: %u vertices\n", total);
            fprintf(stats, "\nGPU ms at %dx%d (shadow / landscape / character+water / post / total)\n",
                SCREEN_WIDTH, SCREEN_HEIGHT);
            for (size_t i = 0; i < batch.timings.size() && i < batch.shots.size(); ++i)
            {
                const auto& t = batch.timings[i];
                fprintf(stats, "%-20ls %6.2f %6.2f %6.2f %6.2f  = %6.2f\n", shotFileNames[batch.shots[i]],
                    t[0], t[1], t[2], t[3], t[0] + t[1] + t[2] + t[3]);
            }
            fclose(stats);
        }
        for (const int shot : batch.shots)
        {
            if (shot < 0 || shot >= IM_ARRAYSIZE(cameraShots)) continue;
            settings.camera = shot;
            camera.setShot(cameraShots[shot]);
            saveScreenshot(batch.folder, 1920, 1080, true);
        }
        batch.folder.clear();
        PostQuitMessage(0);
        return true;
    }
};

GraphicsLab::GraphicsLab() = default;
GraphicsLab::~GraphicsLab() = default;

void GraphicsLab::Init()
{
    m_state = std::make_unique<State>();
    m_state->readCommandLine();
    m_state->camera.setShot(cameraShots[m_state->settings.camera]);
    m_state->ready = m_state->initialize();
    if (!m_state->ready && m_state->error.empty())
        m_state->error = "Failed to initialize graphics lab render resources.";
}

void GraphicsLab::Draw()
{
    ToonPanel::beginFrame();
    bool capture = false;
    if (drawControls(m_state->settings, m_state->toon.settings(), m_state->error,
        m_state->ready,capture,m_state->captureStatus))
        m_state->camera.setShot(cameraShots[m_state->settings.camera]);
    if (m_state->ready)
    {
        const float dt = Manager::animationDeltaSeconds();
        m_state->camera.update(m_state->actor.GetPosition(),dt);
        m_state->renderWindow(dt);
        if (capture) m_state->captureStatus = m_state->saveScreenshot();
        m_state->runBatchCapture();
        if (m_state->timerReady)
        {
            // 垂直同期の待ち時間を含まない、GPU だけの処理時間を表示する。
            const auto& timer = m_state->gpuTimer;
            ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 230.0f, 10.0f), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(220.0f, 0.0f), ImGuiCond_FirstUseEver);
            ImGui::Begin(u8"GPU 計測");
            float total = 0.0f;
            for (int i = 0; i < GpuTimer::sectionCount; ++i)
            {
                ImGui::Text("%s: %.2f ms", GpuTimer::names[i], timer.smoothed[i]);
                total += timer.smoothed[i];
            }
            ImGui::Separator();
            ImGui::Text(u8"合計: %.2f ms", total);
            ImGui::End();
        }
    }
    ToonPanel::draw();
}
