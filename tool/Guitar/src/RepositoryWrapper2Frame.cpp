#include "LogTable2Widget.h"
#include "SubmoduleMainWindow.h"
#include "RepositoryWrapper2Frame.h"

RepositoryWrapper2Frame::RepositoryWrapper2Frame(QWidget *parent)
	: QFrame(parent)
{

}

void RepositoryWrapper2Frame::bind(SubmoduleMainWindow *mw, LogTable2Widget *logtablewidget, FilesListWidget *fileslistwidget, FilesListWidget *unstagedfileslistwidget, FilesListWidget *stagesfileslistwidget, FileDiff2Widget *filediffwidget)
{
	mw_ = mw;
    logtablewidget_ = logtablewidget;
	fileslistwidget_ = fileslistwidget;
	unstagedfileslistwidget_ = unstagedfileslistwidget;
	stagesfileslistwidget_ = stagesfileslistwidget;
	filediffwidget_ = filediffwidget;
	logtablewidget->bind(this);
}

SubmoduleMainWindow *RepositoryWrapper2Frame::submodulemainwindow()
{
	Q_ASSERT(mw_);
	return mw_;
}

SubmoduleMainWindow const *RepositoryWrapper2Frame::submodulemainwindow() const
{
	Q_ASSERT(mw_);
	return mw_;
}

LogTable2Widget *RepositoryWrapper2Frame::logtablewidget()
{
	return logtablewidget_;
}

FilesListWidget *RepositoryWrapper2Frame::fileslistwidget()
{
	return fileslistwidget_;
}

FilesListWidget *RepositoryWrapper2Frame::unstagedFileslistwidget()
{
	return unstagedfileslistwidget_;
}

FilesListWidget *RepositoryWrapper2Frame::stagedFileslistwidget()
{
	return stagesfileslistwidget_;
}

FileDiff2Widget *RepositoryWrapper2Frame::filediffwidget()
{
	return filediffwidget_;
}

const Git::CommitItem *RepositoryWrapper2Frame::commitItem(int row)
{
    return submodulemainwindow()->commitItem(submodulemainwindow()->frame(), row);
}

QIcon RepositoryWrapper2Frame::verifiedIcon(char s) const
{
    return submodulemainwindow()->verifiedIcon(s);
}

QIcon RepositoryWrapper2Frame::committerIcon(int row) const
{
    return submodulemainwindow()->committerIcon(const_cast<RepositoryWrapper2Frame *>(this), row);
}

QList<BranchLabel> const *RepositoryWrapper2Frame::label(int row) const
{
    return submodulemainwindow()->label(this, row);
}

QString RepositoryWrapper2Frame::currentBranchName() const
{
    return submodulemainwindow()->currentBranchName();
}

const Git::CommitItemList &RepositoryWrapper2Frame::getLogs() const
{
    return submodulemainwindow()->getLogs(this);
}

bool RepositoryWrapper2Frame::isAncestorCommit(const QString &id)
{
    return submodulemainwindow()->isAncestorCommit(id);
}

QColor RepositoryWrapper2Frame::color(unsigned int i)
{
    return submodulemainwindow()->color(i);
}

void RepositoryWrapper2Frame::updateAncestorCommitMap()
{
    submodulemainwindow()->updateAncestorCommitMap(this);
}

void RepositoryWrapper2Frame::updateLogTableView()
{
	logtablewidget_->viewport()->update();
}

void RepositoryWrapper2Frame::setFocusToLogTable()
{
	logtablewidget_->setFocus();
}

void RepositoryWrapper2Frame::selectLogTableRow(int row)
{
	logtablewidget_->selectRow(row);
}

void RepositoryWrapper2Frame::prepareLogTableWidget()
{
	QStringList cols = {
		tr("Graph"),
		tr("Commit"),
		tr("Date"),
		tr("Author"),
		tr("Message"),
	};
	int n = cols.size();
	logtablewidget_->setColumnCount(n);
	logtablewidget_->setRowCount(0);
	for (int i = 0; i < n; i++) {
		QString const &text = cols[i];
		auto *item = new QTableWidgetItem(text);
		logtablewidget_->setHorizontalHeaderItem(i, item);
	}

    submodulemainwindow()->updateCommitGraph(this); // コミットグラフを更新
}

void RepositoryWrapper2Frame::clearLogContents()
{
	logtablewidget_->clearContents();
	logtablewidget_->scrollToTop();
}
