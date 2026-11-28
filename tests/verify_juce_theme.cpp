// Drift guard for the two copies of the design tokens.
//
// The Qt console draws from native/ui_theme.h; the JUCE plugin rack draws from
// native/plugins/juce_theme.h, which mirrors it because that target links JUCE
// and PipeWire only. Nothing else in the build would notice a colour changing
// on one side, so parse both headers and compare every shared name.
#include <cstdio>
#include <fstream>
#include <map>
#include <regex>
#include <string>

namespace {
std::string read(const std::string& path)
{
    std::ifstream in(path);
    if (!in)
        return {};
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

unsigned packed(unsigned r, unsigned g, unsigned b) { return (r << 16) | (g << 8) | b; }
} // namespace

int main()
{
    const std::string root = MIXER_SOURCE_DIR;
    const auto qt = read(root + "/native/ui_theme.h");
    const auto juce = read(root + "/native/plugins/juce_theme.h");
    if (qt.empty() || juce.empty())
    {
        std::printf("verify_juce_theme: cannot read native/ui_theme.h or native/plugins/juce_theme.h\n");
        return 1;
    }

    // ui_theme.h: inline const QColor bg{0x14,0x1d,0x25};
    std::map<std::string, unsigned> qtColours;
    const std::regex qtPattern(
        R"(inline\s+const\s+QColor\s+([A-Za-z_]\w*)\s*\{\s*0x([0-9a-fA-F]{2})\s*,\s*0x([0-9a-fA-F]{2})\s*,\s*0x([0-9a-fA-F]{2})\s*\})");
    for (std::sregex_iterator it(qt.begin(), qt.end(), qtPattern), end; it != end; ++it)
        qtColours[(*it)[1]] = packed(std::stoul((*it)[2].str(), nullptr, 16),
                                     std::stoul((*it)[3].str(), nullptr, 16),
                                     std::stoul((*it)[4].str(), nullptr, 16));

    // juce_theme.h: constexpr juce::uint32 bg = 0xff141d25;
    std::map<std::string, unsigned> juceColours;
    const std::regex jucePattern(
        R"(constexpr\s+juce::uint32\s+([A-Za-z_]\w*)\s*=\s*0xff([0-9a-fA-F]{6})\s*;)");
    for (std::sregex_iterator it(juce.begin(), juce.end(), jucePattern), end; it != end; ++it)
        juceColours[(*it)[1]] = unsigned(std::stoul((*it)[2].str(), nullptr, 16));

    int failures = 0;
    if (qtColours.empty() || juceColours.empty())
    {
        std::printf("verify_juce_theme: parsed %zu Qt colours and %zu JUCE colours; one header changed shape\n",
                    qtColours.size(), juceColours.size());
        ++failures;
    }

    for (const auto& [name, value] : qtColours)
    {
        const auto found = juceColours.find(name);
        if (found == juceColours.end())
        {
            std::printf("  %s: in ui_theme.h as #%06x, missing from juce_theme.h\n", name.c_str(), value);
            ++failures;
        }
        else if (found->second != value)
        {
            std::printf("  %s: ui_theme.h #%06x, juce_theme.h #%06x\n", name.c_str(), value, found->second);
            ++failures;
        }
    }
    for (const auto& [name, value] : juceColours)
    {
        if (qtColours.find(name) == qtColours.end())
        {
            std::printf("  %s: in juce_theme.h as #%06x, not in ui_theme.h\n", name.c_str(), value);
            ++failures;
        }
    }

    if (failures != 0)
    {
        std::printf("verify_juce_theme: %d problem(s); native/plugins/juce_theme.h must mirror native/ui_theme.h\n",
                    failures);
        return 1;
    }
    std::printf("verify_juce_theme: %zu colours agree\n", qtColours.size());
    return 0;
}
