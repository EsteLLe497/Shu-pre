// =========================================================
// ファイルの情報[animationModel.h]
// 制作者:Masatora Tanaka    日付：2026/11/01
// =========================================================
#pragma once

#include <unordered_map>

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
	aiMatrix4x4 worldMatrix; // World pose before the mesh bone offset.
	aiMatrix4x4 AnimationMatrix;
	aiMatrix4x4 BindMatrix; // Local transform when the animation has no channel.
	aiMatrix4x4 OffsetMatrix;
};

class AnimationModel : public Component
{
private:
	const aiScene* m_AiScene = nullptr;
	std::string m_ModelPath; // Reuse embedded animation when the source FBX is the model.
	float m_skeletonUnitScale = 1.0f; // Converts skeleton movement to mesh units.
	float m_modelYawOffset = 0.0f; // Visual-only facing correction in radians.
	std::unordered_map<std::string, const aiScene*> m_Animation;

	ID3D11Buffer**	m_VertexBuffer;
	ID3D11Buffer**	m_IndexBuffer;

    // マテリアルを読み込み時に解決し、不透明と透過の描画順を分ける。
    struct DrawPart
    {
        unsigned int meshIndex;
        XMFLOAT4 diffuse;
        ID3D11ShaderResourceView* texture;
    };

    std::unordered_map<const aiTexture*, ID3D11ShaderResourceView*> m_textures;
    std::vector<DrawPart> m_opaqueParts;
    std::vector<DrawPart> m_transparentParts;

	std::vector<DEFORM_VERTEX>* m_DeformVertex;//変形後頂点データ
	std::unordered_map<std::string, BONE> m_Bone;//ボーンデータ（名前で参照）

	void CreateBone(aiNode* node);
	void UpdateBoneMatrix(aiNode* Node, aiMatrix4x4 Matrix);
	void stabilizeTorso(float weight);
    void loadDrawParts();
    ID3D11ShaderResourceView* loadTexture(const aiTexture* embedded);
    void drawParts(const std::vector<DrawPart>& parts, bool alphaBlend);

public:
	using Component::Component;

	void setSkeletonUnitScale(float unitScale) { m_skeletonUnitScale = unitScale; }
	void setModelYawOffset(float yaw) { m_modelYawOffset = yaw; }
	void Load( const char *FileName );
	void LoadAnimation( const char *FileName, const char *Name );
    float getRootMotionSpeed(const char* animationName, float playbackRate) const;
	void Uninit() override;
	void Update(const char* animationName1, double time1,
				const char* animationName2, double time2, float blend);
	void Draw() override;
    void drawOutline();
};