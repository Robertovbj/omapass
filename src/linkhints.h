#pragma once

#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QVariantList>

class LinkHints : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList badges READ badges NOTIFY changed)
    Q_PROPERTY(QString prefix READ prefix NOTIFY changed)

public:
    using QObject::QObject;
    Q_INVOKABLE void watch(QQuickItem *overlay);
    Q_INVOKABLE void cancel();
    QVariantList badges() const { return m_badges; }
    QString prefix() const { return m_prefix; }

signals:
    void changed();

protected:
    bool eventFilter(QObject *object, QEvent *event) override;

private:
    struct Target {
        QPointer<QQuickItem> item;
        QPointF center;
        QPointF labelAt;
    };
    void collect(QQuickItem *item, QRectF clip);
    void start();
    void type(const QString &letter);
    QPointer<QQuickWindow> m_window;
    QPointer<QQuickItem> m_overlay;
    QList<Target> m_targets;
    QVariantList m_badges;
    QString m_prefix;
};
