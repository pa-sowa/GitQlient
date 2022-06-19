#include "GitQlient.h"

#include <CreateRepoDlg.h>
#include <GitBase.h>
#include <GitConfig.h>
#include <GitQlientRepo.h>
#include <GitQlientSettings.h>
#include <GitQlientStyles.h>
#include <InitScreen.h>
#include <InitialRepoConfig.h>
#include <ProgressDlg.h>
#include <QPinnableTabWidget.h>

#include <QApplication>
#include <QCommandLineParser>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QLibrary>
#include <QMenu>
#include <QMessageBox>
#include <QPluginLoader>
#include <QProcess>
#include <QPushButton>
#include <QStackedLayout>
#include <QStandardPaths>
#include <QTabBar>
#include <QTextStream>
#include <QToolButton>

#include <IGitServerWidget.h>
#include <IJenkinsWidget.h>
#include <qtermwidget_interface.h>

#include <QLogger.h>

using namespace QLogger;

GitQlient::GitQlient(QWidget *parent)
   : QWidget(parent)
   , mStackedLayout(new QStackedLayout(this))
   , mRepos(new QPinnableTabWidget())
   , mConfigWidget(new InitScreen())
{
   QLog_Info("UI", "*******************************************");
   QLog_Info("UI", "*          GitQlient has started          *");
   QLog_Info("UI", QString("*                  %1                  *").arg(VER));
   QLog_Info("UI", "*******************************************");

   loadPlugins();

   setStyleSheet(GitQlientStyles::getStyles());

   const auto homeMenu = new QPushButton();
   const auto menu = new QMenu(homeMenu);

   homeMenu->setIcon(QIcon::fromTheme("application-menu", QIcon(":/icons/burger_menu")));
   homeMenu->setIconSize(QSize(17, 17));
   homeMenu->setToolTip("Options");
   homeMenu->setMenu(menu);
   homeMenu->setObjectName("MainMenuBtn");

   menu->installEventFilter(this);

   const auto open = menu->addAction(tr("Open repo..."));
   connect(open, &QAction::triggered, this, &GitQlient::openRepo);

   const auto clone = menu->addAction(tr("Clone repo..."));
   connect(clone, &QAction::triggered, this, &GitQlient::cloneRepo);

   const auto init = menu->addAction(tr("New repo..."));
   connect(init, &QAction::triggered, this, &GitQlient::initRepo);

   menu->addSeparator();

   GitQlientSettings settings;
   const auto recent = new QMenu("Recent repos", menu);
   const auto projects = settings.getRecentProjects();

   for (const auto &project : projects)
   {
      const auto projectName = project.mid(project.lastIndexOf("/") + 1);
      const auto action = recent->addAction(projectName);
      action->setData(project);
      connect(action, &QAction::triggered, this, [this, project]() { openRepoWithPath(project); });
   }

   menu->addMenu(recent);

   const auto mostUsed = new QMenu("Most used repos", menu);
   const auto recentProjects = settings.getMostUsedProjects();

   for (const auto &project : recentProjects)
   {
      const auto projectName = project.mid(project.lastIndexOf("/") + 1);
      const auto action = mostUsed->addAction(projectName);
      action->setData(project);
      connect(action, &QAction::triggered, this, [this, project]() { openRepoWithPath(project); });
   }

   menu->addMenu(mostUsed);

   mRepos->setObjectName("GitQlientTab");
   mRepos->setStyleSheet(GitQlientStyles::getStyles());
   mRepos->setCornerWidget(homeMenu, Qt::TopLeftCorner);
   connect(mRepos, &QTabWidget::tabCloseRequested, this, &GitQlient::closeTab);
   connect(mRepos, &QTabWidget::currentChanged, this, &GitQlient::updateWindowTitle);

   mStackedLayout->setContentsMargins(QMargins());
   mStackedLayout->addWidget(mConfigWidget);
   mStackedLayout->addWidget(mRepos);
   mStackedLayout->setCurrentIndex(0);

   mConfigWidget->onRepoOpened();

   connect(mConfigWidget, &InitScreen::signalOpenRepo, this, &GitQlient::addRepoTab);

   const auto geometry = settings.globalValue("GitQlientGeometry", saveGeometry()).toByteArray();

   if (!geometry.isNull())
      restoreGeometry(geometry);

   const auto gitBase(QSharedPointer<GitBase>::create(""));
   mGit = QSharedPointer<GitConfig>::create(gitBase);

   connect(mGit.data(), &GitConfig::signalCloningProgress, this, &GitQlient::updateProgressDialog,
           Qt::DirectConnection);
   connect(mGit.data(), &GitConfig::signalCloningFailure, this, &GitQlient::showError, Qt::DirectConnection);
}

GitQlient::~GitQlient()
{
   if (mTerminal.second)
   {
      mTerminal.second->sendText("exit\n");
      delete mTerminal.second;
   }

   QStringList pinnedRepos;
   const auto totalTabs = mRepos->count();

   for (auto i = 0; i < totalTabs; ++i)
   {
      if (mRepos->isPinned(i))
      {
         auto repoToRemove = dynamic_cast<GitQlientRepo *>(mRepos->widget(i));
         pinnedRepos.append(repoToRemove->currentDir());
      }
   }

   GitQlientSettings settings;
   settings.setGlobalValue(GitQlientSettings::PinnedRepos, pinnedRepos);
   settings.setGlobalValue("GitQlientGeometry", saveGeometry());

   QLog_Info("UI", "*            Closing GitQlient            *\n\n");
}

bool GitQlient::eventFilter(QObject *obj, QEvent *event)
{

   if (const auto menu = qobject_cast<QMenu *>(obj); menu && event->type() == QEvent::Show)
   {
      auto localPos = menu->parentWidget()->pos();
      auto pos = mapToGlobal(localPos);
      menu->show();
      pos.setY(pos.y() + menu->parentWidget()->height());
      menu->move(pos);
      return true;
   }

   return false;
}

void GitQlient::openRepo()
{

   const QString dirName(QFileDialog::getExistingDirectory(this, "Choose the directory of a Git project"));

   if (!dirName.isEmpty())
      openRepoWithPath(dirName);
}

void GitQlient::openRepoWithPath(const QString &path)
{
   QDir d(path);
   addRepoTab(d.absolutePath());
}

void GitQlient::cloneRepo()
{
   mPathToOpen = "";
   CreateRepoDlg cloneDlg(CreateRepoDlgType::CLONE, mGit);
   connect(&cloneDlg, &CreateRepoDlg::signalOpenWhenFinish, this, [this](const QString &path) { mPathToOpen = path; });

   if (cloneDlg.exec() == QDialog::Accepted)
   {
      const auto data = cloneDlg.getCloneInfo();

      mProgressDlg = new ProgressDlg(tr("Clonin g repository..."), QString(), 100, false);
      connect(mProgressDlg, &ProgressDlg::destroyed, this, [this]() { mProgressDlg = nullptr; });

      mGit->clone(data.first, data.second);

      mProgressDlg->show();
   }
}

void GitQlient::initRepo()
{
   CreateRepoDlg cloneDlg(CreateRepoDlgType::INIT, mGit);
   connect(&cloneDlg, &CreateRepoDlg::signalOpenWhenFinish, this, &GitQlient::openRepoWithPath);
   cloneDlg.exec();
}

void GitQlient::updateProgressDialog(QString stepDescription, int value)
{
   if (!mProgressDlg)
      return;

   if (value >= 0)
   {
      mProgressDlg->setValue(value);

      if (stepDescription.contains("done", Qt::CaseInsensitive) && !mPathToOpen.isEmpty())
      {
         mProgressDlg->close();
         openRepoWithPath(mPathToOpen);

         mPathToOpen = "";
      }
   }

   mProgressDlg->setLabelText(stepDescription);
   mProgressDlg->repaint();
}

void GitQlient::showError(int, QString description)
{
   if (mProgressDlg)
      mProgressDlg->deleteLater();

   QMessageBox::critical(this, tr("Error!"), description);
}

void GitQlient::setRepositories(const QStringList &repositories)
{
   QLog_Info("UI", QString("Adding {%1} repositories").arg(repositories.count()));

   for (const auto &repo : repositories)
      addRepoTab(repo);
}

bool GitQlient::setArgumentsPostInit(const QStringList &arguments)
{
   QLog_Info("UI", QString("External call with the params {%1}").arg(arguments.join(",")));

   QStringList repos;
   const auto ret = parseArguments(arguments, &repos);
   if (ret)
      setRepositories(repos);
   return ret;
}

bool GitQlient::parseArguments(const QStringList &arguments, QStringList *repos)
{
   bool ret = true;
   GitQlientSettings settings;
   auto logLevel
       = static_cast<LogLevel>(settings.globalValue("logsLevel", static_cast<int>(LogLevel::Warning)).toInt());
   bool areLogsEnabled = settings.globalValue("logsEnabled", false).toBool();

   QCommandLineParser parser;
   parser.setApplicationDescription(tr("Multi-platform Git client written with Qt"));
   parser.addPositionalArgument("repos", tr("Git repositories to open"), tr("[repos...]"));

   const QCommandLineOption helpOption = parser.addHelpOption();
   // We don't use parser.addVersionOption() because then it is handled by Qt and we want to show Git SHA also
   const QCommandLineOption versionOption(QStringList() << "v"
                                                        << "version",
                                          tr("Displays version information."));
   parser.addOption(versionOption);

   const QCommandLineOption noLogOption("no-log", tr("Disables logs."));
   parser.addOption(noLogOption);

   const QCommandLineOption logLevelOption("log-level", tr("Sets log level."), tr("level"));
   parser.addOption(logLevelOption);

   parser.process(arguments);

   *repos = parser.positionalArguments();
   if (parser.isSet(noLogOption))
      areLogsEnabled = false;

   if (areLogsEnabled)
   {
      if (parser.isSet(logLevelOption))
      {
         const auto level = static_cast<LogLevel>(parser.value(logLevelOption).toInt());
         if (level >= QLogger::LogLevel::Trace && level <= QLogger::LogLevel::Fatal)
         {
            logLevel = level;
            settings.setGlobalValue("logsLevel", static_cast<int>(level));
         }
      }

      QLoggerManager::getInstance()->overwriteLogLevel(logLevel);
   }
   else
      QLoggerManager::getInstance()->pause();

   if (parser.isSet(versionOption))
   {
      QTextStream out(stdout);
      out << QCoreApplication::applicationName() << ' ' << tr("version") << ' '
          << QCoreApplication::applicationVersion() << " (" << tr("Git SHA ") << SHA_VER << ")\n";
      ret = false;
   }
   if (parser.isSet(helpOption))
      ret = false;

   const auto manager = QLoggerManager::getInstance();
   manager->addDestination("GitKlient.log", { "UI", "Git", "Cache" }, logLevel,
                           QStandardPaths::writableLocation(QStandardPaths::CacheLocation), LogMode::OnlyFile,
                           LogFileDisplay::DateTime, LogMessageDisplay::Default, false);

   return ret;
}

void GitQlient::addRepoTab(const QString &repoPath)
{
   addNewRepoTab(repoPath, false);
}

void GitQlient::addNewRepoTab(const QString &repoPathArg, bool pinned)
{
   const auto repoPath = QFileInfo(repoPathArg).canonicalFilePath();

   if (!mCurrentRepos.contains(repoPath))
   {
      QFileInfo info(QString("%1/.git").arg(repoPath));

      if (info.isFile() || info.isDir())
      {
         const auto repoName = repoPath.contains("/") ? repoPath.split("/").last() : "";

         if (repoName.isEmpty())
         {
            QMessageBox::critical(
                this, tr("Not a repository"),
                tr("The selected folder is not a Git repository. Please make sure you open a Git repository."));
            QLog_Error("UI", "The selected folder is not a Git repository");
            return;
         }

         QSharedPointer<GitBase> git(new GitBase(repoPath));
         QSharedPointer<GitQlientSettings> settings(new GitQlientSettings(git->getGitDir()));

         conditionallyOpenPreConfigDlg(git, settings);

         const auto repo = new GitQlientRepo(git, settings);
         const auto index = pinned ? mRepos->addPinnedTab(repo, repoName) : mRepos->addTab(repo, repoName);

         connect(repo, &GitQlientRepo::signalOpenSubmodule, this, &GitQlient::addRepoTab);
         connect(repo, &GitQlientRepo::repoOpened, this, &GitQlient::onSuccessOpen);
         connect(repo, &GitQlientRepo::currentBranchChanged, this, &GitQlient::updateWindowTitle);

         repo->setRepository(repoName);

         if (!mPlugins.isEmpty() || (mPlugins.empty() && mJenkinsPluginInstance.second)
             || (mPlugins.empty() && mGitServerPluginInstance.second) || (mPlugins.empty() && mTerminal.second))
         {
            decltype(mPlugins) plugins;
            plugins = mPlugins;

            if (mJenkinsPluginInstance.second)
               plugins[mJenkinsPluginInstance.first] = mJenkinsPluginInstance.second->createWidget();

            if (mGitServerPluginInstance.second)
               plugins[mGitServerPluginInstance.first] = mGitServerPluginInstance.second->createWidget(git);

            if (mTerminal.second)
               plugins[mTerminal.first] = dynamic_cast<QObject *>(mTerminal.second->createWidget(0));

            repo->setPlugins(plugins);
         }

         if (!repoPath.isEmpty())
         {
            QProcess p;
            p.setWorkingDirectory(repoPath);
            p.start("git rev-parse", { "--show-superproject-working-tree" });
            p.waitForFinished(5000);

            const auto output = p.readAll().trimmed();
            const auto isSubmodule = !output.isEmpty();

            mRepos->setTabIcon(
                index, isSubmodule ? QIcon(":/icons/submodules") : QIcon::fromTheme("folder", QIcon(":/icons/local")));

            QLog_Info("UI", "Attaching repository to a new tab");

            if (isSubmodule)
            {
               const auto parentRepo = QString::fromUtf8(output.split('/').last());

               mRepos->setTabText(index, QString("%1 \u2192 %2").arg(parentRepo, repoName));

               QLog_Info("UI",
                         QString("Opening the submodule {%1} from the repo {%2} on tab index {%3}")
                             .arg(repoName, parentRepo)
                             .arg(index));
            }
         }

         mRepos->setCurrentIndex(index);
         mStackedLayout->setCurrentIndex(1);

         mCurrentRepos.insert(repoPath);
      }
      else
      {
         QLog_Info("UI", "Trying to open a directory that is not a Git repository.");
         QMessageBox::information(
             this, tr("Not a Git repository"),
             tr("The selected path is not a Git repository. Please make sure you opened the correct directory."));
      }
   }
   else
      QLog_Warning("UI", QString("Repository at {%1} already opened. Skip adding it again.").arg(repoPath));
}

void GitQlient::closeTab(int tabIndex)
{
   const auto repoToRemove = dynamic_cast<GitQlientRepo *>(mRepos->widget(tabIndex));

   QLog_Info("UI", QString("Removing repository {%1}").arg(repoToRemove->currentDir()));

   mCurrentRepos.remove(repoToRemove->currentDir());
   repoToRemove->close();

   const auto totalTabs = mRepos->count() - 1;

   if (totalTabs == 0)
   {
      mStackedLayout->setCurrentIndex(0);
      setWindowTitle(QString("GitKlient %1").arg(VER));
   }
}

void GitQlient::restorePinnedRepos()
{
   const auto pinnedRepos
       = GitQlientSettings().globalValue(GitQlientSettings::PinnedRepos, QStringList()).toStringList();

   for (auto &repo : pinnedRepos)
      addNewRepoTab(repo, true);
}

void GitQlient::onSuccessOpen(const QString &fullPath)
{
   GitQlientSettings().setProjectOpened(fullPath);

   mConfigWidget->onRepoOpened();
}

void GitQlient::conditionallyOpenPreConfigDlg(const QSharedPointer<GitBase> &git,
                                              const QSharedPointer<GitQlientSettings> &settings)
{
   GitConfig config(git);

   const auto showDlg = settings->localValue("ShowInitConfigDialog", true).toBool();
   const auto maxCommits = settings->localValue("MaxCommits", -1).toInt();

   if (maxCommits == -1 || (config.getServerHost().contains("https") && showDlg))
   {
      InitialRepoConfig preConfig(git, settings, this);
      preConfig.exec();
   }
}

void GitQlient::updateWindowTitle()
{

   if (const auto currentTab = dynamic_cast<GitQlientRepo *>(mRepos->currentWidget()))
   {
      if (const auto repoPath = currentTab->currentDir(); !repoPath.isEmpty())
      {
         const auto currentName = repoPath.split("/").last();
         const auto currentBranch = currentTab->currentBranch();

         setWindowTitle(QString("%1 (%2) - GitKlient %3").arg(currentName, currentBranch, VER));
      }
   }
}

void GitQlient::loadPlugins()
{
   QDir pluginsDir(QSettings().value("PluginsFolder").toString());
#if defined(Q_OS_WIN)
   if (pluginsDir.dirName().toLower() == "debug" || pluginsDir.dirName().toLower() == "release")
      pluginsDir.cdUp();
#elif defined(Q_OS_MAC)
   if (pluginsDir.dirName() == "MacOS")
   {
      pluginsDir.cdUp();
      pluginsDir.cdUp();
      pluginsDir.cdUp();
   }
#endif

   const auto entries = pluginsDir.entryList(QDir::Files);

   for (const auto &fileName : entries)
   {
      QPluginLoader pluginLoader(pluginsDir.absoluteFilePath(fileName));
      if (const auto plugin = pluginLoader.instance())
      {
         const auto metadata = pluginLoader.metaData();
         const auto name = metadata.value("MetaData").toObject().value("Name").toString();
         const auto version = metadata.value("MetaData").toObject().value("Version").toString();
         const auto newKey = QString("%1-%2").arg(name, version);

         if (name.contains("jenkins", Qt::CaseInsensitive))
            mJenkinsPluginInstance = qMakePair(newKey, qobject_cast<IJenkinsWidget *>(plugin));
         else if (name.contains("gitserver", Qt::CaseInsensitive))
         {
            bool loaded = true;

            QLibrary webChannel("libQt5WebChannel");
            loaded &= webChannel.load();

            if (!loaded)
               QLog_Error("UI", QString("Impossible to load QtWebChannel: %1").arg(webChannel.errorString()));

            QLibrary webEngineWidgets("libQt5WebEngineWidgets");
            loaded &= webEngineWidgets.load();

            if (loaded)
               mGitServerPluginInstance = qMakePair(newKey, qobject_cast<IGitServerWidget *>(plugin));
            else
               QLog_Error("UI", "It was impossible to load the GitServerPlugin since there are dependencies missing.");
         }
         else if (name.contains("qtermwidget", Qt::CaseInsensitive))
            mTerminal = qMakePair(newKey, qobject_cast<QTermWidgetInterface *>(plugin));
         else
            mPlugins[newKey] = plugin;
      }
      else
      {
         const auto errorStr = pluginLoader.errorString();
         QLog_Error("UI", QString("%1").arg(errorStr));
      }
   }
}
