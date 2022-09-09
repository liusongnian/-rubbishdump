#ifndef SUBMODULEMAINWINDOW_H
#define SUBMODULEMAINWINDOW_H

#include "BranchLabel.h"
#include "Git.h"
#include "MyProcess.h"
#include "RepositoryData.h"
#include "RepositoryWrapper2Frame.h"
#include <QMainWindow>

class AvatarLoader;
class GitObjectCache;
class LogTable2Widget;
class QListWidget;
class QListWidgetItem;
class QTableWidgetItem;
class QTreeWidgetItem;
class RepositoryWrapper2Frame;
class WebContext;
struct GitHubRepositoryInfo;

namespace Ui {
class SubmoduleMainWindow;
}

class SubmoduleMainWindow : public QMainWindow {
	Q_OBJECT
	friend class RepositoryWrapper2Frame;
	friend class MainWindow;
    friend class ImageView2Widget;
	friend class FileDiffSliderWidget;
	friend class FileHistoryWindow;
    friend class FileDiff2Widget;
	friend class AboutDialog;
public:
	enum {
		IndexRole = Qt::UserRole,
		FilePathRole,
		DiffIndexRole,
		ObjectIdRole,
		HeaderRole,
		SubmodulePathRole,
	};
	enum CloneOperation {
		Clone,
		SubmoduleAdd,
	};

	enum class PtyCondition {
		None,
		Clone,
		Fetch,
		Pull,
		Push,
	};

	enum InteractionMode {
		None,
		Busy,
	};

	enum NamedCommitFlag {
		Branches = 0x0001,
		Tags     = 0x0002,
		Remotes  = 0x0100,
	};

	enum class FilesListType {
		SingleList,
		SideBySide,
	};

	struct Task {
		int index = 0;
		int parent = 0;
		Task() = default;
		Task(int index, int parent)
			: index(index)
			, parent(parent)
		{
		}
	};

	struct Element {
		int depth = 0;
		std::vector<int> indexes;
	};

	enum {
		GroupItem = -1,
	};

private:

	struct ObjectData {
		QString id;
		QString path;
		Git::SubmoduleItem submod;
		Git::CommitItem submod_commit;
		QString header;
		int idiff;
	};

    Ui::SubmoduleMainWindow *ui;

	struct Private;
	Private *m;

public:
	explicit SubmoduleMainWindow(QWidget *parent = nullptr);
	~SubmoduleMainWindow() override;

	RepositoryWrapper2Frame *frame();
	RepositoryWrapper2Frame const *frame() const;

	QPixmap const &digitsPixmap() const;

	QColor color(unsigned int i);

	bool isOnlineMode() const;
private:

	void postEvent(QObject *receiver, QEvent *event, int ms_later);
	void postUserFunctionEvent(const std::function<void (const QVariant &, void *)> &fn, QVariant const &v = QVariant(), void *p = nullptr, int ms_later = 0);

	void updateFilesList(RepositoryWrapper2Frame *frame, QString id, bool wait);
	void updateFilesList(RepositoryWrapper2Frame *frame, Git::CommitItem const &commit, bool wait);
	void updateRepositoriesList();

	void openRepository_(GitPtr g, bool keep_selection = false);
	void openRepository_(RepositoryWrapper2Frame *frame, GitPtr g, bool keep_selection = false);

	QStringList selectedFiles_(QListWidget *listwidget) const;
	QStringList selectedFiles() const;
	void for_each_selected_files(std::function<void (QString const &)> const &fn);
	void showFileList(FilesListType files_list_type);

	void clearLog(RepositoryWrapper2Frame *frame);
	void clearFileList(RepositoryWrapper2Frame *frame);
	void clearDiffView(RepositoryWrapper2Frame *frame);
	void clearRepositoryInfo();

	int repositoryIndex_(const QTreeWidgetItem *item) const;
	RepositoryItem const *repositoryItem(const QTreeWidgetItem *item) const;

	QTreeWidgetItem *newQTreeWidgetFolderItem(QString const &name);
	void buildRepoTree(QString const &group, QTreeWidgetItem *item, QList<RepositoryItem> *repos);
	void refrectRepositories();

	void updateDiffView(RepositoryWrapper2Frame *frame, QListWidgetItem *item);
	void updateDiffView(RepositoryWrapper2Frame *frame);
	void updateUnstagedFileCurrentItem(RepositoryWrapper2Frame *frame);
	void updateStagedFileCurrentItem(RepositoryWrapper2Frame *frame);
	void updateStatusBarText(RepositoryWrapper2Frame *frame);
	void setRepositoryInfo(QString const &reponame, QString const &brname);
	int indexOfRepository(const QTreeWidgetItem *treeitem) const;
	void clearRepoFilter();
	void appendCharToRepoFilter(ushort c);
	void backspaceRepoFilter();
	void revertCommit(RepositoryWrapper2Frame *frame);
	void mergeBranch(const QString &commit, Git::MergeFastForward ff, bool squash);
	void mergeBranch(Git::CommitItem const *commit, Git::MergeFastForward ff, bool squash);
	void rebaseBranch(Git::CommitItem const *commit);
	void cherrypick(Git::CommitItem const *commit);
	void merge(RepositoryWrapper2Frame *frame, const Git::CommitItem *commit = nullptr);
    void detectGitServerType(GitPtr g);
	void setRemoteOnline(bool f, bool save);
	void startTimers();
	void onCloneCompleted(bool success, const QVariant &userdata);
	void setNetworkingCommandsEnabled(bool enabled);
	void blame(QListWidgetItem *item);
	void blame();
	QListWidgetItem *currentFileItem() const;
	void execAreYouSureYouWantToContinueConnectingDialog();
	void deleteRemoteBranch(RepositoryWrapper2Frame *frame, Git::CommitItem const *commit);
	QStringList remoteBranches(RepositoryWrapper2Frame *frame, QString const &id, QStringList *all);
	bool isUninitialized();
	void doLogCurrentItemChanged(RepositoryWrapper2Frame *frame);
	void findNext(RepositoryWrapper2Frame *frame);
	void findText(const QString &text);
	void showStatus();
	void onStartEvent();
	void showLogWindow(bool show);
	bool isValidRemoteURL(const QString &url, const QString &sshkey);
	QStringList whichCommand_(const QString &cmdfile1, const QString &cmdfile2 = {});
	QString selectCommand_(const QString &cmdname, const QStringList &cmdfiles, const QStringList &list, QString path, const std::function<void (const QString &)> &callback);
	QString selectCommand_(const QString &cmdname, const QString &cmdfile, const QStringList &list, const QString &path, const std::function<void (const QString &)> &callback);
	const RepositoryItem *findRegisteredRepository(QString *workdir) const;
	static bool git_callback(void *cookie, const char *ptr, int len);
	bool execSetGlobalUserDialog();
	void revertAllFiles();
	void addWorkingCopyDir(QString dir, QString name, bool open);
	bool execWelcomeWizardDialog();
	void execRepositoryPropertyDialog(const RepositoryItem &repo, bool open_repository_menu = false);
	void execSetUserDialog(const Git::User &global_user, const Git::User &repo_user, const QString &reponame);
	void setGitCommand(const QString &path, bool save);
	void setGpgCommand(const QString &path, bool save);
	void setSshCommand(const QString &path, bool save);
	bool checkGitCommand();
	bool saveBlobAs(RepositoryWrapper2Frame *frame, const QString &id, const QString &dstpath);
	bool saveByteArrayAs(const QByteArray &ba, const QString &dstpath);
	static QString makeRepositoryName(const QString &loc);
	bool saveFileAs(const QString &srcpath, const QString &dstpath);
	QString saveAsTemp(RepositoryWrapper2Frame *frame, const QString &id);
	QString executableOrEmpty(const QString &path);
	bool checkExecutable(const QString &path);
	void internalSaveCommandPath(const QString &path, bool save, const QString &name);
	void logGitVersion();
	void internalClearRepositoryInfo();
	void checkUser();
	void openRepository(bool validate, bool waitcursor = true, bool keep_selection = false);
	void updateRepository();
    void reopenRepository(bool log, const std::function<void (GitPtr )> &callback);
	void setCurrentRepository(const RepositoryItem &repo, bool clear_authentication);
	void openSelectedRepository();
	QList<Git::Diff> makeDiffs(RepositoryWrapper2Frame *frame, QString id, bool *ok);
    void queryBranches(RepositoryWrapper2Frame *frame, GitPtr g);
	void updateRemoteInfo();
    void queryRemotes(GitPtr g);
	void clone(QString url = {}, QString dir = {});
    void submodule_add(QString url = {}, const QString &local_dir = {});
    const Git::CommitItem *selectedCommitItem(RepositoryWrapper2Frame *frame) const;
	void commit(RepositoryWrapper2Frame *frame, bool amend = false);
	void commitAmend(RepositoryWrapper2Frame *frame);
	void pushSetUpstream(bool set_upstream, const QString &remote, const QString &branch, bool force);
	bool pushSetUpstream(bool testonly);
	void push();
	void deleteBranch(RepositoryWrapper2Frame *frame, const Git::CommitItem *commit);	void deleteBranch(RepositoryWrapper2Frame *frame);
	void resetFile(const QStringList &paths);
	void clearAuthentication();
	void clearSshAuthentication();
	void internalDeleteTags(const QStringList &tagnames);
	bool internalAddTag(RepositoryWrapper2Frame *frame, const QString &name);
	void createRepository(const QString &dir);
    void setLogEnabled(GitPtr g, bool f);
	void doGitCommand(const std::function<void (GitPtr)> &callback);
	void setWindowTitle_(const Git::User &user);
	void setUnknownRepositoryInfo();
	void setCurrentRemoteName(const QString &name);
	void deleteTags(RepositoryWrapper2Frame *frame, const Git::CommitItem &commit);
	bool isAvatarEnabled() const;
	bool isGitHub() const;
	QStringList remotes() const;
	QList<Git::Branch> findBranch(RepositoryWrapper2Frame *frame, const QString &id);
	QString tempfileHeader() const;
	void deleteTempFiles();
	QString getCommitIdFromTag(RepositoryWrapper2Frame *frame, const QString &tag);
	QString newTempFilePath();
	int limitLogCount() const;
    Git::Object cat_file_(RepositoryWrapper2Frame *frame, GitPtr g, const QString &id);
	bool isThereUncommitedChanges() const;
	static void addDiffItems(const QList<Git::Diff> *diff_list, const std::function<void (const ObjectData &)> &add_item);
    Git::CommitItemList retrieveCommitLog(GitPtr g);
	std::map<QString, QList<Git::Branch> > &branchMapRef(RepositoryWrapper2Frame *frame);
	void updateCommitLogTableLater(RepositoryWrapper2Frame *frame, int ms_later);
    void updateWindowTitle(GitPtr g);
	QString makeCommitInfoText(RepositoryWrapper2Frame *frame, int row, QList<BranchLabel> *label_list);
	void removeRepositoryFromBookmark(int index, bool ask);
	void openTerminal(const RepositoryItem *repo);
	void openExplorer(const RepositoryItem *repo);
	bool askAreYouSureYouWantToRun(const QString &title, const QString &command);
	bool editFile(const QString &path, const QString &title);
	void setAppSettings(const ApplicationSettings &appsettings);
	QIcon getRepositoryIcon() const;
	QIcon getFolderIcon() const;
	QIcon getSignatureGoodIcon() const;
	QIcon getSignatureDubiousIcon() const;
	QIcon getSignatureBadIcon() const;
	QPixmap getTransparentPixmap() const;
	QStringList findGitObject(const QString &id) const;
	void writeLog(const char *ptr, int len);
	void writeLog(const QString &str);
	QList<BranchLabel> sortedLabels(RepositoryWrapper2Frame *frame, int row) const;
	void saveApplicationSettings();
	void loadApplicationSettings();
	void setDiffResult(const QList<Git::Diff> &diffs);
	const QList<Git::SubmoduleItem> &submodules() const;
	void setSubmodules(const QList<Git::SubmoduleItem> &submodules);
	bool runOnRepositoryDir(const std::function<void (QString, QString)> &callback, const RepositoryItem *repo);
	NamedCommitList namedCommitItems(RepositoryWrapper2Frame *frame, int flags);
	static QString getObjectID(QListWidgetItem *item);
	static QString getFilePath(QListWidgetItem *item);
	static QString getSubmodulePath(QListWidgetItem *item);
	static bool isGroupItem(QTreeWidgetItem *item);
	static int indexOfLog(QListWidgetItem *item);
	static int indexOfDiff(QListWidgetItem *item);
	static void updateSubmodules(GitPtr g, const QString &id, QList<Git::SubmoduleItem> *out);
	void saveRepositoryBookmark(RepositoryItem item);
	void changeRepositoryBookmarkName(RepositoryItem item, QString new_name);
	int rowFromCommitId(RepositoryWrapper2Frame *frame, const QString &id);
	QList<Git::Tag> findTag(RepositoryWrapper2Frame *frame, const QString &id);
	void sshSetPassphrase(const std::string &user, const std::string &pass);
	std::string sshPassphraseUser() const;
	std::string sshPassphrasePass() const;
	void httpSetAuthentication(const std::string &user, const std::string &pass);
	std::string httpAuthenticationUser() const;
	std::string httpAuthenticationPass() const;
    const Git::CommitItem *getLog(RepositoryWrapper2Frame const *frame, int index) const;
    void updateCommitGraph(RepositoryWrapper2Frame *frame);
	void initNetworking();
	bool saveRepositoryBookmarks() const;
	QString getBookmarksFilePath() const;
	void stopPtyProcess();
	void abortPtyProcess();
    Git::CommitItemList *getLogsPtr(RepositoryWrapper2Frame *frame);
    void setLogs(RepositoryWrapper2Frame *frame, const Git::CommitItemList &logs);
    void clearLogs(RepositoryWrapper2Frame *frame);
	PtyProcess *getPtyProcess();
	bool getPtyProcessOk() const;
	PtyCondition getPtyCondition();
	void setPtyUserData(const QVariant &userdata);
	void setPtyProcessOk(bool pty_process_ok);
    bool fetch(GitPtr g, bool prune);
    bool fetch_tags_f(GitPtr g);
	void setPtyCondition(const PtyCondition &ptyCondition);
	const QList<RepositoryItem> &getRepos() const;
	QList<RepositoryItem> *getReposPtr();
	AvatarLoader *getAvatarLoader();
	const AvatarLoader *getAvatarLoader() const;
	bool interactionCanceled() const;
	void setInteractionCanceled(bool canceled);
	InteractionMode interactionMode() const;
	void setInteractionMode(const InteractionMode &im);
	QString getRepositoryFilterText() const;
	void setRepositoryFilterText(const QString &text);
	void setUncommitedChanges(bool uncommited_changes);
	QList<Git::Diff> *diffResult();
    std::map<QString, Git::Diff> *getDiffCacheMap(RepositoryWrapper2Frame *frame);
	bool getRemoteChanged() const;
	void setRemoteChanged(bool remote_changed);
	void setServerType(const ServerType &server_type);
	GitHubRepositoryInfo *ptrGitHub();
    std::map<int, QList<BranchLabel> > *getLabelMap(RepositoryWrapper2Frame *frame);
    const std::map<int, QList<BranchLabel> > *getLabelMap(const RepositoryWrapper2Frame *frame) const;
    void clearLabelMap(RepositoryWrapper2Frame *frame);
    GitObjectCache *getObjCache(RepositoryWrapper2Frame *frame);
	bool getForceFetch() const;
	void setForceFetch(bool force_fetch);
    std::map<QString, QList<Git::Tag> > *ptrTagMap(RepositoryWrapper2Frame *frame);
	QString getHeadId() const;
	void setHeadId(const QString &head_id);
	void setPtyProcessCompletionData(const QVariant &value);
	const QVariant &getTempRepoForCloneCompleteV() const;
	void msgNoRepositorySelected();
	bool isRepositoryOpened() const;
	static std::pair<QString, QString> makeFileItemText(const ObjectData &data);
	QString gitCommand() const;
	QPixmap getTransparentPixmap();
    static QListWidgetItem *NewListWidgetFileItem(const SubmoduleMainWindow::ObjectData &data);
	void cancelPendingUserEvents();
protected:
	void customEvent(QEvent *) override;
	void dragEnterEvent(QDragEnterEvent *event) override;
	void keyPressEvent(QKeyEvent *event) override;
	bool event(QEvent *event) override;
	bool eventFilter(QObject *watched, QEvent *event) override;
public:
	void drawDigit(QPainter *pr, int x, int y, int n) const;
	int digitWidth() const;
	int digitHeight() const;
	void setStatusBarText(QString const &text);
	void clearStatusBarText();
    void setCurrentLogRow(RepositoryWrapper2Frame *frame, int row);
	bool shown();
    void deleteTags(RepositoryWrapper2Frame *frame, QStringList const &tagnames);
    bool addTag(RepositoryWrapper2Frame *frame, QString const &name);
    void updateCurrentFilesList(RepositoryWrapper2Frame *frame);
	void postOpenRepositoryFromGitHub(const QString &username, const QString &reponame);
    int selectedLogIndex(RepositoryWrapper2Frame *frame) const;
    void updateAncestorCommitMap(RepositoryWrapper2Frame *frame);
	bool isAncestorCommit(const QString &id);
	void postStartEvent(int ms_later);
	void setShowLabels(bool show, bool save);
	bool isLabelsVisible() const;
    void updateFilesList2(RepositoryWrapper2Frame *frame, const QString &id, QList<Git::Diff> *diff_list, QListWidget *listwidget);
	void execCommitViewWindow(const Git::CommitItem *commit);
    void execCommitPropertyDialog(QWidget *parent, RepositoryWrapper2Frame *frame, const Git::CommitItem *commit);
    void execCommitExploreWindow(RepositoryWrapper2Frame *frame, QWidget *parent, const Git::CommitItem *commit);
	void execFileHistory(const QString &path);
	void execFileHistory(QListWidgetItem *item);
	void showObjectProperty(QListWidgetItem *item);
	void showSubmoduleMainWindow(const QString submodpath);
	void openPatchRepository();
	bool testRemoteRepositoryValidity(const QString &url, const QString &sshkey);
	QString selectGitCommand(bool save);
	QString selectGpgCommand(bool save);
	QString selectSshCommand(bool save);
	const Git::Branch &currentBranch() const;
	void setCurrentBranch(const Git::Branch &b);
	const RepositoryItem &currentRepository() const;
	QString currentRepositoryName() const;
	QString currentRemoteName() const;
	QString currentBranchName() const;
	GitPtr git(const QString &dir, const QString &submodpath, const QString &sshkey) const;
	GitPtr git();
	GitPtr git(Git::SubmoduleItem const &submod);
	void autoOpenRepository(QString dir);
	bool queryCommit(const QString &id, Git::CommitItem *out);
    void checkout(RepositoryWrapper2Frame *frame, QWidget *parent, const Git::CommitItem *commit, std::function<void ()> accepted_callback = {});
    void checkout(RepositoryWrapper2Frame *frame);
    void jumpToCommit(RepositoryWrapper2Frame *frame, QString id);
    Git::Object cat_file(RepositoryWrapper2Frame *frame, const QString &id);
	void addWorkingCopyDir(const QString &dir, bool open);
    bool saveAs(RepositoryWrapper2Frame *frame, const QString &id, const QString &dstpath);
	QString determinFileType(QByteArray in);
    QList<Git::Tag> queryTagList(RepositoryWrapper2Frame *frame);
	TextEditorThemePtr themeForTextEditor();
    bool isValidWorkingCopy(GitPtr g) const;
	void emitWriteLog(const QByteArray &ba);
    QString findFileID(RepositoryWrapper2Frame *frame, const QString &commit_id, const QString &file);
    const Git::CommitItem *commitItem(RepositoryWrapper2Frame *frame, int row) const;
    QIcon committerIcon(RepositoryWrapper2Frame *frame, int row) const;
	void changeSshKey(const QString &localdir, const QString &sshkey);
	static QString abbrevCommitID(const Git::CommitItem &commit);
    const Git::CommitItemList &getLogs(RepositoryWrapper2Frame const *frame) const;
    const QList<BranchLabel> *label(const RepositoryWrapper2Frame *frame, int row) const;
	ApplicationSettings *appsettings();
	const ApplicationSettings *appsettings() const;
	QString defaultWorkingDir() const;
	WebContext *webContext();
	QIcon verifiedIcon(char s) const;
	QAction *addMenuActionProperty(QMenu *menu);
	QString currentWorkingCopyDir() const;
	Git::SubmoduleItem const *querySubmoduleByPath(const QString &path, Git::CommitItem *commit);
	void refresh();
public slots:
	void writeLog_(QByteArray ba);
private slots:
	void updateUI();
	void onLogVisibilityChanged();
	void onPtyProcessCompleted(bool ok, const QVariant &userdata);
	void onRepositoriesTreeDropped();
	void on_action_about_triggered();
	void on_action_clean_df_triggered();
	void on_action_clone_triggered();
	void on_action_commit_triggered();
	void on_action_create_a_repository_triggered();
	void on_action_delete_branch_triggered();
	void on_action_delete_remote_branch_triggered();
	void on_action_edit_git_config_triggered();
	void on_action_edit_gitignore_triggered();
	void on_action_edit_global_gitconfig_triggered();
	void on_action_edit_settings_triggered();
	void on_action_edit_tags_triggered();
	void on_action_exit_triggered();
	void on_action_expand_commit_log_triggered();
	void on_action_expand_diff_view_triggered();
	void on_action_expand_file_list_triggered();
	void on_action_explorer_triggered();
	void on_action_fetch_prune_triggered();
	void on_action_fetch_triggered();
	void on_action_find_next_triggered();
	void on_action_find_triggered();
	void on_action_offline_triggered();
	void on_action_online_triggered();
	void on_action_open_existing_working_copy_triggered();
    void on_action_open_existing_working_copy_1_triggered();
	void on_action_pull_triggered();
	void on_action_push_all_tags_triggered();
	void on_action_push_triggered();
	void on_action_push_u_triggered();
	void on_action_reflog_triggered();
	void on_action_repo_checkout_triggered();
	void on_action_repo_jump_to_head_triggered();
	void on_action_repo_jump_triggered();
	void on_action_repo_merge_triggered();
	void on_action_repositories_panel_triggered();
	void on_action_repository_property_triggered();
	void on_action_repository_status_triggered();
	void on_action_reset_HEAD_1_triggered();
	void on_action_reset_hard_triggered();
	void on_action_set_config_user_triggered();
	void on_action_set_gpg_signing_triggered();
	void on_action_show_labels_triggered();
	void on_action_sidebar_triggered();
	void on_action_stash_apply_triggered();
	void on_action_stash_drop_triggered();
	void on_action_stash_triggered();
	void on_action_stop_process_triggered();
	void on_action_submodule_add_triggered();
	void on_action_submodule_update_triggered();
	void on_action_submodules_triggered();
	void on_action_terminal_triggered();
	void on_action_view_refresh_triggered();
	void on_action_window_log_triggered(bool checked);
	void on_horizontalScrollBar_log_valueChanged(int);
	void on_lineEdit_filter_textChanged(QString const &text);
	void on_listWidget_files_currentRowChanged(int currentRow);
	void on_listWidget_files_customContextMenuRequested(const QPoint &pos);
	void on_listWidget_files_itemDoubleClicked(QListWidgetItem *item);
	void on_listWidget_staged_currentRowChanged(int currentRow);
	void on_listWidget_staged_customContextMenuRequested(const QPoint &pos);
	void on_listWidget_staged_itemDoubleClicked(QListWidgetItem *item);
	void on_listWidget_unstaged_currentRowChanged(int currentRow);
	void on_listWidget_unstaged_customContextMenuRequested(const QPoint &pos);
	void on_listWidget_unstaged_itemDoubleClicked(QListWidgetItem *item);
	void on_radioButton_remote_offline_clicked();
	void on_radioButton_remote_online_clicked();
	void on_tableWidget_log_currentItemChanged(QTableWidgetItem *current, QTableWidgetItem *previous);
	void on_tableWidget_log_customContextMenuRequested(const QPoint &pos);
	void on_tableWidget_log_itemDoubleClicked(QTableWidgetItem *);
	void on_toolButton_clone_clicked();
	void on_toolButton_commit_clicked();
	void on_toolButton_erase_filter_clicked();
	void on_toolButton_explorer_clicked();
	void on_toolButton_fetch_clicked();
	void on_toolButton_pull_clicked();
	void on_toolButton_push_clicked();
	void on_toolButton_select_all_clicked();
	void on_toolButton_stage_clicked();
	void on_toolButton_status_clicked();
	void on_toolButton_stop_process_clicked();
	void on_toolButton_terminal_clicked();
	void on_toolButton_unstage_clicked();
	void on_treeWidget_repos_currentItemChanged(QTreeWidgetItem *current, QTreeWidgetItem *previous);
	void on_treeWidget_repos_customContextMenuRequested(const QPoint &pos);
	void on_treeWidget_repos_itemDoubleClicked(QTreeWidgetItem *item, int column);
	void on_verticalScrollBar_log_valueChanged(int);

    void onAvatarUpdated(RepositoryWrapper2FrameP frame);
	void test();
	void onInterval10ms();
	void on_action_create_desktop_launcher_file_triggered();
protected:
	void closeEvent(QCloseEvent *event) override;
	void internalWriteLog(const char *ptr, int len);
	RepositoryItem const *selectedRepositoryItem() const;
	void removeSelectedRepositoryFromBookmark(bool ask);
protected slots:
	void onLogIdle();
signals:
	void signalWriteLog(QByteArray ba);
	void remoteInfoChanged();
	void signalSetRemoteChanged(bool f);
	void onEscapeKeyPressed();
	void updateButton();
};
#endif // SUBMODULEMAINWINDOW_H
