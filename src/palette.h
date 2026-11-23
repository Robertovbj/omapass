#pragma once

#include <QColor>
#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QString>

// The colours the interface draws with, resolved live from the Omarchy theme
// (~/.local/state/omarchy/current/theme/colors.toml) and re-read whenever the
// desktop switches themes.
//
// The semantic role names are the ones omapass has always used in its own theme
// files (Title, Base, Guidance, …), so a `~/.config/fpass/themes/*.toml`
// still pins whatever it pins — those overrides are applied last and win
// over the Omarchy palette.
class Palette : public QObject {
    Q_OBJECT
    Q_PROPERTY(QColor background READ background NOTIFY changed)
    Q_PROPERTY(QColor surface READ surface NOTIFY changed)
    Q_PROPERTY(QColor selection READ selection NOTIFY changed)
    Q_PROPERTY(QColor base READ base NOTIFY changed)
    Q_PROPERTY(QColor title READ title NOTIFY changed)
    Q_PROPERTY(QColor guidance READ guidance NOTIFY changed)
    Q_PROPERTY(QColor annotation READ annotation NOTIFY changed)
    Q_PROPERTY(QColor important READ important NOTIFY changed)
    Q_PROPERTY(QColor alertInfo READ alertInfo NOTIFY changed)
    Q_PROPERTY(QColor alertWarn READ alertWarn NOTIFY changed)
    Q_PROPERTY(QColor alertError READ alertError NOTIFY changed)
    // The window's own backdrop: the background, faded under Omarchy's
    // Glassy (0.75) and Transparent (0.96, the default) windows so Hyprland's
    // blur shows through. Modals and menus keep `background`, so they stay solid.
    Q_PROPERTY(QColor windowBackground READ windowBackground NOTIFY changed)
    Q_PROPERTY(bool darkMode READ darkMode NOTIFY darkModeChanged)

    // The roles Omarchy's own apps (Hype, Omajim) draw with, mixed from the
    // theme so every theme gets them: the window's foreground and accent, a
    // panel a shade off the background for cards and dialogs, a button fill,
    // a hover tint of the accent, a border, muted text, and the ink that
    // reads on the accent and on the selection.
    Q_PROPERTY(QColor foreground READ foreground NOTIFY changed)
    Q_PROPERTY(QColor accent READ accent NOTIFY changed)
    Q_PROPERTY(QColor accentText READ accentText NOTIFY changed)
    Q_PROPERTY(QColor accentHover READ accentHover NOTIFY changed)
    Q_PROPERTY(QColor panel READ panel NOTIFY changed)
    Q_PROPERTY(QColor button READ button NOTIFY changed)
    Q_PROPERTY(QColor hover READ hover NOTIFY changed)
    Q_PROPERTY(QColor border READ border NOTIFY changed)
    Q_PROPERTY(QColor muted READ muted NOTIFY changed)
    Q_PROPERTY(QColor selectionText READ selectionText NOTIFY changed)
    Q_PROPERTY(QColor error READ error NOTIFY changed)
    // Hyprland's window rounding, so corners inside match the window's own;
    // 0 (sharp, Omarchy's default) until something says otherwise.
    Q_PROPERTY(int radius READ radius NOTIFY radiusChanged)

public:
    explicit Palette(const QHash<QString, QString> &overrides, QObject *parent = nullptr);

    QColor background() const { return m_background; }
    QColor surface() const { return m_surface; }
    QColor selection() const { return m_selection; }
    QColor base() const { return m_base; }
    QColor title() const { return m_title; }
    QColor guidance() const { return m_guidance; }
    QColor annotation() const { return m_annotation; }
    QColor important() const { return m_important; }
    QColor alertInfo() const { return m_alertInfo; }
    QColor alertWarn() const { return m_alertWarn; }
    QColor alertError() const { return m_alertError; }
    QColor windowBackground() const { return m_windowBackground; }
    bool darkMode() const { return m_darkMode; }
    QColor foreground() const { return m_base; }
    QColor accent() const { return m_title; }
    QColor accentText() const { return m_accentText; }
    QColor accentHover() const { return m_accentHover; }
    QColor panel() const { return m_panel; }
    QColor button() const { return m_button; }
    QColor hover() const { return m_hover; }
    QColor border() const { return m_border; }
    QColor muted() const { return m_muted; }
    QColor selectionText() const { return m_selectionText; }
    QColor error() const { return m_alertError; }
    int radius() const { return m_radius; }

public slots:
    // Called when the desktop's light/dark preference changes. Only decides
    // the built-in fallback palette — an Omarchy theme, when present, states
    // its own mode and wins.
    void setSystemDarkMode(bool darkMode);
    void setRadius(int radius);

signals:
    void changed();
    void darkModeChanged();
    void radiusChanged();

private:
    void reload();
    void watchThemeFiles();

    QHash<QString, QString> m_overrides;

    QColor m_background;
    QColor m_surface;
    QColor m_selection;
    QColor m_base;
    QColor m_title;
    QColor m_guidance;
    QColor m_annotation;
    QColor m_important;
    QColor m_alertInfo;
    QColor m_alertWarn;
    QColor m_alertError;
    QColor m_windowBackground;
    QColor m_accentText;
    QColor m_accentHover;
    QColor m_panel;
    QColor m_button;
    QColor m_hover;
    QColor m_border;
    QColor m_muted;
    QColor m_selectionText;
    int m_radius = 0;

    bool m_darkMode = true;
    bool m_systemDarkMode = true;
    QFileSystemWatcher m_watcher;
};
