#include "main.h"
#include "renderer.h"
#include "HeroineConversation.h"
#include "Heroine.h"
#include "Player.h"
#include "manager.h"
#include "AI/DialogueProtocol.h"
#include <fstream>
#include <locale>

void HeroineConversation::Init()
{
    m_Layer = 1;
    std::ifstream personality("asset/ai/heroine-personality.txt", std::ios::binary | std::ios::ate);
    if (personality && personality.tellg() > 0 && personality.tellg() <= 4800)
    {
        std::string text(static_cast<size_t>(personality.tellg()), '\0');
        personality.seekg(0);
        personality.read(&text[0], static_cast<std::streamsize>(text.size()));
        auto candidate = m_config;
        candidate.personality = text;
        if (personality && candidate.valid()) m_config = candidate;
    }
    loadConfig();
    m_status = u8"会話待機中。送信するまではOllamaへ接続しません。";
    m_session.reset(new AI::DialogueSession);
}

void HeroineConversation::Uninit()
{
    m_session.reset();
    GameObject::Uninit();
}

bool HeroineConversation::ask(const std::string& text)
{
    auto* heroine = Manager::GetGameObj<Heroine>();
    const auto* player = Manager::GetGameObj<Player>();
    if (!m_session || !heroine || !player) { m_status = u8"プレイヤーとヒロインが必要です。"; return false; }
    AI::DialogueContext context;
    context.playerDistance = (heroine->GetPosition() - player->GetPosition()).lenght();
    context.heroineMoving = heroine->movement().state() == HeroineMovement::State::moving;
    if (!m_session->submit(text, context, m_config))
    {
        m_status = u8"送信できません。処理中、入力内容、モデル設定を確認してください。";
        return false;
    }
    m_cancelling = false;
    m_lines.push_back({ false, text });
    if (m_lines.size() > 12) m_lines.erase(m_lines.begin());
    m_status = u8"返答を考えています。ゲームの更新は続けます。";
    return true;
}

void HeroineConversation::Update()
{
    m_subtitleSeconds = std::max(0.0f, m_subtitleSeconds - Manager::animationDeltaSeconds());
    if (!m_session) return;
    AI::DialogueResult result;
    if (m_session->poll(result))
    {
        m_status = AI::statusText(result.code);
        if (result.httpStatus && result.code != AI::DialogueCode::success) m_status += " (HTTP " + std::to_string(result.httpStatus) + ")";
        m_lastElapsed = result.elapsedSeconds;
        if (!result.dialogue.empty())
        {
            m_subtitle = result.dialogue;
            showSubtitle();
            m_lines.push_back({ true, result.dialogue });
            if (m_lines.size() > 12) m_lines.erase(m_lines.begin());
        }
    }
    if (m_cancelling && !busy()) { m_cancelling = false; m_status = AI::statusText(AI::DialogueCode::cancelled); }
}

void HeroineConversation::cancel()
{
    if (m_session) m_session->cancel();
    m_cancelling = true;
    m_status = u8"通信を中止しています。";
}

void HeroineConversation::clear()
{
    if (m_session) m_session->clear();
    m_cancelling = false;
    m_lines.clear(); m_subtitle.clear(); m_subtitleSeconds = 0; m_subtitleDuration = 0;
    m_status = u8"会話履歴を消去しました。";
}

void HeroineConversation::showSubtitle()
{
    m_subtitleDuration = m_subtitleSeconds = 6.0f;
}

void HeroineConversation::setSubtitleDuration(float seconds)
{
    const float elapsed = subtitleElapsed();
    m_subtitleDuration = std::max(1.0f, seconds);
    m_subtitleSeconds = std::max(0.0f, m_subtitleDuration - elapsed);
}

bool HeroineConversation::loadConfig()
{
    std::ifstream input("asset/ai/ollama.cfg");
    input.imbue(std::locale::classic());
    const bool loaded = m_config.read(input);
    m_status = loaded ? u8"Ollama設定を読み込みました。" : u8"有効なOllama設定がありません。現在の設定を使用します。";
    return loaded;
}

bool HeroineConversation::saveConfig()
{
    if (!m_config.valid()) { m_status = u8"モデル名・設定値が不正です。"; return false; }
    char temporary[MAX_PATH]{};
    if (!GetTempFileNameA("asset/ai", "ai", 0, temporary)) { m_status = u8"保存先を確認してください。"; return false; }
    std::ofstream output(temporary);
    output.imbue(std::locale::classic()); m_config.write(output); output.close();
    const bool saved = output && MoveFileExA(temporary, "asset/ai/ollama.cfg", MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (!saved) DeleteFileA(temporary);
    m_status = saved ? u8"Ollama設定を保存しました。" : u8"Ollama設定を保存できませんでした。";
    return saved;
}
