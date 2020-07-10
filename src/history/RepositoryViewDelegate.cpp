﻿#include "RepositoryViewDelegate.h"

#include <GitQlientStyles.h>
#include <GitLocal.h>
#include <Lane.h>
#include <LaneType.h>
#include <CommitInfo.h>
#include <CommitHistoryColumns.h>
#include <CommitHistoryView.h>
#include <CommitHistoryModel.h>
#include <RevisionsCache.h>
#include <GitBase.h>

#include <QSortFilterProxyModel>
#include <QPainter>
#include <QPainterPath>

static const int MIN_VIEW_WIDTH_PX = 480;

RepositoryViewDelegate::RepositoryViewDelegate(const QSharedPointer<RevisionsCache> &cache,
                                               const QSharedPointer<GitBase> &git, CommitHistoryView *view)
   : mCache(cache)
   , mGit(git)
   , mView(view)
{
}

void RepositoryViewDelegate::paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &index) const
{
   p->setRenderHints(QPainter::Antialiasing);

   QStyleOptionViewItem newOpt(opt);
   newOpt.font.setPointSize(9);

   if (newOpt.state & QStyle::State_Selected)
      p->fillRect(newOpt.rect, GitQlientStyles::getGraphSelectionColor());
   else if (newOpt.state & QStyle::State_MouseOver)
      p->fillRect(newOpt.rect, GitQlientStyles::getGraphHoverColor());

   const auto row = mView->hasActiveFilter()
       ? dynamic_cast<QSortFilterProxyModel *>(mView->model())->mapToSource(index).row()
       : index.row();

   const auto commit = mCache->getCommitInfoByRow(row);

   if (commit.sha().isEmpty())
      return;

   if (index.column() == static_cast<int>(CommitHistoryColumns::GRAPH))
      paintGraph(p, newOpt, commit);
   else if (index.column() == static_cast<int>(CommitHistoryColumns::LOG))
      paintLog(p, newOpt, commit, index.data().toString());
   else
   {
      p->setPen(GitQlientStyles::getTextColor());
      newOpt.rect.setX(newOpt.rect.x() + 10);

      auto text = index.data().toString();

      if (index.column() == static_cast<int>(CommitHistoryColumns::SHA))
      {
         newOpt.font.setPointSize(10);
         newOpt.font.setFamily("Ubuntu Mono");
         text = text.left(8);
      }

      QFontMetrics fm(newOpt.font);
      p->setFont(newOpt.font);
      p->drawText(newOpt.rect, fm.elidedText(text, Qt::ElideRight, newOpt.rect.width()),
                  QTextOption(Qt::AlignLeft | Qt::AlignVCenter));
   }
}

QSize RepositoryViewDelegate::sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const
{
   return QSize(LANE_WIDTH, ROW_HEIGHT);
}

void RepositoryViewDelegate::paintGraphLane(QPainter *p, const Lane &lane, bool laneHeadPresent, int x1, int x2,
                                            const QColor &col, const QColor &activeCol, const QColor &mergeColor,
                                            bool isWip, bool hasChilds) const
{
   const auto padding = 2;
   x1 += padding;
   x2 += padding;

   const auto h = ROW_HEIGHT / 2;
   const auto m = (x1 + x2) / 2;
   const auto r = (x2 - x1) * 1 / 3;
   const auto spanAngle = 90 * 16;
   const auto angleWidthRight = 2 * (x1 - m);
   const auto angleWidthLeft = 2 * (x2 - m);
   const auto angleHeightUp = 2 * h;
   const auto angleHeightDown = 2 * -h;

   static QPen lanePen(GitQlientStyles::getTextColor(), 2); // fast path here

   // arc
   lanePen.setBrush(col);
   p->setPen(lanePen);

   switch (lane.getType())
   {
      case LaneType::JOIN:
      case LaneType::JOIN_R:
      case LaneType::HEAD:
      case LaneType::HEAD_R: {
         p->drawArc(m, h, angleWidthRight, angleHeightUp, 0 * 16, spanAngle);
         break;
      }
      case LaneType::JOIN_L: {
         p->drawArc(m, h, angleWidthLeft, angleHeightUp, 90 * 16, spanAngle);
         break;
      }
      case LaneType::TAIL:
      case LaneType::TAIL_R: {
         p->drawArc(m, h, angleWidthRight, angleHeightDown, 270 * 16, spanAngle);
         break;
      }
      default:
         break;
   }

   if (isWip)
   {
      lanePen.setColor(activeCol);
      p->setPen(lanePen);
   }

   // vertical line
   if (!(isWip && !hasChilds))
   {
      if (!isWip && !hasChilds
          && (lane.getType() == LaneType::HEAD || lane.getType() == LaneType::INITIAL
              || lane.getType() == LaneType::BRANCH || lane.getType() == LaneType::MERGE_FORK
              || lane.getType() == LaneType::MERGE_FORK_R || lane.getType() == LaneType::MERGE_FORK_L
              || lane.getType() == LaneType::ACTIVE))
         p->drawLine(m, h, m, 2 * h);
      else
      {
         switch (lane.getType())
         {
            case LaneType::ACTIVE:
            case LaneType::NOT_ACTIVE:
            case LaneType::MERGE_FORK:
            case LaneType::MERGE_FORK_R:
            case LaneType::MERGE_FORK_L:
            case LaneType::JOIN:
            case LaneType::JOIN_R:
            case LaneType::JOIN_L:
            case LaneType::CROSS:
               p->drawLine(m, 0, m, 2 * h);
               break;
            case LaneType::HEAD_L:
            case LaneType::BRANCH:
               p->drawLine(m, h, m, 2 * h);
               break;
            case LaneType::TAIL_L:
            case LaneType::INITIAL:
               p->drawLine(m, 0, m, h);
               break;
            default:
               break;
         }
      }
   }

   // center symbol, e.g. rect or ellipse
   auto isCommit = false;
   switch (lane.getType())
   {
      case LaneType::HEAD:
      case LaneType::INITIAL:
      case LaneType::BRANCH:
      case LaneType::MERGE_FORK:
      case LaneType::MERGE_FORK_R:
         isCommit = true;
         p->setPen(QPen(mergeColor, 2));
         p->setBrush(col);
         p->drawEllipse(m - r + 2, h - r + 2, 8, 8);
         break;
      case LaneType::MERGE_FORK_L:
         isCommit = true;
         p->setPen(QPen(laneHeadPresent ? mergeColor : col, 2));
         p->setBrush(col);
         p->drawEllipse(m - r + 2, h - r + 2, 8, 8);
         break;
      case LaneType::ACTIVE: {
         isCommit = true;
         p->setPen(QPen(col, 2));
         p->setBrush(QColor(isWip ? col : GitQlientStyles::getBackgroundColor()));
         p->drawEllipse(m - r + 2, h - r + 2, 8, 8);
      }
      break;
      default:
         break;
   }

   lanePen.setColor(mergeColor);
   p->setPen(lanePen);

   // horizontal line
   switch (lane.getType())
   {
      case LaneType::MERGE_FORK:
      case LaneType::JOIN:
      case LaneType::HEAD:
      case LaneType::TAIL:
      case LaneType::CROSS:
      case LaneType::CROSS_EMPTY:
         p->drawLine(x1 + (isCommit ? 10 : 0), h, x2, h);
         break;
      case LaneType::MERGE_FORK_R:
         p->drawLine(x1 + (isCommit ? 0 : 10), h, m - (isCommit ? 6 : 0), h);
         break;
      case LaneType::MERGE_FORK_L:
      case LaneType::HEAD_L:
      case LaneType::TAIL_L:
         p->drawLine(m + (isCommit ? 6 : 0), h, x2, h);
         break;
      default:
         break;
   }
}

QColor RepositoryViewDelegate::getMergeColor(const Lane &currentLane, const CommitInfo &commit, int currentLaneIndex,
                                             const QColor &defaultColor, bool &isSet) const
{
   auto mergeColor = defaultColor;
   //= GitQlientStyles::getBranchColorAt((commit.getLanesCount() - 1) % GitQlientStyles::getTotalBranchColors());

   switch (currentLane.getType())
   {
      case LaneType::HEAD_L:
      case LaneType::HEAD_R:
      case LaneType::TAIL_L:
      case LaneType::TAIL_R:
      case LaneType::MERGE_FORK_L:
      case LaneType::JOIN_R:
         isSet = true;
         mergeColor = defaultColor;
         break;
      case LaneType::MERGE_FORK_R:
      case LaneType::JOIN_L:
         for (auto laneCount = 0; laneCount < currentLaneIndex; ++laneCount)
         {
            if (commit.getLane(laneCount).equals(LaneType::JOIN_L))
            {
               mergeColor = GitQlientStyles::getBranchColorAt(laneCount % GitQlientStyles::getTotalBranchColors());
               isSet = true;
               break;
            }
         }
         break;
      default:
         break;
   }

   return mergeColor;
}

void RepositoryViewDelegate::paintGraph(QPainter *p, const QStyleOptionViewItem &opt, const CommitInfo &commit) const
{
   p->save();
   p->setClipRect(opt.rect, Qt::IntersectClip);
   p->translate(opt.rect.topLeft());

   if (mView->hasActiveFilter())
   {
      const auto activeColor = GitQlientStyles::getBranchColorAt(0);
      paintGraphLane(p, LaneType::ACTIVE, false, 0, LANE_WIDTH, activeColor, activeColor, activeColor, false,
                     commit.hasChilds());
   }
   else
   {
      if (commit.sha() == CommitInfo::ZERO_SHA)
      {
         const auto activeColor = GitQlientStyles::getBranchColorAt(0);
         QColor color = activeColor;

         if (mCache->pendingLocalChanges())
            color = QColor("#D89000");

         paintGraphLane(p, LaneType::BRANCH, false, 0, LANE_WIDTH, color, activeColor, activeColor, true,
                        commit.parentsCount() != 0);
      }
      else
      {
         const auto laneNum = commit.getLanesCount();
         const auto activeLane = commit.getActiveLane();
         const auto activeColor
             = GitQlientStyles::getBranchColorAt(activeLane % GitQlientStyles::getTotalBranchColors());
         auto x1 = 0;
         auto isSet = false;
         auto laneHeadPresent = false;
         auto mergeColor = GitQlientStyles::getBranchColorAt((laneNum - 1) % GitQlientStyles::getTotalBranchColors());

         for (auto i = laneNum - 1, x2 = LANE_WIDTH * laneNum; i >= 0; --i, x2 -= LANE_WIDTH)
         {
            x1 = x2 - LANE_WIDTH;

            auto currentLane = commit.getLane(i);

            if (!laneHeadPresent && i < laneNum - 1)
            {
               auto prevLane = commit.getLane(i + 1);
               laneHeadPresent
                   = prevLane.isHead() || prevLane.equals(LaneType::JOIN_R) || prevLane.equals(LaneType::JOIN_L);
            }

            if (!currentLane.equals(LaneType::EMPTY))
            {
               auto color = activeColor;

               if (i != activeLane)
                  color = GitQlientStyles::getBranchColorAt(i % GitQlientStyles::getTotalBranchColors());

               if (!isSet)
                  mergeColor = getMergeColor(currentLane, commit, i, color, isSet);

               paintGraphLane(p, currentLane, laneHeadPresent, x1, x2, color, activeColor, mergeColor, false,
                              commit.hasChilds());

               if (mView->hasActiveFilter())
                  break;
            }
         }
      }
   }
   p->restore();
}

void RepositoryViewDelegate::paintLog(QPainter *p, const QStyleOptionViewItem &opt, const CommitInfo &commit,
                                      const QString &text) const
{
   const auto sha = commit.sha();

   if (sha.isEmpty())
      return;

   auto offset = 0;

   if (commit.hasReferences() && !mView->hasActiveFilter())
   {
      offset = 5;
      paintTagBranch(p, opt, offset, sha);
   }

   auto newOpt = opt;
   newOpt.rect.setX(opt.rect.x() + offset + 5);

   QFontMetrics fm(newOpt.font);

   p->setFont(newOpt.font);
   p->setPen(GitQlientStyles::getTextColor());
   p->drawText(newOpt.rect, fm.elidedText(text, Qt::ElideRight, newOpt.rect.width()),
               QTextOption(Qt::AlignLeft | Qt::AlignVCenter));
}

void RepositoryViewDelegate::paintTagBranch(QPainter *painter, QStyleOptionViewItem o, int &startPoint,
                                            const QString &sha) const
{
   QMap<QString, QColor> markValues;
   const auto currentBranch = mGit->getCurrentBranch();
   const auto commit = mCache->getCommitInfo(sha);

   if ((currentBranch.isEmpty() || currentBranch == "HEAD"))
   {
      if (const auto ret = mGit->getLastCommit(); ret.success && commit.sha() == ret.output.toString().trimmed())
         markValues.insert("detached", GitQlientStyles::getDetachedColor());
   }

   if (commit.hasReferences())
   {
      const auto localBranches = commit.getReferences(References::Type::LocalBranch);
      for (const auto &branch : localBranches)
         markValues.insert(branch,
                           branch == currentBranch ? GitQlientStyles::getCurrentBranchColor()
                                                   : GitQlientStyles::getLocalBranchColor());

      const auto remoteBranches = commit.getReferences(References::Type::RemoteBranches);
      for (const auto &branch : remoteBranches)
         markValues.insert(branch, QColor("#011f4b"));

      const auto tags = commit.getReferences(References::Type::LocalTag);
      for (const auto &tag : tags)
         markValues.insert(tag, GitQlientStyles::getTagColor());
   }

   const auto showMinimal = o.rect.width() <= MIN_VIEW_WIDTH_PX;
   const int mark_spacing = 5; // Space between markers in pixels
   const auto mapEnd = markValues.constEnd();

   for (auto mapIt = markValues.constBegin(); mapIt != mapEnd; ++mapIt)
   {
      const auto isCurrentSpot = mapIt.key() == "detached" || mapIt.key() == currentBranch;
      o.font.setBold(isCurrentSpot);

      const auto nameToDisplay = showMinimal ? QString(". . .") : mapIt.key();
      QFontMetrics fm(o.font);
      const auto textBoundingRect = fm.boundingRect(nameToDisplay);
      const int textPadding = 3;
      const auto rectWidth = textBoundingRect.width() + 2 * textPadding;

      painter->save();
      painter->setRenderHint(QPainter::Antialiasing);
      painter->setPen(QPen(mapIt.value(), 2));
      QPainterPath path;
      path.addRoundedRect(QRectF(o.rect.x() + startPoint, o.rect.y() + 4, rectWidth, ROW_HEIGHT - 8), 1, 1);
      painter->fillPath(path, mapIt.value());
      painter->drawPath(path);

      // TODO: Fix this with a nicer way
      painter->setPen(QColor(mapIt.value() == QColor("#dec3c3") ? QString("#000000") : QString("#FFFFFF")));

      const auto fontRect = textBoundingRect.height();
      const auto y = o.rect.y() + ROW_HEIGHT - (ROW_HEIGHT - fontRect) + 2;
      painter->setFont(o.font);
      painter->drawText(o.rect.x() + startPoint + textPadding, y, nameToDisplay);
      painter->restore();

      startPoint += rectWidth + mark_spacing;
   }
}
