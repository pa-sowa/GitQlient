#pragma once

/****************************************************************************************
 ** GitQlient is an application to manage and operate one or several Git repositories. With
 ** GitQlient you will be able to add commits, branches and manage all the options Git provides.
 ** Copyright (C) 2021  Francesc Martinez
 **
 ** LinkedIn: www.linkedin.com/in/cescmm/
 ** Web: www.francescmm.com
 **
 ** This program is free software; you can redistribute it and/or
 ** modify it under the terms of the GNU Lesser General Public
 ** License as published by the Free Software Foundation; either
 ** version 2 of the License, or (at your option) any later version.
 **
 ** This program is distributed in the hope that it will be useful,
 ** but WITHOUT ANY WARRANTY; without even the implied warranty of
 ** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 ** Lesser General Public License for more details.
 **
 ** You should have received a copy of the GNU Lesser General Public
 ** License along with this library; if not, write to the Free Software
 ** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 ***************************************************************************************/

#include <GitExecResult.h>

#include <QSharedPointer>

class GitBase;
class RevisionFiles;

class GitLocal : public QObject
{
   Q_OBJECT

signals:
   void signalWipUpdated();

public:
   enum class CommitResetType
   {
      SOFT,
      MIXED,
      HARD
   };

   explicit GitLocal(const QSharedPointer<GitBase> &gitBase);
   GitExecResult stageFile(const QString &fileName) const;
   GitExecResult cherryPickCommit(const QString &sha) const;
   GitExecResult cherryPickAbort() const;
   GitExecResult cherryPickContinue() const;
   GitExecResult checkoutCommit(const QString &sha) const;
   GitExecResult markFileAsResolved(const QString &fileName);
   GitExecResult markFilesAsResolved(const QStringList &files);
   bool checkoutFile(const QString &fileName) const;

   GitExecResult resetFile(const QString &fileName) const;
   bool resetCommit(const QString &sha, CommitResetType type);
   GitExecResult commitFiles(QStringList &selFiles, const RevisionFiles &allCommitFiles, const QString &msg) const;
   GitExecResult ammendCommit(const QStringList &selFiles, const RevisionFiles &allCommitFiles, const QString &msg,
                              const QString &author = QString()) const;

private:
   QSharedPointer<GitBase> mGitBase;

   GitExecResult updateIndex(const RevisionFiles &files, const QStringList &selFiles) const;
};
