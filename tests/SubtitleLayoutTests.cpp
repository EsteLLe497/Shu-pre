#include "../AI/SubtitleLayout.h"
#include <iostream>
#include <stdexcept>

int main() try
{
    int checks = 0;
    const auto check = [&](bool ok) { ++checks; if (!ok) throw std::runtime_error("Subtitle layout check failed: " + std::to_string(checks)); };
    const auto measure = [](const std::string& text) {
        float width = 0;
        for (unsigned char c : text) if ((c & 0xc0) != 0x80) ++width;
        return width;
    };
    using namespace AI::SubtitleLayout;
    check(paginate("", 10, measure).empty());
    check(paginate(" \r\n\t", 10, measure).empty());
    const auto shortText = paginate(u8"でも「分霊箱」を捨て\nようとしてるだろ？", 22, measure);
    check(shortText.size() == 1 && shortText[0].lines.size() == 2);
    check(shortText[0].lines[0] == u8"でも「分霊箱」を捨て");
    const auto english = paginate("one two three four", 7, measure);
    check(english.size() == 2 && english[0].lines[0] == "one two");
    const auto punctuation = paginate(u8"あいうえお。かきくけこ「さしすせそ」", 5, measure);
    std::string joined;
    for (const auto& page : punctuation)
        for (const auto& line : page.lines)
        {
            check(measure(line) <= 5);
            check(line.substr(0, 3) != u8"。");
            check(line.substr(line.size() - 3) != u8"「");
            joined += line;
        }
    check(joined == u8"あいうえお。かきくけこ「さしすせそ」");
    std::string longText;
    for (int i = 0; i < 240; ++i) longText += u8"景";
    const auto pages = paginate(longText, 20, measure);
    check(pages.size() == 6);
    joined.clear();
    for (const auto& page : pages)
    {
        check(page.lines.size() <= 2);
        check(page.seconds >= 3);
        for (const auto& line : page.lines) { check(measure(line) <= 20); joined += line; }
    }
    check(joined == longText);
    check(duration(pages) >= 30);
    check(pageAt(pages, 0) == 0);
    check(pageAt(pages, pages[0].seconds) == 1);
    check(pageAt(pages, duration(pages)) == 5);
    const auto narrow = paginate(u8"日本語🙂", 0, measure);
    joined.clear();
    for (const auto& page : narrow) for (const auto& line : page.lines) { check(measure(line) == 1); joined += line; }
    check(joined == u8"日本語🙂");
    std::cout << checks << " subtitle checks passed\n";
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
