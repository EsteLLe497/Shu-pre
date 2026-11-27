#include "../HeroineLookAt.h"
#include "../BonePoseRotation.h"
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
    gaze.settings.headEnabled = false; settle(gaze, 6);
    check(close(gaze.headYaw(), 0) && close(gaze.eyeYaw(), 6 * rad), "eyes-only mode");
    gaze.settings.headEnabled = true; gaze.settings.eyesEnabled = false; settle(gaze, 20);
    check(close(gaze.headYaw(), 20 * rad) && close(gaze.eyeYaw(), 0), "head-only fallback");
    gaze.settings.eyesEnabled = true; settle(gaze, 30);
    const float prior = gaze.headYaw(); gaze.update(180 * rad, 0, 2, true, 1.0f / 60);
    check(gaze.headYaw() < prior && gaze.headYaw() > 0 && !gaze.tracking(), "behind target releases smoothly");
    settle(gaze, 180);
    check(fabsf(gaze.headYaw()) < 0.0001f && fabsf(gaze.eyeYaw()) < 0.0001f, "behind target returns to authored pose");
    settle(gaze, 20, 0, 0.1f); check(!gaze.tracking(), "near target is ignored");
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
    for (const auto& invalid : { std::string("heroineLookAt 2"), std::string("heroineLookAt 1 1"), saved.str() + "garbage" })
    {
        std::istringstream bad(invalid); check(!loaded.read(bad), "malformed settings rejected");
        check(loaded.headYawDegrees == 14.25f && !loaded.headEnabled, "failed load preserves settings");
    }
    std::istringstream legacy("heroineLookAt 1\n1 1 1\n20 8 25 15\n12 6 65\n0.3 6 4 18 0.35\n");
    HeroineLookAtSettings migrated;
    check(migrated.read(legacy) && migrated.eyeYawDegrees == 12 && migrated.eyePitchDegrees == 6, "legacy wide preset is bounded on load");
    check(migrated.headYawDegrees == 20 && migrated.eyeOnlyYawDegrees <= migrated.eyeYawDegrees, "migration keeps head limits and valid eye dead zone");
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
    unsigned int leftWeights = 0, rightWeights = 0;
    for (unsigned int m = 0; m < scene->mNumMeshes; ++m)
        for (unsigned int b = 0; b < scene->mMeshes[m]->mNumBones; ++b)
        {
            const auto* bone = scene->mMeshes[m]->mBones[b];
            if (bone->mName == left->mName) leftWeights += bone->mNumWeights;
            if (bone->mName == right->mName) rightWeights += bone->mNumWeights;
        }
    check(leftWeights > 0 && rightWeights > 0, "both eye bones deform real vertices");
    aiReleaseImport(scene);
    std::printf("HeroineLookAt: %d checks passed\n", checks);
}
