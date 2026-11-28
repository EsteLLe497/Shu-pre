// =========================================================
// ファイルの情報[animationModel.h]
// 制作者:Masatora Tanaka    日付：2026/11/01
// =========================================================
#pragma once

#include <unordered_map>
#include <functional>
#include "MorphTarget.h"

#include "assimp/cimport.h"
#include "assimp/scene.h"
#include "assimp/postprocess.h"
#include "assimp/matrix4x4.h"
#pragma comment (lib, "assimp-vc143-mt.lib")

#include "component.h"


//変形後頂点構造体
struct DEFORM_VERTEX
{
	aiVector3D Position;
	aiVector3D Normal;
	int				BoneNum;
	std::string		BoneName[4];//本来はボーンインデックスで管理するべき
	float			BoneWeight[4];
};

//ボーン構造体
struct BONE
{
	aiMatrix4x4 Matrix;
    bool isSkinBone = false;
	aiMatrix4x4 AnimationMatrix;
	aiMatrix4x4 BindMatrix; // Local transform when the animation has no channel.
	aiMatrix4x4 OffsetMatrix;
};

struct BoneCapsule
{
    std::string bone;
    XMFLOAT3 center{};
    XMFLOAT3 axis{};
    float radius = 0.0f;
    float halfLength = 0.0f;
    XMFLOAT4X4 inverseBindSkin{};
};

class AnimationModel : public Component
{
public:
    enum class RootTravel { authored, removeHorizontalCycle, anchorLandingFeet, lockTranslation };

private:
	const aiScene* m_AiScene = nullptr;
	std::string m_ModelPath; // Reuse embedded animation when the source FBX is the model.
	float m_skeletonUnitScale = 1.0f; // Converts skeleton movement to mesh units.
	float m_modelYawOffset = 0.0f; // Visual-only facing correction in radians.
	std::unordered_map<std::string, const aiScene*> m_Animation;

	ID3D11Buffer** m_VertexBuffer = nullptr;
	ID3D11Buffer** m_IndexBuffer = nullptr;
    std::vector<std::string> m_morphNames;
    struct MorphLayer
    {
        std::vector<const aiAnimMesh*> meshes;
        std::string name;
        float weight = 0.0f;
    };

    std::vector<MorphLayer> m_morphLayers;
    MorphLayer m_overlayMorph;
    std::size_t m_activeMorphLayerCount = 0;
    unsigned int m_missingTextures = 0;
    bool m_generateTextureMipmaps = false;
    bool m_useCharacterMaterials = false;
    XMFLOAT3 m_staticHalfExtents{};
    float m_windReach = 0.0f;
    std::vector<UINT> m_indexCounts;

    // マテリアルを読み込み時に解決し、不透明と透過の描画順を分ける。
    enum class CharacterMaterial { generic, skin, hair, faceDetail, cloth, eyeIris, eyeWhite, eyeHighlight };

    struct DrawPart
    {
        unsigned int meshIndex;
        XMFLOAT4 diffuse;
        ID3D11ShaderResourceView* texture;
        CharacterMaterial characterMaterial;
        XMFLOAT4 irisUvBounds;
        XMFLOAT4 wind{};
    };

    std::unordered_map<const aiTexture*, ID3D11ShaderResourceView*> m_textures;
    std::unordered_map<std::string, ID3D11ShaderResourceView*> m_externalTextures;
    std::vector<DrawPart> m_opaqueParts;
    std::vector<DrawPart> m_transparentParts;

	std::vector<DEFORM_VERTEX>* m_DeformVertex = nullptr;
	std::unordered_map<std::string, BONE> m_Bone;//ボーンデータ（名前で参照）

    struct TransitionBone
    {
        BONE* bone;
        aiVector3D scale;
        aiQuaternion rotation;
        aiVector3D position;
    };

    std::vector<TransitionBone> m_transitionPose;
    std::string m_sampledClip;
    RootTravel m_sampledRootTravel = RootTravel::authored;
    float m_transitionElapsed = 0.0f;
    std::function<void(AnimationModel&)> m_poseModifier;
    std::vector<std::pair<BONE*, aiMatrix4x4>> m_poseRestore;

    const aiNode* m_landingArmature = nullptr;
    const aiNode* m_landingLeftFoot = nullptr;
    const aiNode* m_landingRightFoot = nullptr;
    float m_landingFootCenterZ = 0.0f;

	void CreateBone(aiNode* node);
	void UpdateBoneMatrix(aiNode* Node, aiMatrix4x4 Matrix);
    void uploadPose();
    bool samplePose(const char* name, double seconds, RootTravel rootTravel, float dt, bool restart, bool loop);
    float updatePoseTransition(const char* name, RootTravel rootTravel, float dt, bool restart);
    void blendTransitionPose(float weight);
    aiMatrix4x4 poseMatrix(const aiNode* node) const;
    float landingFootCenterZ() const;
    void anchorLandingFeet();
    void sampleBones(const aiAnimation& animation, double tick, RootTravel rootTravel);
    void applyPose(const aiAnimation& animation, double tick, RootTravel rootTravel, float weight);
    void loadDrawParts(bool staticMesh);
    void setMorphLayer(MorphLayer& layer, const std::string& name, float weight);
    ID3D11ShaderResourceView* loadTexture(const aiTexture* embedded);
    void createTextureView(const DirectX::ScratchImage& image, ID3D11ShaderResourceView** view) const;
    void drawParts(const std::vector<DrawPart>& parts, bool alphaBlend, UINT instanceCount = 0);

public:
	using Component::Component;

	void setSkeletonUnitScale(float unitScale) { m_skeletonUnitScale = unitScale; }
	void setModelYawOffset(float yaw) { m_modelYawOffset = yaw; }
	void Load( const char *FileName );
    bool LoadAnimation(const char* fileName, const char* name);
    bool tryLoad(const char* fileName, bool staticMesh = false);
    // Graphics Lab の人物材質とミップマップの切り替え 担当:ozawa souma        日付:2026/11/28
    void enableTextureMipmaps() { m_generateTextureMipmaps = true; }
    void enableCharacterMaterials() { m_useCharacterMaterials = true; }
    XMFLOAT3 staticHalfExtents() const { return m_staticHalfExtents; }
    float windReach() const { return m_windReach; }
    double clipDuration(const char* name) const;
    bool sampleClip(const char* name, double seconds, RootTravel rootTravel = RootTravel::authored);
    bool sampleBlendedClip(const char* name, double seconds, float dt, RootTravel rootTravel, bool restart = false, bool loop = true);
    bool hasAnimation(const char* name) const;
    void unloadAnimation(const char* name);
    void setMorph(const std::string& name, float weight);
    void setOverlayMorph(const std::string& name, float weight);
    void setMorphBlend(const std::string& base, const std::string& target, float blend,
        const std::vector<MorphTarget>& extraMorphs = {}, float time = 0.0f);
    const std::vector<std::string>& morphNames() const { return m_morphNames; }
    unsigned int missingTextures() const { return m_missingTextures; }
	void Uninit() override;
	void Draw() override;
    void drawOutline();
    void drawAt(FXMMATRIX world);
    bool hasTransparentParts() const { return !m_transparentParts.empty(); }
    void drawInstances(UINT count);
    bool fitBoneCapsule(const char* name, BoneCapsule& capsule) const;
    std::vector<std::string> dominantSkinBones(size_t limit) const;
    bool loadEmbeddedClip(const char* name);
    void applyBindPose();
    void refreshPose();
    void setPoseModifier(const std::function<void(AnimationModel&)>& modifier) { m_poseModifier = modifier; }
    bool boneModelTransform(const char* name, aiMatrix4x4& transform) const;
    bool rotateBoneModelSpace(const char* name, const aiMatrix4x4& rotation);
    XMMATRIX capsulePose(const BoneCapsule& capsule) const;
};