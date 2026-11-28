#include "OllamaClient.h"
#include "DialogueProtocol.h"
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Web.Http.h>
#include <winrt/Windows.Web.Http.Filters.h>
#include <winrt/Windows.Web.Http.Headers.h>
#include <thread>
#include <cmath>
#pragma comment(lib, "windowsapp.lib")

namespace
{
    using namespace winrt::Windows::Foundation;
    struct Stopped { AI::DialogueCode code; };

    template<class Operation>
    auto awaitResult(const Operation& operation, const std::atomic_bool& cancelled, AI::Deadline deadline)
    {
        // Poll only on the worker. No completion callback captures a game object.
        while (operation.Status() == AsyncStatus::Started)
        {
            if (cancelled || std::chrono::steady_clock::now() >= deadline)
            {
                operation.Cancel();
                throw Stopped{ cancelled ? AI::DialogueCode::cancelled : AI::DialogueCode::timeout };
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        if (cancelled) throw Stopped{ AI::DialogueCode::cancelled };
        if (std::chrono::steady_clock::now() >= deadline) throw Stopped{ AI::DialogueCode::timeout };
        return operation.GetResults();
    }

    struct HttpCloser
    {
        winrt::Windows::Web::Http::HttpClient client{ nullptr };
        ~HttpCloser() { if (client) { try { client.Close(); } catch (...) {} } }
    };
}

AI::OllamaClient::OllamaClient(HttpPost transport) : m_transport(transport ? std::move(transport) : HttpPost(post)) {}

AI::HttpReply AI::OllamaClient::post(unsigned int port, const std::string& path, const std::string& body,
    const std::atomic_bool& cancelled, Deadline deadline)
{
    HttpReply reply;
    if (port == 0 || port > 65535 || (path != "/api/show" && path != "/api/chat")) return reply;
    if (cancelled) { reply.code = DialogueCode::cancelled; return reply; }
    if (std::chrono::steady_clock::now() >= deadline) { reply.code = DialogueCode::timeout; return reply; }
    try
    {
        using namespace winrt::Windows::Web::Http;
        using namespace winrt::Windows::Web::Http::Filters;
        using namespace winrt::Windows::Storage::Streams;
        HttpBaseProtocolFilter filter;
        filter.AllowAutoRedirect(false);
        filter.UseProxy(false);
        filter.AllowUI(false);
        HttpCloser connection{ HttpClient(filter) };
        HttpRequestMessage request(HttpMethod::Post(), Uri(winrt::to_hstring("http://127.0.0.1:" + std::to_string(port) + path)));
        request.Content(HttpStringContent(winrt::to_hstring(body), UnicodeEncoding::Utf8, L"application/json"));
        const auto response = awaitResult(connection.client.SendRequestAsync(request, HttpCompletionOption::ResponseHeadersRead), cancelled, deadline);
        reply.status = static_cast<unsigned int>(response.StatusCode());
        if (reply.status != 200) return reply;
        constexpr unsigned int limit = 128 * 1024;
        const auto length = response.Content().Headers().ContentLength();
        if (length && length.Value() > limit) { reply.code = DialogueCode::invalidResponse; return reply; }
        const auto stream = awaitResult(response.Content().ReadAsInputStreamAsync(), cancelled, deadline);
        while (true)
        {
            const auto chunk = awaitResult(stream.ReadAsync(Buffer(8192), 8192, InputStreamOptions::Partial), cancelled, deadline);
            if (chunk.Length() == 0) break;
            if (reply.body.size() + chunk.Length() > limit) { reply.code = DialogueCode::invalidResponse; return reply; }
            std::string bytes(chunk.Length(), '\0');
            DataReader::FromBuffer(chunk).ReadBytes(winrt::array_view<unsigned char>(
                reinterpret_cast<unsigned char*>(&bytes[0]), reinterpret_cast<unsigned char*>(&bytes[0]) + bytes.size()));
            reply.body += bytes;
        }
        reply.code = DialogueCode::success;
    }
    catch (const Stopped& stopped) { reply.code = stopped.code; }
    catch (...) { reply.code = cancelled ? DialogueCode::cancelled : DialogueCode::unavailable; }
    return reply;
}

AI::DialogueResult AI::OllamaClient::generate(const DialogueRequest& request, const std::atomic_bool& cancelled) const
{
    DialogueResult result;
    const auto start = std::chrono::steady_clock::now();
    try
    {
        if (!request.config.valid() || !validInput(request.text) || !std::isfinite(request.context.playerDistance)
            || request.context.playerDistance < 0 || request.history.size() > 4) result.code = DialogueCode::invalidResponse;
        else
        {
            const auto deadline = start + std::chrono::milliseconds(request.config.timeoutMs);
            const auto show = m_transport(request.config.port, "/api/show", showRequest(request.config), cancelled, deadline);
            result.code = show.code; result.httpStatus = show.status;
            if (show.code == DialogueCode::success)
            {
                if (!localModelResponse(show.body)) result.code = DialogueCode::remoteModel;
                else
                {
                    const auto chat = m_transport(request.config.port, "/api/chat", chatRequest(request), cancelled, deadline);
                    result.code = chat.code; result.httpStatus = chat.status;
                    if (chat.code == DialogueCode::success && !parseDialogue(chat.body, result.dialogue, result.emotion, result.gesture)) result.code = DialogueCode::invalidResponse;
                }
            }
        }
    }
    catch (...) { result.code = DialogueCode::invalidResponse; }
    if (cancelled) result.code = DialogueCode::cancelled;
    if (result.code != DialogueCode::success) result.dialogue = result.code == DialogueCode::cancelled ? "" : fallbackDialogue();
    if (result.code != DialogueCode::success) result.emotion = HeroineEmotion::neutral;
    if (result.code != DialogueCode::success) result.gesture = HeroineGestureKind::none;
    result.elapsedSeconds = std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();
    return result;
}
