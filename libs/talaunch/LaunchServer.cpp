#include "LaunchServer.h"

#include "dplayreg/DPlayReg.h"
#include "rwe/hpi/HpiArchive.h"
#include "tafencrypt/ServerEncrypt.h"
#include "taflib/HexDump.h"

#include <sstream>
#include <fstream>

#include <QtCore/qcryptographichash.h>
#include <QtCore/qdir.h>
#include <QtCore/qfile.h>
#include <QtCore/qfileinfo.h>
#include <QtCore/qjsonarray.h>
#include <QtCore/qjsondocument.h>
#include <QtCore/qjsonobject.h>
#include <QtCore/qregularexpression.h>
#include <QtCore/qsettings.h>
#include <QtCore/qthread.h>
#include <QtCore/qurlquery.h>

#include <QtNetwork/qnetworkreply.h>

using namespace talaunch;

// 200ms poll: needs to be well under GameMonitor2's draw-deferral window (~3s) so
// outcome corrections from shared mem land before the deferred latch fires.
static const int TICK_RATE_MILLISEC = 200;

LaunchServer::LaunchServer(QHostAddress addr, quint16 port, int keepAliveTimeout):
    m_keepAliveTimeout(keepAliveTimeout * 1000 / TICK_RATE_MILLISEC),
    // m_shutdownCounter is in ticks; convert from the seconds-valued keepAliveTimeout
    // (original code only matched by accident when TICK_RATE_MILLISEC was 1000).
    m_shutdownCounter(keepAliveTimeout * 1000 / TICK_RATE_MILLISEC),
    m_loggedAConnection(false)
{
    qInfo() << "[LaunchServer::LaunchServer] starting server on addr" << addr << "port" << port;
    m_tcpServer.listen(addr, port);
    if (!m_tcpServer.isListening())
    {
        qWarning() << "[LaunchServer::LaunchServer] launch server is not listening!";
    }
    QObject::connect(&m_tcpServer, &QTcpServer::newConnection, this, &LaunchServer::onNewConnection);
    QObject::startTimer(TICK_RATE_MILLISEC);
}

void LaunchServer::onNewConnection()
{
    try
    {
        QTcpSocket* socket = m_tcpServer.nextPendingConnection();
        if (!m_loggedAConnection)
        {
            qInfo() << "[LaunchServer::onNewConnection] accepted connection from" << socket->peerAddress() << "port" << socket->peerPort();
        }
        QObject::connect(socket, &QTcpSocket::readyRead, this, &LaunchServer::onReadyReadTcp);
        QObject::connect(socket, &QTcpSocket::stateChanged, this, &LaunchServer::onSocketStateChanged);
        m_tcpSockets.push_back(socket);
    }
    catch (const std::exception & e)
    {
        qWarning() << "[LaunchServer::onNewConnection] exception:" << e.what();
    }
    catch (...)
    {
        qWarning() << "[LaunchServer::onNewConnection] general exception:";
    }
}

void LaunchServer::onSocketStateChanged(QAbstractSocket::SocketState socketState)
{
    try
    {
        if (socketState == QAbstractSocket::UnconnectedState)
        {
            QTcpSocket* sender = static_cast<QTcpSocket*>(QObject::sender());
            if (!m_loggedAConnection)
            {
                qInfo() << "[LaunchServer::onSocketStateChanged] peer disconnected" << sender->peerAddress() << "port" << sender->peerPort();
                m_loggedAConnection = true;
            }
            m_tcpSockets.removeOne(sender);
            sender->deleteLater();
        }
    }
    catch (const std::exception & e)
    {
        qWarning() << "[LaunchServer::onSocketStateChanged] exception:" << e.what();
    }
    catch (...)
    {
        qWarning() << "[LaunchServer::onSocketStateChanged] general exception:";
    }
}

void LaunchServer::onReadyReadTcp()
{
    try
    {
        QTcpSocket* sender = static_cast<QTcpSocket*>(QObject::sender());
        QByteArray datas = sender->readAll();
        QStringList args = QString::fromUtf8(datas.data(), datas.size()).replace(QRegularExpression("[\n\r]"), "").trimmed().split(' ');

        if (args.size() == 0)
        {
            return;
        }
        else if (args[0] == "/keepalive")
        {
            if (!m_loggedAConnection)
            {
                qInfo() << "[LaunchServer::onReadyReadTcp] received keepalive message" << sender->peerAddress() << "port" << sender->peerPort();
            }
            m_shutdownCounter = m_keepAliveTimeout;
            return;
        }
        else if (args.size() >= 1 && args[0] == "/failversion")
        {
            QString message = args.mid(1).join(" ");
            m_jdPlay.reset();
            notifyClients("FAIL");
            closeTAFGameState();
            emit gameFileVersionMismatch(message);
        }
        else if (args.size() >= 5 && args[0] == "/host")
        {
            args.push_back(""); // default endpoint
            args.push_back(""); // default token
            launchGame(args[1], args[2], args[3], args[4], args[5], args[6], true, false);
        }
        else if (args.size() >= 5 && args[0] == "/join")
        {
            args.push_back(""); // default endpoint
            args.push_back(""); // default token
            launchGame(args[1], args[2], args[3], args[4], args[5], args[6], false, false);
        }
        else if (args.size() >= 5 && args[0] == "/searchjoin")
        {
            launchGame(args[1], args[2], args[3], args[4], "", "", false, true);
        }
    }
    catch (const std::exception & e)
    {
        qWarning() << "[LaunchServer::onReadyReadTcp] exception:" << e.what();
    }
    catch (...)
    {
        qWarning() << "[LaunchServer::onReadyReadTcp] general exception:";
    }
}

static bool TrueLog(const char* s)
{
    qInfo() << s;
    return true;
}

void LaunchServer::launchGame(QString _gameId, QString _guid, QString _player, QString _ipaddr,
    QString submitHashesEndPoint, QString submitHashesToken, bool asHost, bool doSearch)
{
    if (m_jdPlay)
    {
        return;
    }

    bool okGameId;
    int gameId = _gameId.toInt(&okGameId);
    std::string guid = _guid.toStdString();
    std::string player = _player.toStdString();
    std::string ipaddr = _ipaddr.toStdString();

    if (asHost)
    {
        qInfo() << "[LaunchServer::launchGame] host" << guid.c_str() << player.c_str() << ipaddr.c_str();
    }
    else if (doSearch)
    {
        qInfo() << "[LaunchServer::launchGame] searchjoin" << guid.c_str() << player.c_str() << ipaddr.c_str();
    }
    else
    {
        qInfo() << "[LaunchServer::launchGame] join" << guid.c_str() << player.c_str() << ipaddr.c_str();
    }

    m_jdPlay.reset(new jdplay::JDPlay(player.c_str(), 0, NULL)); // "c:\\temp\\jdplay_launch_server.log"));
    if (TrueLog("Initialising JDPlay ...") && !m_jdPlay->initialize(guid.c_str(), ipaddr.c_str(), asHost, 10))
    {
        qWarning() << "[LaunchServer::launchGame] jdplay failed to initialise!" << m_jdPlay->getLastError().c_str();
        qInfo() << "[LaunchServer::launchGame] jdplay log:\n" << m_jdPlay->getLogString().c_str();
        m_jdPlay.reset();
        notifyClients("FAIL");
        closeTAFGameState();
        emit gameFailedToLaunch(_guid);
        return;
    }
    else if (!asHost && doSearch && !(
        TrueLog("Searching ...") && m_jdPlay->searchOnce() || 
        TrueLog("Searching ...") && m_jdPlay->searchOnce() ||
        TrueLog("Searching ...") && m_jdPlay->searchOnce()))
    {
        qWarning() << "[LaunchServer::launchGame] jdplay failed to find a game!" << m_jdPlay->getLastError().c_str();
        qInfo() << "[LaunchServer::launchGame] jdplay log:\n" << m_jdPlay->getLogString().c_str();
        m_jdPlay.reset();
        notifyClients("FAIL");
        closeTAFGameState();
        emit gameFailedToLaunch(_guid);
        return;
    }
    else if (TrueLog("Launching game ...") && !m_jdPlay->launch(true))
    {
        qWarning() << "[LaunchServer::launchGame] jdplay failed to launch!" << m_jdPlay->getLastError().c_str();
        qInfo() << "[LaunchServer::launchGame] jdplay log:\n" << m_jdPlay->getLogString().c_str();
        m_jdPlay.reset();
        notifyClients("FAIL");
        closeTAFGameState();
        emit gameFailedToLaunch(_guid);
        return;
    }
    else {
        qInfo() << "[LaunchServer::launchGame] success";
        qInfo() << "[LaunchServer::launchGame] jdplay log:\n" << m_jdPlay->getLogString().c_str();
        m_joinIsDisabled = false;
        notifyClients("RUNNING");
        openTAFGameState();

        if (okGameId && !submitHashesEndPoint.isEmpty() && !submitHashesToken.isEmpty())
        {
            auto countDown = std::make_shared<int>(60);
            m_submitGameFileHashes = [this, countDown, gameId, _guid, submitHashesEndPoint, submitHashesToken]() {
                --(*countDown);
                if (*countDown == 0)
                {
                    QString gameFileHashesJson = getGameFileHashes(gameId, _guid);
                    this->submitGameFileHashes(gameId, gameId, gameFileHashesJson, submitHashesEndPoint, submitHashesToken);
                }
                return (*countDown <= 0);
            };
        }
    }
}

void LaunchServer::timerEvent(QTimerEvent* event)
{
    try
    {
        DWORD exitCode;
        if (m_jdPlay)
        {
            if (!m_jdPlay->pollStillActive(exitCode))
            {
                qInfo() << QString("[LaunchServer::timerEvent] process exited with code 0x%1 (%2)").arg(exitCode, 8, 16, QChar('0')).arg(qint32(exitCode), 0, 10);
                if (exitCode == 0)
                {
                    notifyClients("IDLE");
                    closeTAFGameState();
                }
                else
                {
                    notifyClients(QString("FAIL %1").arg(exitCode, 0, 16, QChar('0')));
                    closeTAFGameState();
                    emit gameExitedWithError(exitCode);
                }
                m_jdPlay.reset();
                m_submitGameFileHashes = nullptr;
            }
            else if (m_jdPlay->isHost() && !m_joinIsDisabled)
            {
                DPSESSIONDESC2& desc = m_jdPlay->enumSessions();
                std::string jdLog = m_jdPlay->getLogString();
                if (jdLog.size() > 0)
                {
                    qInfo() << "[LaunchServer::timerEvent] jdplay->enumSessions:\n" << jdLog.c_str();
                }
                if (desc.dwFlags & DPSESSION_JOINDISABLED)
                {
                    m_joinIsDisabled = true;
                    notifyClients("LAUNCHED");
                }
            }

            // is it time to submit game file hashes yet?
            // Guard the callback: a throw here (e.g. an unreadable game file) must not abort
            // the rest of timerEvent, and must still disarm the one-shot so we don't spin on it.
            if (m_submitGameFileHashes)
            {
                bool done = true;
                try
                {
                    done = m_submitGameFileHashes();
                }
                catch (const std::exception& e)
                {
                    qWarning() << "[LaunchServer::timerEvent] submitGameFileHashes failed:" << e.what();
                }
                catch (...)
                {
                    qWarning() << "[LaunchServer::timerEvent] submitGameFileHashes failed: unknown error";
                }
                if (done)
                {
                    m_submitGameFileHashes = nullptr;
                }
            }

            pollTAFGameState();
        }

        if (!m_jdPlay || exitCode != STILL_ACTIVE)
        {
            --m_shutdownCounter;
        }

        if (m_shutdownCounter <= 0)
        {
            qInfo() << "[LaunchServer::timerEvent] shutdown counter expired.  terminating";
            emit quit();
        }
    }
    catch (const std::exception & e)
    {
        qWarning() << "[LaunchServer::timerEvent] exception:" << e.what();
    }
    catch (...)
    {
        qWarning() << "[LaunchServer::timerEvent] general exception:";
    }
}

void LaunchServer::openTAFGameState()
{
    m_tafGameStateMap = OpenFileMapping(FILE_MAP_READ, FALSE, TAFGAMESTATE_SHMEM_NAME);
    if (m_tafGameStateMap) {
        m_tafGameStateView = MapViewOfFile(m_tafGameStateMap, FILE_MAP_READ, 0, 0, sizeof(TAFGameState));
    }
    memset(&m_tafGameStatePrev, 0, sizeof(m_tafGameStatePrev));
}

void LaunchServer::closeTAFGameState()
{
    if (m_tafGameStateView) { UnmapViewOfFile(m_tafGameStateView); m_tafGameStateView = nullptr; }
    if (m_tafGameStateMap)  { CloseHandle(m_tafGameStateMap);       m_tafGameStateMap  = NULL;   }
    memset(&m_tafGameStatePrev, 0, sizeof(m_tafGameStatePrev));
    m_lastPlayerStatusMsg.clear();
}

void LaunchServer::pollTAFGameState()
{
    if (!m_tafGameStateMap)
    {
        openTAFGameState();
    }
    if (!m_tafGameStateView)
    {
        return;
    }
    const TAFGameState* shm = static_cast<const TAFGameState*>(m_tafGameStateView);

    // Seqlock read. Retry bound guards against the writer dying mid-write and leaving
    // sequenceNumber permanently odd; we'll just try again on the next tick.
    TAFGameState snapshot;
    uint32_t seq1;
    int retries = 0;
    while (true) {
        if (++retries > 100)
        {
            return;
        }
        seq1 = shm->sequenceNumber;
        if (seq1 == 0)
        {
            return;
        }
        if (seq1 & 1)
        {
            continue;
        }
        MemoryBarrier();
        snapshot = *shm;
        MemoryBarrier();
        if (shm->sequenceNumber == seq1)
        {
            break;
        }
    }

    if (snapshot.magic != TAFGAMESTATE_MAGIC)
    {
        return;
    }
    if (snapshot.sequenceNumber == m_tafGameStatePrev.sequenceNumber)
    {
        return;
    }

    m_tafGameStatePrev = snapshot;

    // Token format: "f0,...,f9:active:unitCount:team:propertyMask:dplayId" (10 per snapshot).
    // structuralTokens is a parallel list with unitCount collapsed to 0/1 — used only to
    // dedup logging on heartbeats and unit-count drift; the full message still ships.
    QStringList tokens;
    QStringList structuralTokens;
    tokens << "PLAYER_STATUS";
    structuralTokens << "PLAYER_STATUS";
    for (int i = 0; i < 10; i++) {
        QStringList flags;
        for (int j = 0; j < 10; j++)
            flags << QString::number(snapshot.playerAllyFlags[i][j]);
        const QString flagJoin = flags.join(',');
        tokens << (flagJoin
                   + ':' + QString::number(snapshot.playerActive[i])
                   + ':' + QString::number(snapshot.playerUnitsNumber[i])
                   + ':' + QString::number(snapshot.playerAllyTeam[i])
                   + ':' + QString::number(snapshot.playerPropertyMask[i])
                   + ':' + QString::number(snapshot.playerDirectPlayId[i]));
        structuralTokens << (flagJoin
                   + ':' + QString::number(snapshot.playerActive[i])
                   + ':' + QString::number(snapshot.playerUnitsNumber[i] > 0 ? 1 : 0)
                   + ':' + QString::number(snapshot.playerAllyTeam[i])
                   + ':' + QString::number(snapshot.playerPropertyMask[i])
                   + ':' + QString::number(snapshot.playerDirectPlayId[i]));
    }
    QString msg = tokens.join(" ");
    QString structuralKey = structuralTokens.join(" ");
    if (structuralKey != m_lastPlayerStatusMsg) {
        qInfo() << "[LaunchServer::notifyClients]" << msg;
        m_lastPlayerStatusMsg = structuralKey;
    }
    notifyClients(msg);
}

void LaunchServer::notifyClients(QString _msg)
{
    std::string msg = _msg.toStdString();
    for (QTcpSocket* socket : m_tcpSockets)
    {
        socket->write(msg.c_str(), 1+msg.size());
        socket->flush();
    }
}

static QString sha256OfFile(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) {
        return QString();
    }

    return QString(hash.result().toHex());
}

static QString generateGameFileHashes(int gameId, const QMap<QString, QString>& dplayApp) {
    QJsonObject root;
    root["gameId"] = gameId;

    QString dirPath = dplayApp.value("CurrentDirectory");
    QString exeFile = dplayApp.value("File");
    QString guid = dplayApp.value("Guid");

    if (dirPath.isEmpty()) {
        root["status"] = "error: missing CurrentDirectory";
        return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
    }
    if (exeFile.isEmpty()) {
        root["status"] = "error: missing File";
        return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
    }
    if (guid.isEmpty()) {
        root["status"] = "error: missing Guid";
        return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
    }

    root["guid"] = guid;

    QDir dir(dirPath);
    if (!dir.exists()) {
        root["status"] = "error: directory does not exist";
        return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
    }

    QJsonObject hashes;

    // 1. Hash main executable
    QString exePath = dir.absoluteFilePath(exeFile);
    QFileInfo exeInfo(exePath);
    if (!exeInfo.exists() || !exeInfo.isFile()) {
        root["status"] = "error: executable not found";
        return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
    }

    QString exeHash = sha256OfFile(exePath);
    if (exeHash.isEmpty()) {
        root["status"] = "error: failed to hash executable";
        return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
    }
    hashes.insert(exeInfo.fileName(), exeHash);

    // 2. Hash DLLs, GP3s
    auto addHashes = [&](const QStringList& patterns) {
        for (const QString& pattern : patterns) {
            QStringList files = dir.entryList(QStringList() << pattern, QDir::Files);
            for (const QString& f : files) {
                QString fullPath = dir.absoluteFilePath(f);
                QString hash = sha256OfFile(fullPath);
                if (!hash.isEmpty()) {
                    hashes.insert(f, hash);
                }
            }
        }
    };

    addHashes({ "*.dll", "*.gp3" });

    // Handle *.ufo files that contain a units* directory.
    // Some installs ship empty/non-HPI .ufo placeholders (e.g. 0-byte stubs); parsing those
    // throws HpiException. Skip any unreadable .ufo individually rather than aborting the whole
    // hash set (and therefore the launch-code submission) for the entire game.
    QStringList ufoFiles = dir.entryList(QStringList() << "*.ufo", QDir::Files);
    for (const QString& f : ufoFiles) {
        QString fullPath = dir.absoluteFilePath(f);

        try {
            std::ifstream file(fullPath.toStdString(), std::ios::binary);
            rwe::HpiArchive archive(&file);
            auto matchingDirs = archive.findRootDirectoriesWithPrefix("units");

            if (!matchingDirs.empty()) {
                QString hash = sha256OfFile(fullPath);
                if (!hash.isEmpty()) {
                    hashes.insert(f, hash);
                }
            }
        }
        catch (const std::exception& e) {
            qWarning() << "[generateGameFileHashes] skipping unreadable .ufo" << f << ":" << e.what();
        }
        catch (...) {
            qWarning() << "[generateGameFileHashes] skipping unreadable .ufo" << f << ": unknown error";
        }
    }

    // 3. List subdirectories (non-recursive)
    QJsonArray subdirs;
    QFileInfoList entries = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo& entry : entries) {
        subdirs.append(entry.fileName());
    }

    root["files"] = hashes;
    root["subdirectories"] = subdirs;
    root["status"] = "success";

    QJsonDocument doc(root);
    return QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
}

QString LaunchServer::getGameFileHashes(int gameId, QString guid)
{
    QMap<QString, QString> dplayApp = dplayreg::GetDplayLobbableApp(guid);
    QString json = generateGameFileHashes(gameId, dplayApp);
    return json;
}

void LaunchServer::submitGameFileHashes(int gameId, int token, QString json, QString endpoint, QString accessToken)
{
    std::string encrypted = tafencrypt::ServerEncrypt(json.toStdString());

    qDebug() << "[submitGameFileHashes] endpoint=" << endpoint;
    qDebug() << "[submitGameFileHashes] accessToken=" << accessToken.mid(0, 16);

    QUrl submitUrl(endpoint);
    QNetworkRequest postRequest(submitUrl);
    postRequest.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    postRequest.setRawHeader("Authorization", QString("Bearer %1").arg(accessToken).toUtf8());

    QUrlQuery postData;
    postData.addQueryItem("gameId", QString::number(gameId));
    postData.addQueryItem("data", QString::fromStdString(encrypted));

    QNetworkReply* postReply = m_nam.post(postRequest, postData.toString(QUrl::FullyEncoded).toUtf8());
    connect(postReply, &QNetworkReply::finished, this, [=]() {
        if (postReply->error() == QNetworkReply::NoError) {
            qDebug() << "Launch code submitted successfully gameid=" << gameId;
        }
        else {
            qWarning() << "Submit failed gameId=" << gameId << postReply->errorString();
            qDebug() << postReply->readAll();  // optional: dump server response
        }
        postReply->deleteLater();
    });
}
