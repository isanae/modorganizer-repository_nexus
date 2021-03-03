#ifndef MODORGANIZER_PLUGIN_REPOSITORY_NEXUS_INCLUDED
#define MODORGANIZER_PLUGIN_REPOSITORY_NEXUS_INCLUDED

#include "warnings_push.h"
#include <ipluginrepository.h>
#include <QString>
#include <QObject>
#include <QList>
#include "warnings_pop.h"

class RepositoryNexus;

class NexusDownload : public MOBase::IRepositoryDownload
{
public:
  enum class Steps
  {
    None = 0,
    GettingUrls,
    Downloading
  };

  NexusDownload(RepositoryNexus* repo, const QString& what, MOBase::IDownloader* d);

  void tick() override;
  void stop() override;
  double progress() const override;

private:
  RepositoryNexus* m_repo;
  const QString m_what;
  MOBase::IDownloader* m_downloader;
  Steps m_step;
  std::shared_ptr<MOBase::IDownload> m_dl;

  MOBase::IDownload::Info makeInfo(const QString& outputFile={}) const;
  QPair<QString, QUrl> parseUrls(int httpCode, const QByteArray& data) const;

  void getUrls();
  void download();
};


class RepositoryNexus : public MOBase::IPluginRepository
{
  Q_OBJECT
  Q_INTERFACES(MOBase::IPlugin MOBase::IPluginRepository)
  Q_PLUGIN_METADATA(IID "org.modorganizer.RepositoryNexus" FILE "repository_nexus.json")

public:
  enum States
  {
    RealFile = 1
  };

  bool init(MOBase::IOrganizer* core) override;
  QString name() const override;
  QString localizedName() const override;
  QString author() const override;
  QString description() const override;
  MOBase::VersionInfo version() const override;
  QList<MOBase::PluginSetting> settings() const override;

  bool canHandleDownload(const QString& what) const override;
  QString downloadFilename(const QString& what) const override;

  std::unique_ptr<MOBase::IRepositoryDownload> download(
    const QString& what, MOBase::IDownloader* d) override;

  QString apiKey() const;
  QString userAgent() const;

private:
  MOBase::IOrganizer* m_core;
};

#endif //MODORGANIZER_PLUGIN_REPOSITORY_NEXUS_INCLUDED
