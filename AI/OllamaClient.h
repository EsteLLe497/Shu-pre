#pragma once

#include "DialogueTypes.h"
#include <atomic>
#include <chrono>
#include <functional>

namespace AI
{
    using Deadline = std::chrono::steady_clock::time_point;
    struct HttpReply
    {
        DialogueCode code = DialogueCode::unavailable;
        unsigned int status = 0;
        std::string body;
    };
    using HttpPost = std::function<HttpReply(unsigned int, const std::string&, const std::string&, const std::atomic_bool&, Deadline)>;

    // Worker-thread entry point. Endpoints are fixed to HTTP loopback, never a public API.
    class OllamaClient
    {
    public:
        explicit OllamaClient(HttpPost transport = {});
        DialogueResult generate(const DialogueRequest& request, const std::atomic_bool& cancelled) const;
        static HttpReply post(unsigned int port, const std::string& path, const std::string& body,
            const std::atomic_bool& cancelled, Deadline deadline);

    private:
        HttpPost m_transport;
    };
}
