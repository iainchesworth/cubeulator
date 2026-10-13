#pragma once

#include <QObject>
#include <qqmlintegration.h>

// Says something to a screen reader without moving focus: "Side 3 of 6
// read", "Camera not found". The one kit function every announcement goes
// through (docs/design/accessibility.md, "Screen readers"), so a platform
// that needs a different mechanism has one place to change.
//
// Qt 6.8 adds QAccessibleAnnouncementEvent, which its UI Automation,
// NSAccessibility, AT-SPI and Android bridges turn into each platform's own
// notification. Whether each reader speaks it is an E6 question; Windows is
// checked in docs/kit.md.
class Announcer : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit Announcer(QObject* parent = nullptr);

    // `target` is the item the announcement is about (the status panel, the
    // notice): readers use it to find the window and, on some platforms, to
    // scope the message. `assertive` interrupts what is being read and is for
    // something that stops the person; everything else is polite.
    Q_INVOKABLE void announce(QObject* target, const QString& message, bool assertive = false);
};
