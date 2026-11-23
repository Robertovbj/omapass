#pragma once

#include <QLocalSocket>
#include <QObject>
#include <QProcess>
#include <QTimer>

// Hyprland's window rounding (decoration:rounding), so the corners drawn
// inside the window match the window's own. Asked of hyprctl on start and
// again whenever Hyprland's event socket announces `configreloaded`, which it
// sends once the new values are in effect (a theme switch, the user's own
// config). Follows the Omarchy app template's Theme (MIT, David Heinemeier
// Hansson). Without Hyprland nothing is asked and the corners stay sharp.
class HyprRounding : public QObject {
    Q_OBJECT

public:
    // Tests pass a fake hyprctl and their own event socket; empty finds the
    // real ones, and only under Hyprland.
    explicit HyprRounding(QObject *parent = nullptr, const QString &hyprctl = {},
                          const QString &events = {});
    ~HyprRounding() override;

    int radius() const { return m_radius; }

signals:
    void radiusChanged(int radius);

private:
    void refresh();

    QString m_hyprctl;
    int m_radius = 0;
    bool m_again = false;
    QProcess m_process;
    QTimer m_timeout;
    QLocalSocket m_events;
    QTimer m_reconnect;
};
