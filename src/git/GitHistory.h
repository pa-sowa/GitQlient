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

class GitHistory
{
public:
   explicit GitHistory(const QSharedPointer<GitBase> &gitBase);

   GitExecResult blame(const QString &file, const QString &commitFrom);
   GitExecResult history(const QString &file);
   GitExecResult getBranchesDiff(const QString &base, const QString &head);
   GitExecResult getCommitDiff(const QString &sha, const QString &diffToSha);
   GitExecResult getFileDiff(const QString &currentSha, const QString &previousSha, const QString &file, bool isStaged);
   GitExecResult getDiffFiles(const QString &sha, const QString &diffToSha);
   GitExecResult getUntrackedFileDiff(const QString &file) const;

private:
   QSharedPointer<GitBase> mGitBase;
};
