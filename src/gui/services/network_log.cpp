#include "gui/services/network_log.h"

#include <QHash>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QString>
#include <QUrl>

namespace pokedex {

// Info-and-above so the outbound-call lines print by default (a bare
// Q_LOGGING_CATEGORY would leave qCInfo filtered on some Qt builds).
Q_LOGGING_CATEGORY(lcNet, "pokedex.net", QtInfoMsg)

QNetworkReply* loggedGet(QNetworkAccessManager* nam, const QNetworkRequest& request) {
    const QUrl url = request.url();
    const QString host = url.host();

    // Session-cumulative count per host. GUI-thread only, so a plain static is
    // safe — no mutex. It answers "are we hammering some API": the running total
    // per host is right there on every line.
    static QHash<QString, int> callsByHost;
    const int count = ++callsByHost[host];

    qCInfo(lcNet).noquote() << "GET" << url.toString(QUrl::PrettyDecoded)
                            << QStringLiteral("[%1: %2 calls this session]").arg(host).arg(count);

    return nam->get(request);
}

}  // namespace pokedex
