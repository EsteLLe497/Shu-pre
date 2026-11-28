#include "../HeroineExpression.h"
#include "../MorphOverlay.h"
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>

namespace
{
    int checks = 0;
    void check(bool ok, const char* message)
    {
        ++checks;
        if (!ok) { std::cerr << message << '\n'; std::exit(1); }
    }
    void settle(HeroineExpression& expression) { for (int i = 0; i < 180; ++i) expression.update(1.0f / 60); }
    float weight(const HeroineExpression& expression, const char* name)
    {
        for (const auto& entry : expression.weights()) if (entry.first == name) return entry.second;
        return 0;
    }
}

int main()
{
    HeroineExpression expression;
    std::ifstream input("asset/ai/heroine-expressions.cfg");
    check(expression.read(input), "shipped settings load");
    std::vector<std::string> names;
    for (int i = 1; i < static_cast<int>(HeroineEmotion::count); ++i)
        for (const auto& channel : expression.preset(static_cast<HeroineEmotion>(i))) names.push_back(channel.name);
    expression.bind(names);
    expression.update(1.0f / 60);
    check(expression.weights().empty(), "neutral preserves original animation");
    expression.setConversation(HeroineExpression::Phase::thinking);
    settle(expression);
    check(expression.target() == HeroineEmotion::troubled && weight(expression, "Fcl_BRW_Sorrow") > 0.2f, "thinking uses subtle troubled expression");
    check(expression.blendWeight() < 0.36f, "thinking is weaker than speech");
    expression.setConversation(HeroineExpression::Phase::speaking, HeroineEmotion::happy);
    expression.update(1.0f / 60);
    check(weight(expression, "Fcl_BRW_Sorrow") > 0 && weight(expression, "Fcl_MTH_Joy") > 0, "expressions crossfade without snapping");
    settle(expression);
    check(std::fabs(weight(expression, "Fcl_MTH_Joy") - 0.6f) < 0.001f, "reply reaches configured weights");
    check(weight(expression, "Fcl_BRW_Sorrow") == 0, "old expression removed");
    expression.preview(HeroineEmotion::surprised, 0.5f);
    expression.setConversation(HeroineExpression::Phase::idle);
    settle(expression);
    check(expression.previewing() && expression.target() == HeroineEmotion::surprised, "manual preview wins over conversation");
    check(std::fabs(weight(expression, "Fcl_EYE_Surprised") - 0.175f) < 0.001f, "preview strength scales preset");
    const auto before = expression.weights();
    expression.update(std::numeric_limits<float>::quiet_NaN());
    expression.update(-1);
    check(before == expression.weights(), "invalid timestep ignored");
    expression.useConversation(); settle(expression);
    check(expression.weights().empty(), "idle and cancellation fade back to original face");
    expression.enabled = false;
    expression.setConversation(HeroineExpression::Phase::speaking, HeroineEmotion::angry);
    settle(expression);
    check(expression.weights().empty(), "automatic expressions can be disabled");
    expression.preview(HeroineEmotion::angry); settle(expression);
    check(!expression.weights().empty(), "manual preview works with automatic disabled");
    expression.bind({}); settle(expression);
    check(expression.weights().empty() && expression.blendWeight() == 0, "model without morphs leaves animation unchanged");
    expression.bind(names);
    expression.preset(HeroineEmotion::angry)[0].name = "missing";
    settle(expression);
    check(weight(expression, "missing") == 0 && weight(expression, "Fcl_MTH_Angry") > 0, "missing channel skipped independently");
    std::ostringstream saved; expression.write(saved);
    HeroineExpression copy; std::istringstream roundTrip(saved.str());
    check(copy.read(roundTrip) && copy.preset(HeroineEmotion::angry)[0].name == "missing", "custom mapping round trip");
    auto invalid = saved.str(); invalid.replace(invalid.find("0.449999988"), 11, "1.5");
    std::istringstream malformed(invalid);
    check(!copy.read(malformed) && copy.preset(HeroineEmotion::happy)[0].weight == 0.45f, "out of range config rejected atomically");
    std::istringstream trailing(saved.str() + "unexpected");
    check(!copy.read(trailing), "unknown config lines rejected");
    for (int i = 0; i < static_cast<int>(HeroineEmotion::count); ++i)
    {
        HeroineEmotion selected = HeroineEmotion::neutral;
        check(parseEmotion(emotionName(static_cast<HeroineEmotion>(i)), selected) && static_cast<int>(selected) == i, "emotion names round trip");
    }
    HeroineEmotion selected = HeroineEmotion::neutral;
    check(!parseEmotion("run arbitrary script", selected) && selected == HeroineEmotion::neutral, "unknown command is not an emotion");
    const aiVector3D base(0, 0, 0), closed(0, -1, 0), smile(0, 0.2f, 0);
    check((MorphOverlay::blend(base, closed, smile, 1) - closed).SquareLength() < 0.00001f, "full proximity closure wins over expression");
    check((MorphOverlay::blend(base, base, smile, 1) - smile).SquareLength() < 0.00001f, "closure leaves unrelated vertices intact");
    expression = HeroineExpression(); expression.bind(names);
    for (int i = 0; i < 180; ++i)
    {
        expression.preview(static_cast<HeroineEmotion>(1 + i % 4)); expression.update(1.0f / 60);
        check(expression.weights().size() <= HeroineExpression::maxChannels && expression.blendWeight() <= 1, "rapid crossfades remain bounded");
    }
    std::cout << checks << " expression checks passed\n";
}
