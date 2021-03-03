#pragma warning(disable: 5039)

#include "repository_nexus.h"
#include <nxmurl.h>
#include <log.h>
#include <utility.h>

#include <warnings_push.h>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QApplication>

#include <wincred.h>
#include <warnings_pop.h>

using namespace MOBase;

QString getCredentials(const QString& credName);

bool RepositoryNexus::init(IOrganizer* core)
{
  m_core = core;
  return true;
}

QString RepositoryNexus::name() const
{
  return "Nexus Provider";
}

QString RepositoryNexus::localizedName() const
{
  return tr("Nexus Provider");
}

QString RepositoryNexus::author() const
{
  return "The Mod Organizer Team";
}

QString RepositoryNexus::description() const
{
  return tr("");
}

VersionInfo RepositoryNexus::version() const
{
  return VersionInfo(1, 0, 0, VersionInfo::RELEASE_FINAL);
}

QList<PluginSetting> RepositoryNexus::settings() const
{
  return QList<PluginSetting>();
}

bool RepositoryNexus::canHandleDownload(const QString& what) const
{
  return what.startsWith("nxm://");
}

QString RepositoryNexus::downloadFilename(const QString& what) const
{
  return QUrl(what).fileName();
}

std::unique_ptr<IRepositoryDownload> RepositoryNexus::download(
  const QString& what, IDownloader* d)
{
  return std::make_unique<NexusDownload>(this, what, d);
}

QString RepositoryNexus::apiKey() const
{
  return getCredentials("ModOrganizer2_APIKEY");
}



NexusDownload::NexusDownload(RepositoryNexus* repo, const QString& what, IDownloader* d)
  : m_repo(repo), m_what(what), m_downloader(d), m_step(Steps::None)
{
}

void NexusDownload::tick()
{
  if (state() == States::Stopping) {
    // state is Stopping if stop() was called while a download is in
    // progress, check if it's finished
    if (!m_dl || m_dl->state() == IDownload::Finished) {
      setState(States::Finished);
      m_dl = {};
    }

    return;
  }


  switch (m_step)
  {
    case Steps::None:
    {
      // first time tick() is called, ask nexus for the urls
      getUrls();
      break;
    }

    case Steps::GettingUrls:
    {
      if (m_dl->state() == IDownload::Finished) {
        // urls received, parse them and download from the best one
        download();
      }

      break;
    }

    case Steps::Downloading:
    {
      if (m_dl->state() == IDownload::Finished) {
        // file received, done
        setState(States::Finished);
      }

      break;
    }
  }
}

IDownload::Info NexusDownload::makeInfo(const QString& outputFile) const
{
  IDownload::Info info;

  // will be empty when getting the urls, they'll stay in the buffer
  info.outputFile = outputFile;

  // nexus requires these
  info.headers.push_back({"APIKEY", m_repo->apiKey()});
  info.headers.push_back({"Content-type", "application/json"});
  info.headers.push_back({"Protocol-Version", "1.0.0"});
  info.headers.push_back({"Application-Name", "MO2"});
  info.headers.push_back({"Application-Version", QApplication::applicationVersion().toUtf8()});
  info.userAgent = m_repo->userAgent();

  return info;
}

void NexusDownload::getUrls()
{
  // parse the nxm url
  NXMUrl nxm(m_what);

  // build the url
  const QString urlTemplate =
    "https://api.nexusmods.com/v1/games/%2/mods/%3/files/%4/"
    "download_link?key=%5&expires=%6";

  const QString url = urlTemplate
    .arg(nxm.game())
    .arg(nxm.modId())
    .arg(nxm.fileId())
    .arg(nxm.key())
    .arg(nxm.expires());

  // send it
  m_dl = m_downloader->add(url, makeInfo());

  setState(States::Downloading);
  m_step = Steps::GettingUrls;
}

void NexusDownload::download()
{
  const auto data = m_dl->buffer();

  if (data.isEmpty()) {
    if (m_dl->httpCode() == 200) {
      log::error("empty response");
    } else {
      log::error("request failed {}", m_dl->httpCode());
    }

    setState(States::Errored, 1);
    return;
  }

  // parse the json response
  const auto s = parseUrls(m_dl->httpCode(), data);

  if (s.second.isEmpty()) {
    // failed
    setState(States::Errored, 1);
    return;
  }

  log::debug("downloading from '{}' at '{}'", s.first, s.second.toString());

  // download the actual file
  m_dl = m_downloader->add(s.second, makeInfo(s.second.fileName()));
  m_step = Steps::Downloading;
}

void NexusDownload::stop()
{
  if (m_dl) {
    // cancel the download
    setState(States::Stopping);
    m_dl->stop();
  } else {
    // already idle
    setState(States::Finished);
  }
}

double NexusDownload::progress() const
{
  if (m_dl) {
    return m_dl->stats().progress;
  } else {
    return -1;
  }
}

QPair<QString, QUrl> NexusDownload::parseUrls(
  int httpCode, const QByteArray& data) const
{
  QJsonDocument d = QJsonDocument::fromJson(data);

  if (d.isNull()) {
    log::error("bad json");
    return {};
  }


  if (httpCode != 200) {
    // transfer was not successful
    QString error = QString("http code %1").arg(httpCode);

    // nexus can send a json response with a 'message' string, check that first
    if (d.isObject()) {
      const auto m = d.object().value("message");
      if (m.isString()) {
        error += ", " + m.toString();
      }
    }

    log::error("download error, {}", error);
    return {};
  }


  if (!d.isArray()) {
    log::error("json document is not an array");
    return {};
  }


  const auto serverVal = d.array()[0];

  if (!serverVal.isObject()) {
    log::error("json server is not an object");
    return {};
  }


  const auto server = serverVal.toObject();
  const auto nameVal = server["name"];
  const auto shortNameVal = server["short_name"];
  const auto uriVal = server["URI"];

  if (!nameVal.isString()) {
    log::error("json server name is not a string");
    return {};
  }

  if (!shortNameVal.isString()) {
    log::error("json server short_name is not a string");
    return {};
  }

  if (!uriVal.isString()) {
    log::error("json server uri is not a string");
    return {};
  }


  const auto name = nameVal.toString();
  const auto shortName = shortNameVal.toString();
  const auto uri = uriVal.toString();

  return {name, uri};
}



struct CredentialFreer
{
  void operator()(CREDENTIALW* c)
  {
    if (c) {
      CredFree(c);
    }
  }
};

using CredentialPtr = std::unique_ptr<CREDENTIALW, CredentialFreer>;


QString getCredentials(const QString& credName)
{
  CREDENTIALW* rawCreds = nullptr;

  const auto ret = CredReadW(
    credName.toStdWString().c_str(), CRED_TYPE_GENERIC, 0, &rawCreds);

  CredentialPtr creds(rawCreds);

  if (!ret) {
    const auto e = GetLastError();

    if (e != ERROR_NOT_FOUND) {
      log::error(
        "failed to retrieve windows credential {}: {}",
        credName, formatSystemMessage(e));
    }

    return {};
  }

  QString value;
  if (creds->CredentialBlob) {
    value = QString::fromWCharArray(
      reinterpret_cast<const wchar_t*>(creds->CredentialBlob),
      static_cast<int>(creds->CredentialBlobSize / sizeof(wchar_t)));
  }

  return value;
}

QString RepositoryNexus::userAgent() const
{
  QStringList comments;
  QString os;

  if (QSysInfo::productType() == "windows") {
    comments << ((QSysInfo::kernelType() == "winnt") ? "Windows_NT " : "Windows ") + QSysInfo::kernelVersion();
  } else {
    comments
      << QSysInfo::kernelType().left(1).toUpper() + QSysInfo::kernelType().mid(1)
      << QSysInfo::productType().left(1).toUpper() + QSysInfo::kernelType().mid(1) + " " + QSysInfo::productVersion();
  }

  comments << ((QSysInfo::buildCpuArchitecture() == "x86_64") ? "x64" : "x86");

  return QString("Mod Organizer/%1 (%2) Qt/%3")
    .arg(m_core->appVersion().displayString(3))
    .arg(comments.join("; "))
    .arg(qVersion());
}
