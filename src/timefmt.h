#pragma once
#include <QString>

namespace TimeFmt {

// Formats a millisecond offset as a clock string, e.g. "1:02:03.456" (or
// "02:03.456" when the time is under an hour and showHours stays implicit).
// The hour segment is only rendered when non-zero.
inline QString formatClock(qint64 ms, bool showMs = false) {
    const qint64 totalSeconds = ms / 1000;
    const int hours   = int(totalSeconds / 3600);
    const int minutes = int((totalSeconds % 3600) / 60);
    const int seconds = int(totalSeconds % 60);
    const int millis  = int(ms % 1000);
    if (showMs) {
        if (hours > 0)
            return QString("%1:%2:%3.%4").arg(hours, 2, 10, QChar('0'))
                .arg(minutes, 2, 10, QChar('0')).arg(seconds, 2, 10, QChar('0'))
                .arg(millis, 3, 10, QChar('0'));
        return QString("%1:%2.%3").arg(minutes, 2, 10, QChar('0'))
            .arg(seconds, 2, 10, QChar('0')).arg(millis, 3, 10, QChar('0'));
    }
    if (hours > 0)
        return QString("%1:%2:%3").arg(hours, 2, 10, QChar('0'))
            .arg(minutes, 2, 10, QChar('0')).arg(seconds, 2, 10, QChar('0'));
    return QString("%1:%2").arg(minutes, 2, 10, QChar('0'))
        .arg(seconds, 2, 10, QChar('0'));
}

} // namespace TimeFmt
