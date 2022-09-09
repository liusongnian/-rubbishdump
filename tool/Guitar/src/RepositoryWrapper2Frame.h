#ifndef REPOSITORYWRAPPER2FRAME_H
#define REPOSITORYWRAPPER2FRAME_H

#include "BranchLabel.h"
#include "Git.h"
#include "GitObjectManager.h"

#include <QFrame>

class SubmoduleMainWindow;
class LogTable2Widget;
class FilesListWidget;
class FileDiff2Widget;

class RepositoryWrapper2Frame : public QFrame {
	Q_OBJECT
	friend class SubmoduleMainWindow;
private:
	Git::CommitItemList logs;

	SubmoduleMainWindow *mw_ = nullptr;
    LogTable2Widget *logtablewidget_ = nullptr;
	FilesListWidget *fileslistwidget_ = nullptr;
	FilesListWidget *unstagedfileslistwidget_ = nullptr;
	FilesListWidget *stagesfileslistwidget_ = nullptr;
    FileDiff2Widget *filediffwidget_ = nullptr;

	std::map<QString, QList<Git::Branch>> branch_map;
	std::map<QString, QList<Git::Tag>> tag_map;
	std::map<int, QList<BranchLabel>> label_map;
	std::map<QString, Git::Diff> diff_cache;

	GitObjectCache objcache;

    SubmoduleMainWindow *submodulemainwindow();
    SubmoduleMainWindow const *submodulemainwindow() const;
public:
	explicit RepositoryWrapper2Frame(QWidget *parent = nullptr);
	Git::CommitItem const *commitItem(int row);
	QIcon verifiedIcon(char s) const;
	QIcon committerIcon(int row) const;
	const QList<BranchLabel> *label(int row) const;
	QString currentBranchName() const;
	const Git::CommitItemList &getLogs() const;
	bool isAncestorCommit(const QString &id);
	QColor color(unsigned int i);
	void updateAncestorCommitMap();
	void bind(SubmoduleMainWindow *mw
              , LogTable2Widget *logtablewidget
			  , FilesListWidget *fileslistwidget
			  , FilesListWidget *unstagedfileslistwidget
			  , FilesListWidget *stagesfileslistwidget
              , FileDiff2Widget *filediffwidget
			  );

	void prepareLogTableWidget();
	void clearLogContents();
    LogTable2Widget *logtablewidget();
	FilesListWidget *fileslistwidget();
	FilesListWidget *unstagedFileslistwidget();
    FileDiff2Widget *filediffwidget();
	FilesListWidget *stagedFileslistwidget();
	void updateLogTableView();
	void setFocusToLogTable();
	void selectLogTableRow(int row);
};

struct RepositoryWrapper2FrameP {
	RepositoryWrapper2Frame *pointer;
	RepositoryWrapper2FrameP(RepositoryWrapper2Frame *pointer = nullptr)
		: pointer(pointer)
	{
	}
};

#endif // REPOSITORYWRAPPER2FRAME_H
