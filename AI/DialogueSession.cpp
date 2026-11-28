#include "DialogueSession.h"
#include "DialogueProtocol.h"
#include "OllamaClient.h"
#include <winrt/base.h>

AI::DialogueSession::DialogueSession(Generator generate)
    : m_generate(generate ? std::move(generate) : Generator([](const DialogueRequest& request, const std::atomic_bool& cancelled) {
        return OllamaClient{}.generate(request, cancelled);
    })), m_worker(&DialogueSession::run, this) {}

AI::DialogueSession::~DialogueSession()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stop = true;
        if (m_cancel) *m_cancel = true;
    }
    m_wake.notify_one();
    if (m_worker.joinable()) m_worker.join();
}

bool AI::DialogueSession::submit(const std::string& text, const DialogueContext& context, const DialogueConfig& config)
{
    if (!validInput(text) || !config.valid()) return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_stop || m_busy || m_ready) return false;
    m_request = { config, context, text, m_history };
    m_requestGeneration = m_generation;
    m_cancel = std::make_shared<std::atomic_bool>(false);
    m_queued = m_busy = true;
    m_wake.notify_one();
    return true;
}

bool AI::DialogueSession::poll(DialogueResult& result)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_ready) return false;
    result = std::move(m_result);
    m_ready = false;
    if (result.code == DialogueCode::success)
    {
        m_history.push_back({ m_resultInput, result.dialogue });
        if (m_history.size() > 4) m_history.erase(m_history.begin());
    }
    return true;
}

bool AI::DialogueSession::busy() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_busy;
}

void AI::DialogueSession::cancel()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_cancel) *m_cancel = true;
    ++m_generation;
    m_ready = false;
}

void AI::DialogueSession::clear()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_cancel) *m_cancel = true;
    ++m_generation;
    m_ready = false;
    m_history.clear();
}

std::vector<AI::DialogueTurn> AI::DialogueSession::history() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_history;
}

void AI::DialogueSession::run()
{
    bool apartment = false;
    try { winrt::init_apartment(winrt::apartment_type::multi_threaded); apartment = true; } catch (...) {}
    while (true)
    {
        DialogueRequest request;
        unsigned int generation;
        std::shared_ptr<std::atomic_bool> cancelled;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_wake.wait(lock, [this] { return m_stop || m_queued; });
            if (m_stop) break;
            request = m_request;
            generation = m_requestGeneration;
            cancelled = m_cancel;
            m_queued = false;
        }
        DialogueResult result;
        try { result = apartment ? m_generate(request, *cancelled) : DialogueResult{}; }
        catch (...) { result.code = DialogueCode::unavailable; }
        if (result.code == DialogueCode::success && !validDialogue(result.dialogue)) result.code = DialogueCode::invalidResponse;
        if (result.code != DialogueCode::success && result.code != DialogueCode::cancelled) result.dialogue = fallbackDialogue();
        if (result.code != DialogueCode::success || result.emotion < HeroineEmotion::neutral || result.emotion >= HeroineEmotion::count)
            result.emotion = HeroineEmotion::neutral;
        if (result.code != DialogueCode::success || result.gesture < HeroineGestureKind::none || result.gesture >= HeroineGestureKind::count)
            result.gesture = HeroineGestureKind::none;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_busy = false;
            if (!m_stop && generation == m_generation)
            {
                m_result = std::move(result);
                m_resultInput = request.text;
                m_ready = true;
            }
        }
    }
    if (apartment) winrt::uninit_apartment();
}
