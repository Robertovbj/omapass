// Target detection and home-row labels adapted from Omajim (MIT),
// Copyright (c) 2026 Higor Henrique Prata Gonçalves.
#include "linkhints.h"

#include <QCoreApplication>
#include <QKeyEvent>
#include <QMouseEvent>


namespace {
const QString alphabet = QStringLiteral("sadfjklewcmpgh");

bool textField(const QObject *item) {
    return item && (item->inherits("QQuickTextInput") || item->inherits("QQuickTextEdit"));
}

bool takesLeftClick(const QObject *object) {
    return object->property("acceptedButtons").value<Qt::MouseButtons>().testFlag(Qt::LeftButton);
}

bool clickable(const QQuickItem *item) {
    if (item->inherits("QQuickAbstractButton") || textField(item))
        return true;
    if (item->inherits("QQuickMouseArea"))
        return takesLeftClick(item) && item->property("cursorShape").value<Qt::CursorShape>() == Qt::PointingHandCursor;
    for (const auto *child : item->children()) {
        if (child->inherits("QQuickTapHandler") && child->property("enabled").toBool() && takesLeftClick(child))
            return true;
    }
    return false;
}
}

void LinkHints::watch(QQuickItem *overlay) {
    m_overlay = overlay;
    auto *window = overlay ? overlay->window() : nullptr;
    if (window == m_window)
        return;
    cancel();
    if (m_window) {
        m_window->removeEventFilter(this);
        disconnect(m_window, nullptr, this, nullptr);
    }
    m_window = window;
    if (window) {
        window->installEventFilter(this);
        connect(window, &QQuickWindow::activeFocusItemChanged, this, &LinkHints::cancel);
    }
}

void LinkHints::cancel() {
    if (m_targets.isEmpty())
        return;
    m_targets.clear();
    m_badges.clear();
    m_prefix.clear();
    emit changed();
}

void LinkHints::collect(QQuickItem *item, QRectF clip) {
    if (!item->isVisible() || !item->isEnabled() || item->opacity() <= 0 || item->inherits("QQuickOverlay"))
        return;
    const QRectF rect = item->mapRectToScene(item->boundingRect());
    const QRectF visible = clip.intersected(rect);
    if (!visible.isEmpty() && clickable(item))
        m_targets.append({item, visible.center(), visible.topLeft()});
    if (item->clip())
        clip = visible;
    if (!clip.isEmpty()) {
        for (auto *child : item->childItems())
            collect(child, clip);
    }
}

void LinkHints::start() {
    auto *root = m_window->contentItem();
    // An open popup owns the pointer. Walk only the topmost popup, even
    // when the field that opened it still has keyboard focus.
    if (m_overlay) {
        QQuickItem *popup = nullptr;
        for (auto *child : m_overlay->childItems()) {
            if (child->inherits("QQuickPopupItem") && child->isVisible() && (!popup || child->z() >= popup->z()))
                popup = child;
        }
        if (popup)
            root = popup;
    }
    collect(root, QRectF(QPointF(), m_window->size()));
    int digits = 1;
    for (auto n = m_targets.size() - 1; n >= alphabet.size(); n /= alphabet.size())
        ++digits;
    for (qsizetype i = 0; i < m_targets.size(); ++i) {
        QString label;
        auto n = i;
        for (int j = 0; j < digits; ++j, n /= alphabet.size())
            label.prepend(alphabet.at(n % alphabet.size()));
        m_badges.append(QVariantMap{{"label", label}, {"x", m_targets[i].labelAt.x()}, {"y", m_targets[i].labelAt.y()}});
    }
    emit changed();
}

void LinkHints::type(const QString &letter) {
    const QString next = m_prefix + letter;
    for (qsizetype i = 0; i < m_badges.size(); ++i) {
        const QString label = m_badges[i].toMap().value("label").toString();
        if (!label.startsWith(next))
            continue;
        if (next != label) {
            m_prefix = next;
            emit changed();
            return;
        }
        const auto target = m_targets[i];
        cancel();
        const auto *window = m_window.data();
        if (!target.item || target.item->window() != window || !target.item->isVisible() || !target.item->isEnabled())
            return;
        const QPointF at = target.center;
        for (auto *item = target.item.data(); item; item = item->parentItem()) {
            if ((item == target.item || item->clip()) && !item->contains(item->mapFromScene(at)))
                return;
        }
        if (textField(target.item)) {
            target.item->forceActiveFocus(Qt::ShortcutFocusReason);
            return;
        }
        const QPointF global = window->mapToGlobal(at);
        QMouseEvent press(QEvent::MouseButtonPress, at, at, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(m_window, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, at, at, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(m_window, &release);
        return;
    }
}

bool LinkHints::eventFilter(QObject *, QEvent *event) {
    const auto kind = event->type();
    if (kind == QEvent::Resize || kind == QEvent::WindowDeactivate || kind == QEvent::MouseButtonPress
        || kind == QEvent::Wheel || kind == QEvent::TouchBegin) {
        cancel();
        return false;
    }
    if (kind != QEvent::KeyPress && kind != QEvent::ShortcutOverride)
        return false;
    auto *key = static_cast<QKeyEvent *>(event);
    const auto *focus = m_window->activeFocusItem();
    const bool startKey = key->key() == Qt::Key_F && !key->modifiers()
        && !(textField(focus) && !focus->property("readOnly").toBool());
    if (m_targets.isEmpty() && !startKey)
        return false;
    key->accept();
    if (kind == QEvent::ShortcutOverride || key->isAutoRepeat())
        return true;
    if (m_targets.isEmpty())
        start();
    else if (key->key() == Qt::Key_Escape)
        cancel();
    else if (key->key() == Qt::Key_Backspace) {
        m_prefix.chop(1);
        emit changed();
    } else if (!key->modifiers() && key->text().size() == 1)
        type(key->text().toLower());
    return true;
}
