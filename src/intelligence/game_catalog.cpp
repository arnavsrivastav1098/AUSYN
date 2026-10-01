#include "game_catalog.h"

#include <QStringView>

namespace Ausyn {
namespace {

const QVector<GameCatalogProfile> kProfiles{
    {
        QStringLiteral("fortnite"), QStringLiteral("Fortnite"),
        {QStringLiteral("FortniteClient-Win64-Shipping.exe")},
        8.0, 16.0, 0.0, 2.0, 0.0,
        QStringLiteral("Core i3-3225 or equivalent"),
        QStringLiteral("Core i5-7300U / Ryzen 3 3300U or equivalent"),
        QStringLiteral("Intel HD 4000 / AMD Radeon Vega 8"),
        QStringLiteral("GeForce GTX 960 / Radeon R9 280 or equivalent DX11 GPU"),
        QStringLiteral("Try Fortnite Performance Mode / Low scalability settings; Epic notes that lowering settings can help on minimum-class systems."),
        QStringLiteral("Start at the publisher's recommended hardware tier; raise visual settings gradually while checking smoothness."),
        QStringLiteral("https://www.epicgames.com/help/c-44968506/c-0/a16548002?lang=en-US"),
        QDate(2026, 9, 30)
    },
    {
        QStringLiteral("cyberpunk2077"), QStringLiteral("Cyberpunk 2077"),
        {QStringLiteral("Cyberpunk2077.exe")},
        12.0, 16.0, 6.0, 8.0, 70.0,
        QStringLiteral("Core i7-6700 / Ryzen 5 1600"),
        QStringLiteral("Core i7-12700 / Ryzen 7 7800X3D"),
        QStringLiteral("GeForce GTX 1060 6GB / Radeon RX 580 8GB / Intel Arc A380"),
        QStringLiteral("GeForce RTX 2060 Super / Radeon RX 5700 XT / Intel Arc A770"),
        QStringLiteral("Start with Low, 1080p, ray tracing off; these match the publisher's non-RT minimum target tier."),
        QStringLiteral("The publisher lists High at 1080p as the recommended non-RT tier; use it as a starting point only when hardware supports it."),
        QStringLiteral("https://support.cdprojektred.com/en/cyberpunk/pc/sp-technical/issue/1556/cyberpunk-2077-system-requirements"),
        QDate(2026, 9, 30)
    },
    {
        QStringLiteral("gtav_enhanced"), QStringLiteral("Grand Theft Auto V Enhanced"),
        {QStringLiteral("GTA5_Enhanced.exe")},
        8.0, 16.0, 4.0, 8.0, 105.0,
        QStringLiteral("Core i7-4770 / AMD FX-9590"),
        QStringLiteral("Core i5-9600K / Ryzen 5 3600"),
        QStringLiteral("GeForce GTX 1630 4GB / Radeon RX 6400 4GB"),
        QStringLiteral("GeForce RTX 3060 8GB / Radeon RX 6600 XT 8GB"),
        QStringLiteral("Start at Low/Normal, keep ray tracing off if present, and reduce resolution or shadows if play is uneven."),
        QStringLiteral("Start near the game's recommended tier, then adjust one setting at a time; higher resolution and ray tracing need more GPU headroom."),
        QStringLiteral("https://support.rockstargames.com/articles/lMQXeP2Z1mN3g9oZiBZFR/grand-theft-auto-v-pc-system-requirements"),
        QDate(2026, 9, 30)
    },
    {
        QStringLiteral("gtav_legacy"), QStringLiteral("Grand Theft Auto V Legacy"),
        {QStringLiteral("GTA5.exe")},
        4.0, 8.0, 1.0, 2.0, 125.0,
        QStringLiteral("Core 2 Quad Q6600 / AMD Phenom 9850"),
        QStringLiteral("Core i5-3470 / AMD FX-8350"),
        QStringLiteral("GeForce 9800 GT / Radeon HD 4870"),
        QStringLiteral("GeForce GTX 660 / Radeon HD 7870"),
        QStringLiteral("Start with Low settings and a modest resolution; lower shadows and distance if the experience stutters."),
        QStringLiteral("Start around the publisher's recommended tier and raise settings gradually while checking smoothness."),
        QStringLiteral("https://support.rockstargames.com/articles/lMQXeP2Z1mN3g9oZiBZFR/grand-theft-auto-v-pc-system-requirements"),
        QDate(2026, 9, 30)
    },
    {
        QStringLiteral("apex_legends"), QStringLiteral("Apex Legends"),
        {QStringLiteral("r5apex.exe")},
        6.0, 8.0, 2.0, 8.0, 75.0,
        QStringLiteral("Core i3-6300 / AMD FX-4350 or equivalent"),
        QStringLiteral("Core i5-3570K / Ryzen 5 or equivalent"),
        QStringLiteral("GeForce GTX 950 / Radeon HD 7790 (DirectX 12, feature level 12_0)"),
        QStringLiteral("GeForce GTX 970 / Radeon R9 290"),
        QStringLiteral("Begin with low graphics settings and a conservative texture budget; the publisher recommends low settings for higher frame rates."),
        QStringLiteral("Use the publisher's recommended 8 GB RAM and 8 GB GPU-memory tier as a starting point; adjust texture budget to actual VRAM."),
        QStringLiteral("https://www.ea.com/en-au/games/apex-legends/about/pc-system-requirements"),
        QDate(2026, 9, 30)
    },
    {
        QStringLiteral("counter_strike_2"), QStringLiteral("Counter-Strike 2"),
        {QStringLiteral("cs2.exe")},
        8.0, 0.0, 1.0, 0.0, 85.0,
        QStringLiteral("4 hardware CPU threads; Intel Core i5-750 or higher"),
        QStringLiteral("Not stated by publisher"),
        QStringLiteral("1 GB or larger DirectX 11-compatible GPU with Shader Model 5.0"),
        QStringLiteral("Not stated by publisher"),
        QStringLiteral("Start with low or medium effects and shadows, then use the in-game frame counter to adjust for steadier play."),
        QStringLiteral("The publisher does not provide a recommended PC tier on its support page; raise settings gradually while monitoring gameplay."),
        QStringLiteral("https://help.steampowered.com/en/wizard/HelpWithGameTechnicalIssue?appid=730"),
        QDate(2026, 9, 30)
    }
};

} // namespace

const QVector<GameCatalogProfile>& GameCatalog::profiles()
{
    return kProfiles;
}

const GameCatalogProfile* GameCatalog::find(const QString& id)
{
    for (const GameCatalogProfile& profile : kProfiles)
        if (profile.id == id) return &profile;
    return nullptr;
}

const GameCatalogProfile* GameCatalog::findExecutable(const QString& executableName)
{
    const QStringView candidate(executableName);
    for (const GameCatalogProfile& profile : kProfiles) {
        for (const QString& name : profile.executableNames)
            if (candidate.compare(QStringView(name), Qt::CaseInsensitive) == 0) return &profile;
    }
    return nullptr;
}

} // namespace Ausyn
