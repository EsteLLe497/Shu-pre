// =========================================================
// ファイルの情報[animationModel.cpp]
// 制作者:Masatora Tanaka    日付：2026/11/01
// =========================================================
	#include "main.h"
	#include "renderer.h"
	#include "animationModel.h"
	#include "GameObject.h"
#include <wrl/client.h>

// =========================================================
// キー時刻の前後を二分探索し、途中の姿勢を補間
// =========================================================
namespace
{
    template<class Key>
    unsigned int findKeyBefore(const Key* keys, unsigned int count, double time)
    {
        unsigned int first = 0;
        unsigned int last = count - 1;
        while (last - first > 1)
        {
            const unsigned int middle = first + (last - first) / 2;
            if (keys[middle].mTime <= time) first = middle;
            else last = middle;
        }
        return first;
    }

    aiVector3D sampleVector(const aiVectorKey* keys, unsigned int count, double time)
    {
        if (count == 1 || time <= keys[0].mTime) return keys[0].mValue;
        if (time >= keys[count - 1].mTime) return keys[count - 1].mValue;

        const unsigned int first = findKeyBefore(keys, count, time);
        const double span = keys[first + 1].mTime - keys[first].mTime;
        const float blend = span > 0.0 ? static_cast<float>((time - keys[first].mTime) / span) : 0.0f;
        return keys[first].mValue * (1.0f - blend) + keys[first + 1].mValue * blend;
    }

    aiQuaternion sampleRotation(const aiQuatKey* keys, unsigned int count, double time)
    {
        if (count == 1 || time <= keys[0].mTime) return keys[0].mValue;
        if (time >= keys[count - 1].mTime) return keys[count - 1].mValue;

        const unsigned int first = findKeyBefore(keys, count, time);
        const double span = keys[first + 1].mTime - keys[first].mTime;
        const float blend = span > 0.0 ? static_cast<float>((time - keys[first].mTime) / span) : 0.0f;
        aiQuaternion rotation;
        aiQuaternion::Interpolate(rotation, keys[first].mValue, keys[first + 1].mValue, blend);
        return rotation.Normalize();
    }

    // Keep forward lean, but lock the root's sideways tilt and heading.
    aiQuaternion keepRootPitch(const aiQuaternion& rotation, const aiQuaternion& bindRotation)
    {
        aiQuaternion bindInverse = bindRotation;
        bindInverse.Conjugate();
        const aiQuaternion relativeRotation = rotation * bindInverse;
        aiQuaternion pitch(relativeRotation.w, relativeRotation.x, 0.0f, 0.0f);
        if (pitch.w * pitch.w + pitch.x * pitch.x < 0.0001f) return bindRotation;
        return pitch.Normalize() * bindRotation;
    }
    double wrapAnimationTime(double time, const aiAnimation* animation)
    {
        if (animation->mDuration <= 0.0) return 0.0;
        const double wrapped = fmod(time, animation->mDuration);
        return wrapped < 0.0 ? wrapped + animation->mDuration : wrapped;
    }
}
// =========================================================
// 肌・白目などを先に描き、透過パーツを深度書き込みなしで重ねる
// =========================================================
void AnimationModel::Draw()
{
    Renderer::SetWorldMatrix(XMMatrixRotationY(m_modelYawOffset) * m_GameObject->GetMatrx());
    auto* context = Renderer::GetDeviceContext();
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    drawParts(m_opaqueParts, false);

    if (m_transparentParts.empty())
        return;

    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> previousDepth;
    UINT stencilRef;
    context->OMGetDepthStencilState(previousDepth.GetAddressOf(), &stencilRef);
    Renderer::SetDepthEnable(false);
    drawParts(m_transparentParts, true);
    context->OMSetDepthStencilState(previousDepth.Get(), stencilRef);
}

// =========================================================
// 輪郭パスでは、虹彩やまつ毛などの透過パーツを押し出さない
// =========================================================
void AnimationModel::drawOutline()
{
    Renderer::SetWorldMatrix(XMMatrixRotationY(m_modelYawOffset) * m_GameObject->GetMatrx());
    Renderer::GetDeviceContext()->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    drawParts(m_opaqueParts, false);
}

void AnimationModel::drawParts(const std::vector<DrawPart>& parts, bool alphaBlend)
{
    auto* context = Renderer::GetDeviceContext();
    constexpr UINT stride = sizeof(VERTEX_3D);
    constexpr UINT offset = 0;

    MATERIAL material{};
    material.AlphaBlend = alphaBlend;
    material.AlphaCutoff = alphaBlend ? 1.0f / 255.0f : 0.5f;

    for (const DrawPart& part : parts)
    {
        material.Diffuse = part.diffuse;
        material.Ambient = part.diffuse;
        material.TextureEnable = part.texture != nullptr;
        Renderer::SetMaterial(material);
        context->PSSetShaderResources(0, 1, &part.texture);

        const unsigned int meshIndex = part.meshIndex;
        context->IASetVertexBuffers(0, 1, &m_VertexBuffer[meshIndex], &stride, &offset);
        context->IASetIndexBuffer(m_IndexBuffer[meshIndex], DXGI_FORMAT_R32_UINT, 0);
        context->DrawIndexed(m_AiScene->mMeshes[meshIndex]->mNumFaces * 3, 0, 0);
    }
}

// =========================================================
// FBX の透過指定で描画順を分け、各パーツのテクスチャを解決する
// =========================================================
void AnimationModel::loadDrawParts()
{
    for (unsigned int meshIndex = 0; meshIndex < m_AiScene->mNumMeshes; ++meshIndex)
    {
        const aiMesh* mesh = m_AiScene->mMeshes[meshIndex];
        const aiMaterial* material = m_AiScene->mMaterials[mesh->mMaterialIndex];

        aiString texturePath;
        aiColor3D diffuse(1.0f, 1.0f, 1.0f);
        float opacity = 1.0f;
        material->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse);
        material->Get(AI_MATKEY_OPACITY, opacity);

        ID3D11ShaderResourceView* texture = nullptr;
        if (material->GetTexture(aiTextureType_DIFFUSE, 0, &texturePath) == AI_SUCCESS)
            texture = loadTexture(m_AiScene->GetEmbeddedTexture(texturePath.C_Str()));

        const bool transparent = material->GetTextureCount(aiTextureType_OPACITY) > 0 || opacity < 1.0f;
        auto& parts = transparent ? m_transparentParts : m_opaqueParts;
        parts.push_back({ meshIndex, { diffuse.r, diffuse.g, diffuse.b, opacity }, texture });
    }
}

ID3D11ShaderResourceView* AnimationModel::loadTexture(const aiTexture* embedded)
{
    if (!embedded)
        return nullptr;

    const auto cached = m_textures.find(embedded);
    if (cached != m_textures.end())
        return cached->second;

    ID3D11ShaderResourceView* texture = nullptr;
    if (embedded->mHeight == 0)
    {
        // 圧縮された埋め込み画像を展開し、アルファ付きのまま GPU に送る。
        ScratchImage image;
        if (SUCCEEDED(LoadFromWICMemory(embedded->pcData, embedded->mWidth, WIC_FLAGS_NONE, nullptr, image)))
        {
            CreateShaderResourceView(Renderer::GetDevice(), image.GetImages(), image.GetImageCount(),
                image.GetMetadata(), &texture);
        }
    }

    m_textures.emplace(embedded, texture);
    return texture;
}

	void AnimationModel::Load(const char* FileName)
	{
		m_ModelPath = FileName;

		m_AiScene = aiImportFile(FileName, aiProcessPreset_TargetRealtime_MaxQuality | aiProcess_ConvertToLeftHanded);
		assert(m_AiScene);

		m_VertexBuffer = new ID3D11Buffer * [m_AiScene->mNumMeshes];
		m_IndexBuffer = new ID3D11Buffer * [m_AiScene->mNumMeshes];


		//変形後頂点配列生成
		m_DeformVertex = new std::vector<DEFORM_VERTEX>[m_AiScene->mNumMeshes];

		//再帰的にボーン生成
		CreateBone(m_AiScene->mRootNode);
		for (unsigned int m = 0; m < m_AiScene->mNumMeshes; m++)
		{
			aiMesh* mesh = m_AiScene->mMeshes[m];

			// 頂点バッファ生成
			{
				VERTEX_3D* vertex = new VERTEX_3D[mesh->mNumVertices];

				for (unsigned int v = 0; v < mesh->mNumVertices; v++)
				{
					vertex[v].Position = XMFLOAT3(mesh->mVertices[v].x, mesh->mVertices[v].y, mesh->mVertices[v].z);
					vertex[v].Normal = XMFLOAT3(mesh->mNormals[v].x, mesh->mNormals[v].y, mesh->mNormals[v].z);
					vertex[v].TexCoord = XMFLOAT2(mesh->mTextureCoords[0][v].x, mesh->mTextureCoords[0][v].y);
					vertex[v].Diffuse = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
				}

				D3D11_BUFFER_DESC bd;
				ZeroMemory(&bd, sizeof(bd));
				bd.Usage = D3D11_USAGE_DYNAMIC;
				bd.ByteWidth = sizeof(VERTEX_3D) * mesh->mNumVertices;
				bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
				bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

				D3D11_SUBRESOURCE_DATA sd;
				ZeroMemory(&sd, sizeof(sd));
				sd.pSysMem = vertex;

				Renderer::GetDevice()->CreateBuffer(&bd, &sd,
					&m_VertexBuffer[m]);

				delete[] vertex;
			}


			// インデックスバッファ生成
			{
				unsigned int* index = new unsigned int[mesh->mNumFaces * 3];

				for (unsigned int f = 0; f < mesh->mNumFaces; f++)
				{
					const aiFace* face = &mesh->mFaces[f];

					assert(face->mNumIndices == 3);

					index[f * 3 + 0] = face->mIndices[0];
					index[f * 3 + 1] = face->mIndices[1];
					index[f * 3 + 2] = face->mIndices[2];
				}

				D3D11_BUFFER_DESC bd;
				ZeroMemory(&bd, sizeof(bd));
				bd.Usage = D3D11_USAGE_DEFAULT;
				bd.ByteWidth = sizeof(unsigned int) * mesh->mNumFaces * 3;
				bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
				bd.CPUAccessFlags = 0;

				D3D11_SUBRESOURCE_DATA sd;
				ZeroMemory(&sd, sizeof(sd));
				sd.pSysMem = index;

				Renderer::GetDevice()->CreateBuffer(&bd, &sd, &m_IndexBuffer[m]);

				delete[] index;
			}



			//変形後頂点データ初期化
			for (unsigned int v = 0; v < mesh->mNumVertices; v++)
			{
				DEFORM_VERTEX deformVertex;
				deformVertex.Position = mesh->mVertices[v];
				deformVertex.Normal = mesh->mNormals[v];
				deformVertex.BoneNum = 0;

				for (unsigned int b = 0; b < 4; b++)
				{
					deformVertex.BoneName[b] = "";
					deformVertex.BoneWeight[b] = 0.0f;
				}

				m_DeformVertex[m].push_back(deformVertex);
			}


			//ボーンデータ初期化
			for (unsigned int b = 0; b < mesh->mNumBones; b++)
			{
				aiBone* bone = mesh->mBones[b];

				m_Bone[bone->mName.C_Str()].OffsetMatrix = bone->mOffsetMatrix;

				//変形後頂点にボーンデータ格納
				for (unsigned int w = 0; w < bone->mNumWeights; w++)
				{
					aiVertexWeight weight = bone->mWeights[w];

					int num = m_DeformVertex[m][weight.mVertexId].BoneNum;

					m_DeformVertex[m][weight.mVertexId].BoneWeight[num] = weight.mWeight;
					m_DeformVertex[m][weight.mVertexId].BoneName[num] = bone->mName.C_Str();
					m_DeformVertex[m][weight.mVertexId].BoneNum++;

					assert(m_DeformVertex[m][weight.mVertexId].BoneNum <= 4);
				}
			}
		}
		loadDrawParts();



	}



	void AnimationModel::LoadAnimation(const char* fileName, const char* name)
{
    // Reuse the model scene when the requested animation is embedded in it.
    if (m_ModelPath == fileName && m_AiScene->HasAnimations())
    {
        m_Animation[name] = m_AiScene;
        return;
    }

    m_Animation[name] = aiImportFile(fileName,
        aiProcessPreset_TargetRealtime_MaxQuality | aiProcess_ConvertToLeftHanded);
    assert(m_Animation[name]);
}
// =========================================================
// Read the authored stride once so gameplay speed matches foot motion.
// =========================================================
float AnimationModel::getRootMotionSpeed(const char* animationName, float playbackRate) const
{
    const auto clip = m_Animation.find(animationName);
    if (clip == m_Animation.end() || !clip->second->HasAnimations()) return 0.0f;

    const aiAnimation* animation = clip->second->mAnimations[0];
    if (animation->mDuration <= 0.0) return 0.0f;

    for (unsigned int channelIndex = 0; channelIndex < animation->mNumChannels; channelIndex++)
    {
        const aiNodeAnim* channel = animation->mChannels[channelIndex];
        if (channel->mNodeName != aiString("Armature") || channel->mNumPositionKeys < 2) continue;

        const aiVector3D travel = channel->mPositionKeys[channel->mNumPositionKeys - 1].mValue
            - channel->mPositionKeys[0].mValue;
        const float distance = sqrtf(travel.x * travel.x + travel.z * travel.z) * m_skeletonUnitScale;
        const double tickRate = animation->mTicksPerSecond > 0.0 ? animation->mTicksPerSecond : 60.0;
        return distance * static_cast<float>(tickRate / animation->mDuration) * playbackRate;
    }
    return 0.0f;
}

void AnimationModel::CreateBone(aiNode* node)
	{
		BONE bone;
		// Preserve the FBX hierarchy transform for nodes without animation keys.
		bone.BindMatrix = node->mTransformation;
		bone.AnimationMatrix = bone.BindMatrix;

		m_Bone[node->mName.C_Str()] = bone;

		for (unsigned int n = 0; n < node->mNumChildren; n++)
		{
			CreateBone(node->mChildren[n]);
		}

	}


	void AnimationModel::Uninit()
	{
		for (unsigned int m = 0; m < m_AiScene->mNumMeshes; m++)
		{
			m_VertexBuffer[m]->Release();
			m_IndexBuffer[m]->Release();
		}

		delete[] m_VertexBuffer;
		delete[] m_IndexBuffer;

		delete[] m_DeformVertex;


        m_opaqueParts.clear();
        m_transparentParts.clear();
        for (const auto& entry : m_textures)
        {
            if (entry.second)
                entry.second->Release();
        }
        m_textures.clear();



		for (std::pair<const std::string, const aiScene*> pair : m_Animation)
		{
			if (pair.second != m_AiScene)
				aiReleaseImport(pair.second);
		}

		aiReleaseImport(m_AiScene);

	}





// =========================================================
// 二つのモーションを補間してスキニング頂点を更新
// =========================================================
void AnimationModel::Update(const char* animationName1, double time1,
    const char* animationName2, double time2, float blend)
{
    const auto firstClip = m_Animation.find(animationName1);
    const auto secondClip = m_Animation.find(animationName2);
    if (firstClip == m_Animation.end() || secondClip == m_Animation.end() ||
        !firstClip->second->HasAnimations() || !secondClip->second->HasAnimations())
    {
        return;
    }

    const aiAnimation* animation1 = firstClip->second->mAnimations[0];
    const aiAnimation* animation2 = secondClip->second->mAnimations[0];
    const double tick1 = wrapAnimationTime(time1, animation1);
    const double tick2 = wrapAnimationTime(time2, animation2);
    const bool inPlace1 = strcmp(animationName1, "Walk") == 0 || strcmp(animationName1, "Run") == 0;
    const bool inPlace2 = strcmp(animationName2, "Walk") == 0 || strcmp(animationName2, "Run") == 0;

    // All channels of a clip share one loop time; each key stream is interpolated.
    for (auto& pair : m_Bone)
    {
        BONE& bone = pair.second;
        bone.AnimationMatrix = bone.BindMatrix;

        const aiNodeAnim* nodeAnim1 = nullptr;
        const aiNodeAnim* nodeAnim2 = nullptr;
        const aiString nodeName(pair.first);
        for (unsigned int c = 0; c < animation1->mNumChannels; c++)
        {
            if (animation1->mChannels[c]->mNodeName == nodeName)
            {
                nodeAnim1 = animation1->mChannels[c];
                break;
            }
        }
        for (unsigned int c = 0; c < animation2->mNumChannels; c++)
        {
            if (animation2->mChannels[c]->mNodeName == nodeName)
            {
                nodeAnim2 = animation2->mChannels[c];
                break;
            }
        }
        if (!nodeAnim1 || !nodeAnim2 ||
            !nodeAnim1->mNumRotationKeys || !nodeAnim1->mNumPositionKeys || !nodeAnim1->mNumScalingKeys ||
            !nodeAnim2->mNumRotationKeys || !nodeAnim2->mNumPositionKeys || !nodeAnim2->mNumScalingKeys)
        {
            continue;
        }

        aiQuaternion rotation1 = sampleRotation(nodeAnim1->mRotationKeys, nodeAnim1->mNumRotationKeys, tick1);
        aiQuaternion rotation2 = sampleRotation(nodeAnim2->mRotationKeys, nodeAnim2->mNumRotationKeys, tick2);
        aiVector3D position1 = sampleVector(nodeAnim1->mPositionKeys, nodeAnim1->mNumPositionKeys, tick1);
        aiVector3D position2 = sampleVector(nodeAnim2->mPositionKeys, nodeAnim2->mNumPositionKeys, tick2);
        const aiVector3D scale1 = sampleVector(nodeAnim1->mScalingKeys, nodeAnim1->mNumScalingKeys, tick1);
        const aiVector3D scale2 = sampleVector(nodeAnim2->mScalingKeys, nodeAnim2->mNumScalingKeys, tick2);

        // Keep the gait forward lean while the gameplay direction controls heading.
        // Pin horizontal root motion while keeping vertical foot contact.
        if (pair.first == "Armature" && (inPlace1 || inPlace2))
        {
            aiVector3D bindScale, bindPosition;
            aiQuaternion bindRotation;
            bone.BindMatrix.Decompose(bindScale, bindRotation, bindPosition);
            if (inPlace1)
            {
                position1.x = bindPosition.x;
                position1.z = bindPosition.z;
                rotation1 = keepRootPitch(rotation1, bindRotation);
            }
            if (inPlace2)
            {
                position2.x = bindPosition.x;
                position2.z = bindPosition.z;
                rotation2 = keepRootPitch(rotation2, bindRotation);
            }
        }

        const aiVector3D position = position1 * (1.0f - blend) + position2 * blend;
        const aiVector3D scale = scale1 * (1.0f - blend) + scale2 * blend;
        aiQuaternion rotation;
        aiQuaternion::Interpolate(rotation, rotation1, rotation2, blend);
        bone.AnimationMatrix = aiMatrix4x4(scale, rotation.Normalize(), position);
    }

    // Bone offsets already describe the imported FBX coordinate system.
    aiMatrix4x4 rootMatrix;
    UpdateBoneMatrix(m_AiScene->mRootNode, rootMatrix);
    const float uprightWeight = (inPlace1 ? 1.0f - blend : 0.0f) + (inPlace2 ? blend : 0.0f);
    stabilizeTorso(uprightWeight);
    for (unsigned int meshIndex = 0; meshIndex < m_AiScene->mNumMeshes; meshIndex++)
    {
        aiMesh* mesh = m_AiScene->mMeshes[meshIndex];
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(Renderer::GetDeviceContext()->Map(m_VertexBuffer[meshIndex], 0,
            D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            continue;
        }

        VERTEX_3D* vertex = static_cast<VERTEX_3D*>(mapped.pData);
        for (unsigned int v = 0; v < mesh->mNumVertices; v++)
        {
            DEFORM_VERTEX& deformVertex = m_DeformVertex[meshIndex][v];
            aiMatrix4x4 matrices[4];
            for (unsigned int b = 0; b < 4; b++)
            {
                matrices[b] = m_Bone[deformVertex.BoneName[b]].Matrix;
            }
            aiMatrix4x4 skinMatrix = matrices[0] * deformVertex.BoneWeight[0]
                + matrices[1] * deformVertex.BoneWeight[1]
                + matrices[2] * deformVertex.BoneWeight[2]
                + matrices[3] * deformVertex.BoneWeight[3];

            deformVertex.Position = mesh->mVertices[v];
            deformVertex.Position *= skinMatrix;
            skinMatrix.a4 = skinMatrix.b4 = skinMatrix.c4 = 0.0f;
            deformVertex.Normal = mesh->mNormals[v];
            deformVertex.Normal *= skinMatrix;

            vertex[v].Position = XMFLOAT3(deformVertex.Position.x, deformVertex.Position.y, deformVertex.Position.z);
            vertex[v].Normal = XMFLOAT3(deformVertex.Normal.x, deformVertex.Normal.y, deformVertex.Normal.z);
            vertex[v].TexCoord = XMFLOAT2(mesh->mTextureCoords[0][v].x, mesh->mTextureCoords[0][v].y);
            vertex[v].Diffuse = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
        }
        Renderer::GetDeviceContext()->Unmap(m_VertexBuffer[meshIndex], 0);
    }
}

// =========================================================
// 移動中は背骨から上を垂直に保ち、足とスカートの動きは残す
// =========================================================
void AnimationModel::stabilizeTorso(float weight)
{
    if (weight <= 0.0f) return;

    const auto spineIt = m_Bone.find("J_Bip_C_Spine");
    const auto hipsIt = m_Bone.find("J_Bip_C_Hips");
    const auto headIt = m_Bone.find("J_Bip_C_Head");
    if (spineIt == m_Bone.end() || hipsIt == m_Bone.end() || headIt == m_Bone.end()) return;

    BONE& spine = spineIt->second;
    const aiMatrix4x4& spineWorld = spine.worldMatrix;
    const aiMatrix4x4& headWorld = headIt->second.worldMatrix;
    const aiVector3D spinePosition(spineWorld.a4, spineWorld.b4, spineWorld.c4);
    const aiVector3D headPosition(headWorld.a4, headWorld.b4, headWorld.c4);
    aiVector3D torsoAxis = headPosition - spinePosition;
    if (torsoAxis.SquareLength() < 0.0001f) return;
    torsoAxis.Normalize();

    // Rotate in world space, then convert that correction into the spine's local space.
    const aiVector3D worldUp(0.0f, 1.0f, 0.0f);
    aiVector3D correctionAxis = torsoAxis ^ worldUp;
    const float axisLength = correctionAxis.Length();
    if (axisLength < 0.0001f) return;
    correctionAxis /= axisLength;
    const float dot = fmaxf(-1.0f, fminf(1.0f, torsoAxis * worldUp));
    const aiQuaternion worldCorrection(correctionAxis, atan2f(axisLength, dot) * weight);

    aiVector3D parentScale, parentPosition, spineScale, spineLocalPosition;
    aiQuaternion parentRotation, spineRotation;
    hipsIt->second.worldMatrix.Decompose(parentScale, parentRotation, parentPosition);
    spine.AnimationMatrix.Decompose(spineScale, spineRotation, spineLocalPosition);
    parentRotation.Normalize();
    aiQuaternion parentInverse = parentRotation;
    parentInverse.Conjugate();
    const aiQuaternion localCorrection = parentInverse * worldCorrection * parentRotation;
    spineRotation = localCorrection * spineRotation;
    spine.AnimationMatrix = aiMatrix4x4(spineScale, spineRotation.Normalize(), spineLocalPosition);

    // Rebuild matrices once after correcting the torso pose.
    aiMatrix4x4 rootMatrix;
    UpdateBoneMatrix(m_AiScene->mRootNode, rootMatrix);
}
void AnimationModel::UpdateBoneMatrix(aiNode* node, aiMatrix4x4 matrix)
	{
		BONE* bone = &m_Bone[node->mName.C_Str()];

		aiMatrix4x4 worldMatrix;
		worldMatrix = matrix * bone->AnimationMatrix;
		bone->worldMatrix = worldMatrix;
		// Convert the FBX skeleton units before applying each mesh bone offset.
		aiMatrix4x4 unitMatrix;
		unitMatrix.a1 = m_skeletonUnitScale;
		unitMatrix.b2 = m_skeletonUnitScale;
		unitMatrix.c3 = m_skeletonUnitScale;
		bone->Matrix = unitMatrix * worldMatrix * bone->OffsetMatrix;
		for (unsigned int n = 0; n < node->mNumChildren; n++)
		{
			UpdateBoneMatrix(node->mChildren[n], worldMatrix);
		}
	}

