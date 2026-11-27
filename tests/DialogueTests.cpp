#include "../AI/DialogueProtocol.h"
#include "../AI/OllamaClient.h"
#include "../AI/DialogueSession.h"
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <sstream>
#include <thread>

namespace
{
    using namespace std::chrono;
    using namespace winrt::Windows::Data::Json;
    int checks = 0;
    void check(bool ok, const char* message)
    {
        ++checks;
        if (!ok) { std::fprintf(stderr, "FAILED: %s\n", message); std::exit(1); }
    }
    std::string envelope(const std::string& text)
    {
        JsonObject body, message, root;
        body.SetNamedValue(L"dialogue", JsonValue::CreateStringValue(winrt::to_hstring(text)));
        message.SetNamedValue(L"content", JsonValue::CreateStringValue(body.Stringify()));
        root.SetNamedValue(L"done", JsonValue::CreateBooleanValue(true));
        root.SetNamedValue(L"message", message);
        return winrt::to_string(root.Stringify());
    }
    AI::DialogueRequest request(const char* model = "test-local")
    {
        AI::DialogueRequest value;
        value.config.model = model;
        value.text = u8"こんにちは。\"駅\"まで散歩しよう。";
        value.context.playerDistance = 1.5f;
        return value;
    }
    bool waitResult(AI::DialogueSession& session, AI::DialogueResult& result)
    {
        const auto end = steady_clock::now() + seconds(3);
        while (steady_clock::now() < end)
        {
            if (session.poll(result)) return true;
            std::this_thread::sleep_for(milliseconds(1));
        }
        return false;
    }

    void protocolTests()
    {
        check(AI::validInput(u8"こんにちは🌻"), "Japanese and supplementary Unicode input");
        check(!AI::validInput(" \n\t") && !AI::validInput(u8"　"), "blank input rejected");
        check(!AI::validInput(std::string("a\0b", 3)), "embedded control character rejected");
        check(!AI::validInput(std::string("\xc0\xaf", 2)), "invalid UTF8 rejected");
        check(!AI::validInput(std::string(401, 'a')) && !AI::validDialogue(std::string(241, 'a')), "character limits enforced");
        auto config = request().config;
        check(config.valid(), "local model config valid");
        config.model = "qwen3:cloud"; check(!config.valid(), "cloud model name rejected");
        config.model = "https://example.com"; check(!config.valid(), "model name is not a remote endpoint");
        config = request().config; config.port = 0; check(!config.valid(), "invalid port rejected");
        config = request().config; config.timeoutMs = 0; check(!config.valid(), "unbounded timeout rejected");
        config = request().config; config.port = 12345;
        std::ostringstream saved; config.write(saved);
        AI::DialogueConfig loaded; std::istringstream source(saved.str());
        check(loaded.read(source) && loaded.port == 12345 && loaded.model == "test-local", "config round trip");
        std::istringstream bad("heroineOllama 9 anything");
        check(!loaded.read(bad) && loaded.port == 12345, "failed config load is atomic");
        std::string dialogue = "unchanged";
        check(AI::parseDialogue(envelope(u8"うん、一緒に歩こう。"), dialogue) && dialogue == u8"うん、一緒に歩こう。", "structured Japanese reply parsed");
        for (const char* invalid : { "{}", "not json", "{\"done\":false}", "{\"done\":true,\"done_reason\":\"length\"}" })
            check(!AI::parseDialogue(invalid, dialogue), "invalid envelope rejected");
        auto root = JsonObject::Parse(winrt::to_hstring(envelope("ok")));
        JsonObject injected; injected.SetNamedValue(L"dialogue", JsonValue::CreateStringValue(L"ok"));
        injected.SetNamedValue(L"move_to", JsonValue::CreateStringValue(L"arbitrary"));
        auto message = root.GetNamedObject(L"message"); message.SetNamedValue(L"content", JsonValue::CreateStringValue(injected.Stringify()));
        check(!AI::parseDialogue(winrt::to_string(root.Stringify()), dialogue), "unexpected action field rejected");
        check(!AI::parseDialogue(envelope(""), dialogue), "empty response rejected");
        check(AI::localModelResponse("{\"details\":{}}"), "local show accepted");
        check(!AI::localModelResponse("{}") && !AI::localModelResponse("{\"details\":{},\"remote_host\":\"https://ollama.com\"}"), "remote or unproven model rejected");
        auto value = request(); value.history.push_back({ u8"私の名前は田中です。", u8"よろしくね。" });
        const auto payload = JsonObject::Parse(winrt::to_hstring(AI::chatRequest(value)));
        check(!payload.GetNamedBoolean(L"think") && !payload.GetNamedBoolean(L"stream"), "thinking disabled and buffered JSON requested");
        check(!payload.GetNamedObject(L"format").GetNamedBoolean(L"additionalProperties"), "output schema rejects actions");
        const auto messages = payload.GetNamedArray(L"messages");
        const auto last = JsonObject::Parse(messages.GetAt(messages.Size() - 1).GetObject().GetNamedString(L"content"));
        check(winrt::to_string(last.GetNamedString(L"player_input")) == value.text && messages.Size() == 4, "question escaped and history included");
        value.history.assign(4, { std::string(400, 'x'), std::string(240, 'y') });
        const auto bounded = JsonObject::Parse(winrt::to_hstring(AI::chatRequest(value))).GetNamedArray(L"messages");
        check(bounded.Size() <= 6, "whole recent turns selected within byte budget");
        std::atomic_bool cancelled(false); int calls = 0;
        AI::OllamaClient fake([&](unsigned int, const std::string& path, const std::string&, const std::atomic_bool&, AI::Deadline) {
            ++calls; return AI::HttpReply{ AI::DialogueCode::success, 200, path == "/api/show" ? "{\"details\":{}}" : envelope("valid") };
        });
        check(fake.generate(request(), cancelled).code == AI::DialogueCode::success && calls == 2, "show precedes chat");
        AI::OllamaClient remote([&](unsigned int, const std::string&, const std::string&, const std::atomic_bool&, AI::Deadline) {
            return AI::HttpReply{ AI::DialogueCode::success, 200, "{\"details\":{},\"remote_model\":\"cloud\"}" };
        });
        const auto rejected = remote.generate(request(), cancelled);
        check(rejected.code == AI::DialogueCode::remoteModel && rejected.dialogue == AI::fallbackDialogue(), "remote rejection has local fallback");
    }

    void sessionTests()
    {
        std::promise<void> entered; auto enteredFuture = entered.get_future();
        std::atomic_bool release(false); const auto mainThread = std::this_thread::get_id(); std::atomic_bool separate(false);
        AI::DialogueSession slow([&](const AI::DialogueRequest& value, const std::atomic_bool&) {
            separate = std::this_thread::get_id() != mainThread;
            entered.set_value();
            while (!release) std::this_thread::sleep_for(milliseconds(1));
            return AI::DialogueResult{ AI::DialogueCode::success, value.text };
        });
        const auto start = steady_clock::now();
        check(slow.submit("hello", {}, request().config), "submit accepted");
        check(steady_clock::now() - start < milliseconds(100), "submit does not wait for inference");
        check(enteredFuture.wait_for(seconds(2)) == std::future_status::ready && separate, "generator runs on worker");
        AI::DialogueResult result;
        check(!slow.poll(result) && !slow.submit("another", {}, request().config), "poll remains responsive and concurrent request rejected");
        slow.clear(); release = true;
        for (int i = 0; i < 2000 && slow.busy(); ++i) std::this_thread::sleep_for(milliseconds(1));
        check(!slow.busy() && !slow.poll(result) && slow.history().empty(), "cleared in-flight result cannot reappear");
        AI::DialogueSession session([](const AI::DialogueRequest&, const std::atomic_bool&) { return AI::DialogueResult{ AI::DialogueCode::success, "answer" }; });
        for (int i = 0; i < 6; ++i)
        {
            check(session.submit("question " + std::to_string(i), {}, request().config), "next turn accepted");
            check(waitResult(session, result), "completed reply collected");
        }
        check(session.history().size() == 4 && session.history()[0].player == "question 2", "history keeps four completed turns");
        AI::DialogueSession invalid([](const AI::DialogueRequest&, const std::atomic_bool&) { return AI::DialogueResult{ AI::DialogueCode::success, "" }; });
        invalid.submit("hello", {}, request().config); check(waitResult(invalid, result), "invalid generator completed");
        check(result.code == AI::DialogueCode::invalidResponse && invalid.history().empty(), "invalid replies do not enter memory");
        const auto stopStart = steady_clock::now();
        {
            AI::DialogueSession ending([](const AI::DialogueRequest&, const std::atomic_bool& cancelled) {
                while (!cancelled) std::this_thread::sleep_for(milliseconds(1));
                return AI::DialogueResult{ AI::DialogueCode::cancelled, "" };
            });
            ending.submit("hello", {}, request().config);
        }
        check(steady_clock::now() - stopStart < seconds(1), "shutdown cancels and joins safely");
    }

    void networkTests(unsigned int port)
    {
        std::atomic_bool cancelled(false); AI::OllamaClient client;
        for (const auto& item : { std::make_pair("test-success", AI::DialogueCode::success),
            { "test-remote", AI::DialogueCode::remoteModel }, { "test-invalid", AI::DialogueCode::invalidResponse },
            { "test-redirect", AI::DialogueCode::unavailable }, { "test-oversize", AI::DialogueCode::invalidResponse },
            { "test-chunked", AI::DialogueCode::invalidResponse }, { "test-error", AI::DialogueCode::unavailable },
            { "test-delay", AI::DialogueCode::timeout }, { "test-slowbody", AI::DialogueCode::timeout },
            { "test-total-deadline", AI::DialogueCode::timeout } })
        {
            auto value = request(item.first); value.config.port = port;
            value.config.timeoutMs = std::string(item.first).find("delay") != std::string::npos || (std::string(item.first) == "test-slowbody" || std::string(item.first) == "test-total-deadline") ? 100 : 3000;
            const auto result = client.generate(value, cancelled);
            check(result.code == item.second, item.first);
        }
        AI::DialogueSession session;
        auto value = request("test-delay"); value.config.port = port;
        check(session.submit(value.text, value.context, value.config), "real HTTP request submitted");
        std::this_thread::sleep_for(milliseconds(60));
        const auto start = steady_clock::now(); session.cancel();
        for (int i = 0; i < 1000 && session.busy(); ++i) std::this_thread::sleep_for(milliseconds(1));
        AI::DialogueResult discarded;
        check(!session.busy() && !session.poll(discarded) && steady_clock::now() - start < seconds(1), "real HTTP cancellation discards reply promptly");
        auto oversized = request("test-chunked"); oversized.config.port = port;
        const auto capped = AI::OllamaClient::post(port, "/api/chat", AI::chatRequest(oversized), cancelled, steady_clock::now() + seconds(3));
        check(capped.code == AI::DialogueCode::invalidResponse && capped.body.size() <= 128 * 1024, "transport itself bounds chunked body memory");
    }
}

int main(int argc, char** argv)
{
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    if (argc > 1 && std::string(argv[1]) == "--network") networkTests(static_cast<unsigned int>(std::stoul(argv[2])));
    else if (argc > 1 && std::string(argv[1]) == "--offline")
    {
        auto value = request(); value.config.port = static_cast<unsigned int>(std::stoul(argv[2])); value.config.timeoutMs = 1000;
        std::atomic_bool cancelled(false);
        const auto result = AI::OllamaClient{}.generate(value, cancelled);
        check((result.code == AI::DialogueCode::unavailable || result.code == AI::DialogueCode::timeout)
            && result.dialogue == AI::fallbackDialogue(), "stopped server returns fallback");
    }
    else if (argc > 1 && std::string(argv[1]) == "--live")
    {
        AI::DialogueSession session;
        auto value = request("qwen3:4b"); value.text = u8"こんにちは。一緒に散歩しない？";
        check(session.submit(value.text, value.context, value.config), "live submit");
        AI::DialogueResult result;
        const auto end = steady_clock::now() + seconds(50);
        while (!session.poll(result) && steady_clock::now() < end) std::this_thread::sleep_for(milliseconds(10));
        std::printf("Live code=%d seconds=%.2f dialogue=%s\n", static_cast<int>(result.code), result.elapsedSeconds, result.dialogue.c_str());
        check(result.code == AI::DialogueCode::success && AI::validDialogue(result.dialogue), "live structured reply");
    }
    else { protocolTests(); sessionTests(); }
    std::printf("Dialogue: %d checks passed\n", checks);
    winrt::uninit_apartment();
}
