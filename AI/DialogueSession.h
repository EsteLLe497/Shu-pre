#pragma once

#include "DialogueTypes.h"
#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace AI
{
    // Thread boundary: value snapshots in, validated text out. No renderer/actor pointers.
    class DialogueSession
    {
    public:
        using Generator = std::function<DialogueResult(const DialogueRequest&, const std::atomic_bool&)>;
        explicit DialogueSession(Generator generate = {});
        ~DialogueSession();
        DialogueSession(const DialogueSession&) = delete;
        DialogueSession& operator=(const DialogueSession&) = delete;
        bool submit(const std::string& text, const DialogueContext& context, const DialogueConfig& config);
        bool poll(DialogueResult& result);
        bool busy() const;
        void cancel();
        void clear();
        std::vector<DialogueTurn> history() const;

    private:
        void run();
        mutable std::mutex m_mutex;
        std::condition_variable m_wake;
        Generator m_generate;
        bool m_stop = false;
        bool m_busy = false;
        bool m_queued = false;
        bool m_ready = false;
        unsigned int m_generation = 0;
        unsigned int m_requestGeneration = 0;
        DialogueRequest m_request;
        DialogueResult m_result;
        std::string m_resultInput;
        std::vector<DialogueTurn> m_history;
        std::shared_ptr<std::atomic_bool> m_cancel;
        std::thread m_worker;
    };
}
