#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

// Asks for a file or a folder through the desktop's own chooser,
// xdg-desktop-portal's org.freedesktop.portal.FileChooser, so the dialog is
// the one the rest of Omarchy uses. Follows Omajim's (from Omacut, MIT, David
// Heinemeier Hansson), with folders and file filters added.
//
// One question at a time; `purpose` comes back with the answer, so the sheet
// that asked knows the answer is its own.
class PortalFilePicker : public QObject {
    Q_OBJECT
    // False without a session bus (Windows, macOS, a bare container): the
    // QML falls back to Qt's own dialogs there.
    Q_PROPERTY(bool available READ available CONSTANT)

public:
    explicit PortalFilePicker(QObject *parent = nullptr);

    bool available() const;

    // `filters`: [{ "name": "Imagens", "patterns": ["*.png", "*.svg"] }].
    Q_INVOKABLE void openFile(const QString &purpose, const QString &title,
                              const QVariantList &filters = {});
    Q_INVOKABLE void openFolder(const QString &purpose, const QString &title);

signals:
    // An empty path means the user closed the chooser without picking.
    void picked(const QString &purpose, const QString &path);
    void failed(const QString &purpose, const QString &error);

private slots:
    void onResponse(uint response, const QVariantMap &results);

private:
    void request(const QString &purpose, const QString &title, QVariantMap options);

    QString m_requestPath;
    QString m_purpose;
};
