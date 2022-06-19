﻿#include "BranchesWidget.h"

#include <AddSubtreeDlg.h>
#include <BranchTreeWidget.h>
#include <BranchesWidgetMinimal.h>
#include <ClickableFrame.h>
#include <GitBase.h>
#include <GitCache.h>
#include <GitConfig.h>
#include <GitQlientBranchItemRole.h>
#include <GitQlientSettings.h>
#include <GitStashes.h>
#include <GitSubmodules.h>
#include <GitSubtree.h>
#include <GitTags.h>
#include <StashesContextMenu.h>
#include <SubmodulesContextMenu.h>

#include <QApplication>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

#include <QLogger.h>

using namespace QLogger;
using namespace GitQlient;

namespace
{
constexpr auto k_arrowSize = QSize(15, 15);

QTreeWidgetItem *getChild(QTreeWidgetItem *parent, const QString &childName)
{
   QTreeWidgetItem *child = nullptr;

   if (parent)
   {
      const auto childrenCount = parent->childCount();

      for (auto i = 0; i < childrenCount; ++i)
         if (parent->child(i)->text(0) == childName)
            child = parent->child(i);
   }

   return child;
}

struct RemoteBranch
{
   QString remote;
   QString branch;
   QString sha;
};

QIcon restoreIcon()
{
   return QIcon::fromTheme("window-maximize", QIcon(":/icons/add"));
}

QIcon minimizeIcon()
{
   return QIcon::fromTheme("window-minimize", QIcon(":/icons/remove"));
}

} // anonymous

BranchesWidget::BranchesWidget(const QSharedPointer<GitCache> &cache, const QSharedPointer<GitBase> &git,
                               QWidget *parent)
   : QFrame(parent)
   , mCache(cache)
   , mGit(git)
   , mGitTags(new GitTags(mGit))
   , mLocalBranchesTree(new BranchTreeWidget(mCache, mGit, this))
   , mLocalBranchesTitleLabel(new QLabel(tr("Branches (0)")))
   , mRemoteBranchesTree(new BranchTreeWidget(mCache, mGit, this))
   , mRemoteBranchesTitleLabel(new QLabel(tr("Remote (0)")))
   , mTagsTree(new RefTreeWidget(this))
   , mTagsTitleLabel(new QLabel(tr("Tags (0)")))
   , mStashesList(new QListWidget())
   , mStashesTitleLabel(new QLabel(tr("Stashes (0)")))
   , mSubmodulesTitleLabel(new QLabel(tr("Submodules (0)")))
   , mSubmodulesList(new QListWidget())
   , mSubtreeTitleLabel(new QLabel(tr("Subtrees (0)")))
   , mSubtreeList(new QListWidget())
   , mMinimize(new QPushButton())
   , mMinimal(new BranchesWidgetMinimal(mCache, mGit))
{
   connect(mCache.get(), &GitCache::signalCacheUpdated, this, &BranchesWidget::showBranches);
   connect(mCache.get(), &GitCache::signalCacheUpdated, this, &BranchesWidget::processTags);

   setAttribute(Qt::WA_DeleteOnClose);

   GitQlientSettings settings(mGit->getGitDir());

   const auto localFrame = addSection(mLocalBranchesTitleLabel, mLocalBranchesTree, settings, "LocalHeader");
   mLocalBranchesTree->setLocalRepo(true);
   mLocalBranchesTree->setColumnCount(1);
   mLocalBranchesTree->setObjectName("LocalBranches");

   const auto remoteFrame = addSection(mRemoteBranchesTitleLabel, mRemoteBranchesTree, settings, "RemoteHeader");
   mRemoteBranchesTree->setColumnCount(1);

   const auto tagsFrame = addSection(mTagsTitleLabel, mTagsTree, settings, "TagsHeader");
   mTagsTree->setColumnCount(1);
   mTagsTree->setContextMenuPolicy(Qt::CustomContextMenu);
   mTagsTree->setRootIsDecorated(false);

   const auto stashFrame = addSection(mStashesTitleLabel, mStashesList, settings, "StashesHeader");
   const auto submoduleFrame = addSection(mSubmodulesTitleLabel, mSubmodulesList, settings, "SubmodulesHeader");
   const auto subtreeFrame = addSection(mSubtreeTitleLabel, mSubtreeList, settings, "SubtreeHeader");

   const auto searchBranch = new QLineEdit();
   searchBranch->setPlaceholderText(tr("Prese ENTER to search a branch or tag..."));
   searchBranch->setObjectName("SearchInput");
   connect(searchBranch, &QLineEdit::returnPressed, this, &BranchesWidget::onSearchBranch);

   mMinimize->setIcon(QIcon::fromTheme("sidebar-collapse-right", QIcon(":/icons/ahead")));
   mMinimize->setToolTip(tr("Show minimalist view"));
   mMinimize->setObjectName("BranchesWidgetOptionsButton");
   connect(mMinimize, &QPushButton::clicked, this, &BranchesWidget::minimalView);

   const auto mainControlsLayout = new QHBoxLayout();
   mainControlsLayout->setContentsMargins(QMargins());
   mainControlsLayout->setSpacing(5);
   mainControlsLayout->addWidget(mMinimize);
   mainControlsLayout->addWidget(searchBranch);

   const auto separator = new QFrame();
   separator->setObjectName("separator");

   const auto panelsLayout = new QVBoxLayout();
   panelsLayout->setContentsMargins(QMargins());
   panelsLayout->setSpacing(0);
   panelsLayout->addWidget(localFrame);
   panelsLayout->addWidget(remoteFrame);
   panelsLayout->addWidget(tagsFrame);
   panelsLayout->addWidget(stashFrame);
   panelsLayout->addWidget(submoduleFrame);
   panelsLayout->addWidget(subtreeFrame);
   panelsLayout->addWidget(separator);

   const auto panelsFrame = new QFrame();
   panelsFrame->setObjectName("panelsFrame");
   panelsFrame->setLayout(panelsLayout);

   const auto vLayout = new QVBoxLayout();
   vLayout->setContentsMargins(0, 0, 0, 0);
   vLayout->setSpacing(0);
   vLayout->addLayout(mainControlsLayout);
   vLayout->addSpacing(5);
   vLayout->addWidget(panelsFrame);

   mFullBranchFrame = new QFrame();

   const auto mainBranchLayout = new QHBoxLayout(mFullBranchFrame);
   mainBranchLayout->setContentsMargins(QMargins());
   mainBranchLayout->setSpacing(0);
   mainBranchLayout->addLayout(vLayout);

   const auto mainLayout = new QGridLayout(this);
   mainLayout->setContentsMargins(QMargins());
   mainLayout->setSpacing(0);
   mainLayout->addWidget(mFullBranchFrame, 0, 0, 3, 1);
   mainLayout->addItem(new QSpacerItem(1, 1, QSizePolicy::Fixed, QSizePolicy::Expanding), 0, 1);
   mainLayout->addWidget(mMinimal, 1, 1);
   mainLayout->addItem(new QSpacerItem(1, 1, QSizePolicy::Fixed, QSizePolicy::Expanding), 2, 1);

   const auto isMinimalVisible = settings.localValue("MinimalBranchesView", false).toBool();
   mFullBranchFrame->setVisible(!isMinimalVisible);
   mMinimal->setVisible(isMinimalVisible);
   connect(mMinimal, &BranchesWidgetMinimal::showFullBranchesView, this, &BranchesWidget::fullView);
   connect(mMinimal, &BranchesWidgetMinimal::commitSelected, this, &BranchesWidget::signalSelectCommit);
   connect(mMinimal, &BranchesWidgetMinimal::stashSelected, this, &BranchesWidget::onStashSelected);

   connect(mLocalBranchesTree, &BranchTreeWidget::signalSelectCommit, this, &BranchesWidget::signalSelectCommit);
   connect(mLocalBranchesTree, &BranchTreeWidget::signalSelectCommit, mRemoteBranchesTree,
           &BranchTreeWidget::clearSelection);
   connect(mLocalBranchesTree, &BranchTreeWidget::fullReload, this, &BranchesWidget::fullReload);
   connect(mLocalBranchesTree, &BranchTreeWidget::logReload, this, &BranchesWidget::logReload);
   connect(mLocalBranchesTree, &BranchTreeWidget::signalMergeRequired, this, &BranchesWidget::signalMergeRequired);
   connect(mLocalBranchesTree, &BranchTreeWidget::mergeSqushRequested, this, &BranchesWidget::mergeSqushRequested);
   connect(mLocalBranchesTree, &BranchTreeWidget::signalPullConflict, this, &BranchesWidget::signalPullConflict);

   connect(mRemoteBranchesTree, &BranchTreeWidget::signalSelectCommit, this, &BranchesWidget::signalSelectCommit);
   connect(mRemoteBranchesTree, &BranchTreeWidget::signalSelectCommit, mLocalBranchesTree,
           &BranchTreeWidget::clearSelection);
   connect(mRemoteBranchesTree, &BranchTreeWidget::fullReload, this, &BranchesWidget::fullReload);
   connect(mRemoteBranchesTree, &BranchTreeWidget::logReload, this, &BranchesWidget::logReload);
   connect(mRemoteBranchesTree, &BranchTreeWidget::signalMergeRequired, this, &BranchesWidget::signalMergeRequired);
   connect(mRemoteBranchesTree, &BranchTreeWidget::mergeSqushRequested, this, &BranchesWidget::mergeSqushRequested);

   connect(mTagsTree, &QTreeWidget::itemClicked, this, &BranchesWidget::onTagClicked);
   connect(mTagsTree, &QListWidget::customContextMenuRequested, this, &BranchesWidget::showTagsContextMenu);
   connect(mStashesList, &QListWidget::itemClicked, this, &BranchesWidget::onStashClicked);
   connect(mStashesList, &QListWidget::customContextMenuRequested, this, &BranchesWidget::showStashesContextMenu);
   connect(mSubmodulesList, &QListWidget::customContextMenuRequested, this, &BranchesWidget::showSubmodulesContextMenu);
   connect(mSubtreeList, &QListWidget::customContextMenuRequested, this, &BranchesWidget::showSubtreesContextMenu);

   connect(mGitTags.get(), &GitTags::remoteTagsReceived, mCache.get(), &GitCache::updateTags);
}

bool BranchesWidget::isMinimalViewActive() const
{
   GitQlientSettings settings(mGit->getGitDir());
   return settings.localValue("MinimalBranchesView", false).toBool();
}

void BranchesWidget::showBranches()
{
   QLog_Info("UI", QString("Loading branches data"));

   clear();
   mMinimal->clearActions();

   QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));

   auto branches = mCache->getBranches(References::Type::LocalBranch);
   std::map<QString, QString> branchShaMap;
   std::map<QString, QString> branchFolderShaMap;

   for (const auto &pair : qAsConst(branches))
   {
      for (const auto &branch : pair.second)
      {
         if (branch.contains("/"))
            branchFolderShaMap.insert(std::make_pair(branch, pair.first));
         else
            branchShaMap.insert(std::make_pair(branch, pair.first));
      }
   }

   if (!branches.empty())
   {
      QLog_Info("UI", QString("Fetched {%1} local branches").arg(branches.count()));
      QLog_Info("UI", QString("Processing local branches..."));

      for (const auto &iter : branchFolderShaMap)
      {
         if (!iter.first.contains("HEAD->"))
         {
            processLocalBranch(iter.second, iter.first);
            mMinimal->configureLocalMenu(iter.second, iter.first);
         }
      }

      auto count = 0;
      for (const auto &iter : branchShaMap)
      {
         if (!iter.first.contains("HEAD->"))
         {
            ++count;
            processLocalBranch(iter.second, iter.first);
            mMinimal->configureLocalMenu(iter.second, iter.first);
         }
      }

      QLog_Info("UI", QString("... local branches processed"));
   }

   // TODO:
   // mRemoteBranchesCount->setText(QString("(%1)").arg(count));

   branches.clear();
   branches.squeeze();
   branchShaMap.clear();
   branchFolderShaMap.clear();
   branches = mCache->getBranches(References::Type::RemoteBranches);

   QVector<RemoteBranch> remoteBranches;

   for (const auto &pair : qAsConst(branches))
   {
      for (const auto &branch : pair.second)
      {
         remoteBranches.append(
             RemoteBranch { branch.mid(0, branch.indexOf("/")), branch.mid(branch.indexOf("/") + 1), pair.first });
      }
   }

   std::sort(remoteBranches.begin(), remoteBranches.end(), [](const RemoteBranch &r1, const RemoteBranch &r2) {
      return r1.remote == r2.remote ? r1.branch < r2.branch : r1.remote < r2.remote;
   });

   for (const auto &branchInfo : qAsConst(remoteBranches))
   {
      const auto fullBranchName = QString("%1/%2").arg(branchInfo.remote, branchInfo.branch);

      if (branchInfo.branch.contains("/"))
         branchFolderShaMap.insert(std::make_pair(fullBranchName, branchInfo.sha));
      else
         branchShaMap.insert(std::make_pair(fullBranchName, branchInfo.sha));
   }

   if (!branches.empty())
   {
      QLog_Info("UI", QString("Fetched {%1} remote branches").arg(branches.count()));
      QLog_Info("UI", QString("Processing remote branches..."));

      for (const auto &iter : branchFolderShaMap)
      {
         if (!iter.first.contains("HEAD->"))
         {
            processRemoteBranch(iter.second, iter.first);
            mMinimal->configureRemoteMenu(iter.second, iter.first);
         }
      }

      int count = 0;
      for (const auto &iter : branchShaMap)
      {
         if (!iter.first.contains("HEAD->"))
         {
            ++count;
            processRemoteBranch(iter.second, iter.first);
            mMinimal->configureRemoteMenu(iter.second, iter.first);
         }
      }

      // TODO:
      // mRemoteBranchesCount->setText(QString("(%1)").arg(count));

      branches.clear();
      branches.squeeze();

      QLog_Info("UI", QString("... remote branches processed"));
   }

   processStashes();
   processSubmodules();
   processSubtrees();

   QApplication::restoreOverrideCursor();

   adjustBranchesTree(mLocalBranchesTree);
}

void BranchesWidget::refreshCurrentBranchLink()
{
   mLocalBranchesTree->reloadCurrentBranchLink();
}

void BranchesWidget::clear()
{
   blockSignals(true);
   mLocalBranchesTree->clear();
   mRemoteBranchesTree->clear();
   blockSignals(false);
}

void BranchesWidget::fullView()
{
   mFullBranchFrame->setVisible(true);
   mMinimal->setVisible(false);

   emit minimalViewStateChanged(false);

   GitQlientSettings settings(mGit->getGitDir());
   settings.setLocalValue("MinimalBranchesView", mMinimal->isVisible());
}

void BranchesWidget::returnToSavedView()
{
   GitQlientSettings settings(mGit->getGitDir());
   const auto savedState = settings.localValue("MinimalBranchesView", false).toBool();

   if (savedState != mMinimal->isVisible())
   {
      mFullBranchFrame->setVisible(!savedState);
      mMinimal->setVisible(savedState);

      emit minimalViewStateChanged(savedState);
   }
}

void BranchesWidget::minimalView()
{
   forceMinimalView();

   GitQlientSettings settings(mGit->getGitDir());
   settings.setLocalValue("MinimalBranchesView", mMinimal->isVisible());
}

void BranchesWidget::forceMinimalView()
{
   mFullBranchFrame->setVisible(false);
   mMinimal->setVisible(true);

   emit minimalViewStateChanged(true);
}

void BranchesWidget::onPanelsVisibilityChaned()
{
   GitQlientSettings settings(mGit->getGitDir());

   for (const auto &p : m_panels)
   {
      auto visible = settings.localValue(p.key, true).toBool();
      auto icon = !visible ? restoreIcon() : minimizeIcon();
      p.arrowLabel->setPixmap(icon.pixmap(k_arrowSize));
      p.viewWidget->setVisible(visible);
   }
}

void BranchesWidget::processLocalBranch(const QString &sha, QString branch)
{
   QLog_Debug("UI", QString("Adding local branch {%1}").arg(branch));

   auto isCurrentBranch = false;

   if (branch == mGit->getCurrentBranch())
      isCurrentBranch = true;

   const auto fullBranchName = branch;

   QVector<QTreeWidgetItem *> parents;
   QTreeWidgetItem *parent = nullptr;
   auto folders = branch.split("/");
   branch = folders.takeLast();

   for (const auto &folder : qAsConst(folders))
   {
      QTreeWidgetItem *child = nullptr;

      if (parent)
      {
         child = getChild(parent, folder);
         parents.append(child);
      }
      else
      {
         for (auto i = 0; i < mLocalBranchesTree->topLevelItemCount(); ++i)
         {
            if (mLocalBranchesTree->topLevelItem(i)->text(0) == folder)
            {
               child = mLocalBranchesTree->topLevelItem(i);
               parents.append(child);
            }
         }
      }

      if (!child)
      {
         const auto item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem();
         item->setText(0, folder);

         if (!parent)
            mLocalBranchesTree->addTopLevelItem(item);

         parent = item;
         parents.append(parent);
      }
      else
      {
         parent = child;
         parents.append(child);
      }
   }

   auto item = new QTreeWidgetItem(parent);
   item->setChildIndicatorPolicy(QTreeWidgetItem::DontShowIndicator);
   item->setText(0, branch);
   item->setData(0, GitQlient::IsCurrentBranchRole, isCurrentBranch);
   item->setData(0, GitQlient::FullNameRole, fullBranchName);
   item->setData(0, GitQlient::LocalBranchRole, true);
   item->setData(0, GitQlient::ShaRole, sha);
   item->setData(0, Qt::ToolTipRole, fullBranchName);
   item->setData(0, GitQlient::IsLeaf, true);
   item->setIcon(0, QIcon::fromTheme("vcs-branch", QIcon(":/icons/repo_indicator")));

   if (branch == "detached")
   {
      QFont font = item->font(0);
      font.setItalic(true);
      item->setFont(0, font);
   }

   if (isCurrentBranch)
   {
      item->setSelected(true);

      QFont font = item->font(0);
      font.setBold(true);
      item->setFont(0, font);

      for (const auto parent : parents)
      {
         mLocalBranchesTree->setCurrentItem(item);
         mLocalBranchesTree->expandItem(parent);
         const auto indexToScroll = mLocalBranchesTree->currentIndex();
         mLocalBranchesTree->scrollTo(indexToScroll);
      }
   }

   parents.clear();
   parents.squeeze();

   mLocalBranchesTree->addTopLevelItem(item);

   QLog_Debug("UI", QString("Finish gathering local branch information"));
}

void BranchesWidget::processRemoteBranch(const QString &sha, QString branch)
{
   const auto fullBranchName = branch;
   auto folders = branch.split("/");
   branch = folders.takeLast();

   QTreeWidgetItem *parent = nullptr;

   for (const auto &folder : qAsConst(folders))
   {
      QTreeWidgetItem *child = nullptr;

      if (parent)
         child = getChild(parent, folder);
      else
      {
         for (auto i = 0; i < mRemoteBranchesTree->topLevelItemCount(); ++i)
         {
            if (mRemoteBranchesTree->topLevelItem(i)->text(0) == folder)
               child = mRemoteBranchesTree->topLevelItem(i);
         }
      }

      if (!child)
      {
         const auto item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem();
         item->setText(0, folder);

         if (!parent)
         {
            item->setData(0, GitQlient::IsRoot, true);
            item->setIcon(0, QIcon::fromTheme("folder-cloud", QIcon(":/icons/folder_indicator")));
            mRemoteBranchesTree->addTopLevelItem(item);
         }

         parent = item;
      }
      else
         parent = child;
   }

   QLog_Trace("UI", QString("Adding remote branch {%1}").arg(branch));

   const auto item = new QTreeWidgetItem(parent);
   item->setChildIndicatorPolicy(QTreeWidgetItem::DontShowIndicator);
   item->setText(0, branch);
   item->setData(0, GitQlient::FullNameRole, fullBranchName);
   item->setData(0, GitQlient::LocalBranchRole, false);
   item->setData(0, GitQlient::ShaRole, sha);
   item->setData(0, Qt::ToolTipRole, fullBranchName);
   item->setData(0, GitQlient::IsLeaf, true);
   item->setIcon(0, QIcon::fromTheme("vcs-branch", QIcon(":/icons/repo_indicator")));
}

void BranchesWidget::processTags()
{
   mTagsTree->clear();

   const auto localTags = mCache->getTags(References::Type::LocalTag);
   auto remoteTags = mCache->getTags(References::Type::RemoteTag);
   int tagsCount = 0;

   for (auto iter = localTags.cbegin(); iter != localTags.cend(); ++iter)
   {
      QTreeWidgetItem *parent = nullptr;
      auto fullTagName = iter.key();
      auto folders = fullTagName.split("/");
      auto tagName = folders.takeLast();

      for (const auto &folder : qAsConst(folders))
      {
         QTreeWidgetItem *child = nullptr;

         if (parent)
            child = getChild(parent, folder);
         else
         {
            for (auto i = 0; i < mTagsTree->topLevelItemCount(); ++i)
            {
               if (mTagsTree->topLevelItem(i)->text(0) == folder)
                  child = mTagsTree->topLevelItem(i);
            }
         }

         if (!child)
         {
            const auto item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem();
            item->setText(0, folder);

            if (!parent)
               mTagsTree->addTopLevelItem(item);

            parent = item;
         }
         else
            parent = child;
      }

      const auto item = new QTreeWidgetItem(parent);

      if (!remoteTags.contains(fullTagName))
      {
         tagName += " (local)";
         item->setData(0, LocalBranchRole, false);
      }
      else
      {
         item->setData(0, LocalBranchRole, true);
         remoteTags.remove(fullTagName);
      }

      QLog_Trace("UI", QString("Adding tag {%1}").arg(tagName));

      item->setChildIndicatorPolicy(QTreeWidgetItem::DontShowIndicator);
      item->setText(0, tagName);
      item->setData(0, GitQlient::FullNameRole, fullTagName);
      item->setData(0, GitQlient::ShaRole, iter.value());
      item->setData(0, Qt::ToolTipRole, fullTagName);
      item->setData(0, GitQlient::IsLeaf, true);
      item->setIcon(0, QIcon::fromTheme("tag", QIcon(":/icons/tag_indicator")));

      mTagsTree->addTopLevelItem(item);
      ++tagsCount;
   }

   for (auto iter = remoteTags.cbegin(); iter != remoteTags.cend(); ++iter)
   {
      QTreeWidgetItem *parent = nullptr;
      auto fullTagName = iter.key();
      auto folders = fullTagName.split("/");
      auto tagName = folders.takeLast();

      for (const auto &folder : qAsConst(folders))
      {
         QTreeWidgetItem *child = nullptr;

         if (parent)
            child = getChild(parent, folder);
         else
         {
            for (auto i = 0; i < mTagsTree->topLevelItemCount(); ++i)
            {
               if (mTagsTree->topLevelItem(i)->text(0) == folder)
                  child = mTagsTree->topLevelItem(i);
            }
         }

         if (!child)
         {
            const auto item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem();
            item->setText(0, folder);

            if (!parent)
               mTagsTree->addTopLevelItem(item);

            parent = item;
         }
         else
            parent = child;
      }

      QLog_Trace("UI", QString("Adding tag {%1}").arg(tagName));

      const auto item = new QTreeWidgetItem(parent);
      item->setChildIndicatorPolicy(QTreeWidgetItem::DontShowIndicator);
      item->setText(0, tagName);
      item->setData(0, GitQlient::FullNameRole, fullTagName);
      item->setData(0, GitQlient::ShaRole, iter.value());
      item->setData(0, Qt::ToolTipRole, fullTagName);
      item->setData(0, GitQlient::IsLeaf, true);
      item->setIcon(0, QIcon::fromTheme("tag", QIcon(":/icons/tag_indicator")));

      mTagsTree->addTopLevelItem(item);
      ++tagsCount;
   }

   mTagsTree->update();
   mTagsTitleLabel->setText(tr("Tags (%1)").arg(tagsCount));
}

void BranchesWidget::processStashes()
{
   mStashesList->clear();

   GitStashes git(mGit);
   const auto stashes = git.getStashes();

   QLog_Info("UI", QString("Fetching {%1} stashes").arg(stashes.count()));

   for (const auto &stash : stashes)
   {
      const auto stashId = stash.split(":").first();
      const auto stashDesc = stash.split("}: ").last();
      const auto item = new QListWidgetItem(stashDesc);
      item->setData(Qt::UserRole, stashId);
      mStashesList->addItem(item);
      mMinimal->configureStashesMenu(stashId, stashDesc);
   }

   mStashesTitleLabel->setText(tr("Stashes (%1)").arg(stashes.count()));
}

void BranchesWidget::processSubmodules()
{
   mSubmodulesList->clear();

   GitSubmodules git(mGit);
   const auto submodules = git.getSubmodules();

   QLog_Info("UI", QString("Fetching {%1} submodules").arg(submodules.count()));

   for (const auto &submodule : submodules)
   {
      mSubmodulesList->addItem(submodule);
      mMinimal->configureSubmodulesMenu(submodule);
   }

   mSubmodulesTitleLabel->setText(tr("Submodules (%1)").arg(submodules.count()));
}

void BranchesWidget::processSubtrees()
{
   mSubtreeList->clear();

   GitSubtree git(mGit);

   const auto ret = git.list();

   if (ret.success)
   {
      const auto rawData = ret.output;
      const auto commits = rawData.split("\n\n");
      auto count = 0;

      for (auto &subtreeRawData : commits)
      {
         if (!subtreeRawData.isEmpty())
         {
            QString name;
            QString sha;
            auto fields = subtreeRawData.split("\n");

            for (auto &field : fields)
            {
               if (field.contains("git-subtree-dir:"))
                  name = field.remove("git-subtree-dir:").trimmed();
               else if (field.contains("git-subtree-split"))
                  sha = field.remove("git-subtree-split:").trimmed();
            }

            mSubtreeList->addItem(name);
            ++count;
         }
      }

      mSubtreeTitleLabel->setText(tr("Subtrees (%1)").arg(count));
   }
}

void BranchesWidget::adjustBranchesTree(BranchTreeWidget *treeWidget)
{
   for (auto i = 1; i < treeWidget->columnCount(); ++i)
      treeWidget->resizeColumnToContents(i);

   treeWidget->header()->setSectionResizeMode(0, QHeaderView::Stretch);

   for (auto i = 1; i < treeWidget->columnCount(); ++i)
      treeWidget->header()->setSectionResizeMode(i, QHeaderView::ResizeToContents);

   treeWidget->header()->setStretchLastSection(false);
}

void BranchesWidget::showTagsContextMenu(const QPoint &p)
{
   const auto item = mTagsTree->itemAt(p);

   if (!item)
      return;

   const auto tagName = item->data(0, GitQlient::FullNameRole).toString();

   if (!tagName.isEmpty())
   {
      const auto isRemote = item->data(0, LocalBranchRole).toBool();
      const auto menu = new QMenu(this);
      const auto removeTagAction = menu->addAction(tr("Remove tag"));
      connect(removeTagAction, &QAction::triggered, this, [this, tagName, isRemote]() {
         QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
         GitTags git(mGit);
         const auto ret = git.removeTag(tagName, isRemote);
         QApplication::restoreOverrideCursor();

         if (ret.success)
            mGitTags->getRemoteTags();
      });

      const auto pushTagAction = menu->addAction(tr("Push tag"));
      pushTagAction->setEnabled(!isRemote);
      connect(pushTagAction, &QAction::triggered, this, [this, tagName]() {
         QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
         GitTags git(mGit);
         const auto ret = git.pushTag(tagName);
         QApplication::restoreOverrideCursor();

         if (ret.success)
            mGitTags->getRemoteTags();
      });

      menu->exec(mTagsTree->viewport()->mapToGlobal(p));
   }
}

void BranchesWidget::showStashesContextMenu(const QPoint &p)
{
   QLog_Info("UI", QString("Requesting context menu for stashes"));

   const auto index = mStashesList->indexAt(p);

   if (index.isValid())
   {
      const auto menu = new StashesContextMenu(mGit, index.data(Qt::UserRole).toString(), this);
      connect(menu, &StashesContextMenu::signalUpdateView, this, &BranchesWidget::fullReload);
      connect(menu, &StashesContextMenu::signalContentRemoved, this, &BranchesWidget::fullReload);
      menu->exec(mStashesList->viewport()->mapToGlobal(p));
   }
}

void BranchesWidget::showSubmodulesContextMenu(const QPoint &p)
{
   QLog_Info("UI", QString("Requesting context menu for submodules"));

   const auto menu = new SubmodulesContextMenu(mGit, mSubmodulesList->indexAt(p), this);
   connect(menu, &SubmodulesContextMenu::openSubmodule, this, &BranchesWidget::signalOpenSubmodule);
   connect(menu, &SubmodulesContextMenu::infoUpdated, this, &BranchesWidget::fullReload);

   menu->exec(mSubmodulesList->viewport()->mapToGlobal(p));
}

void BranchesWidget::showSubtreesContextMenu(const QPoint &p)
{
   QLog_Info("UI", QString("Requesting context menu for subtrees"));

   QModelIndex index = mSubtreeList->indexAt(p);

   const auto menu = new QMenu(this);

   if (index.isValid())
   {
      connect(menu->addAction(tr("Pull")), &QAction::triggered, this, [this, index]() {
         QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));

         const auto prefix = index.data().toString();
         const auto subtreeData = getSubtreeData(prefix);

         GitSubtree git(mGit);
         const auto ret = git.pull(subtreeData.first, subtreeData.second, prefix);
         QApplication::restoreOverrideCursor();

         if (ret.success)
            emit fullReload();
         else
            QMessageBox::warning(this, tr("Error when pulling"), ret.output);
      });
      /*
      connect(menu->addAction(tr("Merge")), &QAction::triggered, this, [this, index]() {
         QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));

         const auto subtreeData = getSubtreeData(index.data().toString());

         GitSubtree git(mGit);
         const auto ret = git->pull(subtreeData.first, subtreeData.second);
         QApplication::restoreOverrideCursor();

         if (ret.success)
            emit signalBranchesUpdated();
      });
*/
      connect(menu->addAction(tr("Push")), &QAction::triggered, this, [this, index]() {
         QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));

         const auto prefix = index.data().toString();
         const auto subtreeData = getSubtreeData(prefix);

         GitSubtree git(mGit);
         const auto ret = git.push(subtreeData.first, subtreeData.second, prefix);
         QApplication::restoreOverrideCursor();

         if (ret.success)
            emit fullReload();
         else
            QMessageBox::warning(this, tr("Error when pushing"), ret.output);
      });

      const auto addSubtree = menu->addAction(tr("Configure"));
      connect(addSubtree, &QAction::triggered, this, [this, index]() {
         const auto prefix = index.data().toString();
         const auto subtreeData = getSubtreeData(prefix);
         AddSubtreeDlg addDlg(prefix, subtreeData.first, subtreeData.second, mGit);
         const auto ret = addDlg.exec();
         if (ret == QDialog::Accepted)
            emit fullReload();
      });
      // menu->addAction(tr("Split"));
   }
   else
   {
      const auto addSubtree = menu->addAction(tr("Add subtree"));
      connect(addSubtree, &QAction::triggered, this, [this]() {
         AddSubtreeDlg addDlg(mGit);
         const auto ret = addDlg.exec();
         if (ret == QDialog::Accepted)
            emit fullReload();
      });
   }

   menu->exec(mSubtreeList->viewport()->mapToGlobal(p));
}

void BranchesWidget::onTagClicked(QTreeWidgetItem *item)
{
   if (item && item->data(0, IsLeaf).toBool())
      emit signalSelectCommit(item->data(0, ShaRole).toString());
}

void BranchesWidget::onStashClicked(QListWidgetItem *item)
{
   onStashSelected(item->data(Qt::UserRole).toString());
}

void BranchesWidget::onStashSelected(const QString &stashId)
{
   GitTags git(mGit);
   const auto sha = git.getTagCommit(stashId).output;

   emit signalSelectCommit(sha);
}

void BranchesWidget::onSearchBranch()
{
   const auto lineEdit = qobject_cast<QLineEdit *>(sender());

   const auto text = lineEdit->text();

   if (mLastSearch != text)
   {
      mLastSearch = text;
      mLastIndex = mLocalBranchesTree->focusOnBranch(mLastSearch);
      mLastTreeSearched = mLocalBranchesTree;

      if (mLastIndex == -1)
      {
         mLastIndex = mRemoteBranchesTree->focusOnBranch(mLastSearch);
         mLastTreeSearched = mRemoteBranchesTree;

         if (mLastIndex == -1)
         {
            mLastIndex = mTagsTree->focusOnBranch(mLastSearch);
            mLastTreeSearched = mTagsTree;

            if (mLastIndex == -1)
               mLastTreeSearched = mLocalBranchesTree;
         }
      }
   }
   else
   {
      if (mLastTreeSearched == mLocalBranchesTree)
      {
         if (mLastIndex != -1)
         {
            mLastIndex = mLocalBranchesTree->focusOnBranch(mLastSearch, mLastIndex);
            mLastTreeSearched = mLocalBranchesTree;
         }

         if (mLastIndex == -1)
         {
            mLastIndex = mRemoteBranchesTree->focusOnBranch(mLastSearch);
            mLastTreeSearched = mRemoteBranchesTree;
         }

         if (mLastIndex == -1)
         {
            mLastIndex = mTagsTree->focusOnBranch(mLastSearch);
            mLastTreeSearched = mTagsTree;
         }
      }
      else if (mLastTreeSearched == mRemoteBranchesTree)
      {
         if (mLastIndex == -1)
         {
            mLastIndex = mRemoteBranchesTree->focusOnBranch(mLastSearch);
            mLastTreeSearched = mRemoteBranchesTree;
         }

         if (mLastIndex == -1)
         {
            mLastIndex = mTagsTree->focusOnBranch(mLastSearch);
            mLastTreeSearched = mTagsTree;
         }
      }
      else if (mLastIndex != -1)
      {
         mLastIndex = mTagsTree->focusOnBranch(mLastSearch, mLastIndex);
         mLastTreeSearched = mTagsTree;

         if (mLastIndex == -1)
            mLastTreeSearched = mLocalBranchesTree;
      }
   }
}

QPair<QString, QString> BranchesWidget::getSubtreeData(const QString &prefix)
{
   GitQlientSettings settings(mGit->getGitDir());
   bool end = false;
   QString url;
   QString ref;

   for (auto i = 0; !end; ++i)
   {
      const auto repo = settings.localValue(QString("Subtrees/%1.prefix").arg(i), "");

      if (repo.toString() == prefix)
      {
         auto tmpUrl = settings.localValue(QString("Subtrees/%1.url").arg(i)).toString();
         auto tmpRef = settings.localValue(QString("Subtrees/%1.ref").arg(i)).toString();

         if (tmpUrl.isEmpty() || tmpRef.isEmpty())
         {
            const auto resp
                = QMessageBox::question(this, tr("Subtree configuration not found!"),
                                        tr("The subtree configuration was not found. It could be that it was created "
                                           "outside GitKlient.<br>To operate with this subtree, it needs to be "
                                           "configured.<br><br><b>Do you want to configure it now?<b>"));

            if (resp == QMessageBox::Yes)
            {
               AddSubtreeDlg stDlg(prefix, mGit, this);
               const auto ret = stDlg.exec();

               if (ret == QDialog::Accepted)
               {
                  tmpUrl = settings.localValue(QString("Subtrees/%1.url").arg(i)).toString();
                  tmpRef = settings.localValue(QString("Subtrees/%1.ref").arg(i)).toString();

                  if (tmpUrl.isEmpty() || tmpRef.isEmpty())
                     QMessageBox::critical(this, tr("Unexpected error!"),
                                           tr("An unidentified error happened while using subtrees. Please contact the "
                                              "creator of GitKlient for support."));
                  else
                  {
                     url = tmpUrl;
                     ref = tmpRef;
                  }
               }
            }

            end = true;
         }
         else
         {
            url = tmpUrl;
            ref = tmpRef;
            end = true;
         }
      }
      else if (repo.toString().isEmpty())
      {
         const auto resp
             = QMessageBox::question(this, tr("Subtree configuration not found!"),
                                     tr("The subtree configuration was not found. It could be that it was created "
                                        "outside GitKlient.<br>To operate with this subtree, it needs to be "
                                        "configured.<br><br><b>Do you want to configure it now?<b>"));

         if (resp == QMessageBox::Yes)
         {
            AddSubtreeDlg stDlg(prefix, mGit, this);
            const auto ret = stDlg.exec();

            if (ret == QDialog::Accepted)
            {
               const auto tmpUrl = settings.localValue(QString("Subtrees/%1.url").arg(i)).toString();
               const auto tmpRef = settings.localValue(QString("Subtrees/%1.ref").arg(i)).toString();

               if (tmpUrl.isEmpty() || tmpRef.isEmpty())
                  QMessageBox::critical(this, tr("Unexpected error!"),
                                        tr("An unidentified error happened while using subtrees. Please contact the "
                                           "creator of GitKlient for support."));
               else
               {
                  url = tmpUrl;
                  ref = tmpRef;
               }
            }
         }

         end = true;
      }
   }

   return qMakePair(url, ref);
}

QFrame *BranchesWidget::addSection(QLabel *titleLabel, QAbstractItemView *view, GitQlientSettings &settings,
                                   const QString &key)
{
   auto arrowLabel = new QLabel(this);

   if (const auto visible = settings.localValue(key, true).toBool(); !visible)
   {
      const auto icon = !visible ? restoreIcon() : minimizeIcon();
      arrowLabel->setPixmap(icon.pixmap(k_arrowSize));
      view->setVisible(visible);
   }
   else
      arrowLabel->setPixmap(minimizeIcon().pixmap(k_arrowSize));

   const auto headerFrame = new ClickableFrame();
   const auto headerLayout = new QHBoxLayout(headerFrame);
   headerLayout->setContentsMargins(0, 0, 0, 0);
   headerLayout->addWidget(titleLabel);
   headerLayout->addStretch();
   headerLayout->addWidget(arrowLabel);

   view->setContextMenuPolicy(Qt::CustomContextMenu);

   const auto sectionLayout = new QVBoxLayout();
   sectionLayout->setContentsMargins(QMargins());
   sectionLayout->setSpacing(0);
   sectionLayout->addWidget(headerFrame);
   sectionLayout->addSpacing(5);
   sectionLayout->addWidget(view);

   const auto sectionFrame = new QFrame();
   sectionFrame->setObjectName("sectionFrame");
   sectionFrame->setLayout(sectionLayout);

   connect(headerFrame, &ClickableFrame::clicked, this, [this, arrowLabel, view, key = QString(key)]() {
      const auto isVisible = view->isVisible();
      const auto icon = isVisible ? restoreIcon() : minimizeIcon();
      arrowLabel->setPixmap(icon.pixmap(k_arrowSize));
      view->setVisible(!isVisible);

      GitQlientSettings settings(mGit->getGitDir());
      settings.setLocalValue(key, !isVisible);

      // TODO: do we need this signal in the app?
      emit panelsVisibilityChanged();
   });

   Panel p;
   p.key = key;
   p.arrowLabel = arrowLabel;
   p.viewWidget = view;
   m_panels.append(p);

   return sectionFrame;
}
