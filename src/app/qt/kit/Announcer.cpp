#include "Announcer.hpp"

#include <QAccessible>

Announcer::Announcer(QObject* parent) : QObject(parent) {}

void Announcer::announce(QObject* target, const QString& message, bool assertive) {
    // Whether anyone is listening is Qt's to decide: updateAccessibility
    // drops the event when no platform reader (or test handler) is active.
    if (target == nullptr || message.isEmpty()) {
        return;
    }
    QAccessibleAnnouncementEvent event(target, message);
    event.setPoliteness(assertive ? QAccessible::AnnouncementPoliteness::Assertive
                                  : QAccessible::AnnouncementPoliteness::Polite);
    QAccessible::updateAccessibility(&event);
}
