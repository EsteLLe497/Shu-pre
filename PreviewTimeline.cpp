// =========================================================
// ファイルの情報[PreviewTimeline.cpp]
//
// 制作者:Masatora Tanaka        日付：2026/11/11
// =========================================================
#include "PreviewTimeline.h"
#include "ActionSequence.h"
#include <algorithm>
#include <cmath>

// =========================================================
// 秒で進め、ループ時は最後のフレームを越えた時間も引き継ぐ
// =========================================================
void PreviewTimeline::advance(float dt)
{
    if (!playing)
        return;

    const float next = time + dt * speed;
    if (loop && next >= duration)
    {
        time = std::fmod(next, duration);
        return;
    }

    seek(next);
    if (time >= duration)
        playing = false;
}

void PreviewTimeline::seek(float seconds)
{
    time = std::max(0.0f, std::min(duration, seconds));
}

bool PreviewTimeline::trackEnabled(int track) const
{
    if (soloTrack >= 0)
        return track == soloTrack;

    return track == 0 ? motionEnabled : (track == 1 ? faceEnabled : effectEnabled);
}

// =========================================================
// 開始・最大・終了の3キーを補間し、シーク時も同じ重みを返す
// =========================================================
float PreviewTimeline::faceWeight() const
{
    if (!trackEnabled(1))
        return 0.0f;

    return sampleExpression(time, faceStart, facePeak, faceEnd, faceStrength, smoothFaceBlend);
}

float PreviewTimeline::effectAge() const
{
    return trackEnabled(2) ? time - effectStart : -1.0f;
}
