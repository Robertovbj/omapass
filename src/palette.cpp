#include "palette.h"

#include "config.h"

#include <QDir>
#include <QFile>

#include <cmath>

namespace {

QString omarchyCurrentDir() {
    return QDir::homePath() + QStringLiteral("/.local/state/omarchy/current");
}

QString omarchyThemeDir() {
    return omarchyCurrentDir() + QStringLiteral("/theme");
}

QString omarchyColorsPath() {
    return omarchyThemeDir() + QStringLiteral("/colors.toml");
}

// Omarchy's window transparency (omarchy hyprland window transparency) is a
// toggle file: opaque and glassy each add one, and neither means transparent.
QString omarchyTogglesDir() {
    return omarchyCurrentDir() + QStringLiteral("/../toggles/hypr");
}

// How opaque the window's backdrop is. Without an Omarchy theme there is
// nothing to be see-through with.
double windowOpacity() {
    if (!QFile::exists(omarchyColorsPath()) ||
        QFile::exists(omarchyTogglesDir() + QStringLiteral("/window-opaque.lua")))
        return 1.0;
    if (QFile::exists(omarchyTogglesDir() + QStringLiteral("/window-glassy.lua")))
        return 0.75;
    return 0.96;
}

// Catppuccin Mocha / Latte, the palettes carried over from the TUI version.
// Only reached when no Omarchy theme is installed.
struct Fallback {
    QColor background, surface, selection, base, title, guidance;
    QColor annotation, important, alertInfo, alertWarn, alertError;
};

Fallback fallbackPalette(bool dark) {
    if (dark) {
        return {QColor("#1e1e2e"), QColor("#313244"), QColor("#45475a"), QColor("#cdd6f4"),
                QColor("#74c7ec"), QColor("#7f849c"), QColor("#cba6f7"), QColor("#eba0ac"),
                QColor("#a6e3a1"), QColor("#f9e2af"), QColor("#f38ba8")};
    }
    return {QColor("#eff1f5"), QColor("#dce0e8"), QColor("#ccd0da"), QColor("#4c4f69"),
            QColor("#209fb5"), QColor("#8c8fa1"), QColor("#8839ef"), QColor("#e64553"),
            QColor("#40a02b"), QColor("#df8e1d"), QColor("#d20f39")};
}

// Take the first key that the theme actually defines; Omarchy themes vary in
// which optional colours they carry.
QColor pick(const QHash<QString, QString> &colors, const QStringList &keys, QColor fallback) {
    for (const QString &key : keys) {
        const QColor color(colors.value(key));
        if (color.isValid())
            return color;
    }
    return fallback;
}

// `base` moved `amount` of the way towards `ink`.
QColor mix(const QColor &base, const QColor &ink, double amount) {
    return QColor::fromRgbF(base.redF() + (ink.redF() - base.redF()) * amount,
                            base.greenF() + (ink.greenF() - base.greenF()) * amount,
                            base.blueF() + (ink.blueF() - base.blueF()) * amount);
}

// Black or white, whichever reads on `color` (WCAG relative luminance).
QColor contrastInk(const QColor &color) {
    const auto linear = [](double v) {
        return v <= .04045 ? v / 12.92 : std::pow((v + .055) / 1.055, 2.4);
    };
    const double luminance = .2126 * linear(color.redF()) + .7152 * linear(color.greenF())
                             + .0722 * linear(color.blueF());
    return luminance > .179 ? QColor("#111111") : QColor("#ffffff");
}

bool isDarkColor(const QColor &color) {
    return 0.299 * color.redF() + 0.587 * color.greenF() + 0.114 * color.blueF() < 0.5;
}

}

Palette::Palette(const QHash<QString, QString> &overrides, QObject *parent)
    : QObject(parent), m_overrides(overrides) {
    reload();
    watchThemeFiles();

    // Omarchy replaces the whole `current/theme` symlink on a theme switch,
    // so the directory above the file has to be watched too — and the watch
    // list is rebuilt each time, since the old paths stop existing.
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this]() {
        reload();
        watchThemeFiles();
    });
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this]() {
        reload();
        watchThemeFiles();
    });
}

void Palette::setSystemDarkMode(bool darkMode) {
    if (m_systemDarkMode == darkMode)
        return;

    m_systemDarkMode = darkMode;
    reload();
}

void Palette::setRadius(int radius) {
    if (radius < 0 || radius == m_radius)
        return;
    m_radius = radius;
    emit radiusChanged();
}

void Palette::reload() {
    const QHash<QString, QString> colors = Config::readFlatToml(omarchyColorsPath());

    // The theme's own `mode` is authoritative; failing that, the background's
    // luminance says it plainly. Only a themeless desktop falls back to the
    // portal's preference.
    const QString mode = colors.value(QStringLiteral("mode"));
    const QColor themeBackground(colors.value(QStringLiteral("background")));
    bool dark = m_systemDarkMode;
    if (mode == QStringLiteral("dark"))
        dark = true;
    else if (mode == QStringLiteral("light"))
        dark = false;
    else if (themeBackground.isValid())
        dark = isDarkColor(themeBackground);

    const Fallback fallback = fallbackPalette(dark);

    m_background = pick(colors, {QStringLiteral("background")}, fallback.background);
    m_surface = pick(colors, {QStringLiteral("lighter_background"), QStringLiteral("dark_background")},
                     fallback.surface);
    m_selection = pick(colors, {QStringLiteral("selection")}, fallback.selection);
    m_base = pick(colors, {QStringLiteral("foreground")}, fallback.base);
    m_title = pick(colors, {QStringLiteral("accent"), QStringLiteral("blue")}, fallback.title);
    m_guidance = pick(colors, {QStringLiteral("muted"), QStringLiteral("dark_foreground")},
                      fallback.guidance);
    m_annotation = pick(colors, {QStringLiteral("magenta")}, fallback.annotation);
    m_important = pick(colors, {QStringLiteral("orange"), QStringLiteral("red")}, fallback.important);
    m_alertInfo = pick(colors, {QStringLiteral("green")}, fallback.alertInfo);
    m_alertWarn = pick(colors, {QStringLiteral("yellow")}, fallback.alertWarn);
    m_alertError = pick(colors, {QStringLiteral("red")}, fallback.alertError);

    m_windowBackground = m_background;
    m_windowBackground.setAlphaF(windowOpacity());

    const auto applyOverride = [this](const QString &role, QColor &target) {
        const QColor color(m_overrides.value(role));
        if (color.isValid())
            target = color;
    };
    applyOverride(QStringLiteral("Base"), m_base);
    applyOverride(QStringLiteral("Title"), m_title);
    applyOverride(QStringLiteral("Guidance"), m_guidance);
    applyOverride(QStringLiteral("Annotation"), m_annotation);
    applyOverride(QStringLiteral("Important"), m_important);
    applyOverride(QStringLiteral("AlertInfo"), m_alertInfo);
    applyOverride(QStringLiteral("AlertWarn"), m_alertWarn);
    applyOverride(QStringLiteral("AlertError"), m_alertError);

    // Derived after the overrides, so a pinned Base or Title carries into
    // the panel, the hover and the rest. The mixes are Hype's.
    m_panel = mix(m_background, m_base, .025);
    m_button = pick(colors, {QStringLiteral("lighter_background")}, mix(m_background, m_base, .08));
    m_hover = mix(m_background, m_title, .22);
    m_border = mix(m_background, m_base, .22);
    m_muted = mix(m_background, m_base, .62);
    m_accentText = contrastInk(m_title);
    m_accentHover = mix(m_title, m_base, .15);
    m_selectionText = contrastInk(m_selection);

    if (dark != m_darkMode) {
        m_darkMode = dark;
        emit darkModeChanged();
    }

    emit changed();
}

void Palette::watchThemeFiles() {
    const QStringList watched = m_watcher.files() + m_watcher.directories();
    if (!watched.isEmpty())
        m_watcher.removePaths(watched);

    if (QDir(omarchyCurrentDir()).exists())
        m_watcher.addPath(omarchyCurrentDir());
    if (QDir(omarchyThemeDir()).exists())
        m_watcher.addPath(omarchyThemeDir());
    if (QFile::exists(omarchyColorsPath()))
        m_watcher.addPath(omarchyColorsPath());

    const QString toggles = QDir::cleanPath(omarchyTogglesDir());
    if (QDir(QDir::cleanPath(toggles + QStringLiteral("/.."))).exists())
        m_watcher.addPath(QDir::cleanPath(toggles + QStringLiteral("/..")));
    if (QDir(toggles).exists())
        m_watcher.addPath(toggles);
}
