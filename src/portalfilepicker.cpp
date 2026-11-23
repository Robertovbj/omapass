#include "portalfilepicker.h"

#include "i18n.h"

#include <QUrl>
#include <QUuid>

#ifdef OMAPASS_HAS_DBUS
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>

namespace {

const char *Service = "org.freedesktop.portal.Desktop";
const char *Path = "/org/freedesktop/portal/desktop";

// The portal's filters, a(sa(us)): a name and its rules, each a kind (0 a
// glob, 1 a MIME type) and the pattern.
struct FilterRule {
    uint kind = 0;
    QString pattern;
};
struct Filter {
    QString name;
    QList<FilterRule> rules;
};

QDBusArgument &operator<<(QDBusArgument &argument, const FilterRule &rule) {
    argument.beginStructure();
    argument << rule.kind << rule.pattern;
    argument.endStructure();
    return argument;
}
const QDBusArgument &operator>>(const QDBusArgument &argument, FilterRule &rule) {
    argument.beginStructure();
    argument >> rule.kind >> rule.pattern;
    argument.endStructure();
    return argument;
}
QDBusArgument &operator<<(QDBusArgument &argument, const Filter &filter) {
    argument.beginStructure();
    argument << filter.name << filter.rules;
    argument.endStructure();
    return argument;
}
const QDBusArgument &operator>>(const QDBusArgument &argument, Filter &filter) {
    argument.beginStructure();
    argument >> filter.name >> filter.rules;
    argument.endStructure();
    return argument;
}

}

Q_DECLARE_METATYPE(FilterRule)
Q_DECLARE_METATYPE(Filter)
#endif

PortalFilePicker::PortalFilePicker(QObject *parent) : QObject(parent) {
#ifdef OMAPASS_HAS_DBUS
    qDBusRegisterMetaType<FilterRule>();
    qDBusRegisterMetaType<QList<FilterRule>>();
    qDBusRegisterMetaType<Filter>();
    qDBusRegisterMetaType<QList<Filter>>();
#endif
}

bool PortalFilePicker::available() const {
#ifdef OMAPASS_HAS_DBUS
    return QDBusConnection::sessionBus().isConnected();
#else
    return false;
#endif
}

void PortalFilePicker::openFile(const QString &purpose, const QString &title,
                                const QVariantList &filters) {
    QVariantMap options{{QStringLiteral("multiple"), false}};
#ifdef OMAPASS_HAS_DBUS
    QList<Filter> portalFilters;
    for (const QVariant &value : filters) {
        const QVariantMap map = value.toMap();
        Filter filter;
        filter.name = map.value(QStringLiteral("name")).toString();
        for (const QString &pattern : map.value(QStringLiteral("patterns")).toStringList())
            filter.rules.append({0, pattern});
        if (!filter.rules.isEmpty())
            portalFilters.append(filter);
    }
    if (!portalFilters.isEmpty())
        options.insert(QStringLiteral("filters"), QVariant::fromValue(portalFilters));
#else
    Q_UNUSED(filters)
#endif
    request(purpose, title, options);
}

void PortalFilePicker::openFolder(const QString &purpose, const QString &title) {
    request(purpose, title, {{QStringLiteral("directory"), true}});
}

void PortalFilePicker::request(const QString &purpose, const QString &title, QVariantMap options) {
#ifdef OMAPASS_HAS_DBUS
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        emit failed(purpose, I18n::t(QStringLiteral("picker.no_bus")));
        return;
    }

    // Subscribe to the request's Response before calling, at the path the
    // portal will use, so a fast answer can't arrive before we listen.
    const QString token = QStringLiteral("omapass") + QUuid::createUuid().toString(QUuid::Id128);
    QString sender = bus.baseService().mid(1);
    sender.replace(QLatin1Char('.'), QLatin1Char('_'));
    if (!m_requestPath.isEmpty())
        bus.disconnect(QString::fromLatin1(Service), m_requestPath,
                       QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Response"), this,
                       SLOT(onResponse(uint, QVariantMap)));
    m_purpose = purpose;
    m_requestPath = QStringLiteral("%1/request/%2/%3").arg(QString::fromLatin1(Path), sender, token);
    bus.connect(QString::fromLatin1(Service), m_requestPath, QStringLiteral("org.freedesktop.portal.Request"),
                QStringLiteral("Response"), this, SLOT(onResponse(uint, QVariantMap)));

    options.insert(QStringLiteral("handle_token"), token);
    QDBusMessage call = QDBusMessage::createMethodCall(QString::fromLatin1(Service), QString::fromLatin1(Path),
                                                       QStringLiteral("org.freedesktop.portal.FileChooser"),
                                                       QStringLiteral("OpenFile"));
    call << QString() << title << options;
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, purpose](QDBusPendingCallWatcher *watcher) {
        QDBusPendingReply<QDBusObjectPath> reply = *watcher;
        watcher->deleteLater();
        if (reply.isError())
            emit failed(purpose, I18n::t(QStringLiteral("picker.failed"), {{QStringLiteral("error"),
                                                                            reply.error().message()}}));
    });
#else
    Q_UNUSED(title)
    Q_UNUSED(options)
    emit failed(purpose, I18n::t(QStringLiteral("picker.no_bus")));
#endif
}

void PortalFilePicker::onResponse(uint response, const QVariantMap &results) {
    // 0 is a pick, 1 the user cancelling, anything else a portal error.
    if (response == 1) {
        emit picked(m_purpose, QString());
        return;
    }
    const QStringList uris = results.value(QStringLiteral("uris")).toStringList();
    if (response != 0 || uris.isEmpty()) {
        emit failed(m_purpose, I18n::t(QStringLiteral("picker.no_file")));
        return;
    }
    emit picked(m_purpose, QUrl(uris.first()).toLocalFile());
}
