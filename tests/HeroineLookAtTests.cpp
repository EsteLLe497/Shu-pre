#include "../HeroineLookAt.h"
#include "../BonePoseRotation.h"
#include "../MorphOverlay.h"
#include "../assimp/cimport.h"
#include "../assimp/scene.h"
#include "../assimp/postprocess.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <sstream>

namespace
{
    constexpr float rad = 0.017453292519943295f;
    int checks = 0;
    void check(bool condition, const char* message)
    {
        ++checks;
        if (!condition) { std::fprintf(stderr, "FAILED: %s\n", message); std::exit(1); }
    }
    void settle(HeroineLookAt& gaze, float yaw, float pitch = 0, float distance = 2)
    {
        for (int i = 0; i < 600; ++i) gaze.update(yaw * rad, pitch * rad, distance, true, 1.0f / 60);
    }
    bool close(float a, float b) { return fabsf(a - b) < 0.0001f; }
    aiMatrix4x4 world(const aiNode* node)
    {
        return node ? world(node->mParent) * node->mTransformation : aiMatrix4x4();
    }
    float difference(const aiMatrix4x4& a, const aiMatrix4x4& b)
    {
        float result = 0;
        for (unsigned row = 0; row < 4; ++row)
            for (unsigned column = 0; column < 4; ++column) result = std::max(result, fabsf(a[row][column] - b[row][column]));
        return result;
    }
}

int main()
{
    HeroineLookAt gaze;
    check(gaze.settings.valid(), "default settings valid");
    settle(gaze, 4, 1);
    check(close(gaze.headYaw(), 0) && close(gaze.headPitch(), 0), "small target moves eyes only");
    check(close(gaze.eyeYaw(), 4 * rad) && close(gaze.eyePitch(), 1 * rad), "eyes acquire target");
    gaze.reset(); gaze.update(30 * rad, 0, 2, true, 1.0f / 60);
    check(gaze.eyeYaw() > gaze.headYaw() && gaze.headYaw() < 18 * rad, "eyes lead the slower head");
    settle(gaze, 28);
    check(close(gaze.headYaw(), 20 * rad) && close(gaze.eyeYaw(), 8 * rad), "head stops at limit and eyes take remainder");
    bool bounded = true;
    for (int yaw = -180; yaw <= 180; yaw += 10)
        for (int pitch = -90; pitch <= 90; pitch += 10)
        {
            settle(gaze, static_cast<float>(yaw), static_cast<float>(pitch));
            bounded = bounded && fabsf(gaze.headYaw()) <= 20 * rad && fabsf(gaze.headPitch()) <= 8 * rad
                && fabsf(gaze.eyeYaw()) <= 8 * rad && fabsf(gaze.eyePitch()) <= 4 * rad
                && powf(gaze.eyeYaw() / (8 * rad), 2) + powf(gaze.eyePitch() / (4 * rad), 2) <= 1.00001f;
        }
    check(bounded, "joint limits hold for all target directions");
    settle(gaze, 50);
    check(gaze.tracking() && close(gaze.headYaw(), 20 * rad) && close(gaze.eyeYaw(), 8 * rad), "side target stays acquired without enlarging joint limits");
    settle(gaze, -50);
    check(gaze.tracking() && close(gaze.headYaw(), -20 * rad) && close(gaze.eyeYaw(), -8 * rad), "wide acquisition is symmetric");
    settle(gaze, 65);
    check(!gaze.tracking() && fabsf(gaze.headYaw()) < 0.0001f && fabsf(gaze.eyeYaw()) < 0.0001f, "configured side boundary releases");
    gaze.settings.viewYawDegrees = 40; settle(gaze, 50);
    check(!gaze.tracking(), "narrow acquisition setting is respected");
    gaze.settings.viewYawDegrees = 75; settle(gaze, 60);
    check(gaze.tracking(), "wider acquisition setting is respected");
    gaze.settings.viewYawDegrees = 65;
    gaze.settings.headEnabled = false; settle(gaze, 6);
    check(close(gaze.headYaw(), 0) && close(gaze.eyeYaw(), 6 * rad), "eyes-only mode");
    gaze.settings.headEnabled = true; gaze.settings.eyesEnabled = false; settle(gaze, 20);
    check(close(gaze.headYaw(), 20 * rad) && close(gaze.eyeYaw(), 0), "head-only fallback");
    gaze.settings.eyesEnabled = true; settle(gaze, 30);
    const float prior = gaze.headYaw(); gaze.update(180 * rad, 0, 2, true, 1.0f / 60);
    check(gaze.headYaw() < prior && gaze.headYaw() > 0 && !gaze.tracking(), "behind target releases smoothly");
    settle(gaze, 180);
    check(fabsf(gaze.headYaw()) < 0.0001f && fabsf(gaze.eyeYaw()) < 0.0001f, "behind target returns to authored pose");
    settle(gaze, 20, 0, 0.1f); check(gaze.tracking(), "near target remains acquired");
    check(gaze.eyeClosure() > 0.9f, "near contact closes eyes");
    settle(gaze, 0, 75, 0.2f); check(gaze.tracking(), "steep near vertical target stays acquired");
    check(fabsf(gaze.headPitch()) <= 8 * rad && fabsf(gaze.eyePitch()) <= 4 * rad, "near vertical gaze stays within joint limits");
    settle(gaze, 20);
    gaze.update(20 * rad, 0, 0.2f, true, 1.0f / 60, 0.01f);
    check(gaze.eyeClosure() > 0 && gaze.eyeClosure() < 1, "eyelids blend smoothly on approach");
    for (int i = 0; i < 120; ++i) gaze.update(20 * rad, 0, 0.2f, true, 1.0f / 60, 0);
    check(gaze.eyeClosure() == 1 && gaze.tracking(), "horizontal body contact closes eyes despite eye height difference");
    const float heldHead = gaze.headYaw(), heldEyes = gaze.eyeYaw();
    gaze.update(0, 0, 0, true, 0.1f);
    check(gaze.tracking() && gaze.headYaw() == heldHead && gaze.eyeYaw() == heldEyes, "zero distance holds last valid direction");
    gaze.update(0, 0, 2, true, 1.0f / 60);
    check(gaze.eyeClosure() > 0 && gaze.eyeClosure() < 1, "eyelids blend open after separation");
    settle(gaze, 20); check(gaze.eyeClosure() == 0, "separation reopens eyes");
    gaze.update(0, 0, 10, true, 0.1f, 0);
    check(!gaze.tracking() && gaze.eyeClosure() == 0, "far vertical target cannot trigger contact from horizontal distance alone");
    gaze.settings.closeEyesWhenNear = false; settle(gaze, 20, 0, 0.01f);
    check(gaze.eyeClosure() == 0 && gaze.tracking(), "closure can be disabled without releasing near gaze");
    gaze.settings.closeEyesWhenNear = true;
    settle(gaze, 20, 0, 10); check(!gaze.tracking() && fabsf(gaze.eyeYaw()) < 0.0001f, "far target releases");
    settle(gaze, 25); gaze.settings.enabled = false;
    settle(gaze, 25); check(!gaze.tracking() && fabsf(gaze.headYaw()) < 0.0001f, "disabled releases");
    gaze.settings.enabled = true;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    settle(gaze, 20); gaze.update(nan, nan, nan, true, 0.1f);
    check(std::isfinite(gaze.headYaw()) && std::isfinite(gaze.eyeYaw()) && !gaze.tracking(), "invalid target never contaminates pose");
    const float unchanged = gaze.headYaw();
    gaze.update(1, 1, 2, true, nan); gaze.update(1, 1, 2, true, -1);
    check(gaze.headYaw() == unchanged, "invalid delta time ignored");
    settle(gaze, 40); gaze.settings.headYawDegrees = 5; gaze.update(40 * rad, 0, 2, true, 0.01f);
    check(fabsf(gaze.headYaw()) <= 5 * rad, "lowering limits cannot leave old excessive rotation");
    HeroineLookAt mirrorA, mirrorB;
    settle(mirrorA, 25, 10); settle(mirrorB, -25, -10);
    check(close(mirrorA.headYaw(), -mirrorB.headYaw()) && close(mirrorA.eyePitch(), -mirrorB.eyePitch()), "directions are symmetric");
    HeroineLookAt at30, at60;
    for (int i = 0; i < 30; ++i) at30.update(30 * rad, 10 * rad, 2, true, 1.0f / 30);
    for (int i = 0; i < 60; ++i) at60.update(30 * rad, 10 * rad, 2, true, 1.0f / 60);
    check(fabsf(at30.headYaw() - at60.headYaw()) < 0.001f && fabsf(at30.eyeYaw() - at60.eyeYaw()) < 0.001f, "frame-rate stable pursuit");
    HeroineLookAt longFrame, shortFrame;
    longFrame.update(20 * rad, 0, 2, true, 10); shortFrame.update(20 * rad, 0, 2, true, 0.1f);
    check(close(longFrame.eyeYaw(), shortFrame.eyeYaw()), "long frames bounded");
    settle(gaze, 10); for (int i = 0; i < 600; ++i) gaze.update(10 * rad, 0, 2, false, 1.0f / 60);
    check(!gaze.tracking() && fabsf(gaze.eyeYaw()) < 0.0001f, "missing player releases");

    HeroineLookAtSettings settings;
    settings.headEnabled = false; settings.headYawDegrees = 14.25f;
    std::ostringstream saved; settings.write(saved);
    HeroineLookAtSettings loaded; std::istringstream input(saved.str());
    check(loaded.read(input) && !loaded.headEnabled && loaded.headYawDegrees == 14.25f, "settings round trip");
    std::ostringstream savedAgain; loaded.write(savedAgain);
    check(savedAgain.str() == saved.str(), "all settings preserved");
    check(loaded.closeEyesWhenNear && close(loaded.fullyClosedDistance, 0.08f), "version 2 closure settings round trip");
    HeroineLookAtSettings closePreset;
    closePreset.closeEyesWhenNear = false; closePreset.fullyClosedDistance = 0.04f; closePreset.eyelidResponse = 20;
    std::ostringstream closedSaved; closePreset.write(closedSaved);
    std::istringstream closedInput(closedSaved.str());
    HeroineLookAtSettings closeLoaded;
    check(closeLoaded.read(closedInput) && !closeLoaded.closeEyesWhenNear && close(closeLoaded.fullyClosedDistance, 0.04f)
        && closeLoaded.eyelidResponse == 20, "custom closure configuration persists");
    for (const auto& invalid : { std::string("heroineLookAt 2"), std::string("heroineLookAt 1 1"), saved.str() + "garbage" })
    {
        std::istringstream bad(invalid); check(!loaded.read(bad), "malformed settings rejected");
        check(loaded.headYawDegrees == 14.25f && !loaded.headEnabled, "failed load preserves settings");
    }
    std::istringstream legacy("heroineLookAt 1\n1 1 1\n20 8 25 15\n12 6 65\n0.3 6 4 18 0.35\n");
    HeroineLookAtSettings migrated;
    check(migrated.read(legacy) && migrated.eyeYawDegrees == 12 && migrated.eyePitchDegrees == 6, "legacy wide preset is bounded on load");
    check(migrated.headYawDegrees == 20 && migrated.eyeOnlyYawDegrees <= migrated.eyeYawDegrees, "migration keeps head limits and valid eye dead zone");
    check(migrated.closeEyesWhenNear && close(migrated.fullyClosedDistance, 0.08f), "version 1 enables default contact closure");
    HeroineLookAt lowered;
    settle(lowered, 28);
    lowered.settings.eyeYawDegrees = 2; lowered.settings.eyePitchDegrees = 1; lowered.settings.constrainEyes();
    lowered.update(15 * rad, 10 * rad, 2, true, 0.016f);
    check(powf(lowered.eyeYaw() / (2 * rad), 2) + powf(lowered.eyePitch() / rad, 2) <= 1.00001f, "narrowing eye limits immediately enforces diagonal cone");
    HeroineLookAt noEyes;
    noEyes.settings.eyeYawDegrees = noEyes.settings.eyePitchDegrees = 0;
    noEyes.settings.constrainEyes(); settle(noEyes, 20);
    check(noEyes.eyeYaw() == 0 && noEyes.eyePitch() == 0, "zero eye ranges remain finite and stationary");
    settings.headYawDegrees = nan; check(!settings.valid(), "NaN settings rejected");

    const auto* scene = aiImportFile("asset/model/Player_Movement/Player_Preview.fbx",
        aiProcessPreset_TargetRealtime_MaxQuality | aiProcess_ConvertToLeftHanded);
    check(scene != nullptr, "actual heroine FBX loads");
    const auto* head = scene->mRootNode->FindNode("J_Bip_C_Head");
    const auto* left = scene->mRootNode->FindNode("J_Adj_L_FaceEye");
    const auto* right = scene->mRootNode->FindNode("J_Adj_R_FaceEye");
    check(head && left && right && left->mParent == head && right->mParent == head, "head and independent eyes exist");
    aiMatrix4x4 rotation; aiMatrix4x4::RotationY(15 * rad, rotation);
    const auto parent = world(head);
    const auto leftBefore = parent * left->mTransformation, rightBefore = parent * right->mTransformation;
    const auto leftAfter = parent * BonePoseRotation::apply(parent, left->mTransformation, rotation);
    const auto rightAfter = parent * BonePoseRotation::apply(parent, right->mTransformation, rotation);
    check(close(leftBefore.a4, leftAfter.a4) && close(leftBefore.b4, leftAfter.b4) && close(leftBefore.c4, leftAfter.c4), "left eye pivot remains fixed");
    check(close(rightBefore.a4, rightAfter.a4) && close(rightBefore.b4, rightAfter.b4) && close(rightBefore.c4, rightAfter.c4), "right eye pivot remains fixed");
    auto inverseL = leftBefore; inverseL.Inverse();
    auto inverseR = rightBefore; inverseR.Inverse();
    auto deltaL = leftAfter * inverseL, deltaR = rightAfter * inverseR;
    deltaL.a4 = deltaL.b4 = deltaL.c4 = 0; deltaR.a4 = deltaR.b4 = deltaR.c4 = 0;
    check(difference(deltaL, rotation) < 0.00001f && difference(deltaR, rotation) < 0.00001f, "both eyes rotate identically despite different FBX axes");
    check(difference(BonePoseRotation::apply(parent, left->mTransformation, aiMatrix4x4()), left->mTransformation) < 0.00001f, "zero correction preserves authored eye pose");
    unsigned int leftWeights = 0, rightWeights = 0, closeVertices = 0;
    bool closeBlendCorrect = true;
    for (unsigned int m = 0; m < scene->mNumMeshes; ++m)
        for (unsigned int b = 0; b < scene->mMeshes[m]->mNumBones; ++b)
        {
            const auto* bone = scene->mMeshes[m]->mBones[b];
            if (bone->mName == left->mName) leftWeights += bone->mNumWeights;
            if (bone->mName == right->mName) rightWeights += bone->mNumWeights;
        }
    check(leftWeights > 0 && rightWeights > 0, "both eye bones deform real vertices");
    for (unsigned int m = 0; m < scene->mNumMeshes; ++m)
    {
        const auto* mesh = scene->mMeshes[m];
        for (unsigned int a = 0; a < mesh->mNumAnimMeshes; ++a)
        {
            const auto* shape = mesh->mAnimMeshes[a];
            if (shape->mName != aiString("Fcl_EYE_Close") || !shape->mVertices) continue;
            for (unsigned int v = 0; v < mesh->mNumVertices; ++v)
                if (MorphOverlay::affects(mesh->mVertices[v], shape->mVertices[v]))
                {
                    ++closeVertices;
                    const auto closed = MorphOverlay::blend(mesh->mVertices[v], shape->mVertices[v], shape->mVertices[v], 1);
                    closeBlendCorrect = closeBlendCorrect && (closed - shape->mVertices[v]).SquareLength() < 0.000000000001f;
                }
        }
    }
    check(closeBlendCorrect, "already closed eyelid is not closed twice");
    check(closeVertices > 0, "actual FBX provides a working eye-close morph");
    const aiVector3D base(0, 0, 0), closed(0, -1, 0), expression(1, 0, 0);
    check((MorphOverlay::blend(base, base, expression, 1) - expression).SquareLength() == 0, "unaffected mouth expression is preserved");
    check((MorphOverlay::blend(base, closed, expression, 0) - expression).SquareLength() == 0, "zero overlay preserves expression");
    check((MorphOverlay::blend(base, closed, expression, 0.5f) - aiVector3D(0.5f, -0.5f, 0)).SquareLength() < 0.00001f, "eyelid overlay interpolates over existing expression");
    aiReleaseImport(scene);
    std::printf("HeroineLookAt: %d checks passed\n", checks);
}
