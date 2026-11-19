// =========================================================
// ファイルの情報[animationModel.cpp]
// 制作者:Masatora Tanaka    日付：2026/11/01
// =========================================================
	#include "main.h"
	#include "renderer.h"
	#include "animationModel.h"
	#include "GameObject.h"
#include <wrl/client.h>
#include "ActionSequence.h"

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

    // 1周分の水平移動を均等に引き、歩幅の中の揺れや上下動は残す。
    aiVector3D removeHorizontalCycleTravel(const aiNodeAnim& channel, double tick, double duration)
    {
        const auto* keys = channel.mPositionKeys;
        const auto count = channel.mNumPositionKeys;
        auto position = sampleVector(keys, count, tick);
        const auto travel = sampleVector(keys, count, duration) - sampleVector(keys, count, 0.0);
        const float phase = static_cast<float>(tick / duration);
        position.x -= travel.x * phase;
        position.z -= travel.z * phase;
        return position;
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
    drawAt(XMMatrixRotationY(m_modelYawOffset) * m_GameObject->GetMatrx());
}

void AnimationModel::drawAt(FXMMATRIX world)
{
    Renderer::SetWorldMatrix(world);
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
        {
            texture = loadTexture(m_AiScene->GetEmbeddedTexture(texturePath.C_Str()));
            if (!texture)
            {
                const std::string reference = texturePath.C_Str();
                const auto filename = reference.substr(reference.find_last_of("/\\") + 1);
                const auto folder = m_ModelPath.substr(0, m_ModelPath.find_last_of("/\\") + 1);
                const auto path = folder + filename;
                auto& external = m_externalTextures[path];
                if (!external)
                {
                    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.c_str(), -1, nullptr, 0);
                    std::wstring widePath(length, L'\0');
                    ScratchImage image;
                    if (length > 0 && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                        path.c_str(), -1, &widePath[0], length)
                        && SUCCEEDED(LoadFromWICFile(widePath.c_str(), WIC_FLAGS_NONE, nullptr, image)))
                        CreateShaderResourceView(Renderer::GetDevice(), image.GetImages(), image.GetImageCount(),
                            image.GetMetadata(), &external);
                }
                texture = external;
            }
            if (!texture)
                ++m_missingTextures;
        }

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

// =========================================================
// モデル本体を読み込み、描画用のバッファとモーフ一覧を作る
// =========================================================
void AnimationModel::Load(const char* fileName)
{
    const bool loaded = tryLoad(fileName);
    assert(loaded);
    (void)loaded;
}

bool AnimationModel::tryLoad(const char* fileName, bool staticMesh)
{
    if (m_AiScene)
        return false;

    m_ModelPath = fileName;
    unsigned int flags = aiProcessPreset_TargetRealtime_MaxQuality | aiProcess_ConvertToLeftHanded;
    if (staticMesh)
        flags |= aiProcess_PreTransformVertices;
    m_AiScene = aiImportFile(fileName, flags);
    if (!m_AiScene)
        return false;

    if (!m_AiScene->mRootNode || m_AiScene->mNumMeshes == 0)
    {
        Uninit();
        return false;
    }

    const unsigned int meshCount = m_AiScene->mNumMeshes;
    aiVector3D center{};
    float unit = 1.0f;
    if (staticMesh)
    {
        aiVector3D low(FLT_MAX), high(-FLT_MAX);
        for (unsigned int m = 0; m < meshCount; ++m)
        {
            const auto& mesh = *m_AiScene->mMeshes[m];
            for (unsigned int v = 0; v < mesh.mNumVertices; ++v)
            {
                const auto& p = mesh.mVertices[v];
                low = { std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z) };
                high = { std::max(high.x, p.x), std::max(high.y, p.y), std::max(high.z, p.z) };
            }
        }
        const auto size = high - low;
        unit = std::max({ size.x, size.y, size.z });
        center = (low + high) * 0.5f;
        if (!std::isfinite(unit) || unit <= 0.0f)
        {
            aiReleaseImport(m_AiScene);
            m_AiScene = nullptr;
            return false;
        }
        m_staticHalfExtents = { size.x / (2.0f * unit), size.y / (2.0f * unit), size.z / (2.0f * unit) };
    }
    m_VertexBuffer = new ID3D11Buffer*[meshCount]{};
    m_IndexBuffer = new ID3D11Buffer*[meshCount]{};
    m_DeformVertex = new std::vector<DEFORM_VERTEX>[meshCount];
    CreateBone(m_AiScene->mRootNode);
    // 着地時にだけ使う支点と、基準姿勢での足元を覚えておく。
    const auto* root = m_AiScene->mRootNode;
    m_landingArmature = root->FindNode(aiString("Armature"));
    m_landingLeftFoot = root->FindNode(aiString("J_Bip_L_Foot"));
    m_landingRightFoot = root->FindNode(aiString("J_Bip_R_Foot"));
    if (m_landingLeftFoot && m_landingRightFoot)
        m_landingFootCenterZ = landingFootCenterZ();

    for (unsigned int m = 0; m < meshCount; ++m)
    {
        const aiMesh* mesh = m_AiScene->mMeshes[m];
        if (!mesh->HasPositions() || !mesh->HasNormals() || !mesh->HasFaces())
        {
            Uninit();
            return false;
        }

        std::vector<VERTEX_3D> vertices(mesh->mNumVertices);
        m_DeformVertex[m].resize(mesh->mNumVertices);
        for (unsigned int v = 0; v < mesh->mNumVertices; ++v)
        {
            const auto position = (mesh->mVertices[v] - center) / unit;
            const auto& normal = mesh->mNormals[v];
            vertices[v] = { { position.x, position.y, position.z }, { normal.x, normal.y, normal.z },
                { 1.0f, 1.0f, 1.0f, 1.0f }, mesh->HasTextureCoords(0)
                    ? XMFLOAT2(mesh->mTextureCoords[0][v].x, mesh->mTextureCoords[0][v].y)
                    : XMFLOAT2(0.0f, 0.0f) };
        }

        std::vector<unsigned int> indices;
        indices.reserve(static_cast<size_t>(mesh->mNumFaces) * 3);
        for (unsigned int f = 0; f < mesh->mNumFaces; ++f)
        {
            const auto& face = mesh->mFaces[f];
            if (face.mNumIndices != 3)
            {
                Uninit();
                return false;
            }

            indices.insert(indices.end(), face.mIndices, face.mIndices + 3);
        }

        auto* device = Renderer::GetDevice();
        D3D11_BUFFER_DESC desc{};
        D3D11_SUBRESOURCE_DATA data{};
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.ByteWidth = static_cast<UINT>(sizeof(VERTEX_3D) * vertices.size());
        desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        data.pSysMem = vertices.data();
        const HRESULT vertexResult = device->CreateBuffer(&desc, &data, &m_VertexBuffer[m]);

        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.ByteWidth = static_cast<UINT>(sizeof(unsigned int) * indices.size());
        desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
        desc.CPUAccessFlags = 0;
        data.pSysMem = indices.data();
        if (FAILED(vertexResult) || FAILED(device->CreateBuffer(&desc, &data, &m_IndexBuffer[m])))
        {
            Uninit();
            return false;
        }

        for (unsigned int b = 0; b < mesh->mNumBones; ++b)
        {
            const aiBone* bone = mesh->mBones[b];
            const auto boneEntry = m_Bone.find(bone->mName.C_Str());
            if (boneEntry == m_Bone.end())
            {
                Uninit();
                return false;
            }
            boneEntry->second.OffsetMatrix = bone->mOffsetMatrix;
            boneEntry->second.isSkinBone = true;

            for (unsigned int w = 0; w < bone->mNumWeights; ++w)
            {
                const auto& weight = bone->mWeights[w];
                if (weight.mVertexId >= mesh->mNumVertices || m_DeformVertex[m][weight.mVertexId].BoneNum >= 4)
                {
                    Uninit();
                    return false;
                }

                auto& vertex = m_DeformVertex[m][weight.mVertexId];
                const int slot = vertex.BoneNum++;
                vertex.BoneName[slot] = bone->mName.C_Str();
                vertex.BoneWeight[slot] = weight.mWeight;
            }
        }

        for (unsigned int i = 0; i < mesh->mNumAnimMeshes; ++i)
        {
            const aiAnimMesh* morph = mesh->mAnimMeshes[i];
            if (!morph->mVertices || morph->mNumVertices != mesh->mNumVertices || !morph->mName.length)
                continue;

            const char* name = morph->mName.C_Str();
            if (std::find(m_morphNames.begin(), m_morphNames.end(), name) == m_morphNames.end())
                m_morphNames.emplace_back(name);
        }
    }

    loadDrawParts();
    return true;
}

// =========================================================
// 体のモーションを登録し、プレビューでは秒をティックに変換する
// =========================================================
bool AnimationModel::LoadAnimation(const char* fileName, const char* name)
{
    if (!m_AiScene)
        return false;

    if (m_Animation.find(name) != m_Animation.end())
        return true;

    const aiScene* clip = m_ModelPath == fileName ? m_AiScene : aiImportFile(fileName,
        aiProcessPreset_TargetRealtime_MaxQuality | aiProcess_ConvertToLeftHanded);
    if (!clip || !clip->HasAnimations())
    {
        if (clip && clip != m_AiScene)
            aiReleaseImport(clip);
        return false;
    }

    const auto* animation = clip->mAnimations[0];
    for (unsigned int i = 0; i < animation->mNumChannels; ++i)
    {
        const auto bone = m_Bone.find(animation->mChannels[i]->mNodeName.C_Str());
        if (bone != m_Bone.end() && bone->second.isSkinBone)
        {
            m_Animation.emplace(name, clip);
            return true;
        }
    }

    if (clip != m_AiScene)
        aiReleaseImport(clip);
    return false;
}

double AnimationModel::clipDuration(const char* name) const
{
    const auto clip = m_Animation.find(name);
    if (clip == m_Animation.end())
        return 0.0;
    const auto& animation = *clip->second->mAnimations[0];
    return animation.mDuration / (animation.mTicksPerSecond > 0.0 ? animation.mTicksPerSecond : 60.0);
}

bool AnimationModel::hasAnimation(const char* name) const
{
    return m_Animation.find(name) != m_Animation.end();
}

// =========================================================
// FBXの時刻に対応する単一クリップの姿勢を適用する
// =========================================================
bool AnimationModel::sampleClip(const char* name, double seconds, RootTravel rootTravel)
{
    return samplePose(name, seconds, rootTravel, 0.0f, false, true);
}

bool AnimationModel::sampleBlendedClip(const char* name, double seconds, float dt, RootTravel rootTravel, bool restart, bool loop)
{
    return samplePose(name, seconds, rootTravel, dt, restart, loop);
}

bool AnimationModel::samplePose(const char* name, double seconds, RootTravel rootTravel, float dt, bool restart, bool loop)
{
    const auto clip = m_Animation.find(name);
    if (clip == m_Animation.end())
        return false;

    const auto& animation = *clip->second->mAnimations[0];
    const double rate = animation.mTicksPerSecond > 0.0 ? animation.mTicksPerSecond : 60.0;
    const float weight = dt > 0.0f ? updatePoseTransition(name, rootTravel, dt, restart) : 1.0f;
    // 単発再生は終端で止め、最後のフレームで先頭へ戻さない。
    const double time = seconds * rate;
    const double tick = loop ? wrapAnimationTime(time, &animation) : std::min(time, animation.mDuration);
    applyPose(animation, tick, rootTravel, weight);
    return true;
}

// 表示中のポーズを保存し、短時間で次の姿勢へつなぐ。
float AnimationModel::updatePoseTransition(const char* name, RootTravel rootTravel, float dt, bool restart)
{
    constexpr float duration = 0.10f;
    if (restart || m_sampledClip != name || m_sampledRootTravel != rootTravel)
    {
        if (!m_sampledClip.empty())
        {
            m_transitionPose.clear();
            m_transitionPose.reserve(m_Bone.size());
            for (auto& pair : m_Bone)
            {
                TransitionBone source{};
                source.bone = &pair.second;
                source.bone->AnimationMatrix.Decompose(source.scale, source.rotation, source.position);
                source.rotation.Normalize();
                m_transitionPose.push_back(source);
            }
            m_transitionElapsed = 0.0f;
        }

        m_sampledClip = name;
        m_sampledRootTravel = rootTravel;
    }

    if (m_transitionPose.empty() || m_transitionElapsed >= duration)
        return 1.0f;

    const float phase = m_transitionElapsed / duration;
    m_transitionElapsed += dt;
    return phase * phase * (3.0f - 2.0f * phase);
}

// 保存した姿勢と次のクリップの姿勢を、ボーンごとに補間する。
void AnimationModel::blendTransitionPose(float weight)
{
    for (const auto& source : m_transitionPose)
    {
        auto& matrix = source.bone->AnimationMatrix;
        aiVector3D scale, position;
        aiQuaternion rotation, blendedRotation;
        matrix.Decompose(scale, rotation, position);
        aiQuaternion::Interpolate(blendedRotation, source.rotation, rotation.Normalize(), weight);
        const float sourceWeight = 1.0f - weight;
        matrix = aiMatrix4x4(source.scale * sourceWeight + scale * weight, blendedRotation.Normalize(),
            source.position * sourceWeight + position * weight);
    }
}

// =========================================================
// プレビューで使い終えた追加クリップのメモリを解放する
// =========================================================
void AnimationModel::unloadAnimation(const char* name)
{
    const auto clip = m_Animation.find(name);
    if (clip == m_Animation.end())
        return;

    if (clip->second != m_AiScene)
        aiReleaseImport(clip->second);
    m_Animation.erase(clip);
}

// =========================================================
// モーフの検索は名前を切り替えたときにだけ行う
// =========================================================
void AnimationModel::setMorph(const std::string& name, float weight)
{
    m_activeMorphLayerCount = name.empty() || weight <= 0.0f ? 0 : 1;
    if (!m_activeMorphLayerCount)
        return;

    if (m_morphLayers.empty())
        m_morphLayers.resize(1);
    setMorphLayer(m_morphLayers[0], name, weight);
}

void AnimationModel::setMorphBlend(const std::string& base, const std::string& target, float blend,
    const std::vector<MorphTarget>& extraMorphs, float time)
{
    const auto extraCount = extraMorphs.size() > MorphTarget::maxExtraTargets
        ? MorphTarget::maxExtraTargets : extraMorphs.size();
    m_activeMorphLayerCount = extraCount + 2;
    if (m_morphLayers.size() < m_activeMorphLayerCount)
        m_morphLayers.resize(m_activeMorphLayerCount);

    setMorphLayer(m_morphLayers[0], base, 1.0f - blend);
    setMorphLayer(m_morphLayers[1], target, blend);
    for (std::size_t i = 0; i < extraCount; ++i)
    {
        const auto& morph = extraMorphs[i];
        const float weight = sampleExpression(time, morph.start, morph.peak, morph.end, morph.weight, morph.smooth);
        setMorphLayer(m_morphLayers[i + 2], morph.name, weight);
    }
}

void AnimationModel::setMorphLayer(MorphLayer& layer, const std::string& name, float weight)
{
    layer.weight = fmaxf(0.0f, fminf(1.0f, weight));
    if (!m_AiScene || name == layer.name)
        return;

    layer.name = name;
    layer.meshes.resize(m_AiScene->mNumMeshes, nullptr);
    if (name.empty())
    {
        std::fill(layer.meshes.begin(), layer.meshes.end(), nullptr);
        return;
    }
    for (unsigned int m = 0; m < m_AiScene->mNumMeshes; ++m)
    {
        const aiMesh* mesh = m_AiScene->mMeshes[m];
        layer.meshes[m] = nullptr;
        for (unsigned int i = 0; i < mesh->mNumAnimMeshes; ++i)
        {
            const aiAnimMesh* morph = mesh->mAnimMeshes[i];
            if (morph->mVertices && morph->mNumVertices == mesh->mNumVertices && name == morph->mName.C_Str())
            {
                layer.meshes[m] = morph;
                break;
            }
        }
    }
}

// =========================================================
// Read the authored stride once so gameplay speed matches foot motion.
// =========================================================
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
        if (!m_AiScene)
            return;
		for (unsigned int m = 0; m < m_AiScene->mNumMeshes; m++)
		{
            if (m_VertexBuffer && m_VertexBuffer[m])
                m_VertexBuffer[m]->Release();
            if (m_IndexBuffer && m_IndexBuffer[m])
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
        for (const auto& entry : m_externalTextures)
            if (entry.second)
                entry.second->Release();
        m_externalTextures.clear();



		for (std::pair<const std::string, const aiScene*> pair : m_Animation)
		{
			if (pair.second != m_AiScene)
				aiReleaseImport(pair.second);
		}

        m_Animation.clear();
        m_transitionPose.clear();
        m_sampledClip.clear();
        m_Bone.clear();
        m_landingArmature = nullptr;
        m_landingLeftFoot = nullptr;
        m_landingRightFoot = nullptr;
        m_morphNames.clear();
        m_morphLayers.clear();
        m_activeMorphLayerCount = 0;
        m_missingTextures = 0;
        m_VertexBuffer = nullptr;
        m_IndexBuffer = nullptr;
        m_DeformVertex = nullptr;
        aiReleaseImport(m_AiScene);
        m_AiScene = nullptr;

	}





// =========================================================
// 二つのモーションを補間してスキニング頂点を更新
// =========================================================
// ボーンのオフセットを掛ける前の姿勢を、モデル座標で求める。
aiMatrix4x4 AnimationModel::poseMatrix(const aiNode* node) const
{
    if (!node)
        return aiMatrix4x4();

    return poseMatrix(node->mParent) * m_Bone.at(node->mName.C_Str()).AnimationMatrix;
}

float AnimationModel::landingFootCenterZ() const
{
    return (poseMatrix(m_landingLeftFoot).c4 + poseMatrix(m_landingRightFoot).c4) * 0.5f;
}

// 足元の前後位置を揃える。ボーンの回転と高さは変更しない。
void AnimationModel::anchorLandingFeet()
{
    if (!m_landingArmature || !m_landingLeftFoot || !m_landingRightFoot)
        return;

    const float correction = m_landingFootCenterZ - landingFootCenterZ();
    auto parentInverse = poseMatrix(m_landingArmature->mParent);
    parentInverse.Inverse();

    auto& root = m_Bone.at(m_landingArmature->mName.C_Str()).AnimationMatrix;
    root.a4 += parentInverse.a3 * correction;
    root.b4 += parentInverse.b3 * correction;
    root.c4 += parentInverse.c3 * correction;
}

// FBXのキーから対象のボーン姿勢を求める。
void AnimationModel::sampleBones(const aiAnimation& animation, double tick, RootTravel rootTravel)
{
    for (auto& pair : m_Bone)
        pair.second.AnimationMatrix = pair.second.BindMatrix;

    // FBXにあるボーンだけを動かし、キーがないボーンは基準姿勢で描く。
    for (unsigned int i = 0; i < animation.mNumChannels; ++i)
    {
        const auto* channel = animation.mChannels[i];
        const auto bone = m_Bone.find(channel->mNodeName.C_Str());
        if (bone == m_Bone.end() || !channel->mNumRotationKeys ||
            !channel->mNumPositionKeys || !channel->mNumScalingKeys)
            continue;

        auto rotation = sampleRotation(channel->mRotationKeys, channel->mNumRotationKeys, tick);
        const bool removeTravel = rootTravel == RootTravel::removeHorizontalCycle && animation.mDuration > 0.0
            && channel->mNodeName == aiString("Armature");
        auto position = removeTravel
            ? removeHorizontalCycleTravel(*channel, tick, animation.mDuration)
            : sampleVector(channel->mPositionKeys, channel->mNumPositionKeys, tick);
        // ジャンプと着地の位置はゲーム側の移動・接地で制御する。
        if ((rootTravel == RootTravel::anchorLandingFeet || rootTravel == RootTravel::lockTranslation)
            && channel->mNodeName == aiString("Armature"))
        {
            const auto& bind = bone->second.BindMatrix;
            position.x = bind.a4;
            position.z = bind.c4;
            if (rootTravel == RootTravel::lockTranslation)
                position.y = bind.b4;
        }
        const auto scale = sampleVector(channel->mScalingKeys, channel->mNumScalingKeys, tick);
        bone->second.AnimationMatrix = aiMatrix4x4(scale, rotation.Normalize(), position);
    }
}

void AnimationModel::applyPose(const aiAnimation& animation, double tick, RootTravel rootTravel, float weight)
{
    // 切り替え直後は表示中の姿勢をそのまま描く。
    if (weight > 0.0f)
    {
        sampleBones(animation, tick, rootTravel);
        if (rootTravel == RootTravel::anchorLandingFeet)
            anchorLandingFeet();
        if (weight < 1.0f)
            blendTransitionPose(weight);
    }

    // FBXのボーンオフセットを使ってスキニング行列を求める。
    aiMatrix4x4 rootMatrix;
    UpdateBoneMatrix(m_AiScene->mRootNode, rootMatrix);
    for (unsigned int meshIndex = 0; meshIndex < m_AiScene->mNumMeshes; meshIndex++)
    {
        aiMesh* mesh = m_AiScene->mMeshes[meshIndex];
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(Renderer::GetDeviceContext()->Map(m_VertexBuffer[meshIndex], 0,
            D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            continue;
        }

        // このメッシュで実際に変形するキーだけを頂点ループの前に集める。
        struct ActiveMorph
        {
            const aiAnimMesh* mesh;
            float weight;
        };

        ActiveMorph morphs[MorphTarget::maxExtraTargets + 2];
        std::size_t morphCount = 0;
        for (std::size_t i = 0; i < m_activeMorphLayerCount; ++i)
        {
            const auto& layer = m_morphLayers[i];
            if (layer.weight > 0.0f && !layer.name.empty() && layer.meshes[meshIndex])
                morphs[morphCount++] = { layer.meshes[meshIndex], layer.weight };
        }
        VERTEX_3D* vertex = static_cast<VERTEX_3D*>(mapped.pData);
        for (unsigned int v = 0; v < mesh->mNumVertices; v++)
        {
            DEFORM_VERTEX& deformVertex = m_DeformVertex[meshIndex][v];
            deformVertex.Position = mesh->mVertices[v];
            deformVertex.Normal = mesh->mNormals[v];

            // 表情はボーンで動かす前の頂点と法線に合成する。
            for (std::size_t i = 0; i < morphCount; ++i)
            {
                const aiAnimMesh* morph = morphs[i].mesh;
                const float weight = morphs[i].weight;
                deformVertex.Position += (morph->mVertices[v] - mesh->mVertices[v]) * weight;
                if (morph->mNormals)
                    deformVertex.Normal += (morph->mNormals[v] - mesh->mNormals[v]) * weight;
            }

            if (deformVertex.BoneNum > 0)
            {
                aiMatrix4x4 skinMatrix = m_Bone.at(deformVertex.BoneName[0]).Matrix * deformVertex.BoneWeight[0];
                for (int b = 1; b < deformVertex.BoneNum; ++b)
                    skinMatrix = skinMatrix + m_Bone.at(deformVertex.BoneName[b]).Matrix * deformVertex.BoneWeight[b];

                deformVertex.Position *= skinMatrix;
                skinMatrix.a4 = skinMatrix.b4 = skinMatrix.c4 = 0.0f;
                deformVertex.Normal *= skinMatrix;
            }

            vertex[v].Position = XMFLOAT3(deformVertex.Position.x, deformVertex.Position.y, deformVertex.Position.z);
            vertex[v].Normal = XMFLOAT3(deformVertex.Normal.x, deformVertex.Normal.y, deformVertex.Normal.z);
            vertex[v].TexCoord = mesh->HasTextureCoords(0)
                ? XMFLOAT2(mesh->mTextureCoords[0][v].x, mesh->mTextureCoords[0][v].y)
                : XMFLOAT2(0.0f, 0.0f);
            vertex[v].Diffuse = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
        }
        Renderer::GetDeviceContext()->Unmap(m_VertexBuffer[meshIndex], 0);
    }
}

void AnimationModel::UpdateBoneMatrix(aiNode* node, aiMatrix4x4 matrix)
	{
		BONE* bone = &m_Bone[node->mName.C_Str()];

		aiMatrix4x4 worldMatrix;
		worldMatrix = matrix * bone->AnimationMatrix;
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

