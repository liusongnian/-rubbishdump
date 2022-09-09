#include "SubmoduleMainWindow.h"
#include "ui_SubmoduleMainWindow.h"
#include "AboutDialog.h"
#include "ApplicationGlobal.h"
#include "AreYouSureYouWantToContinueConnectingDialog.h"
#include "BlameWindow.h"
#include "CheckoutDialog.h"
#include "CherryPickDialog.h"
#include "CloneDialog.h"
#include "CloneFromGitHubDialog.h"
#include "CommitDialog.h"
#include "CommitExploreWindow.h"
#include "CommitPropertyDialog.h"
#include "CommitViewWindow.h"
#include "CreateRepositoryDialog.h"
#include "DeleteBranchDialog.h"
#include "DoYouWantToInitDialog.h"
#include "EditGitIgnoreDialog.h"
#include "EditTagsDialog.h"
#include "FileHistoryWindow.h"
#include "FilePropertyDialog.h"
#include "FileUtil.h"
#include "FindCommitDialog.h"
#include "GitDiff.h"
#include "JumpDialog.h"
#include "LineEditDialog.h"
#include "MemoryReader.h"
#include "MergeDialog.h"
#include "MySettings.h"
#include "ObjectBrowserDialog.h"
#include "PushDialog.h"
#include "ReflogWindow.h"
#include "RepositoryPropertyDialog.h"
#include "SelectCommandDialog.h"
#include "SetGlobalUserDialog.h"
#include "SetGpgSigningDialog.h"
#include "SetUserDialog.h"
#include "SettingsDialog.h"
#include "StatusLabel.h"
#include "SubmoduleAddDialog.h"
#include "SubmoduleMainWindow.h"
#include "SubmoduleUpdateDialog.h"
#include "SubmodulesDialog.h"
#include "Terminal.h"
#include "TextEditDialog.h"
#include "UserEvent.h"
#include "WelcomeWizardDialog.h"
#include "coloredit/ColorDialog.h"
#include "common/misc.h"
#include "gunzip.h"
#include "platform.h"
#include "webclient.h"
#include <QBuffer>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileIconProvider>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QShortcut>
#include <QStandardPaths>
#include <QTimer>

#ifdef Q_OS_MAC
namespace {

bool isValidDir(QString const &dir)
{
    if (dir.indexOf('\"') >= 0 || dir.indexOf('\\') >= 0) return false;
    return QFileInfo(dir).isDir();
}

}

#include <QProcess>
#endif


struct EventItem {
    QObject *receiver = nullptr;
    QEvent *event = nullptr;
    QDateTime at;
    EventItem(QObject *receiver, QEvent *event, QDateTime const &at)
        : receiver(receiver)
        , event(event)
        , at(at)
    {
    }
};

struct SubmoduleMainWindow::Private {

    QIcon repository_icon;
    QIcon folder_icon;
    QIcon signature_good_icon;
    QIcon signature_dubious_icon;
    QIcon signature_bad_icon;
    QPixmap transparent_pixmap;

    QString starting_dir;
    Git::Context gcx;
    RepositoryItem current_repo;

    QList<RepositoryItem> repos;
    QList<Git::Diff> diff_result;
    QList<Git::SubmoduleItem> submodules;

    QStringList added;
    QStringList remotes;
    QString current_remote_name;
    Git::Branch current_branch;
    unsigned int temp_file_counter = 0;

    std::string ssh_passphrase_user;
    std::string ssh_passphrase_pass;

    std::string http_uid;
    std::string http_pwd;

    std::map<QString, GitHubAPI::User> committer_map; // key is email

    PtyProcess pty_process;
    bool pty_process_ok = false;
    SubmoduleMainWindow::PtyCondition pty_condition = SubmoduleMainWindow::PtyCondition::None;

	WebContext webcx = {WebClient::HTTP_1_0};

    AvatarLoader avatar_loader;

    bool interaction_canceled = false;
    SubmoduleMainWindow::InteractionMode interaction_mode = SubmoduleMainWindow::InteractionMode::None;

    QString repository_filter_text;
    bool uncommited_changes = false;

    bool remote_changed = false;

    ServerType server_type = ServerType::Standard;
    GitHubRepositoryInfo github;

    QString head_id;
    bool force_fetch = false;

    RepositoryItem temp_repo_for_clone_complete;
    QVariant pty_process_completion_data;

    std::vector<EventItem> event_item_list;

    bool is_online_mode = true;
    QTimer interval_10ms_timer;
    QImage graph_color;
    QPixmap digits;
    StatusLabel *status_bar_label;

    QObject *last_focused_file_list = nullptr;

    QListWidgetItem *last_selected_file_item = nullptr;

    bool searching = false;
    QString search_text;

    int repos_panel_width = 0;

    std::set<QString> ancestors;

    QWidget *focused_widget = nullptr;
    QList<int> splitter_h_sizes;
};

SubmoduleMainWindow::SubmoduleMainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::SubmoduleMainWindow)
    , m(new Private)
{
    ui->setupUi(this);
    qDebug()<<"SubmoduleMainWindow ui->frame_repository_wrapper->bind";
    ui->frame_repository_wrapper->bind((SubmoduleMainWindow *)this
                                       , ui->tableWidget_log
                                       , ui->listWidget_files
                                       , ui->listWidget_unstaged
                                       , ui->listWidget_staged
                                       , ui->widget_diff_view
                                       );
    qDebug()<<"MainWindow ui->frame_repository_wrapper->bind exit";
    //QString text = "Submodule ";
    //setWindowTitle(text);
    ui->splitter_v->setSizes({100, 400});
    ui->splitter_h->setSizes({200, 100, 200});

    m->status_bar_label = new StatusLabel(this);
    ui->statusBar->addWidget(m->status_bar_label);

    frame()->filediffwidget()->bind(this);

    qApp->installEventFilter(this);

    setShowLabels(appsettings()->show_labels, false);

    ui->widget_log->setupForLogWidget(ui->verticalScrollBar_log, ui->horizontalScrollBar_log, themeForTextEditor());
    onLogVisibilityChanged();

    initNetworking();

    showFileList(FilesListType::SingleList);

    m->digits.load(":/image/digits.png");
    m->graph_color = global->theme->graphColorMap();

    frame()->prepareLogTableWidget();
    connect(this, &SubmoduleMainWindow::signalWriteLog, this, &SubmoduleMainWindow::writeLog_);

    connect(ui->dockWidget_log, &QDockWidget::visibilityChanged, this, &SubmoduleMainWindow::onLogVisibilityChanged);
    connect(ui->widget_log, &TextEditorWidget::idle, this, &SubmoduleMainWindow::onLogIdle);

    connect(ui->treeWidget_repos, &RepositoriesTreeWidget::dropped, this, &SubmoduleMainWindow::onRepositoriesTreeDropped);

    connect((AbstractPtyProcess *)getPtyProcess(), &AbstractPtyProcess::completed, this, &SubmoduleMainWindow::onPtyProcessCompleted);

    connect(this, &SubmoduleMainWindow::remoteInfoChanged, [&](){
        ui->lineEdit_remote->setText(currentRemoteName());
    });

    connect(this, &SubmoduleMainWindow::signalSetRemoteChanged, [&](bool f){
        setRemoteChanged(f);
        updateButton();
    });

    connect(new QShortcut(QKeySequence("Ctrl+T"), this), &QShortcut::activated, this, &SubmoduleMainWindow::test);

    //

    QString path = getBookmarksFilePath();
    qDebug()<<"getBookmarksFilePath"<<path;
    *getReposPtr() = RepositoryBookmark::load(path);
    updateRepositoriesList();

    {
        // アイコン取得機能
        webContext()->set_keep_alive_enabled(true);
        getAvatarLoader()->start((MainWindow *)this);
        //connect(getAvatarLoader(), &AvatarLoader::updated, this, &SubmoduleMainWindow::onAvatarUpdated);  //bypass
    }

    connect(frame()->filediffwidget(), &FileDiff2Widget::textcodecChanged, [&](){ updateDiffView(frame()); });

    if (!global->start_with_shift_key && appsettings()->remember_and_restore_window_position) {
        Qt::WindowStates state = windowState();
        MySettings settings;

        settings.beginGroup("MainWindow");
        bool maximized = settings.value("Maximized").toBool();
        restoreGeometry(settings.value("Geometry").toByteArray());
        settings.endGroup();
        if (maximized) {
            state |= Qt::WindowMaximized;
            setWindowState(state);
        }
    }

    ui->action_sidebar->setChecked(true);
    startTimers();

}

SubmoduleMainWindow::~SubmoduleMainWindow()
{
    delete ui;
}


RepositoryWrapper2Frame *SubmoduleMainWindow::frame()
{
    return ui->frame_repository_wrapper;
}

RepositoryWrapper2Frame const *SubmoduleMainWindow::frame() const
{
    return ui->frame_repository_wrapper;
}

/**
 * @brief イベントをポストする
 * @param receiver 宛先
 * @param event イベント
 * @param ms_later 遅延時間（0なら即座）
 */
void SubmoduleMainWindow::postEvent(QObject *receiver, QEvent *event, int ms_later)
{
    if (ms_later <= 0) {
        QApplication::postEvent(this, event);
    } else {
        auto at = QDateTime::currentDateTime().addMSecs(ms_later);
        m->event_item_list.emplace_back(receiver, event, at);
        std::stable_sort(m->event_item_list.begin(), m->event_item_list.end(), [](EventItem const &l, EventItem const &r){
            return l.at > r.at; // 降順
        });
    }
}

/**
 * @brief ユーザー関数イベントをポストする
 * @param fn 関数
 * @param v QVariant
 * @param p ポインタ
 * @param ms_later 遅延時間（0なら即座）
 */
void SubmoduleMainWindow::postUserFunctionEvent(const std::function<void (const QVariant &, void *ptr)> &fn, const QVariant &v, void *p, int ms_later)
{
    postEvent(this, new UserFunctionEvent(fn, v, p), ms_later);
}

/**
 * @brief 未送信のイベントをすべて削除する
 */
void SubmoduleMainWindow::cancelPendingUserEvents()
{
    for (auto &item : m->event_item_list) {
        delete item.event;
    }
    m->event_item_list.clear();
}

/**
 * @brief 開始イベントをポストする
 */
void SubmoduleMainWindow::postStartEvent(int ms_later)
{
    postEvent(this, new StartEvent, ms_later);
}

/**
 * @brief インターバルタイマを開始する
 */
void SubmoduleMainWindow::startTimers()
{
    // タイマ開始
    connect(&m->interval_10ms_timer, &QTimer::timeout, this, &SubmoduleMainWindow::onInterval10ms);
    m->interval_10ms_timer.setInterval(10);
    m->interval_10ms_timer.start();
}

/**
 * @brief 10ms間隔のインターバルタイマ
 */
void SubmoduleMainWindow::onInterval10ms()
{
    {
        // ユーザーイベントの処理

        std::vector<EventItem> items; // 処理するイベント

        QDateTime now = QDateTime::currentDateTime(); // 現在時刻

        size_t i = m->event_item_list.size(); // 後ろから走査
        while (i > 0) {
            i--;
            if (m->event_item_list[i].at <= now) { // 予約時間を過ぎていたら
                items.push_back(m->event_item_list[i]); // 処理リストに追加
                m->event_item_list.erase(m->event_item_list.begin() + (int)i); // 処理待ちリストから削除
            }
        }

        // イベントをポストする
        for (auto it = items.rbegin(); it != items.rend(); it++) {
            QApplication::postEvent(it->receiver, it->event);
        }
    }

    {
        // PTYプロセスの監視

        bool running = getPtyProcess()->isRunning();
        if (ui->toolButton_stop_process->isEnabled() != running) {
            ui->toolButton_stop_process->setEnabled(running); // ボタンの状態を設定
            ui->action_stop_process->setEnabled(running);
            setNetworkingCommandsEnabled(!running);
        }
        if (!running) {
            setInteractionMode(InteractionMode::None);
        }

        // PTYプロセスの出力をログに書き込む
        while (1) {
            char tmp[1024];
            int len = getPtyProcess()->readOutput(tmp, sizeof(tmp));
            if (len < 1) break;
            writeLog(tmp, len);
        }
    }
}

bool SubmoduleMainWindow::shown()
{
    m->repos_panel_width = ui->stackedWidget_leftpanel->width();
    ui->stackedWidget_leftpanel->setCurrentWidget(ui->page_repos);
    ui->action_repositories_panel->setChecked(true);

    {
        MySettings settings;
        {
            settings.beginGroup("Remote");
            bool f = settings.value("Online", true).toBool();
            settings.endGroup();
            setRemoteOnline(f, false);
        }
        {
            settings.beginGroup("SubmoduleMainWindow");
            int n = settings.value("FirstColumnWidth", 50).toInt();
            if (n < 10) n = 50;
            frame()->logtablewidget()->setColumnWidth(0, n);
            settings.endGroup();
        }
    }
    updateUI();

    postStartEvent(100); // 開始イベント（100ms後）

    return true;
}

bool SubmoduleMainWindow::isUninitialized()
{
    return !misc::isExecutable(appsettings()->git_command);
}

void SubmoduleMainWindow::onStartEvent()
{
    if (isUninitialized()) { // gitコマンドの有効性チェック
        if (!execWelcomeWizardDialog()) { // ようこそダイアログを表示
            close(); // キャンセルされたらプログラム終了
        }
    }
    if (isUninitialized()) { // 正しく初期設定されたか
        postStartEvent(100); // 初期設定されなかったら、もういちどようこそダイアログを出す（100ms後）
    } else {
        // 外部コマンド登録
        setGitCommand(appsettings()->git_command, false);
        setGpgCommand(appsettings()->gpg_command, false);
        setSshCommand(appsettings()->ssh_command, false);

        // メインウィンドウのタイトルを設定
        updateWindowTitle(git());

        // プログラムバーション表示
        writeLog(AboutDialog::appVersion() + '\n');
        // gitコマンドバージョン表示
        logGitVersion();
    }
}

void SubmoduleMainWindow::setCurrentLogRow(RepositoryWrapper2Frame *frame, int row)
{
    if (row >= 0 && row < frame->logtablewidget()->rowCount()) {
        updateStatusBarText(frame);
        frame->logtablewidget()->setFocus();
        frame->logtablewidget()->setCurrentCell(row, 2);
    }
}

bool SubmoduleMainWindow::eventFilter(QObject *watched, QEvent *event)
{
    QEvent::Type et = event->type();

    if (et == QEvent::KeyPress) {
        if (QApplication::activeModalWidget()) {
            // thru
        } else {
            auto *e = dynamic_cast<QKeyEvent *>(event);
            Q_ASSERT(e);
            int k = e->key();
            if (k == Qt::Key_Escape) {
                if (centralWidget()->isAncestorOf(qApp->focusWidget())) {
                    ui->treeWidget_repos->setFocus();
                    return true;
                }
            }
            if (e->modifiers() & Qt::ControlModifier) {
                if (k == Qt::Key_Up || k == Qt::Key_Down) {
                    int rows = frame()->logtablewidget()->rowCount();
                    int row = frame()->logtablewidget()->currentRow();
                    if (k == Qt::Key_Up) {
                        if (row > 0) {
                            row--;
                        }
                    } else if (k == Qt::Key_Down) {
                        if (row + 1 < rows) {
                            row++;
                        }
                    }
                    frame()->logtablewidget()->setCurrentCell(row, 0);
                    return true;
                }
            }
            if (watched == ui->treeWidget_repos) {
                if (k == Qt::Key_Enter || k == Qt::Key_Return) {
                    openSelectedRepository();
                    return true;
                }
                if (!(e->modifiers() & Qt::ControlModifier)) {
                    if (k >= 0 && k < 128 && QChar((uchar)k).isLetterOrNumber()) {
                        appendCharToRepoFilter(k);
                        return true;
                    }
                    if (k == Qt::Key_Backspace) {
                        backspaceRepoFilter();
                        return true;
                    }
                    if (k == Qt::Key_Escape) {
                        clearRepoFilter();
                        return true;
                    }
                }
            } else if (watched == frame()->logtablewidget()) {
                if (k == Qt::Key_Home) {
                    setCurrentLogRow(frame(), 0);
                    return true;
                }
                if (k == Qt::Key_Escape) {
                    ui->treeWidget_repos->setFocus();
                    return true;
                }
            } else if (watched == frame()->fileslistwidget() || watched == frame()->unstagedFileslistwidget() || watched == frame()->stagedFileslistwidget()) {
                if (k == Qt::Key_Escape) {
                    frame()->logtablewidget()->setFocus();
                    return true;
                }
            }
        }
    } else if (et == QEvent::FocusIn) {
        auto SelectItem = [](QListWidget *w){
            int row = w->currentRow();
            if (row < 0) {
                row = 0;
                w->setCurrentRow(row);
            }
            w->setItemSelected(w->item(row), true);
            w->viewport()->update();
        };
        // ファイルリストがフォーカスを得たとき、diffビューを更新する。（コンテキストメニュー対応）
        if (watched == frame()->unstagedFileslistwidget()) {
            m->last_focused_file_list = watched;
            updateStatusBarText(frame());
            updateUnstagedFileCurrentItem(frame());
            SelectItem(frame()->unstagedFileslistwidget());
            return true;
        }
        if (watched == frame()->stagedFileslistwidget()) {
            m->last_focused_file_list = watched;
            updateStatusBarText(frame());
            updateStagedFileCurrentItem(frame());
            SelectItem(frame()->stagedFileslistwidget());
            return true;
        }
        if (watched == frame()->fileslistwidget()) {
            m->last_focused_file_list = watched;
            SelectItem(frame()->fileslistwidget());
            return true;
        }
    }
    return false;
}

bool SubmoduleMainWindow::event(QEvent *event)
{
    QEvent::Type et = event->type();
    if (et == QEvent::KeyPress) {
        auto *e = dynamic_cast<QKeyEvent *>(event);
        Q_ASSERT(e);
        int k = e->key();
        if (k == Qt::Key_Escape) {
            emit onEscapeKeyPressed();
        } else if (k == Qt::Key_Delete) {
            if (qApp->focusWidget() == ui->treeWidget_repos) {
                removeSelectedRepositoryFromBookmark(true);
                return true;
            }
        }
    } else if (et == (QEvent::Type)UserEvent::UserFunction) {
        if (auto *e = (UserFunctionEvent *)event) {
            e->func(e->var, e->ptr);
            return true;
        }
    }
    return QMainWindow::event(event);
}

void SubmoduleMainWindow::customEvent(QEvent *e)
{
    if (e->type() == (QEvent::Type)UserEvent::Start) {
        onStartEvent();
        return;
    }
}

void SubmoduleMainWindow::closeEvent(QCloseEvent *event)
{
    MySettings settings;

    if (appsettings()->remember_and_restore_window_position) {
        setWindowOpacity(0);
        Qt::WindowStates state = windowState();
        bool maximized = (state & Qt::WindowMaximized) != 0;
        if (maximized) {
            state &= ~Qt::WindowMaximized;
            setWindowState(state);
        }
        {
            settings.beginGroup("SubmoduleMainWindow");
            settings.setValue("Maximized", maximized);
            settings.setValue("Geometry", saveGeometry());
            settings.endGroup();
        }
    }

    {
        settings.beginGroup("SubmoduleMainWindow");
        settings.setValue("FirstColumnWidth", frame()->logtablewidget()->columnWidth(0));
        settings.endGroup();
    }

    QMainWindow::closeEvent(event);
}

void SubmoduleMainWindow::setStatusBarText(QString const &text)
{
    m->status_bar_label->setText(text);
}

void SubmoduleMainWindow::clearStatusBarText()
{
    setStatusBarText(QString());
}

void SubmoduleMainWindow::onLogVisibilityChanged()
{
    ui->action_window_log->setChecked(ui->dockWidget_log->isVisible());
}

void SubmoduleMainWindow::internalWriteLog(char const *ptr, int len)
{
    ui->widget_log->logicalMoveToBottom();
    ui->widget_log->write(ptr, len, false);
    ui->widget_log->setChanged(false);
    setInteractionCanceled(false);
}

void SubmoduleMainWindow::buildRepoTree(QString const &group, QTreeWidgetItem *item, QList<RepositoryItem> *repos)
{
    QString name = item->text(0);
    if (isGroupItem(item)) {
        int n = item->childCount();
        for (int i = 0; i < n; i++) {
            QTreeWidgetItem *child = item->child(i);
            QString sub = group / name;
            buildRepoTree(sub, child, repos);
        }
    } else {
        RepositoryItem const *repo = repositoryItem(item);
        if (repo) {
            RepositoryItem newrepo = *repo;
            newrepo.name = name;
            newrepo.group = group;
            item->setData(0, IndexRole, repos->size());
            repos->push_back(newrepo);
        }
    }
}

void SubmoduleMainWindow::refrectRepositories()
{
    QList<RepositoryItem> newrepos;
    int n = ui->treeWidget_repos->topLevelItemCount();
    for (int i = 0; i < n; i++) {
        QTreeWidgetItem *item = ui->treeWidget_repos->topLevelItem(i);
        buildRepoTree(QString(), item, &newrepos);
    }
    *getReposPtr() = std::move(newrepos);
    saveRepositoryBookmarks();
}

void SubmoduleMainWindow::onRepositoriesTreeDropped()
{
    refrectRepositories();
    QTreeWidgetItem *item = ui->treeWidget_repos->currentItem();
    if (item) item->setExpanded(true);
}

const QPixmap &SubmoduleMainWindow::digitsPixmap() const
{
    return m->digits;
}

int SubmoduleMainWindow::digitWidth() const
{
    return 5;
}

int SubmoduleMainWindow::digitHeight() const
{
    return 7;
}

void SubmoduleMainWindow::drawDigit(QPainter *pr, int x, int y, int n) const
{
    int w = digitWidth();
    int h = digitHeight();
    pr->drawPixmap(x, y, w, h, m->digits, n * w, 0, w, h);
}

QString SubmoduleMainWindow::defaultWorkingDir() const
{
    return appsettings()->default_working_dir;
}

WebContext *SubmoduleMainWindow::webContext()
{
    return &m->webcx;
}

QIcon SubmoduleMainWindow::verifiedIcon(char s) const
{
    Git::SignatureGrade g = Git::evaluateSignature(s);
    switch (g) {
    case Git::SignatureGrade::Good:
        return m->signature_good_icon;
    case Git::SignatureGrade::Bad:
        return m->signature_bad_icon;
    case Git::SignatureGrade::Unknown:
    case Git::SignatureGrade::Dubious:
    case Git::SignatureGrade::Missing:
        return m->signature_dubious_icon;
    }
    return QIcon();
}

QAction *SubmoduleMainWindow::addMenuActionProperty(QMenu *menu)
{
    return menu->addAction(tr("&Property"));
}

QString SubmoduleMainWindow::currentWorkingCopyDir() const
{
    return m->current_repo.local_dir;
}

/**
 * @brief サブモジュール情報を取得する
 * @param path
 * @param commit コミット情報を取得（nullptr可）
 * @return
 */
Git::SubmoduleItem const *SubmoduleMainWindow::querySubmoduleByPath(const QString &path, Git::CommitItem *commit)
{
    if (commit) *commit = {};
    for (auto const &submod : m->submodules) {
        if (submod.path == path) {
            if (commit) {
                GitPtr g = git(submod);
                g->queryCommit(submod.id, commit);
            }
            return &submod;
        }
    }
    return nullptr;
}

QColor SubmoduleMainWindow::color(unsigned int i)
{
    unsigned int n = (unsigned int)m->graph_color.width();
    if (n > 0) {
        n--;
        if (i > n) i = n;
        QRgb const *p = reinterpret_cast<QRgb const *>(m->graph_color.scanLine(0));
        return QColor(qRed(p[i]), qGreen(p[i]), qBlue(p[i]));
    }
    return Qt::black;
}

RepositoryItem const *SubmoduleMainWindow::findRegisteredRepository(QString *workdir) const
{
    *workdir = QDir(*workdir).absolutePath();
    workdir->replace('\\', '/');

    if (Git::isValidWorkingCopy(*workdir)) {
        for (RepositoryItem const &item : getRepos()) {
            Qt::CaseSensitivity cs = Qt::CaseSensitive;
#ifdef Q_OS_WIN
            cs = Qt::CaseInsensitive;
#endif
            if (workdir->compare(item.local_dir, cs) == 0) {
                return &item;
            }
        }
    }
    return nullptr;
}

bool SubmoduleMainWindow::git_callback(void *cookie, const char *ptr, int len)
{
    auto *mw = (SubmoduleMainWindow *)cookie;
    mw->emitWriteLog(QByteArray(ptr, len));
    return true;
}

bool SubmoduleMainWindow::execSetGlobalUserDialog()
{
    SetGlobalUserDialog dlg((MainWindow *)this);
    if (dlg.exec() == QDialog::Accepted) {
        GitPtr g = git();
        Git::User user = dlg.user();
        g->setUser(user, true);
        updateWindowTitle(g);
        return true;
    }
    return false;
}

void SubmoduleMainWindow::revertAllFiles()
{
    GitPtr g = git();
    if (!isValidWorkingCopy(g)) return;

    QString cmd = "git reset --hard HEAD";
    if (askAreYouSureYouWantToRun(tr("Revert all files"), "> " + cmd)) {
        g->resetAllFiles();
        openRepository(true);
    }
}

void SubmoduleMainWindow::addWorkingCopyDir(QString dir, QString name, bool open)
{
    if (dir.endsWith(".git")) {
        int i = dir.size();
        if (i > 4) {
            ushort c = dir.utf16()[i - 5];
            if (c == '/' || c == '\\') {
                dir = dir.mid(0, i - 5);
            }
        }
    }

    if (!Git::isValidWorkingCopy(dir)) {
        if (QFileInfo(dir).isDir()) {
            QString text;
            text += tr("The folder is not a valid git repository.") + '\n';
            text += '\n';
            text += dir + '\n';
            text += '\n';
            text += tr("Do you want to initialize it as a git repository?") + '\n';
            int r = QMessageBox::information(this, tr("Initialize Repository") , text, QMessageBox::Yes, QMessageBox::No);
            if (r == QMessageBox::Yes) {
                createRepository(dir);
            }
        }
        return;
    }

    if (name.isEmpty()) {
        name = makeRepositoryName(dir);
    }

    RepositoryItem item;
    item.local_dir = dir;
    item.name = name;
    saveRepositoryBookmark(item);

    if (open) {
        setCurrentRepository(item, true);
        GitPtr g = git(item.local_dir, {}, {});
        openRepository_(g);
    }
}

bool SubmoduleMainWindow::execWelcomeWizardDialog()
{
    WelcomeWizardDialog dlg((MainWindow *)this);
    dlg.set_git_command_path(appsettings()->git_command);
    dlg.set_default_working_folder(appsettings()->default_working_dir);
    if (misc::isExecutable(appsettings()->git_command)) {
        gitCommand() = appsettings()->git_command;
        Git g(m->gcx, {}, {}, {});
        Git::User user = g.getUser(Git::Source::Global);
        dlg.set_user_name(user.name);
        dlg.set_user_email(user.email);
    }
    if (dlg.exec() == QDialog::Accepted) {
        setGitCommand(dlg.git_command_path(), false);
        appsettings()->default_working_dir = dlg.default_working_folder();
        saveApplicationSettings();

        if (misc::isExecutable(appsettings()->git_command)) {
            GitPtr g = git();
            Git::User user;
            user.name = dlg.user_name();
            user.email = dlg.user_email();
            g->setUser(user, true);
        }

        return true;
    }
    return false;
}

void SubmoduleMainWindow::execRepositoryPropertyDialog(const RepositoryItem &repo, bool open_repository_menu)
{
    QString workdir = repo.local_dir;

    if (workdir.isEmpty()) {
        workdir = currentWorkingCopyDir();
    }
    QString name = repo.name;
    if (name.isEmpty()) {
        name = makeRepositoryName(workdir);
    }
    GitPtr g = git(workdir, {}, repo.ssh_key);
    RepositoryPropertyDialog dlg((MainWindow *)this, &m->gcx, g, repo, open_repository_menu);
    dlg.exec();
    if (dlg.isRemoteChanged()) {
        emit remoteInfoChanged();
    }
    if (dlg.isNameChanged()) {
        this->changeRepositoryBookmarkName(repo, dlg.getName());
    }
}

void SubmoduleMainWindow::execSetUserDialog(const Git::User &global_user, const Git::User &repo_user, const QString &reponame)
{
    SetUserDialog dlg((MainWindow *)this, global_user, repo_user, reponame);
    if (dlg.exec() == QDialog::Accepted) {
        GitPtr g = git();
        Git::User user = dlg.user();
        if (dlg.isGlobalChecked()) {
            g->setUser(user, true);
        }
        if (dlg.isRepositoryChecked()) {
            g->setUser(user, false);
        }
        updateWindowTitle(g);
    }
}

void SubmoduleMainWindow::setGitCommand(QString const &path, bool save)
{
    qDebug()<<"SubmoduleMainWindow::setGitCommand"<<path;
    appsettings()->git_command = m->gcx.git_command = executableOrEmpty(path);

    internalSaveCommandPath(path, save, "GitCommand");
}

void SubmoduleMainWindow::setGpgCommand(QString const &path, bool save)
{
    appsettings()->gpg_command = executableOrEmpty(path);

    internalSaveCommandPath(path, save, "GpgCommand");
    if (!global->appsettings.gpg_command.isEmpty()) {
        GitPtr g = git();
        g->configGpgProgram(global->appsettings.gpg_command, true);
    }
}

void SubmoduleMainWindow::setSshCommand(QString const &path, bool save)
{
    appsettings()->ssh_command = m->gcx.ssh_command = executableOrEmpty(path);

    internalSaveCommandPath(path, save, "SshCommand");
}

bool SubmoduleMainWindow::checkGitCommand()
{
    while (1) {
        if (misc::isExecutable(gitCommand())) {
            return true;
        }
        if (selectGitCommand(true).isEmpty()) {
            close();
            return false;
        }
    }
}

bool SubmoduleMainWindow::saveBlobAs(RepositoryWrapper2Frame *frame, const QString &id, const QString &dstpath)
{
    Git::Object obj = cat_file(frame, id);
    if (!obj.content.isEmpty()) {
        if (saveByteArrayAs(obj.content, dstpath)) {
            return true;
        }
    } else {
        QString msg = "Failed to get the content of the object '%1'";
        msg = msg.arg(id);
        qDebug() << msg;
    }
    return false;
}

bool SubmoduleMainWindow::saveByteArrayAs(const QByteArray &ba, const QString &dstpath)
{
    QFile file(dstpath);
    if (file.open(QFile::WriteOnly)) {
        file.write(ba);
        file.close();
        return true;
    }
    QString msg = "Failed to open the file '%1' for write";
    msg = msg.arg(dstpath);
    qDebug() << msg;
    return false;
}

QString SubmoduleMainWindow::makeRepositoryName(const QString &loc)
{
    int i = loc.lastIndexOf('/');
    int j = loc.lastIndexOf('\\');
    if (i < j) i = j;
    if (i >= 0) {
        i++;
        j = loc.size();
        if (loc.endsWith(".git")) {
            j -= 4;
        }
        return loc.mid(i, j - i);
    }
    return QString();
}

bool SubmoduleMainWindow::saveFileAs(const QString &srcpath, const QString &dstpath)
{
    QFile f(srcpath);
    if (f.open(QFile::ReadOnly)) {
        QByteArray ba = f.readAll();
        if (saveByteArrayAs(ba, dstpath)) {
            return true;
        }
    } else {
        QString msg = "Failed to open the file '%1' for read";
        msg = msg.arg(srcpath);
        qDebug() << msg;
    }
    return false;
}

QString SubmoduleMainWindow::saveAsTemp(RepositoryWrapper2Frame *frame, const QString &id)
{
    QString path = newTempFilePath();
    saveAs(frame, id, path);
    return path;
}

QString SubmoduleMainWindow::executableOrEmpty(const QString &path)
{
    return checkExecutable(path) ? path : QString();
}

bool SubmoduleMainWindow::checkExecutable(const QString &path)
{
    if (QFileInfo(path).isExecutable()) {
        return true;
    }
    QString text = "The specified program '%1' is not executable.\n";
    text = text.arg(path);
    writeLog(text);
    return false;
}

void SubmoduleMainWindow::internalSaveCommandPath(const QString &path, bool save, const QString &name)
{
    if (checkExecutable(path)) {
        if (save) {
            MySettings s;
            s.beginGroup("Global");
            s.setValue(name, path);
            s.endGroup();
        }
    }
}

void SubmoduleMainWindow::logGitVersion()
{
    GitPtr g = git();
    QString s = g->version();
    if (!s.isEmpty()) {
        s += '\n';
        writeLog(s);
    }
}

void SubmoduleMainWindow::internalClearRepositoryInfo()
{
    setHeadId(QString());
    setCurrentBranch(Git::Branch());
    setServerType(ServerType::Standard);
    m->github = GitHubRepositoryInfo();
}

void SubmoduleMainWindow::checkUser()
{
    Git g(m->gcx, {}, {}, {});
    while (1) {
        Git::User user = g.getUser(Git::Source::Global);
        if (!user.name.isEmpty() && !user.email.isEmpty()) {
            return; // ok
        }
        if (!execSetGlobalUserDialog()) {
            return;
        }
    }
}

void SubmoduleMainWindow::openRepository(bool validate, bool waitcursor, bool keep_selection)
{

    qDebug()<<"openRepository"<<validate<<waitcursor<<keep_selection;
    if (validate) {
        QString dir = currentWorkingCopyDir();
        if (!QFileInfo(dir).isDir()) {
            int r = QMessageBox::warning(this, tr("Open Repository"), dir + "\n\n" + tr("No such folder") + "\n\n" + tr("Remove from bookmark?"), QMessageBox::Ok, QMessageBox::Cancel);
            if (r == QMessageBox::Ok) {
                removeSelectedRepositoryFromBookmark(false);
            }
            return;
        }
        if (!Git::isValidWorkingCopy(dir)) {
            QMessageBox::warning(this, tr("Open Repository"), tr("Not a valid git repository") + "\n\n" + dir);
            return;
        }
    }

    if (waitcursor) {
        OverrideWaitCursor;
        openRepository(false, false, keep_selection);
        return;
    }

    GitPtr g = git(); // ポインタの有効性チェックはしない（nullptrでも続行）
    qDebug()<<"openRepository2";
    openRepository_(g, keep_selection);
}

void SubmoduleMainWindow::updateRepository()
{
    GitPtr g = git();
    if (!isValidWorkingCopy(g)) return;

    OverrideWaitCursor;
    openRepository_(g);
}

void SubmoduleMainWindow::reopenRepository(bool log, const std::function<void (GitPtr )> &callback)
{
    GitPtr g = git();
    if (!isValidWorkingCopy(g)) return;

    OverrideWaitCursor;
    if (log) {
        setLogEnabled(g, true);
        AsyncExecGitThread_ th(g, callback);
        th.start();
        while (1) {
            if (th.wait(1)) break;
            QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        }
        setLogEnabled(g, false);
    } else {
        callback(g);
    }
    openRepository_(g);
}

void SubmoduleMainWindow::setCurrentRepository(const RepositoryItem &repo, bool clear_authentication)
{
    if (clear_authentication) {
        clearAuthentication();
    }
    m->current_repo = repo;
}

void SubmoduleMainWindow::openSelectedRepository()
{
    qDebug()<<"openSelectedRepository";
    RepositoryItem const *repo = selectedRepositoryItem();
    if (repo) {
        setCurrentRepository(*repo, true);
        openRepository(true);
    }
}

QList<Git::Diff> SubmoduleMainWindow::makeDiffs(RepositoryWrapper2Frame *frame, QString id, bool *ok)
{
    QList<Git::Diff> out;

    GitPtr g = git();
    if (!isValidWorkingCopy(g)) {
        if (ok) *ok = false;
        return {};
    }

    Git::FileStatusList list = g->status_s();
    setUncommitedChanges(!list.empty());

    if (id.isEmpty() && !isThereUncommitedChanges()) {
        id = getObjCache(frame)->revParse("HEAD");
    }

    QList<Git::SubmoduleItem> mods;
    updateSubmodules(g, id, &mods);
    setSubmodules(mods);

    bool uncommited = (id.isEmpty() && isThereUncommitedChanges());

    GitDiff dm(getObjCache(frame));
    if (uncommited) {
        dm.diff_uncommited(submodules(), &out);
    } else {
        dm.diff(id, submodules(), &out);
    }

    if (ok) *ok = true;
    return out;
}

void SubmoduleMainWindow::queryBranches(RepositoryWrapper2Frame *frame, GitPtr g)
{
    Q_ASSERT(g);
    frame->branch_map.clear();
    QList<Git::Branch> branches = g->branches();
    for (Git::Branch const &b : branches) {
        if (b.isCurrent()) {
            setCurrentBranch(b);
        }
        branchMapRef(frame)[b.id].append(b);
    }
}

void SubmoduleMainWindow::updateRemoteInfo()
{
    queryRemotes(git());

    m->current_remote_name = QString();
    {
        Git::Branch const &r = currentBranch();
        m->current_remote_name = r.remote;
    }
    if (m->current_remote_name.isEmpty()) {
        if (m->remotes.size() == 1) {
            m->current_remote_name = m->remotes[0];
        }
    }

    emit remoteInfoChanged();
}

void SubmoduleMainWindow::queryRemotes(GitPtr g)
{
    if (!g) return;
    m->remotes = g->getRemotes();
    std::sort(m->remotes.begin(), m->remotes.end());
}

void SubmoduleMainWindow::clone(QString url, QString dir)
{
    if (!isOnlineMode()) return;

    if (dir.isEmpty()) {
        dir = defaultWorkingDir();
    }

    while (1) {
        QString ssh_key;
        CloneDialog dlg((MainWindow *)this, url, dir, &m->gcx);
        if (dlg.exec() != QDialog::Accepted) {
            return;
        }
        const CloneDialog::Action action = dlg.action();
        url = dlg.url();
        dir = dlg.dir();
        ssh_key = dlg.overridedSshKey();

        RepositoryItem repos_item_data;
        repos_item_data.local_dir = dir;
        repos_item_data.local_dir.replace('\\', '/');
        repos_item_data.name = makeRepositoryName(dir);
        repos_item_data.ssh_key = ssh_key;

        // クローン先ディレクトリを求める

        Git::CloneData clone_data = Git::preclone(url, dir);

        if (action == CloneDialog::Action::Clone) {
            // 既存チェック

            QFileInfo info(dir);
            if (info.isFile()) {
                QString msg = dir + "\n\n" + tr("A file with same name already exists");
                QMessageBox::warning(this, tr("Clone"), msg);
                continue;
            }
            if (info.isDir()) {
                QString msg = dir + "\n\n" + tr("A folder with same name already exists");
                QMessageBox::warning(this, tr("Clone"), msg);
                continue;
            }

            // クローン先ディレクトリの存在チェック

            QString basedir = misc::normalizePathSeparator(clone_data.basedir);
            if (!QFileInfo(basedir).isDir()) {
                int i = basedir.indexOf('/');
                int j = basedir.indexOf('\\');
                if (i < j) i = j;
                if (i < 0) {
                    QString msg = basedir + "\n\n" + tr("Invalid folder");
                    QMessageBox::warning(this, tr("Clone"), msg);
                    continue;
                }

                QString msg = basedir + "\n\n" + tr("No such folder. Create it now?");
                if (QMessageBox::warning(this, tr("Clone"), msg, QMessageBox::Ok, QMessageBox::Cancel) != QMessageBox::Ok) {
                    continue;
                }

                // ディレクトリを作成

                QString base = basedir.mid(0, i + 1);
                QString sub = basedir.mid(i + 1);
                QDir(base).mkpath(sub);
            }

            GitPtr g = git({}, {}, repos_item_data.ssh_key);
            setPtyUserData(QVariant::fromValue<RepositoryItem>(repos_item_data));
            setPtyCondition(PtyCondition::Clone);
            setPtyProcessOk(true);
            g->clone(clone_data, getPtyProcess());
        } else if (action == CloneDialog::Action::AddExisting) {
            addWorkingCopyDir(dir, true);
        }

        return; // done
    }
}

void SubmoduleMainWindow::submodule_add(QString url, QString const &local_dir)
{
    if (!isOnlineMode()) return;
    if (local_dir.isEmpty()) return;

    QString dir = local_dir;

    while (1) {
        SubmoduleAddDialog dlg((MainWindow *)this, url, dir, &m->gcx);
        if (dlg.exec() != QDialog::Accepted) {
            return;
        }
        url = dlg.url();
        dir = dlg.dir();
        const QString ssh_key = dlg.overridedSshKey();

        RepositoryItem repos_item_data;
        repos_item_data.local_dir = dir;
        repos_item_data.local_dir.replace('\\', '/');
        repos_item_data.name = makeRepositoryName(dir);
        repos_item_data.ssh_key = ssh_key;

        Git::CloneData data = Git::preclone(url, dir);
        bool force = dlg.isForce();

        GitPtr g = git(local_dir, {}, repos_item_data.ssh_key);

        auto callback = [&](GitPtr g){
            g->submodule_add(data, force, getPtyProcess());
        };

        {
            OverrideWaitCursor;
            {
                setLogEnabled(g, true);
                AsyncExecGitThread_ th(g, callback);
                th.start();
                while (1) {
                    if (th.wait(1)) break;
                    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
                }
                setLogEnabled(g, false);
            }
            openRepository_(g);
        }

        return; // done
    }
}

const Git::CommitItem *SubmoduleMainWindow::selectedCommitItem(RepositoryWrapper2Frame *frame) const
{
    int i = selectedLogIndex(frame);
    return commitItem(frame, i);
}

void SubmoduleMainWindow::commit(RepositoryWrapper2Frame *frame, bool amend)
{
    GitPtr g = git();
    if (!isValidWorkingCopy(g)) return;

    QString message;
    QString previousMessage;

    if (amend) {
        message = getLogs(frame)[0].message;
    } else {
        QString id = g->getCherryPicking();
        if (Git::isValidID(id)) {
            message = g->getMessage(id);
        } else {
            for (Git::CommitItem const &item : getLogs(frame)) {
                if (!item.commit_id.isEmpty()) {
                    previousMessage = item.message;
                    break;
                }
            }
        }
    }

    while (1) {
        Git::User user = g->getUser(Git::Source::Default);
        QString sign_id = g->signingKey(Git::Source::Default);
        gpg::Data key;
        {
            QList<gpg::Data> keys;
            gpg::listKeys(global->appsettings.gpg_command, &keys);
            for (gpg::Data const &k : keys) {
                if (k.id == sign_id) {
                    key = k;
                }
            }
        }
        CommitDialog dlg((MainWindow *)this, currentRepositoryName(), user, key, previousMessage);
        dlg.setText(message);
        if (dlg.exec() == QDialog::Accepted) {
            QString text = dlg.text();
            if (text.isEmpty()) {
                QMessageBox::warning(this, tr("Commit"), tr("Commit message can not be omitted."));
                continue;
            }
            bool sign = dlg.isSigningEnabled();
            bool ok;
            if (amend || dlg.isAmend()) {
                ok = g->commit_amend_m(text, sign, getPtyProcess());
            } else {
                ok = g->commit(text, sign, getPtyProcess());
            }
            if (ok) {
                setForceFetch(true);
                updateStatusBarText(frame);
                openRepository(true);
            } else {
                QString err = g->errorMessage().trimmed();
                err += "\n*** ";
                err += tr("Failed to commit");
                err += " ***\n";
                writeLog(err);
            }
        }
        break;
    }
}

void SubmoduleMainWindow::commitAmend(RepositoryWrapper2Frame *frame)
{
    commit(frame, true);
}

void SubmoduleMainWindow::pushSetUpstream(bool set_upstream, const QString &remote, const QString &branch, bool force)
{
    if (remote.isEmpty()) return;
    if (branch.isEmpty()) return;

    int exitcode = 0;
    QString errormsg;

    reopenRepository(true, [&](GitPtr g){
		g->push_u(set_upstream, remote, branch, force, getPtyProcess());
        while (1) {
            if (getPtyProcess()->wait(1)) break;
            QApplication::processEvents();
        }
        exitcode = getPtyProcess()->getExitCode();
        errormsg = getPtyProcess()->getMessage();
    });

    if (exitcode == 128) {
        if (errormsg.indexOf("Connection refused") >= 0) {
            QMessageBox::critical(this, qApp->applicationName(), tr("Connection refused."));
            return;
        }
    }

    updateRemoteInfo();
}

bool SubmoduleMainWindow::pushSetUpstream(bool testonly)
{
    GitPtr g = git();
    if (!isValidWorkingCopy(g)) return false;
    QStringList remotes = g->getRemotes();

    QString current_branch = currentBranchName();

    QStringList branches;
    for (Git::Branch const &b : g->branches()) {
        branches.push_back(b.name);
    }

    if (remotes.isEmpty() || branches.isEmpty()) {
        return false;
    }

    if (testonly) {
        return true;
    }

    PushDialog dlg((MainWindow *)this, remotes, branches, PushDialog::RemoteBranch(QString(), current_branch));
    if (dlg.exec() == QDialog::Accepted) {
        PushDialog::Action a = dlg.action();
        if (a == PushDialog::PushSimple) {
            push();
        } else if (a == PushDialog::PushSetUpstream) {
			bool set_upstream = dlg.isSetUpStream();
            QString remote = dlg.remote();
            QString branch = dlg.branch();
			bool force = dlg.isForce();
			pushSetUpstream(set_upstream, remote, branch, force);
        }
        return true;
    }

    return false;
}

void SubmoduleMainWindow::push()
{
    if (!isOnlineMode()) return;

    GitPtr g = git();
    if (!isValidWorkingCopy(g)) return;

    if (g->getRemotes().isEmpty()) {
        QMessageBox::warning(this, qApp->applicationName(), tr("No remote repository is registered."));
        RepositoryItem const &repo = currentRepository();
        execRepositoryPropertyDialog(repo, true);
        return;
    }

    int exitcode = 0;
    QString errormsg;

    reopenRepository(true, [&](GitPtr g){
        g->push(false, getPtyProcess());
        while (1) {
            if (getPtyProcess()->wait(1)) break;
            QApplication::processEvents();
        }
        exitcode = getPtyProcess()->getExitCode();
        errormsg = getPtyProcess()->getMessage();
    });

    if (exitcode == 128) {
        if (errormsg.indexOf("no upstream branch") >= 0) {
            QString brname = currentBranchName();

            QString msg = tr("The current branch %1 has no upstream branch.");
            msg = msg.arg(brname);
            msg += '\n';
            msg += tr("You try push --set-upstream");
            QMessageBox::warning(this, qApp->applicationName(), msg);
            pushSetUpstream(false);
            return;
        }
        if (errormsg.indexOf("Connection refused") >= 0) {
            QMessageBox::critical(this, qApp->applicationName(), tr("Connection refused."));
            return;
        }
    }
}

void SubmoduleMainWindow::deleteBranch(RepositoryWrapper2Frame *frame, const Git::CommitItem *commit)
{
    if (!commit) return;

    GitPtr g = git();
    if (!isValidWorkingCopy(g)) return;

    QStringList all_branch_names;
    QStringList current_local_branch_names;
    {
        NamedCommitList named_commits = namedCommitItems(frame, Branches);
        for (NamedCommitItem const &item : named_commits) {
            if (item.name == "HEAD") continue;
            if (item.id == commit->commit_id) {
                current_local_branch_names.push_back(item.name);
            }
            all_branch_names.push_back(item.name);
        }
    }

    DeleteBranchDialog dlg((MainWindow *)this, false, all_branch_names, current_local_branch_names);
    if (dlg.exec() == QDialog::Accepted) {
        setLogEnabled(g, true);
        QStringList names = dlg.selectedBranchNames();
        int count = 0;
        for (QString const &name : names) {
            if (g->git(QString("branch -D \"%1\"").arg(name))) {
                count++;
            } else {
                writeLog(tr("Failed to delete the branch '%1'").arg(name) + '\n');
            }
        }
        if (count > 0) {
            openRepository(true, true, true);
        }
    }
}

void SubmoduleMainWindow::deleteBranch(RepositoryWrapper2Frame *frame)
{
    deleteBranch(frame, selectedCommitItem(frame));
}

void SubmoduleMainWindow::resetFile(const QStringList &paths)
{
    GitPtr g = git();
    if (!isValidWorkingCopy(g)) return;

    if (paths.isEmpty()) {
        // nop
    } else {
        QString cmd = "git checkout -- \"%1\"";
        cmd = cmd.arg(paths[0]);
        if (askAreYouSureYouWantToRun(tr("Reset a file"), "> " + cmd)) {
            for (QString const &path : paths) {
                g->resetFile(path);
            }
            openRepository(true);
        }
    }
}

void SubmoduleMainWindow::clearAuthentication()
{
    clearSshAuthentication();
    m->http_uid.clear();
    m->http_pwd.clear();
}

void SubmoduleMainWindow::clearSshAuthentication()
{
    m->ssh_passphrase_user.clear();
    m->ssh_passphrase_pass.clear();
}

void SubmoduleMainWindow::internalDeleteTags(const QStringList &tagnames)
{
    GitPtr g = git();
    if (!isValidWorkingCopy(g)) return;

    if (!tagnames.isEmpty()) {
        reopenRepository(false, [&](GitPtr g){
            for (QString const &name : tagnames) {
                g->delete_tag(name, true);
            }
        });
    }
}

bool SubmoduleMainWindow::internalAddTag(RepositoryWrapper2Frame *frame, const QString &name)
{
    if (name.isEmpty()) return false;

    GitPtr g = git();
    if (!isValidWorkingCopy(g)) return false;

    QString commit_id;

    Git::CommitItem const *commit = selectedCommitItem(frame);
    if (commit && !commit->commit_id.isEmpty()) {
        commit_id = commit->commit_id;
    }

    if (!Git::isValidID(commit_id)) return false;

    bool ok = false;
    reopenRepository(false, [&](GitPtr g){
        ok = g->tag(name, commit_id);
    });

    return ok;
}

void SubmoduleMainWindow::createRepository(const QString &dir)
{
    CreateRepositoryDialog dlg((MainWindow *)this, dir);
    if (dlg.exec() == QDialog::Accepted) {
        QString path = dlg.path();
        if (QFileInfo(path).isDir()) {
            if (Git::isValidWorkingCopy(path)) {
                // A valid git repository already exists there.
            } else {
                GitPtr g = git(path, {}, {});
                if (g->init()) {
                    QString name = dlg.name();
                    if (!name.isEmpty()) {
                        addWorkingCopyDir(path, name, true);
                    }
                    QString remote_name = dlg.remoteName();
                    QString remote_url = dlg.remoteURL();
					QString ssh_key = dlg.overridedSshKey();
					if (!remote_name.isEmpty() && !remote_url.isEmpty()) {
                        Git::Remote r;
                        r.name = remote_name;
                        r.url = remote_url;
						r.ssh_key = ssh_key;
                        g->addRemoteURL(r);
                        //changeSshKey(path, ssh_key);  //bypass
					}
                }
            }
        } else {
            // not dir
        }
    }
}

void SubmoduleMainWindow::setLogEnabled(GitPtr g, bool f)
{
    if (f) {
        g->setLogCallback(git_callback, this);
    } else {
        g->setLogCallback(nullptr, nullptr);
    }
}

void SubmoduleMainWindow::doGitCommand(const std::function<void (GitPtr)> &callback)
{
    GitPtr g = git();
    if (isValidWorkingCopy(g)) {
        OverrideWaitCursor;
        callback(g);
        openRepository(false, false);
    }
}

void SubmoduleMainWindow::setWindowTitle_(const Git::User &user)
{
    if (user.name.isEmpty() && user.email.isEmpty()) {
        setWindowTitle(qApp->applicationName());
    } else {
        setWindowTitle(QString("%1 : %2 <%3>")
                       .arg(qApp->applicationName())
                       .arg(user.name)
                       .arg(user.email)
                       );
    }
}

void SubmoduleMainWindow::setUnknownRepositoryInfo()
{
    setRepositoryInfo("---", "");

    Git g(m->gcx, {}, {}, {});
    Git::User user = g.getUser(Git::Source::Global);
    setWindowTitle_(user);
}

void SubmoduleMainWindow::setCurrentRemoteName(const QString &name)
{
    m->current_remote_name = name;
}

void SubmoduleMainWindow::deleteTags(RepositoryWrapper2Frame *frame, const Git::CommitItem &commit)
{
    auto it = ptrTagMap(frame)->find(commit.commit_id);
    if (it != ptrTagMap(frame)->end()) {
        QStringList names;
        QList<Git::Tag> const &tags = it->second;
        for (Git::Tag const &tag : tags) {
            names.push_back(tag.name);
        }
        deleteTags(frame, names);
    }
}

bool SubmoduleMainWindow::isAvatarEnabled() const
{
    return appsettings()->get_committer_icon;
}

bool SubmoduleMainWindow::isGitHub() const
{
    return m->server_type == ServerType::GitHub;
}

QStringList SubmoduleMainWindow::remotes() const
{
    return m->remotes;
}

QList<Git::Branch> SubmoduleMainWindow::findBranch(RepositoryWrapper2Frame *frame, const QString &id)
{
    auto it = branchMapRef(frame).find(id);
    if (it != branchMapRef(frame).end()) {
        return it->second;
    }
    return QList<Git::Branch>();
}

QString SubmoduleMainWindow::tempfileHeader() const
{
    QString name = "jp_soramimi_Guitar_%1_";
    return name.arg(QApplication::applicationPid());
}

void SubmoduleMainWindow::deleteTempFiles()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    QString name = tempfileHeader();
    QDirIterator it(dir, { name + "*" });
    while (it.hasNext()) {
        QString path = it.next();
        QFile::remove(path);
    }
}

QString SubmoduleMainWindow::getCommitIdFromTag(RepositoryWrapper2Frame *frame, const QString &tag)
{
    return getObjCache(frame)->getCommitIdFromTag(tag);
}

QString SubmoduleMainWindow::newTempFilePath()
{
    QString tmpdir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    QString path = tmpdir / tempfileHeader() + QString::number(m->temp_file_counter);
    m->temp_file_counter++;
    return path;
}

int SubmoduleMainWindow::limitLogCount() const
{
    const int n = appsettings()->maximum_number_of_commit_item_acquisitions;
    return (n >= 1 && n <= 100000) ? n : 10000;
}

Git::Object SubmoduleMainWindow::cat_file_(RepositoryWrapper2Frame *frame, GitPtr g, const QString &id)
{
    qDebug() << "SubmoduleMainWindow::cat_file_"<<id;
    if (isValidWorkingCopy(g)) {
        QString path_prefix = PATH_PREFIX;
        if (id.startsWith(path_prefix)) {
            QString path = g->workingDir();
            path = path / id.mid(path_prefix.size());
            QFile file(path);
            if (file.open(QFile::ReadOnly)) {
                Git::Object obj;
                obj.content = file.readAll();
                return obj;
            }
        } else if (Git::isValidID(id)) {
            return getObjCache(frame)->catFile(id);;
        }
    }
    return Git::Object();
}

bool SubmoduleMainWindow::isThereUncommitedChanges() const
{
    return m->uncommited_changes;
}

int SubmoduleMainWindow::repositoryIndex_(QTreeWidgetItem const *item) const
{
    if (item) {
        int i = item->data(0, IndexRole).toInt();
        if (i >= 0 && i < getRepos().size()) {
            return i;
        }
    }
    return -1;
}

RepositoryItem const *SubmoduleMainWindow::repositoryItem(QTreeWidgetItem const *item) const
{
    int row = repositoryIndex_(item);
    qDebug()<<"on_treeWidget_repos_customContextMenuRequested"<<row;
    QList<RepositoryItem> const &repos = getRepos();
    return (row >= 0 && row < repos.size()) ? &repos[row] : nullptr;
}

RepositoryItem const *SubmoduleMainWindow::selectedRepositoryItem() const
{
    qDebug()<<"selectedRepositoryItem";
    return repositoryItem(ui->treeWidget_repos->currentItem());
}

static QTreeWidgetItem *newQTreeWidgetItem()
{
    auto *item = new QTreeWidgetItem;
    item->setSizeHint(0, QSize(20, 20));
    return item;
}

QTreeWidgetItem *SubmoduleMainWindow::newQTreeWidgetFolderItem(QString const &name)
{
    QTreeWidgetItem *item = newQTreeWidgetItem();
    item->setText(0, name);
    item->setData(0, IndexRole, GroupItem);
    item->setIcon(0, getFolderIcon());
    item->setFlags(item->flags() | Qt::ItemIsEditable);
    return item;
}

void SubmoduleMainWindow::updateRepositoriesList()
{
    QString path = getBookmarksFilePath();

    auto *repos = getReposPtr();
    *repos = RepositoryBookmark::load(path);

    QString filter = getRepositoryFilterText();

    ui->treeWidget_repos->clear();

    std::map<QString, QTreeWidgetItem *> parentmap;

    for (int i = 0; i < repos->size(); i++) {
        RepositoryItem const &repo = repos->at(i);
        if (!filter.isEmpty() && repo.name.indexOf(filter, 0, Qt::CaseInsensitive) < 0) {
            continue;
        }
        QTreeWidgetItem *parent = nullptr;

        QString group = repo.group;
        if (group.startsWith('/')) {
            group = group.mid(1);
        }
        if (group == "") {
            group = "Default";
        }
        auto it = parentmap.find(group);
        if (it != parentmap.end()) {
            parent = it->second;
        } else {
            QStringList list = group.split('/', QString::SkipEmptyParts);
            if (list.isEmpty()) {
                list.push_back("Default");
            }
            QString groupPath = "", groupPathWithCurrent;
            for (QString const &name : list) {
                if (name.isEmpty()) continue;
                groupPathWithCurrent = groupPath + name;
                auto it = parentmap.find(groupPathWithCurrent);
                if (it != parentmap.end()) {
                    parent = it->second;
                } else {
                    QTreeWidgetItem *newItem = newQTreeWidgetFolderItem(name);
                    if (!parent) {
                        ui->treeWidget_repos->addTopLevelItem(newItem);
                    } else {
                        parent->addChild(newItem);
                    }
                    parent = newItem;
                    parentmap[groupPathWithCurrent] = newItem;
                    newItem->setExpanded(true);
                }
                groupPath = groupPathWithCurrent + '/';
            }
            Q_ASSERT(parent);
        }
        parent->setData(0, FilePathRole, "");

        QTreeWidgetItem *child = newQTreeWidgetItem();
        child->setText(0, repo.name);
        child->setData(0, IndexRole, i);
        child->setIcon(0, getRepositoryIcon());
        child->setFlags(child->flags() & ~Qt::ItemIsDropEnabled);
        parent->addChild(child);
        parent->setExpanded(true);
    }
}

void SubmoduleMainWindow::showFileList(FilesListType files_list_type)
{
    switch (files_list_type) {
    case FilesListType::SingleList:
        ui->stackedWidget_filelist->setCurrentWidget(ui->page_files);
        break;
    case FilesListType::SideBySide:
        ui->stackedWidget_filelist->setCurrentWidget(ui->page_uncommited);
        break;
    }
}

/**
 * @brief ファイルリストを消去
 * @param frame
 */
void SubmoduleMainWindow::clearFileList(RepositoryWrapper2Frame *frame)
{
    showFileList(FilesListType::SingleList);
    frame->unstagedFileslistwidget()->clear();
    frame->stagedFileslistwidget()->clear();
    frame->fileslistwidget()->clear();
}

void SubmoduleMainWindow::clearDiffView(RepositoryWrapper2Frame *frame)
{
    frame->filediffwidget()->clearDiffView();
}

void SubmoduleMainWindow::clearRepositoryInfo()
{
    internalClearRepositoryInfo();
    ui->label_repo_name->setText(QString());
    ui->label_branch_name->setText(QString());
}

void SubmoduleMainWindow::setRepositoryInfo(QString const &reponame, QString const &brname)
{
    ui->label_repo_name->setText(reponame);
    ui->label_branch_name->setText(brname);
}

/**
 * @brief 指定のコミットにおけるサブモジュールリストを取得
 * @param g
 * @param id
 * @param out
 */
void SubmoduleMainWindow::updateSubmodules(GitPtr g, QString const &id, QList<Git::SubmoduleItem> *out)
{
    *out = {};
    QList<Git::SubmoduleItem> submodules;
    if (id.isEmpty()) {
        submodules = g->submodules();
    } else {
        GitTreeItemList list;
        GitObjectCache objcache;
        objcache.setup(g);
        // サブモジュールリストを取得する
        {
            GitCommit tree;
            GitCommit::parseCommit(&objcache, id, &tree);
            parseGitTreeObject(&objcache, tree.tree_id, {}, &list);
            for (GitTreeItem const &item : list) {
                if (item.type == GitTreeItem::Type::BLOB && item.name == ".gitmodules") {
                    Git::Object obj = objcache.catFile(item.id);
                    if (!obj.content.isEmpty()) {
                        parseGitSubModules(obj.content, &submodules);
                    }
                }
            }
        }
        // サブモジュールに対応するIDを求める
        for (int i = 0; i < submodules.size(); i++) {
            QStringList vars = submodules[i].path.split('/');
            for (int j = 0; j < vars.size(); j++) {
                for (int k = 0; k < list.size(); k++) {
                    if (list[k].name == vars[j]) {
                        if (list[k].type == GitTreeItem::Type::BLOB) {
                            if (j + 1 == vars.size()) {
                                submodules[i].id = list[k].id;
                                goto done;
                            }
                        } else if (list[k].type == GitTreeItem::Type::TREE) {
                            Git::Object obj = objcache.catFile(list[k].id);
                            parseGitTreeObject(obj.content, {}, &list);
                            break;
                        }
                    }
                }
            }
done:;
        }
    }
    *out = submodules;
}

void SubmoduleMainWindow::saveRepositoryBookmark(RepositoryItem item)
{
    if (item.local_dir.isEmpty()) return;

    if (item.name.isEmpty()) {
        item.name = tr("Unnamed");
    }

    auto *repos = getReposPtr();

    bool done = false;
    for (auto &repo : *repos) {
        RepositoryItem *p = &repo;
        if (item.local_dir == p->local_dir) {
            *p = item;
            done = true;
            break;
        }
    }
    if (!done) {
        repos->push_back(item);
    }
    saveRepositoryBookmarks();
    updateRepositoriesList();
}

void SubmoduleMainWindow::changeRepositoryBookmarkName(RepositoryItem item, QString new_name)
{
    item.name = new_name;
    saveRepositoryBookmark(item);
}

int SubmoduleMainWindow::rowFromCommitId(RepositoryWrapper2Frame *frame, const QString &id)
{
    auto const &logs = getLogs(frame);
    for (size_t i = 0; i < logs.size(); i++) {
        Git::CommitItem const &item = logs[i];
        if (item.commit_id == id) {
            return (int)i;
        }
    }
    return -1;
}

QList<Git::Tag> SubmoduleMainWindow::findTag(RepositoryWrapper2Frame *frame, const QString &id)
{
    auto it = ptrTagMap(frame)->find(id);
    if (it != ptrTagMap(frame)->end()) {
        return it->second;
    }
    return QList<Git::Tag>();
}

void SubmoduleMainWindow::sshSetPassphrase(const std::string &user, const std::string &pass)
{
    m->ssh_passphrase_user = user;
    m->ssh_passphrase_pass = pass;
}

std::string SubmoduleMainWindow::sshPassphraseUser() const
{
    return m->ssh_passphrase_user;
}

std::string SubmoduleMainWindow::sshPassphrasePass() const
{
    return m->ssh_passphrase_pass;
}

void SubmoduleMainWindow::httpSetAuthentication(const std::string &user, const std::string &pass)
{
    m->http_uid = user;
    m->http_pwd = pass;
}

std::string SubmoduleMainWindow::httpAuthenticationUser() const
{
    return m->http_uid;
}

std::string SubmoduleMainWindow::httpAuthenticationPass() const
{
    return m->http_pwd;
}

const Git::CommitItemList &SubmoduleMainWindow::getLogs(RepositoryWrapper2Frame const *frame) const
{
    return frame->logs;
}

const QList<BranchLabel> *SubmoduleMainWindow::label(const RepositoryWrapper2Frame *frame, int row) const
{
    auto it = getLabelMap(frame)->find(row);
    if (it != getLabelMap(frame)->end()) {
        return &it->second;
    }
    return nullptr;
}

ApplicationSettings *SubmoduleMainWindow::appsettings()
{
    return &global->appsettings;
}

const ApplicationSettings *SubmoduleMainWindow::appsettings() const
{
    return &global->appsettings;
}

const Git::CommitItem *SubmoduleMainWindow::getLog(RepositoryWrapper2Frame const *frame, int index) const
{
    Git::CommitItemList const &logs = frame->logs;
    return (index >= 0 && index < (int)logs.size()) ? &logs[index] : nullptr;
}

void SubmoduleMainWindow::updateCommitGraph(RepositoryWrapper2Frame *frame)
{
    auto const &logs = getLogs(frame);
    auto *logsp = getLogsPtr(frame);


    const int LogCount = (int)logs.size();
    // 樹形図情報を構築する
    if (LogCount > 0) {
        auto LogItem = [&](int i)->Git::CommitItem &{ return logsp->at((size_t)i); };
        enum { // 有向グラフを構築するあいだ CommitItem::marker_depth をフラグとして使用する
            UNKNOWN = 0,
            KNOWN = 1,
        };
        for (Git::CommitItem &item : *logsp) {
            item.marker_depth = UNKNOWN;
        }
        // コミットハッシュを検索して、親コミットのインデックスを求める
        for (int i = 0; i < LogCount; i++) {
            Git::CommitItem *item = &LogItem(i);
            item->parent_lines.clear();
            if (item->parent_ids.empty()) {
                item->resolved = true;
            } else {
                for (int j = 0; j < item->parent_ids.size(); j++) { // 親の数だけループ
                    QString const &parent_id = item->parent_ids[j]; // 親のハッシュ値
                    for (int k = i + 1; k < (int)LogCount; k++) { // 親を探す
                        if (LogItem(k).commit_id == parent_id) { // ハッシュ値が一致したらそれが親
                            item->parent_lines.emplace_back(k); // インデックス値を記憶
                            LogItem(k).has_child = true;
                            LogItem(k).marker_depth = KNOWN;
                            item->resolved = true;
                            break;
                        }
                    }
                }
            }
        }
        std::vector<Element> elements; // 線分リスト
        { // 線分リストを作成する
            std::deque<Task> tasks; // 未処理タスクリスト
            {
                for (int i = 0; i < LogCount; i++) {
                    Git::CommitItem *item = &LogItem((int)i);
                    if (item->marker_depth == UNKNOWN) {
                        int n = (int)item->parent_lines.size(); // 最初のコミットアイテム
                        for (int j = 0; j < n; j++) {
                            tasks.emplace_back(i, j); // タスクを追加
                        }
                    }
                    item->marker_depth = UNKNOWN;
                }
            }
            while (!tasks.empty()) { // タスクが残っているならループ
                Element e;
                Task task;
                { // 最初のタスクを取り出す
                    task = tasks.front();
                    tasks.pop_front();
                }
                e.indexes.push_back(task.index); // 先頭のインデックスを追加
                int index = LogItem(task.index).parent_lines[task.parent].index; // 開始インデックス
                while (index > 0 && index < LogCount) { // 最後に到達するまでループ
                    e.indexes.push_back(index); // インデックスを追加
                    size_t n = LogItem(index).parent_lines.size(); // 親の数
                    if (n == 0) break; // 親がないなら終了
                    Git::CommitItem *item = &LogItem(index);
                    if (item->marker_depth == KNOWN) break; // 既知のアイテムに到達したら終了
                    item->marker_depth = KNOWN; // 既知のアイテムにする
                    for (int i = 1; i < (int)n; i++) {
                        tasks.emplace_back(index, i); // タスク追加
                    }
                    index = LogItem(index).parent_lines[0].index; // 次の親（親リストの先頭の要素）
                }
                if (e.indexes.size() >= 2) {
                    elements.push_back(e);
                }
            }
        }
        // 線情報をクリア
        for (Git::CommitItem &item : *logsp) {
            item.marker_depth = -1;
            item.parent_lines.clear();
        }
        // マークと線の深さを決める
        if (!elements.empty()) {
            { // 優先順位を調整する
                std::sort(elements.begin(), elements.end(), [](Element const &left, Element const &right){
                    int i = 0;
                    { // 長いものを優先して左へ
                        int l = left.indexes.back() - left.indexes.front();
                        int r = right.indexes.back() - right.indexes.front();
                        i = r - l; // 降順
                    }
                    if (i == 0) {
                        // コミットが新しいものを優先して左へ
                        int l = left.indexes.front();
                        int r = right.indexes.front();
                        i = l - r; // 昇順
                    }
                    return i < 0;
                });
                // 子の無いブランチ（タグ等）が複数連続しているとき、古いコミットを右に寄せる
                {
                    for (int i = 0; i + 1 < (int)elements.size(); i++) {
                        Element *e = &elements[i];
                        int index1 = e->indexes.front();
                        if (index1 > 0 && !LogItem(index1).has_child) { // 子がない
                            // 新しいコミットを探す
                            for (int j = i + 1; j < (int)elements.size(); j++) { // 現在位置より後ろを探す
                                Element *f = &elements[j];
                                int index2 = f->indexes.front();
                                if (index1 == index2 + 1) { // 一つだけ新しいコミット
                                    Element t = std::move(*f);
                                    elements.erase(elements.begin() + j); // 移動元を削除
                                    elements.insert(elements.begin() + i, std::move(t)); // 現在位置に挿入
                                }
                            }
                            // 古いコミットを探す
                            int j = 0;
                            while (j < i) { // 現在位置より前を探す
                                Element *f = &elements[j];
                                int index2 = f->indexes.front();
                                if (index1 + 1 == index2) { // 一つだけ古いコミット
                                    Element t = std::move(*f);
                                    elements.erase(elements.begin() + j); // 移動元を削除
                                    elements.insert(elements.begin() + i, std::move(t)); // 現在位置の次に挿入
                                    index1 = index2;
                                    f = e;
                                } else {
                                    j++;
                                }
                            }
                        }
                    }
                }
            }
            { // 最初の線は深さを0にする
                Element *e = &elements.front();
                for (size_t i = 0; i < e->indexes.size(); i++) {
                    int index = e->indexes[i];
                    LogItem(index).marker_depth = 0; // マークの深さを設定
                    e->depth = 0; // 線の深さを設定
                }
            }
            // 最初以外の線分の深さを決める
            for (size_t i = 1; i < elements.size(); i++) { // 最初以外をループ
                Element *e = &elements[i];
                int depth = 1;
                while (1) { // 失敗したら繰り返し
                    for (size_t j = 0; j < i; j++) { // 既に処理済みの線を調べる
                        Element const *f = &elements[j]; // 検査対象
                        if (e->indexes.size() == 2) { // 二つしかない場合
                            int from = e->indexes[0]; // 始点
                            int to = e->indexes[1];   // 終点
                            if (LogItem(from).has_child) {
                                for (size_t k = 0; k + 1 < f->indexes.size(); k++) { // 検査対象の全ての線分を調べる
                                    int curr = f->indexes[k];
                                    int next = f->indexes[k + 1];
                                    if (from > curr && to == next) { // 決定済みの線に直結できるか判定
                                        e->indexes.back() = from + 1; // 現在の一行下に直結する
                                        e->depth = elements[j].depth; // 接続先の深さ
                                        goto DONE; // 決定
                                    }
                                }
                            }
                        }
                        if (depth == f->depth) { // 同じ深さ
                            if (e->indexes.back() > f->indexes.front() && e->indexes.front() < f->indexes.back()) { // 重なっている
                                goto FAIL; // この深さには線を置けないのでやりなおし
                            }
                        }
                    }
                    for (size_t j = 0; j < e->indexes.size(); j++) {
                        int index = e->indexes[j];
                        Git::CommitItem *item = &LogItem(index);
                        if (j == 0 && item->has_child) { // 最初のポイントで子がある場合
                            // nop
                        } else if ((j > 0 && j + 1 < e->indexes.size()) || item->marker_depth < 0) { // 最初と最後以外、または、未確定の場合
                            item->marker_depth = depth; // マークの深さを設定
                        }
                    }
                    e->depth = depth; // 深さを決定
                    goto DONE; // 決定
FAIL:;
                    depth++; // 一段深くして再挑戦
                }
DONE:;
            }
            // 線情報を生成する
            for (auto &e : elements) {
                auto ColorNumber = [&](){ return e.depth; };
                size_t count = e.indexes.size();
                for (size_t i = 0; i + 1 < count; i++) {
                    int curr = e.indexes[i];
                    int next = e.indexes[i + 1];
                    TreeLine line(next, e.depth);
                    line.color_number = ColorNumber();
                    line.bend_early = (i + 2 < count || !LogItem(next).resolved);
                    if (i + 2 == count) {
                        int join = false;
                        if (count > 2) { // 直結ではない
                            join = true;
                        } else if (!LogItem(curr).has_child) { // 子がない
                            join = true;
                            int d = LogItem(curr).marker_depth; // 開始点の深さ
                            for (int j = curr + 1; j < next; j++) {
                                Git::CommitItem *item = &LogItem(j);
                                if (item->marker_depth == d) { // 衝突する
                                    join = false; // 迂回する
                                    break;
                                }
                            }
                        }
                        if (join) {
                            line.depth = LogItem(next).marker_depth; // 合流する先のアイテムの深さと同じにする
                        }
                    }
                    LogItem(curr).parent_lines.push_back(line); // 線を追加
                }
            }
        } else {
            if (LogCount == 1) { // コミットが一つだけ
                LogItem(0).marker_depth = 0;
            }
        }
    }
}

void SubmoduleMainWindow::initNetworking()
{
    std::string http_proxy;
    std::string https_proxy;
    if (appsettings()->proxy_type == "auto") {
#ifdef Q_OS_WIN
        http_proxy = misc::makeProxyServerURL(getWin32HttpProxy().toStdString());
#else
        auto getienv = [](std::string const &name)->char const *{
            char **p = environ;
            while (*p) {
                if (strncasecmp(*p, name.c_str(), name.size()) == 0) {
                    char const *e = *p + name.size();
                    if (*e == '=') {
                        return e + 1;
                    }
                }
                p++;
            }
            return nullptr;
        };
        char const *p;
        p = getienv("http_proxy");
        if (p) {
            http_proxy = misc::makeProxyServerURL(std::string(p));
        }
        p = getienv("https_proxy");
        if (p) {
            https_proxy = misc::makeProxyServerURL(std::string(p));
        }
#endif
    } else if (appsettings()->proxy_type == "manual") {
        http_proxy = appsettings()->proxy_server.toStdString();
    }
    webContext()->set_http_proxy(http_proxy);
    webContext()->set_https_proxy(https_proxy);
}

bool SubmoduleMainWindow::saveRepositoryBookmarks() const
{
    QString path = getBookmarksFilePath();
    return RepositoryBookmark::save(path, &getRepos());
}

QString SubmoduleMainWindow::getBookmarksFilePath() const
{
    return global->app_config_dir / "bookmarks.xml";
}

void SubmoduleMainWindow::stopPtyProcess()
{
    getPtyProcess()->stop();
    QDir::setCurrent(m->starting_dir);
}

void SubmoduleMainWindow::abortPtyProcess()
{
    stopPtyProcess();
    setPtyProcessOk(false);
    setInteractionCanceled(true);
}

Git::CommitItemList *SubmoduleMainWindow::getLogsPtr(RepositoryWrapper2Frame *frame)
{
    return &frame->logs;
}

void SubmoduleMainWindow::setLogs(RepositoryWrapper2Frame *frame, const Git::CommitItemList &logs)
{
    frame->logs = logs;
}

void SubmoduleMainWindow::clearLogs(RepositoryWrapper2Frame *frame)
{
    frame->logs.clear();
}

PtyProcess *SubmoduleMainWindow::getPtyProcess()
{
    return &m->pty_process;
}

bool SubmoduleMainWindow::getPtyProcessOk() const
{
    return m->pty_process_ok;
}

SubmoduleMainWindow::PtyCondition SubmoduleMainWindow::getPtyCondition()
{
    return m->pty_condition;
}

void SubmoduleMainWindow::setPtyUserData(const QVariant &userdata)
{
    m->pty_process.setVariant(userdata);
}

void SubmoduleMainWindow::setPtyProcessOk(bool pty_process_ok)
{
    m->pty_process_ok = pty_process_ok;
}

bool SubmoduleMainWindow::fetch(GitPtr g, bool prune)
{
    setPtyCondition(PtyCondition::Fetch);
    setPtyProcessOk(true);
    g->fetch(getPtyProcess(), prune);
    while (1) {
        if (getPtyProcess()->wait(1)) break;
        QApplication::processEvents();
    }
    return getPtyProcessOk();
}

bool SubmoduleMainWindow::fetch_tags_f(GitPtr g)
{
    setPtyCondition(PtyCondition::Fetch);
    setPtyProcessOk(true);
    g->fetch_tags_f(getPtyProcess());
    while (1) {
        if (getPtyProcess()->wait(1)) break;
        QApplication::processEvents();
    }
    return getPtyProcessOk();
}

void SubmoduleMainWindow::setPtyCondition(const SubmoduleMainWindow::PtyCondition &ptyCondition)
{
    m->pty_condition = ptyCondition;
}

const QList<RepositoryItem> &SubmoduleMainWindow::getRepos() const
{
    return m->repos;
}

QList<RepositoryItem> *SubmoduleMainWindow::getReposPtr()
{
    return &m->repos;
}

AvatarLoader *SubmoduleMainWindow::getAvatarLoader()
{
    return &m->avatar_loader;
}

const AvatarLoader *SubmoduleMainWindow::getAvatarLoader() const
{
    return &m->avatar_loader;
}

bool SubmoduleMainWindow::interactionCanceled() const
{
    return m->interaction_canceled;
}

void SubmoduleMainWindow::setInteractionCanceled(bool canceled)
{
    m->interaction_canceled = canceled;
}

SubmoduleMainWindow::InteractionMode SubmoduleMainWindow::interactionMode() const
{
    return m->interaction_mode;
}

void SubmoduleMainWindow::setInteractionMode(const SubmoduleMainWindow::InteractionMode &im)
{
    m->interaction_mode = im;
}

QString SubmoduleMainWindow::getRepositoryFilterText() const
{
    return m->repository_filter_text;
}

void SubmoduleMainWindow::setRepositoryFilterText(const QString &text)
{
    m->repository_filter_text = text;
}

void SubmoduleMainWindow::setUncommitedChanges(bool uncommited_changes)
{
    m->uncommited_changes = uncommited_changes;
}

QList<Git::Diff> *SubmoduleMainWindow::diffResult()
{
    return &m->diff_result;
}

std::map<QString, Git::Diff> *SubmoduleMainWindow::getDiffCacheMap(RepositoryWrapper2Frame *frame)
{
    return &frame->diff_cache;
}

bool SubmoduleMainWindow::getRemoteChanged() const
{
    return m->remote_changed;
}

void SubmoduleMainWindow::setRemoteChanged(bool remote_changed)
{
    m->remote_changed = remote_changed;
}

void SubmoduleMainWindow::setServerType(const ServerType &server_type)
{
    m->server_type = server_type;
}

GitHubRepositoryInfo *SubmoduleMainWindow::ptrGitHub()
{
    return &m->github;
}

std::map<int, QList<BranchLabel> > *SubmoduleMainWindow::getLabelMap(RepositoryWrapper2Frame *frame)
{
    return &frame->label_map;
}

const std::map<int, QList<BranchLabel> > *SubmoduleMainWindow::getLabelMap(const RepositoryWrapper2Frame *frame) const
{
    return &frame->label_map;
}

void SubmoduleMainWindow::clearLabelMap(RepositoryWrapper2Frame *frame)
{
    frame->label_map.clear();
}

GitObjectCache *SubmoduleMainWindow::getObjCache(RepositoryWrapper2Frame *frame)
{
    return &frame->objcache;
}

bool SubmoduleMainWindow::getForceFetch() const
{
    return m->force_fetch;
}

void SubmoduleMainWindow::setForceFetch(bool force_fetch)
{
    m->force_fetch = force_fetch;
}

std::map<QString, QList<Git::Tag> > *SubmoduleMainWindow::ptrTagMap(RepositoryWrapper2Frame *frame)
{
    return &frame->tag_map;
}

QString SubmoduleMainWindow::getHeadId() const
{
    return m->head_id;
}

void SubmoduleMainWindow::setHeadId(const QString &head_id)
{
    m->head_id = head_id;
}

void SubmoduleMainWindow::setPtyProcessCompletionData(const QVariant &value)
{
    m->pty_process_completion_data = value;
}

const QVariant &SubmoduleMainWindow::getTempRepoForCloneCompleteV() const
{
    return m->pty_process_completion_data;
}

void SubmoduleMainWindow::msgNoRepositorySelected()
{
    QMessageBox::warning(this, qApp->applicationName(), tr("No repository selected"));
}

bool SubmoduleMainWindow::isRepositoryOpened() const
{
    return Git::isValidWorkingCopy(currentWorkingCopyDir());
}

QString SubmoduleMainWindow::gitCommand() const
{
    return m->gcx.git_command;
}

QPixmap SubmoduleMainWindow::getTransparentPixmap()
{
    return m->transparent_pixmap;
}

/**
 * @brief リストウィジェット用ファイルアイテムを作成する
 * @param data
 * @return
 */
QListWidgetItem *SubmoduleMainWindow::NewListWidgetFileItem(SubmoduleMainWindow::ObjectData const &data)
{
    const bool issubmodule = data.submod; // サブモジュール

    QString header = data.header; // ヘッダ（バッジ識別子）
    if (header.isEmpty()) {
        header = "(??\?) "; // damn trigraph
    }


    QString text = data.path; // テキスト
    if (issubmodule) {
        text += QString(" <%0> [%1] %2")
                .arg(data.submod.id.mid(0, 7))
                .arg(misc::makeDateTimeString(data.submod_commit.commit_date))
                .arg(data.submod_commit.message)
                ;
    }

    QListWidgetItem *item = new QListWidgetItem(text);
    item->setSizeHint(QSize(item->sizeHint().width(), 18));
    item->setData(FilePathRole, data.path);
    item->setData(DiffIndexRole, data.idiff);
    item->setData(ObjectIdRole, data.id);
    item->setData(HeaderRole, header);
    item->setData(SubmodulePathRole, data.submod.path);
    if (issubmodule) {
        item->setToolTip(text); // ツールチップ
    }
    return item;
}

/**
 * @brief 差分リスト情報をもとにリストウィジェットへアイテムを追加する
 * @param diff_list
 * @param fn_add_item
 */
void SubmoduleMainWindow::addDiffItems(const QList<Git::Diff> *diff_list, const std::function<void (ObjectData const &data)> &fn_add_item)
{
    for (int idiff = 0; idiff < diff_list->size(); idiff++) {
        Git::Diff const &diff = diff_list->at(idiff);
        QString header;

        switch (diff.type) {
        case Git::Diff::Type::Modify:   header = "(chg) "; break;
        case Git::Diff::Type::Copy:     header = "(cpy) "; break;
        case Git::Diff::Type::Rename:   header = "(ren) "; break;
        case Git::Diff::Type::Create:   header = "(add) "; break;
        case Git::Diff::Type::Delete:   header = "(del) "; break;
        case Git::Diff::Type::ChType:   header = "(chg) "; break;
        case Git::Diff::Type::Unmerged: header = "(unmerged) "; break;
        default: header = "() "; break;
        }

        ObjectData data;
        data.id = diff.blob.b_id;
        data.path = diff.path;
        data.submod = diff.b_submodule.item;
        data.submod_commit = diff.b_submodule.commit;
        data.header = header;
        data.idiff = idiff;
        fn_add_item(data);
    }
}

Git::CommitItemList SubmoduleMainWindow::retrieveCommitLog(GitPtr g)
{
    Git::CommitItemList list = g->log(limitLogCount());

    // 親子関係を調べて、順番が狂っていたら、修正する。

    std::set<QString> set;

    const size_t count = list.size();
    size_t limit = count;

    size_t i = 0;
    while (i < count) {
        size_t newpos = (size_t)-1;
        for (QString const &parent : list[i].parent_ids) {
            if (set.find(parent) != set.end()) {
                for (size_t j = 0; j < i; j++) {
                    if (parent == list[j].commit_id) {
                        if (newpos == (size_t)-1 || j < newpos) {
                            newpos = j;
                        }
                        qDebug() << "fix commit order" << list[i].commit_id;
                        break;
                    }
                }
            }
        }
        set.insert(set.end(), list[i].commit_id);
        if (newpos != (size_t)-1) {
            if (limit == 0) break; // まず無いと思うが、もし、無限ループに陥ったら
            Git::CommitItem t = list[i];
            t.strange_date = true;
            list.erase(list.begin() + (int)i);
            list.insert(list.begin() + (int)newpos, t);
            i = newpos;
            limit--;
        }
        i++;
    }

    return list;
}

std::map<QString, QList<Git::Branch> > &SubmoduleMainWindow::branchMapRef(RepositoryWrapper2Frame *frame)
{
    //	return m1->branch_map;
    return frame->branch_map;
}

/**
 * @brief コミットログを更新（100ms遅延）
 */
void SubmoduleMainWindow::updateCommitLogTableLater(RepositoryWrapper2Frame *frame, int ms_later)
{
    postUserFunctionEvent([&](QVariant const &, void *ptr){
        qDebug() << (void *)ptr;
        if (ptr) {
            RepositoryWrapper2Frame *frame = reinterpret_cast<RepositoryWrapper2Frame *>(ptr);
            frame->logtablewidget()->viewport()->update();
        }
    }, {}, reinterpret_cast<void *>(frame), ms_later);
}

void SubmoduleMainWindow::updateWindowTitle(GitPtr g)
{
    if (isValidWorkingCopy(g)) {
        Git::User user = g->getUser(Git::Source::Default);
        setWindowTitle_(user);
    } else {
        setUnknownRepositoryInfo();
    }
}

QString SubmoduleMainWindow::makeCommitInfoText(RepositoryWrapper2Frame *frame, int row, QList<BranchLabel> *label_list)
{
    QString message_ex;
    Git::CommitItem const *commit = &getLogs(frame)[row];
    { // branch
        if (label_list) {
            if (commit->commit_id == getHeadId()) {
                BranchLabel label(BranchLabel::Head);
                label.text = "HEAD";
                label_list->push_back(label);
            }
        }
        QList<Git::Branch> list = findBranch(frame, commit->commit_id);
        for (Git::Branch const &b : list) {
            if (b.flags & Git::Branch::HeadDetachedAt) continue;
            if (b.flags & Git::Branch::HeadDetachedFrom) continue;
            BranchLabel label(BranchLabel::LocalBranch);
            label.text = b.name;
            if (!b.remote.isEmpty()) {
                label.kind = BranchLabel::RemoteBranch;
                label.text = "remotes" / b.remote / label.text;
            }
            if (b.ahead > 0) {
                label.info += tr(", %1 ahead").arg(b.ahead);
            }
            if (b.behind > 0) {
                label.info += tr(", %1 behind").arg(b.behind);
            }
            message_ex += " {" + label.text + label.info + '}';
            if (label_list) label_list->push_back(label);
        }
    }
    { // tag
        QList<Git::Tag> list = findTag(frame, commit->commit_id);
        for (Git::Tag const &t : list) {
            BranchLabel label(BranchLabel::Tag);
            label.text = t.name;
            message_ex += QString(" {#%1}").arg(label.text);
            if (label_list) label_list->push_back(label);
        }
    }
    return message_ex;
}

void SubmoduleMainWindow::removeRepositoryFromBookmark(int index, bool ask)
{
    if (ask) {
        int r = QMessageBox::warning(this, tr("Confirm Remove"), tr("Are you sure you want to remove the repository from bookmarks?") + '\n' + tr("(Files will NOT be deleted)"), QMessageBox::Ok, QMessageBox::Cancel);
        if (r != QMessageBox::Ok) return;
    }
    auto *repos = getReposPtr();
    if (index >= 0 && index < repos->size()) {
        repos->erase(repos->begin() + index);
        saveRepositoryBookmarks();
        updateRepositoriesList();
    }
}

void SubmoduleMainWindow::openTerminal(const RepositoryItem *repo)
{
    runOnRepositoryDir([](QString dir, QString ssh_key){
#ifdef Q_OS_MAC
        if (!isValidDir(dir)) return;
        QString cmd = "open -n -a /Applications/Utilities/Terminal.app --args \"%1\"";
        cmd = cmd.arg(dir);
        QProcess::execute(cmd);
#else
        Terminal::open(dir, ssh_key);
#endif
    }, repo);
}

void SubmoduleMainWindow::openExplorer(const RepositoryItem *repo)
{
    runOnRepositoryDir([](QString dir, QString ssh_key){
        (void)ssh_key;
#ifdef Q_OS_MAC
        if (!isValidDir(dir)) return;
        QString cmd = "open \"%1\"";
        cmd = cmd.arg(dir);
        QProcess::execute(cmd);
#else
        QString url = QString::fromLatin1(QUrl::toPercentEncoding(dir));
#ifdef Q_OS_WIN
        QString scheme = "file:///";
#else
        QString scheme = "file://";
#endif
        url = scheme + url.replace("%2F", "/");
        QDesktopServices::openUrl(url);
#endif
    }, repo);
}

bool SubmoduleMainWindow::askAreYouSureYouWantToRun(const QString &title, const QString &command)
{
    QString message = tr("Are you sure you want to run the following command?");
    QString text = "%1\n\n%2";
    text = text.arg(message).arg(command);
    return QMessageBox::warning(this, title, text, QMessageBox::Ok, QMessageBox::Cancel) == QMessageBox::Ok;
}

bool SubmoduleMainWindow::editFile(const QString &path, const QString &title)
{
    return TextEditDialog::editFile(this, path, title);
}

void SubmoduleMainWindow::setAppSettings(const ApplicationSettings &appsettings)
{
    global->appsettings = appsettings;
}

QIcon SubmoduleMainWindow::getRepositoryIcon() const
{
    return m->repository_icon;
}

QIcon SubmoduleMainWindow::getFolderIcon() const
{
    return m->folder_icon;
}

QIcon SubmoduleMainWindow::getSignatureGoodIcon() const
{
    return m->signature_good_icon;
}

QIcon SubmoduleMainWindow::getSignatureDubiousIcon() const
{
    return m->signature_dubious_icon;
}

QIcon SubmoduleMainWindow::getSignatureBadIcon() const
{
    return m->signature_bad_icon;
}

QPixmap SubmoduleMainWindow::getTransparentPixmap() const
{
    return m->transparent_pixmap;
}

QStringList SubmoduleMainWindow::findGitObject(const QString &id) const
{
    QStringList list;
    std::set<QString> set;
    if (Git::isValidID(id)) {
        {
            QString a = id.mid(0, 2);
            QString b = id.mid(2);
            QString dir = m->current_repo.local_dir / ".git/objects" / a;
            QDirIterator it(dir);
            while (it.hasNext()) {
                it.next();
                QFileInfo info = it.fileInfo();
                if (info.isFile()) {
                    QString c = info.fileName();
                    if (c.startsWith(b)) {
                        set.insert(set.end(), a + c);
                    }
                }
            }
        }
        {
            QString dir = m->current_repo.local_dir / ".git/objects/pack";
            QDirIterator it(dir);
            while (it.hasNext()) {
                it.next();
                QFileInfo info = it.fileInfo();
                if (info.isFile() && info.fileName().startsWith("pack-") && info.fileName().endsWith(".idx")) {
                    GitPackIdxV2 idx;
                    idx.parse(info.absoluteFilePath());
                    idx.each([&](GitPackIdxItem const *item){
                        if (item->id.startsWith(id)) {
                            set.insert(item->id);
                        }
                        return true;
                    });
                }
            }
        }
        for (QString const &s : set) {
            list.push_back(s);
        }
    }
    return list;
}

void SubmoduleMainWindow::writeLog(const char *ptr, int len)
{
    internalWriteLog(ptr, len);
}

void SubmoduleMainWindow::writeLog(const QString &str)
{
    std::string s = str.toStdString();
    writeLog(s.c_str(), (int)s.size());
}

QList<BranchLabel> SubmoduleMainWindow::sortedLabels(RepositoryWrapper2Frame *frame, int row) const
{
    QList<BranchLabel> list;
    auto const *p = const_cast<SubmoduleMainWindow *>(this)->label(frame, row);
    if (p && !p->empty()) {
        list = *p;
        std::sort(list.begin(), list.end(), [](BranchLabel const &l, BranchLabel const &r){
            auto Compare = [](BranchLabel const &l, BranchLabel const &r){
                if (l.kind < r.kind) return -1;
                if (l.kind > r.kind) return 1;
                if (l.text < r.text) return -1;
                if (l.text > r.text) return 1;
                return 0;
            };
            return Compare(l, r) < 0;
        });
    }
    return list;
}

void SubmoduleMainWindow::saveApplicationSettings()
{
    SettingsDialog::saveSettings(appsettings());
}

void SubmoduleMainWindow::loadApplicationSettings()
{
    SettingsDialog::loadSettings(appsettings());
}

void SubmoduleMainWindow::setDiffResult(const QList<Git::Diff> &diffs)
{
    m->diff_result = diffs;
}

const QList<Git::SubmoduleItem> &SubmoduleMainWindow::submodules() const
{
    return m->submodules;
}

void SubmoduleMainWindow::setSubmodules(const QList<Git::SubmoduleItem> &submodules)
{
    m->submodules = submodules;
}

bool SubmoduleMainWindow::runOnRepositoryDir(const std::function<void (QString, QString)> &callback, const RepositoryItem *repo)
{
    if (!repo) {
        repo = &m->current_repo;
    }
    QString dir = repo->local_dir;
    dir.replace('\\', '/');
    if (QFileInfo(dir).isDir()) {
        callback(dir, repo->ssh_key);
        return true;
    }
    msgNoRepositorySelected();
    return false;
}

NamedCommitList SubmoduleMainWindow::namedCommitItems(RepositoryWrapper2Frame *frame, int flags)
{
    NamedCommitList items;
    if (flags & Branches) {
        for (auto const &pair: branchMapRef(frame)) {
            QList<Git::Branch> const &list = pair.second;
            for (Git::Branch const &b : list) {
                if (b.isHeadDetached()) continue;
                if (flags & NamedCommitFlag::Remotes) {
                    // nop
                } else {
                    if (!b.remote.isEmpty()) continue;
                }
                NamedCommitItem item;
                if (b.remote.isEmpty()) {
                    if (b.name == "HEAD") {
                        item.type = NamedCommitItem::Type::None;
                    } else {
                        item.type = NamedCommitItem::Type::BranchLocal;
                    }
                } else {
                    item.type = NamedCommitItem::Type::BranchRemote;
                    item.remote = b.remote;
                }
                item.name = b.name;
                item.id = b.id;
                items.push_back(item);
            }
        }
    }
    if (flags & Tags) {
        for (auto const &pair: *ptrTagMap(frame)) {
            QList<Git::Tag> const &list = pair.second;
            for (Git::Tag const &t : list) {
                NamedCommitItem item;
                item.type = NamedCommitItem::Type::Tag;
                item.name = t.name;
                item.id = t.id;
                if (item.name.startsWith("refs/tags/")) {
                    item.name = item.name.mid(10);
                }
                items.push_back(item);
            }
        }
    }
    return items;
}

QString SubmoduleMainWindow::getObjectID(QListWidgetItem *item)
{
    if (!item) return {};
    return item->data(ObjectIdRole).toString();
}

QString SubmoduleMainWindow::getFilePath(QListWidgetItem *item)
{
    if (!item) return {};
    return item->data(FilePathRole).toString();
}

QString SubmoduleMainWindow::getSubmodulePath(QListWidgetItem *item)
{
    if (!item) return {};
    return item->data(SubmodulePathRole).toString();
}

bool SubmoduleMainWindow::isGroupItem(QTreeWidgetItem *item)
{
    if (item) {
        int index = item->data(0, IndexRole).toInt();
        if (index == GroupItem) {
            return true;
        }
    }
    return false;
}

int SubmoduleMainWindow::indexOfLog(QListWidgetItem *item)
{
    if (!item) return -1;
    return item->data(IndexRole).toInt();
}

int SubmoduleMainWindow::indexOfDiff(QListWidgetItem *item)
{
    if (!item) return -1;
    return item->data(DiffIndexRole).toInt();
}

/**
 * @brief ファイルリストを更新
 * @param id コミットID
 * @param wait
 */
void SubmoduleMainWindow::updateFilesList(RepositoryWrapper2Frame *frame, QString id, bool wait)
{
    GitPtr g = git();
    if (!isValidWorkingCopy(g)) return;

    if (!wait) return;

    clearFileList(frame);

    Git::FileStatusList stats = g->status_s();
    setUncommitedChanges(!stats.empty());

    FilesListType files_list_type = FilesListType::SingleList;

    bool staged = false;
    auto AddItem = [&](ObjectData const &data){
        QListWidgetItem *item = NewListWidgetFileItem(data);
        switch (files_list_type) {
        case FilesListType::SingleList:
            frame->fileslistwidget()->addItem(item);
            break;
        case FilesListType::SideBySide:
            if (staged) {
                frame->stagedFileslistwidget()->addItem(item);
            } else {
                frame->unstagedFileslistwidget()->addItem(item);
            }
            break;
        }
    };

    if (id.isEmpty()) {
        bool uncommited = isThereUncommitedChanges();
        if (uncommited) {
            files_list_type = FilesListType::SideBySide;
        }
        bool ok = false;
        auto diffs = makeDiffs(frame, uncommited ? QString() : id, &ok);
        setDiffResult(diffs);
        if (!ok) return;

        std::map<QString, int> diffmap;

        for (int idiff = 0; idiff < diffResult()->size(); idiff++) {
            Git::Diff const &diff = diffResult()->at(idiff);
            QString filename = diff.path;
            if (!filename.isEmpty()) {
                diffmap[filename] = idiff;
            }
        }

        showFileList(files_list_type);

        for (Git::FileStatus const &s : stats) {
            staged = (s.isStaged() && s.code_y() == ' ');
            int idiff = -1;
            QString header;
            auto it = diffmap.find(s.path1());
            Git::Diff const *diff = nullptr;
            if (it != diffmap.end()) {
                idiff = it->second;
                diff = &diffResult()->at(idiff);
            }
            QString path = s.path1();
            if (s.code() == Git::FileStatusCode::Unknown) {
                qDebug() << "something wrong...";
            } else if (s.code() == Git::FileStatusCode::Untracked) {
                // nop
            } else if (s.isUnmerged()) {
                header += "(unmerged) ";
            } else if (s.code() == Git::FileStatusCode::AddedToIndex) {
                header = "(add) ";
            } else if (s.code_x() == 'D' || s.code_y() == 'D' || s.code() == Git::FileStatusCode::DeletedFromIndex) {
                header = "(del) ";
            } else if (s.code_x() == 'R' || s.code() == Git::FileStatusCode::RenamedInIndex) {
                header = "(ren) ";
                path = s.path2(); // renamed newer path
            } else if (s.code_x() == 'M' || s.code_y() == 'M') {
                header = "(chg) ";
            }
            ObjectData data;
            data.path = path;
            data.header = header;
            data.idiff = idiff;
            if (diff) {
                data.submod = diff->b_submodule.item; // TODO:
                if (data.submod) {
                    GitPtr g = git(data.submod);
                    g->queryCommit(data.submod.id, &data.submod_commit);
                }
            }
            AddItem(data);
        }
    } else {
        bool ok = false;
        auto diffs = makeDiffs(frame, id, &ok);
        setDiffResult(diffs);
        if (!ok) return;

        showFileList(files_list_type);
        addDiffItems(diffResult(), AddItem);
    }

    for (Git::Diff const &diff : *diffResult()) {
        QString key = GitDiff::makeKey(diff);
        (*getDiffCacheMap(frame))[key] = diff;
    }
}

/**
 * @brief ファイルリストを更新
 * @param id
 * @param diff_list
 * @param listwidget
 */
void SubmoduleMainWindow::updateFilesList2(RepositoryWrapper2Frame *frame, QString const &id, QList<Git::Diff> *diff_list, QListWidget *listwidget)
{
    GitPtr g = git();
    if (!isValidWorkingCopy(g)) return;

    listwidget->clear();

    auto AddItem = [&](ObjectData const &data){
        QListWidgetItem *item = NewListWidgetFileItem(data);
        listwidget->addItem(item);
    };

    GitDiff dm(getObjCache(frame));
    if (!dm.diff(id, submodules(), diff_list)) return;

    addDiffItems(diff_list, AddItem);
}

void SubmoduleMainWindow::execCommitViewWindow(const Git::CommitItem *commit)
{
    CommitViewWindow win((MainWindow *)this, commit);
    win.exec();
}

void SubmoduleMainWindow::updateFilesList(RepositoryWrapper2Frame *frame, Git::CommitItem const &commit, bool wait)
{
    QString id;
    if (Git::isUncommited(commit)) {
        // empty id for uncommited changes
    } else {
        id = commit.commit_id;
    }
    updateFilesList(frame, id, wait);
}

void SubmoduleMainWindow::updateCurrentFilesList(RepositoryWrapper2Frame *frame)
{
    auto logs = getLogs(frame);
    qDebug()<<"updateCurrentFilesList";
    QTableWidgetItem *item = frame->logtablewidget()->item(selectedLogIndex(frame), 0);
    if (!item) return;
    int index = item->data(IndexRole).toInt();
    int count = (int)logs.size();
    if (index < count) {
        updateFilesList(frame, logs[index], true);
    }
}

void SubmoduleMainWindow::detectGitServerType(GitPtr g)
{
    setServerType(ServerType::Standard);
    *ptrGitHub() = GitHubRepositoryInfo();
    qDebug()<<"detectGitServerType";
    QString push_url;
    QList<Git::Remote> remotes;
    qDebug()<<"detectGitServerType1";
    g->getRemoteURLs(&remotes);
    qDebug()<<"detectGitServerType2";
    for (Git::Remote const &r : remotes) {
        if (r.purpose == "push") {
            push_url = r.url;
        }
    }

    auto Check = [&](QString const &s){
        int i = push_url.indexOf(s);
        if (i > 0) return i + s.size();
        return 0;
    };

    // check GitHub
    int pos = Check("@github.com:");
    if (pos == 0) {
        pos = Check("://github.com/");
    }
    if (pos > 0) {
        int end = push_url.size();
        {
            QString s = ".git";
            if (push_url.endsWith(s)) {
                end -= s.size();
            }
        }
        QString s = push_url.mid(pos, end - pos);
        int i = s.indexOf('/');
        if (i > 0) {
            auto *p = ptrGitHub();
            QString user = s.mid(0, i);
            QString repo = s.mid(i + 1);
            p->owner_account_name = user;
            p->repository_name = repo;
        }
        setServerType(ServerType::GitHub);
    }
}

void SubmoduleMainWindow::clearLog(RepositoryWrapper2Frame *frame)
{
    clearLogs(frame);
    qDebug()<<"clearLog";
    clearLabelMap(frame);
    qDebug()<<"clearLog1";
    setUncommitedChanges(false);
    qDebug()<<"clearLog2";
    frame->clearLogContents();
    qDebug()<<"clearLog3";
}

void SubmoduleMainWindow::openRepository_(GitPtr g, bool keep_selection)
{
    openRepository_(frame(), g, keep_selection);
}

void SubmoduleMainWindow::openRepository_(RepositoryWrapper2Frame *frame, GitPtr g, bool keep_selection)
{
    getObjCache(frame)->setup(g);

    int scroll_pos = -1;
    int select_row = -1;

    if (keep_selection) {
        scroll_pos = frame->logtablewidget()->verticalScrollBar()->value();
        select_row = frame->logtablewidget()->currentRow();
    }
    qDebug()<<"openRepository_";
    if (isValidWorkingCopy(g), 1) { ///
	qDebug()<<"openRepository_2";
        bool do_fetch = isOnlineMode() && (getForceFetch() || appsettings()->automatically_fetch_when_opening_the_repository);
        qDebug()<<"openRepository_3";
        setForceFetch(false);
        qDebug()<<"openRepository_4";
        if (do_fetch) {
        qDebug()<<"openRepository_5";
           // if (!fetch(g, false)) {
           //         qDebug()<<"openRepository_6";
            //    return;
           // }
        }
qDebug()<<"openRepository_r";
        clearLog(frame);
        clearRepositoryInfo();
qDebug()<<"openRepository_r1";
        detectGitServerType(g);
qDebug()<<"openRepository_r2";
        updateFilesList(frame, QString(), true);
qDebug()<<"openRepository_r3";
        bool canceled = false;
        frame->logtablewidget()->setEnabled(false);
qDebug()<<"openRepository_r4";
        // ログを取得
        setLogs(frame, retrieveCommitLog(g));
        // ブランチを取得
        queryBranches(frame, g);
        // タグを取得
        ptrTagMap(frame)->clear();
        QList<Git::Tag> tags = g->tags();
        for (Git::Tag const &tag : tags) {
            Git::Tag t = tag;
            t.id = getObjCache(frame)->getCommitIdFromTag(t.id);
            (*ptrTagMap(frame))[t.id].push_back(t);
        }

        frame->logtablewidget()->setEnabled(true);
        updateCommitLogTableLater(frame, 100); // ミコットログを更新（100ms後）
        if (canceled) return;

        QString branch_name;
        if (currentBranch().flags & Git::Branch::HeadDetachedAt) {
            branch_name += QString("(HEAD detached at %1)").arg(currentBranchName());
        }
        if (currentBranch().flags & Git::Branch::HeadDetachedFrom) {
            branch_name += QString("(HEAD detached from %1)").arg(currentBranchName());
        }
        if (branch_name.isEmpty()) {
            branch_name = currentBranchName();
        }

        QString repo_name = currentRepositoryName();
        setRepositoryInfo(repo_name, branch_name);
    } else {
        clearLog(frame);
        clearRepositoryInfo();
    }

    if (!g) return;

    updateRemoteInfo();

    updateWindowTitle(g);

    setHeadId(getObjCache(frame)->revParse("HEAD"));

    if (isThereUncommitedChanges()) {
        Git::CommitItem item;
        item.parent_ids.push_back(currentBranch().id);
        item.message = tr("Uncommited changes");
        auto p = getLogsPtr(frame);
        p->insert(p->begin(), item);
    }

    frame->prepareLogTableWidget();

    auto const &logs = getLogs(frame);
    const int count = (int)logs.size();

    frame->logtablewidget()->setRowCount(count);

    int selrow = 0;

    for (int row = 0; row < count; row++) {
        Git::CommitItem const *commit = &logs[row];
        {
            auto *item = new QTableWidgetItem;
            item->setData(IndexRole, row);
            frame->logtablewidget()->setItem(row, 0, item);
        }
        int col = 1; // カラム0はコミットグラフなので、その次から
        auto AddColumn = [&](QString const &text, bool bold, QString const &tooltip){
            auto *item = new QTableWidgetItem(text);
            if (!tooltip.isEmpty()) {
                QString tt = tooltip;
                tt.replace('\n', ' ');
                tt = tt.toHtmlEscaped();
                tt = "<p style='white-space: pre'>" + tt + "</p>";
                item->setToolTip(tt);
            }
            if (bold) {
                QFont font = item->font();
                font.setBold(true);
                item->setFont(font);
            }
            frame->logtablewidget()->setItem(row, col, item);
            col++;
        };
        QString commit_id;
        QString datetime;
        QString author;
        QString message;
        QString message_ex;
        bool isHEAD = (commit->commit_id == getHeadId());
        bool bold = false;
        {
            if (Git::isUncommited(*commit)) { // 未コミットの時
                bold = true; // 太字
                selrow = row;
            } else {
                if (isHEAD && !isThereUncommitedChanges()) { // HEADで、未コミットがないとき
                    bold = true; // 太字
                    selrow = row;
                }
                commit_id = abbrevCommitID(*commit);
            }
            datetime = misc::makeDateTimeString(commit->commit_date);
            author = commit->author;
            message = commit->message;
            message_ex = makeCommitInfoText(frame, row, &(*getLabelMap(frame))[row]);
        }
        AddColumn(commit_id, false, QString());
        AddColumn(datetime, false, QString());
        AddColumn(author, false, QString());
        AddColumn(message, bold, message + message_ex);
        frame->logtablewidget()->setRowHeight(row, 24);
    }
    int t = frame->logtablewidget()->columnWidth(0);
    frame->logtablewidget()->resizeColumnsToContents();
    frame->logtablewidget()->setColumnWidth(0, t);
    frame->logtablewidget()->horizontalHeader()->setStretchLastSection(false);
    frame->logtablewidget()->horizontalHeader()->setStretchLastSection(true);

    m->last_focused_file_list = nullptr;

    frame->logtablewidget()->setFocus();

    if (select_row < 0) {
        setCurrentLogRow(frame, selrow);
    } else {
        setCurrentLogRow(frame, select_row);
        frame->logtablewidget()->verticalScrollBar()->setValue(scroll_pos >= 0 ? scroll_pos : 0);
    }

    updateUI();
}

void SubmoduleMainWindow::removeSelectedRepositoryFromBookmark(bool ask)
{
    int i = indexOfRepository(ui->treeWidget_repos->currentItem());
    removeRepositoryFromBookmark(i, ask);
}

void SubmoduleMainWindow::setNetworkingCommandsEnabled(bool enabled)
{
    ui->action_clone->setEnabled(enabled);

    ui->toolButton_clone->setEnabled(enabled);

    if (!Git::isValidWorkingCopy(currentWorkingCopyDir())) {
        enabled = false;
    }

    bool opened = !currentRepository().name.isEmpty();
    ui->action_fetch->setEnabled(enabled || opened);
    ui->toolButton_fetch->setEnabled(enabled || opened);

    if (isOnlineMode()) {
        ui->action_fetch->setText(tr("Fetch"));
        ui->toolButton_fetch->setText(tr("Fetch"));
    } else {
        ui->action_fetch->setText(tr("Update"));
        ui->toolButton_fetch->setText(tr("Update"));
    }

    ui->action_fetch_prune->setEnabled(enabled);
    ui->action_pull->setEnabled(enabled);
    ui->action_push->setEnabled(enabled);
    ui->action_push_u->setEnabled(enabled);
    ui->action_push_all_tags->setEnabled(enabled);

    ui->toolButton_pull->setEnabled(enabled);
    ui->toolButton_push->setEnabled(enabled);
}

void SubmoduleMainWindow::updateUI()
{
    setNetworkingCommandsEnabled(isOnlineMode());

    ui->toolButton_fetch->setDot(getRemoteChanged());

    Git::Branch b = currentBranch();
    ui->toolButton_push->setNumber(b.ahead > 0 ? b.ahead : -1);
    ui->toolButton_pull->setNumber(b.behind > 0 ? b.behind : -1);

    {
        bool f = isRepositoryOpened();
        ui->toolButton_status->setEnabled(f);
        ui->toolButton_terminal->setEnabled(f);
        ui->toolButton_explorer->setEnabled(f);
        ui->action_repository_status->setEnabled(f);
        ui->action_terminal->setEnabled(f);
        ui->action_explorer->setEnabled(f);
    }
}

void SubmoduleMainWindow::updateStatusBarText(RepositoryWrapper2Frame *frame)
{
    QString text;
    qDebug()<<"SubmoduleMainWindow updateStatusBarText";
    QWidget *w = qApp->focusWidget();
    if (w == ui->treeWidget_repos) {
        RepositoryItem const *repo = selectedRepositoryItem();
        if (repo) {
            text = QString("%1 : %2")
                    .arg(repo->name)
                    .arg(misc::normalizePathSeparator(repo->local_dir))
                    ;
        }
    } else if (w == frame->logtablewidget()) {
        QTableWidgetItem *item = frame->logtablewidget()->item(selectedLogIndex(frame), 0);
        if (item) {
            auto const &logs = getLogs(frame);
            int row = item->data(IndexRole).toInt();
            if (row < (int)logs.size()) {
                Git::CommitItem const &commit = logs[row];
                if (Git::isUncommited(commit)) {
                    text = tr("Uncommited changes");
                } else {
                    QString id = commit.commit_id;
                    text = QString("%1 : %2%3")
                            .arg(id.mid(0, 7))
                            .arg(commit.message)
                            .arg(makeCommitInfoText(frame, row, nullptr))
                            ;
                }
            }
        }
    }
    qDebug()<<"SubmoduleMainWindow updateStatusBarText"<<text;
    setStatusBarText(text);
}

void SubmoduleMainWindow::mergeBranch(QString const &commit, Git::MergeFastForward ff, bool squash)
{
    if (commit.isEmpty()) return;

    GitPtr g = git();
    if (!isValidWorkingCopy(g)) return;

    g->mergeBranch(commit, ff, squash);
    openRepository(true);
}

void SubmoduleMainWindow::mergeBranch(Git::CommitItem const *commit, Git::MergeFastForward ff, bool squash)
{
    if (!commit) return;
    mergeBranch(commit->commit_id, ff, squash);
}

void SubmoduleMainWindow::rebaseBranch(Git::CommitItem const *commit)
{
    if (!commit) return;

    GitPtr g = git();
    if (!isValidWorkingCopy(g)) return;

    QString text = tr("Are you sure you want to rebase the commit?");
    text += "\n\n";
    text += "> git rebase " + commit->commit_id;
    int r = QMessageBox::information(this, tr("Rebase"), text, QMessageBox::Ok, QMessageBox::Cancel);
    if (r == QMessageBox::Ok) {
        g->rebaseBranch(commit->commit_id);
        openRepository(true);
    }
}

void SubmoduleMainWindow::cherrypick(Git::CommitItem const *commit)
{
    if (!commit) return;

    GitPtr g = git();
    if (!isValidWorkingCopy(g)) return;



    int n = commit->parent_ids.size();
    if (n == 1) {
        g->cherrypick(commit->commit_id);
    } else if (n > 1) {
        Git::CommitItem head;
        Git::CommitItem pick;
        g->queryCommit(g->rev_parse("HEAD"), &head);
        g->queryCommit(commit->commit_id, &pick);
        QList<Git::CommitItem> parents;
        for (int i = 0; i < n; i++) {
            QString id = commit->commit_id + QString("^%1").arg(i + 1);
            id = g->rev_parse(id);
            Git::CommitItem item;
            g->queryCommit(id, &item);
            parents.push_back(item);
        }
        CherryPickDialog dlg((MainWindow *)this, head, pick, parents);
        if (dlg.exec() == QDialog::Accepted) {
            QString cmd = "-m %1 ";
            cmd = cmd.arg(dlg.number());
            if (dlg.allowEmpty()) {
                cmd += "--allow-empty ";
            }
            cmd += commit->commit_id;
            g->cherrypick(cmd);
        } else {
            return;
        }
    }

    openRepository(true);
}

void SubmoduleMainWindow::merge(RepositoryWrapper2Frame *frame, Git::CommitItem const *commit)
{
    if (isThereUncommitedChanges()) return;

    if (!commit) {
        int row = selectedLogIndex(frame);
        commit = commitItem(frame, row);
        if (!commit) return;
    }

    if (!Git::isValidID(commit->commit_id)) return;

    static const char MergeFastForward[] = "MergeFastForward";

    QString fastforward;
    {
        MySettings s;
        s.beginGroup("Behavior");
        fastforward = s.value(MergeFastForward).toString();
        s.endGroup();
    }

    std::vector<QString> labels;
    {
        int row = selectedLogIndex(frame);
        QList<BranchLabel> const *v = label(frame, row);
        for (BranchLabel const &label : *v) {
            if (label.kind == BranchLabel::LocalBranch || label.kind == BranchLabel::Tag) {
                labels.push_back(label.text);
            }
        }
        std::sort(labels.begin(), labels.end());
        labels.erase(std::unique(labels.begin(), labels.end()), labels.end());
    }

    labels.push_back(commit->commit_id);

    QString branch_name = currentBranchName();

    MergeDialog dlg(fastforward, labels, branch_name, this);
    if (dlg.exec() == QDialog::Accepted) {
        fastforward = dlg.getFastForwardPolicy();
        bool squash = dlg.isSquashEnabled();
        {
            MySettings s;
            s.beginGroup("Behavior");
            s.setValue(MergeFastForward, fastforward);
            s.endGroup();
        }
        QString from = dlg.mergeFrom();
        mergeBranch(from, MergeDialog::ff(fastforward), squash);
    }
}

void SubmoduleMainWindow::showStatus()
{
    auto g = git();
    if (!g->isValidWorkingCopy()) {
        msgNoRepositorySelected();
        return;
    }
    QString s = g->status();
    TextEditDialog dlg((MainWindow *)this);
    dlg.setWindowTitle(tr("Status"));
    dlg.setText(s, true);
    dlg.exec();
}

void SubmoduleMainWindow::on_action_commit_triggered()
{
    commit(frame());
}

void SubmoduleMainWindow::on_action_fetch_triggered()
{
    if (isOnlineMode()) {
        reopenRepository(true, [&](GitPtr g){
            fetch(g, false);
        });
    } else {
        updateRepository();
    }
}



void SubmoduleMainWindow::on_action_fetch_prune_triggered()
{
    if (!isOnlineMode()) return;

    reopenRepository(true, [&](GitPtr g){
        fetch(g, true);
    });
}

void SubmoduleMainWindow::on_action_push_triggered()
{
    push();
}

void SubmoduleMainWindow::on_action_pull_triggered()
{
    if (!isOnlineMode()) return;

    reopenRepository(true, [&](GitPtr g){
        setPtyCondition(PtyCondition::Pull);
        setPtyProcessOk(true);
        g->pull(getPtyProcess());
        while (1) {
            if (getPtyProcess()->wait(1)) break;
            QApplication::processEvents();
        }
    });
}

void SubmoduleMainWindow::on_toolButton_push_clicked()
{
    ui->action_push->trigger();
}

void SubmoduleMainWindow::on_toolButton_pull_clicked()
{
    ui->action_pull->trigger();
}

void SubmoduleMainWindow::on_toolButton_status_clicked()
{
    showStatus();
}

void SubmoduleMainWindow::on_action_repository_status_triggered()
{
    showStatus();
}

void SubmoduleMainWindow::on_treeWidget_repos_currentItemChanged(QTreeWidgetItem * /*current*/, QTreeWidgetItem * /*previous*/)
{
    qDebug()<<"on_treeWidget_repos_currentItemChanged";
    updateStatusBarText(frame());
}

void SubmoduleMainWindow::on_treeWidget_repos_itemDoubleClicked(QTreeWidgetItem * /*item*/, int /*column*/)
{
    qDebug()<<"on_treeWidget_repos_itemDoubleClicked";
    openSelectedRepository();
}

void SubmoduleMainWindow::execCommitPropertyDialog(QWidget *parent, RepositoryWrapper2Frame *frame, Git::CommitItem const *commit)
{
    CommitPropertyDialog dlg(parent, (MainWindow *)this, (RepositoryWrapperFrame *)frame, commit);
    dlg.exec();
}

void SubmoduleMainWindow::execCommitExploreWindow(RepositoryWrapper2Frame *frame, QWidget *parent, const Git::CommitItem *commit)
{
    CommitExploreWindow win(parent, (MainWindow *)this, getObjCache(frame), commit);
    win.exec();
}

void SubmoduleMainWindow::execFileHistory(const QString &path)
{
    if (path.isEmpty()) return;

    GitPtr g = git();
    if (!isValidWorkingCopy(g)) return;

    FileHistoryWindow dlg((MainWindow *)this);
    dlg.prepare(g, path);
    dlg.exec();
}

int SubmoduleMainWindow::indexOfRepository(QTreeWidgetItem const *treeitem) const
{
    if (!treeitem) return -1;
    return treeitem->data(0, IndexRole).toInt();
}

void SubmoduleMainWindow::on_treeWidget_repos_customContextMenuRequested(const QPoint &pos)
{
    QTreeWidgetItem *treeitem = ui->treeWidget_repos->currentItem();
    if (!treeitem) return;

    RepositoryItem const *repo = repositoryItem(treeitem);
    qDebug()<<"on_treeWidget_repos_customContextMenuRequested";
    int index = indexOfRepository(treeitem);
    if (isGroupItem(treeitem)) { // group item
        QMenu menu;
        QAction *a_add_new_group = menu.addAction(tr("&Add new group"));
        QAction *a_delete_group = menu.addAction(tr("&Delete group"));
        QAction *a_rename_group = menu.addAction(tr("&Rename group"));
        QPoint pt = ui->treeWidget_repos->mapToGlobal(pos);
        QAction *a = menu.exec(pt + QPoint(8, -8));
        if (a) {
            if (a == a_add_new_group) {
                QTreeWidgetItem *child = newQTreeWidgetFolderItem(tr("New group"));
                treeitem->addChild(child);
                child->setFlags(child->flags() | Qt::ItemIsEditable);
                ui->treeWidget_repos->setCurrentItem(child);
                return;
            }
            if (a == a_delete_group) {
                QTreeWidgetItem *parent = treeitem->parent();
                if (parent) {
                    int i = parent->indexOfChild(treeitem);
                    delete parent->takeChild(i);
                } else {
                    int i = ui->treeWidget_repos->indexOfTopLevelItem(treeitem);
                    delete ui->treeWidget_repos->takeTopLevelItem(i);
                }
                refrectRepositories();
                return;
            }
            if (a == a_rename_group) {
                ui->treeWidget_repos->editItem(treeitem);
                return;
            }
        }
    } else if (repo) { // repository item
        QString open_terminal = tr("Open &terminal");
        QString open_commandprompt = tr("Open command promp&t");
        QMenu menu;
        QAction *a_open = menu.addAction(tr("&Open"));
        menu.addSeparator();
#ifdef Q_OS_WIN
        QAction *a_open_terminal = menu.addAction(open_commandprompt);
        (void)open_terminal;
#else
        QAction *a_open_terminal = menu.addAction(open_terminal);
        (void)open_commandprompt;
#endif
        a_open_terminal->setIcon(QIcon(":/image/terminal.svg"));
        menu.addSeparator();
        QAction *a_open_folder = menu.addAction(tr("Open &folder"));
        a_open_folder->setIcon(QIcon(":/image/explorer.svg"));

        menu.addSeparator();

        QAction *a_remove = menu.addAction(tr("&Remove"));

        menu.addSeparator();
        QAction *a_open_patch_repository = menu.addAction(tr("Open patch_repository"));
        menu.addSeparator();
        QAction *a_properties = addMenuActionProperty(&menu);

        QPoint pt = ui->treeWidget_repos->mapToGlobal(pos);
        QAction *a = menu.exec(pt + QPoint(8, -8));

        if (a) {
            if (a == a_open) {
                openSelectedRepository();
                return;
            }
            if (a == a_open_folder) {
                openExplorer(repo);
                return;
            }
            if (a == a_open_terminal) {
                openTerminal(repo);
                return;
            }
            if (a == a_remove) {
                removeRepositoryFromBookmark(index, true);
                return;
            }
            if (a == a_properties) {
                execRepositoryPropertyDialog(*repo);
                return;
            }
            if (a == a_open_patch_repository) {
                openPatchRepository();
                return;
            }
        }
    }
}

void SubmoduleMainWindow::on_tableWidget_log_customContextMenuRequested(const QPoint &pos)
{
    int row = selectedLogIndex(frame());
    Git::CommitItem const *commit = commitItem(frame(), row);
    qDebug()<<"on_tableWidget_log_customContextMenuRequested"<<row;
    if (commit) {
        bool is_valid_commit_id = Git::isValidID(commit->commit_id);

        QMenu menu;

        QAction *a_copy_id_7letters = is_valid_commit_id ? menu.addAction(tr("Copy commit id (7 letters)")) : nullptr;
        QAction *a_copy_id_complete = is_valid_commit_id ? menu.addAction(tr("Copy commit id (completely)")) : nullptr;

        std::set<QAction *> copy_label_actions;
        {
            QList<BranchLabel> v = sortedLabels(frame(), row);
            if (!v.isEmpty()) {
                auto *copy_lebel_menu = menu.addMenu("Copy label");
                for (BranchLabel const &l : v) {
                    QAction *a = copy_lebel_menu->addAction(l.text);
                    copy_label_actions.insert(copy_label_actions.end(), a);
                }
            }
        }

        menu.addSeparator();

        QAction *a_checkout = menu.addAction(tr("Checkout/Branch..."));

        menu.addSeparator();

        QAction *a_edit_message = nullptr;

        auto canEditMessage = [&](){
            if (commit->has_child) return false; // 子がないこと
            if (Git::isUncommited(*commit)) return false; // 未コミットがないこと
            bool is_head = false;
            bool has_remote_branch = false;
            QList<BranchLabel> const *labels = label(frame(), row);
            for (const BranchLabel &label : *labels) {
                if (label.kind == BranchLabel::Head) {
                    is_head = true;
                } else if (label.kind == BranchLabel::RemoteBranch) {
                    has_remote_branch = true;
                }
            }
            return is_head && !has_remote_branch; // HEAD && リモートブランチ無し
        };
        if (canEditMessage()) {
            a_edit_message = menu.addAction(tr("Edit message..."));
        }

        QAction *a_merge = is_valid_commit_id ? menu.addAction(tr("Merge")) : nullptr;
        QAction *a_rebase = is_valid_commit_id ? menu.addAction(tr("Rebase")) : nullptr;
        QAction *a_cherrypick = is_valid_commit_id ? menu.addAction(tr("Cherry-pick")) : nullptr;
        QAction *a_edit_tags = is_valid_commit_id ? menu.addAction(tr("Edit tags...")) : nullptr;
        QAction *a_revert = is_valid_commit_id ? menu.addAction(tr("Revert")) : nullptr;

        menu.addSeparator();

        QAction *a_delbranch = is_valid_commit_id ? menu.addAction(tr("Delete branch...")) : nullptr;
        QAction *a_delrembranch = remoteBranches(frame(), commit->commit_id, nullptr).isEmpty() ? nullptr : menu.addAction(tr("Delete remote branch..."));

        menu.addSeparator();

        QAction *a_explore = is_valid_commit_id ? menu.addAction(tr("Explore")) : nullptr;
        QAction *a_properties = addMenuActionProperty(&menu);

        QAction *a = menu.exec(frame()->logtablewidget()->viewport()->mapToGlobal(pos) + QPoint(8, -8));
        if (a) {
            if (a == a_copy_id_7letters) {
                qApp->clipboard()->setText(commit->commit_id.mid(0, 7));
                return;
            }
            if (a == a_copy_id_complete) {
                qApp->clipboard()->setText(commit->commit_id);
                return;
            }
            if (a == a_properties) {
                execCommitPropertyDialog(this, frame(), commit);
                return;
            }
            if (a == a_edit_message) {
                commitAmend(frame());
                return;
            }
            if (a == a_checkout) {
                checkout(frame(), this, commit);
                return;
            }
            if (a == a_delbranch) {
                deleteBranch(frame(), commit);
                return;
            }
            if (a == a_delrembranch) {
                deleteRemoteBranch(frame(), commit);
                return;
            }
            if (a == a_merge) {
                merge(frame(), commit);
                return;
            }
            if (a == a_rebase) {
                rebaseBranch(commit);
                return;
            }
            if (a == a_cherrypick) {
                cherrypick(commit);
                return;
            }
            if (a == a_edit_tags) {
                ui->action_edit_tags->trigger();
                return;
            }
            if (a == a_revert) {
                revertCommit(frame());
                return;
            }
            if (a == a_explore) {
                execCommitExploreWindow(frame(), this, commit);
                return;
            }
            if (copy_label_actions.find(a) != copy_label_actions.end()) {
                QString text = a->text();
                QApplication::clipboard()->setText(text);
                return;
            }
        }
    }
}

void SubmoduleMainWindow::on_listWidget_files_customContextMenuRequested(const QPoint &pos)
{
    GitPtr g = git();
    if (!isValidWorkingCopy(g)) return;

    QMenu menu;
    QAction *a_delete = menu.addAction(tr("Delete"));
    QAction *a_untrack = menu.addAction(tr("Untrack"));
    QAction *a_history = menu.addAction(tr("History"));
    QAction *a_blame = menu.addAction(tr("Blame"));
    QAction *a_properties = addMenuActionProperty(&menu);

    QPoint pt = frame()->fileslistwidget()->mapToGlobal(pos) + QPoint(8, -8);
    QAction *a = menu.exec(pt);
    if (a) {
        QListWidgetItem *item = frame()->fileslistwidget()->currentItem();
        if (a == a_delete) {
            if (askAreYouSureYouWantToRun("Delete", tr("Delete selected files."))) {
                for_each_selected_files([&](QString const &path){
                    g->removeFile(path);
                    g->chdirexec([&](){
                        QFile(path).remove();
                        return true;
                    });
                });
                openRepository(false);
            }
        } else if (a == a_untrack) {
            if (askAreYouSureYouWantToRun("Untrack", tr("rm --cached files"))) {
                for_each_selected_files([&](QString const &path){
                    g->rm_cached(path);
                });
                openRepository(false);
            }
        } else if (a == a_history) {
            execFileHistory(item);
        } else if (a == a_blame) {
            blame(item);
        } else if (a == a_properties) {
            showObjectProperty(item);
        }
    }
}

void SubmoduleMainWindow::on_listWidget_unstaged_customContextMenuRequested(const QPoint &pos)
{
    GitPtr g = git();
    if (!isValidWorkingCopy(g)) return;

    QList<QListWidgetItem *> items = frame()->unstagedFileslistwidget()->selectedItems();
    if (!items.isEmpty()) {
        QMenu menu;
        QAction *a_stage = menu.addAction(tr("Stage"));
        QAction *a_reset_file = menu.addAction(tr("Reset"));
        QAction *a_ignore = menu.addAction(tr("Ignore"));
        QAction *a_delete = menu.addAction(tr("Delete"));
        QAction *a_untrack = menu.addAction(tr("Untrack"));
        QAction *a_history = menu.addAction(tr("History"));
        QAction *a_blame = menu.addAction(tr("Blame"));
        QAction *a_properties = addMenuActionProperty(&menu);
        QPoint pt = frame()->unstagedFileslistwidget()->mapToGlobal(pos) + QPoint(8, -8);
        QAction *a = menu.exec(pt);
        if (a) {
            QListWidgetItem *item = frame()->unstagedFileslistwidget()->currentItem();
            if (a == a_stage) {
                for_each_selected_files([&](QString const &path){
                    g->stage(path);
                });
                updateCurrentFilesList(frame());
                return;
            }
            if (a == a_reset_file) {
                QStringList paths;
                for_each_selected_files([&](QString const &path){
                    paths.push_back(path);
                });
                resetFile(paths);
                return;
            }
            if (a == a_ignore) {
                QString gitignore_path = currentWorkingCopyDir() / ".gitignore";
                if (items.size() == 1) {
                    QString file = getFilePath(items[0]);
                    EditGitIgnoreDialog dlg((MainWindow *)this, gitignore_path, file);
                    if (dlg.exec() == QDialog::Accepted) {
                        QString appending = dlg.text();
                        if (!appending.isEmpty()) {
                            QString text;

                            QString path = gitignore_path;
                            path.replace('/', QDir::separator());

                            {
                                QFile file(path);
                                if (file.open(QFile::ReadOnly)) {
                                    text += QString::fromUtf8(file.readAll());
                                }
                            }

                            int n = text.size();
                            if (n > 0 && text[int(n - 1)] != '\n') {
                                text += '\n'; // 最後に改行を追加
                            }

                            text += appending + '\n';

                            {
                                QFile file(path);
                                if (file.open(QFile::WriteOnly)) {
                                    file.write(text.toUtf8());
                                }
                            }
                            updateCurrentFilesList(frame());
                            return;
                        }
                    } else {
                        return;
                    }
                }

                QString append;
                for_each_selected_files([&](QString const &path){
                    if (path == ".gitignore") {
                        // skip
                    } else {
                        append += path + '\n';
                    }
                });
                if (TextEditDialog::editFile(this, gitignore_path, ".gitignore", append)) {
                    updateCurrentFilesList(frame());
                }
                return;
            }
            if (a == a_delete) {
                if (askAreYouSureYouWantToRun("Delete", "Delete selected files.")) {
                    for_each_selected_files([&](QString const &path){
                        g->removeFile(path);
                        g->chdirexec([&](){
                            QFile(path).remove();
                            return true;
                        });
                    });
                    openRepository(false);
                }
                return;
            }
            if (a == a_untrack) {
                if (askAreYouSureYouWantToRun("Untrack", "rm --cached")) {
                    for_each_selected_files([&](QString const &path){
                        g->rm_cached(path);
                    });
                    openRepository(false);
                }
                return;
            }
            if (a == a_history) {
                execFileHistory(item);
                return;
            }
            if (a == a_blame) {
                blame(item);
                return;
            }
            if (a == a_properties) {
                showObjectProperty(item);
                return;
            }
        }
    }
}

void SubmoduleMainWindow::on_listWidget_staged_customContextMenuRequested(const QPoint &pos)
{
    GitPtr g = git();
    if (!isValidWorkingCopy(g)) return;

    QListWidgetItem *item = frame()->stagedFileslistwidget()->currentItem();
    if (item) {
        QString path = getFilePath(item);
        QString fullpath = currentWorkingCopyDir() / path;
        if (QFileInfo(fullpath).isFile()) {
            QMenu menu;
            QAction *a_unstage = menu.addAction(tr("Unstage"));
            QAction *a_history = menu.addAction(tr("History"));
            QAction *a_blame = menu.addAction(tr("Blame"));
            QAction *a_properties = addMenuActionProperty(&menu);
            QPoint pt = frame()->stagedFileslistwidget()->mapToGlobal(pos) + QPoint(8, -8);
            QAction *a = menu.exec(pt);
            if (a) {
                QListWidgetItem *item = frame()->unstagedFileslistwidget()->currentItem();
                if (a == a_unstage) {
                    g->unstage(path);
                    openRepository(false);
                } else if (a == a_history) {
                    execFileHistory(item);
                } else if (a == a_blame) {
                    blame(item);
                } else if (a == a_properties) {
                    showObjectProperty(item);
                }
            }
        }
    }
}

QStringList SubmoduleMainWindow::selectedFiles_(QListWidget *listwidget) const
{
    QStringList list;
    QList<QListWidgetItem *> items = listwidget->selectedItems();
    for (QListWidgetItem *item : items) {
        QString path = getFilePath(item);
        list.push_back(path);
    }
    return list;

}

QStringList SubmoduleMainWindow::selectedFiles() const
{
    if (m->last_focused_file_list == ui->listWidget_files)    return selectedFiles_(ui->listWidget_files);
    if (m->last_focused_file_list == ui->listWidget_staged)   return selectedFiles_(ui->listWidget_staged);
    if (m->last_focused_file_list == ui->listWidget_unstaged) return selectedFiles_(ui->listWidget_unstaged);
    return QStringList();
}

void SubmoduleMainWindow::for_each_selected_files(std::function<void(QString const&)> const &fn)
{
    for (QString const &path : selectedFiles()) {
        fn(path);
    }
}

void SubmoduleMainWindow::execFileHistory(QListWidgetItem *item)
{
    if (item) {
        QString path = getFilePath(item);
        if (!path.isEmpty()) {
            execFileHistory(path);
        }
    }
}

/**
 * @brief オブジェクトプロパティ
 * @param item
 */
void SubmoduleMainWindow::showObjectProperty(QListWidgetItem *item)
{
        // ファイルプロパティダイアログを表示する
        QString path = getFilePath(item);
        QString id = getObjectID(item);
        FilePropertyDialog dlg((MainWindow *)this);
        dlg.exec(this, path, id);
}

bool SubmoduleMainWindow::testRemoteRepositoryValidity(const QString &url, const QString &sshkey)
{
    bool ok;
    {
        OverrideWaitCursor;
        ok = isValidRemoteURL(url, sshkey);
    }

    QString pass = tr("The URL is a valid repository");
    QString fail = tr("Failed to access the URL");

    QString text = "%1\n\n%2";
    text = text.arg(url).arg(ok ? pass : fail);

    QString title = tr("Remote Repository");

    if (ok) {
        QMessageBox::information(this, title, text);
    } else {
        QMessageBox::critical(this, title, text);
    }

    return ok;
}

QString SubmoduleMainWindow::selectGitCommand(bool save)
{
    char const *exe = GIT_COMMAND;

    QString path = gitCommand();

    auto fn = [&](QString const &path){
        setGitCommand(path, save);
    };

    QStringList list = whichCommand_(exe);
#ifdef Q_OS_WIN
    {
        QStringList newlist;
        QString suffix1 = "\\bin\\" GIT_COMMAND;
        QString suffix2 = "\\cmd\\" GIT_COMMAND;
        for (QString const &s : list) {
            newlist.push_back(s);
            auto DoIt = [&](QString const &suffix){
                if (s.endsWith(suffix)) {
                    QString t = s.mid(0, s.size() - suffix.size());
                    QString t1 = t + "\\mingw64\\bin\\" GIT_COMMAND;
                    if (misc::isExecutable(t1)) newlist.push_back(t1);
                    QString t2 = t + "\\mingw\\bin\\" GIT_COMMAND;
                    if (misc::isExecutable(t2)) newlist.push_back(t2);
                }
            };
            DoIt(suffix1);
            DoIt(suffix2);
        }
        std::sort(newlist.begin(), newlist.end());
        auto end = std::unique(newlist.begin(), newlist.end());
        list.clear();
        for (auto it = newlist.begin(); it != end; it++) {
            list.push_back(*it);
        }
    }
#endif
    return selectCommand_("Git", exe, list, path, fn);
}

QString SubmoduleMainWindow::selectGpgCommand(bool save)
{
    QString path = global->appsettings.gpg_command;

    auto fn = [&](QString const &path){
        setGpgCommand(path, save);
    };

    QStringList list = whichCommand_(GPG_COMMAND, GPG2_COMMAND);

    QStringList cmdlist;
    cmdlist.push_back(GPG_COMMAND);
    cmdlist.push_back(GPG2_COMMAND);
    return selectCommand_("GPG", cmdlist, list, path, fn);
}

QString SubmoduleMainWindow::selectSshCommand(bool save)
{
    QString path = m->gcx.ssh_command;

    auto fn = [&](QString const &path){
        setSshCommand(path, save);
    };

    QStringList list = whichCommand_(SSH_COMMAND);

    QStringList cmdlist;
    cmdlist.push_back(SSH_COMMAND);
    return selectCommand_("ssh", cmdlist, list, path, fn);
}

const Git::Branch &SubmoduleMainWindow::currentBranch() const
{
    return m->current_branch;
}

void SubmoduleMainWindow::setCurrentBranch(const Git::Branch &b)
{
    m->current_branch = b;
}

const RepositoryItem &SubmoduleMainWindow::currentRepository() const
{
    return m->current_repo;
}

QString SubmoduleMainWindow::currentRepositoryName() const
{
    return currentRepository().name;
}

QString SubmoduleMainWindow::currentRemoteName() const
{
    return m->current_remote_name;
}

QString SubmoduleMainWindow::currentBranchName() const
{
    return currentBranch().name;
}

GitPtr SubmoduleMainWindow::git(const QString &dir, const QString &submodpath, const QString &sshkey) const
{
    qDebug()<<"SubmoduleMainWindow::Git"<<m->gcx.git_command<<dir<<submodpath<<sshkey;
    GitPtr g = std::make_shared<Git>(m->gcx, dir, submodpath, sshkey);
    if (g && QFileInfo(g->gitCommand()).isExecutable()) {
        g->setLogCallback(git_callback, (void *)this);
        return g;
    } else {
        QString text = tr("git command not specified") + '\n';
        const_cast<SubmoduleMainWindow *>(this)->writeLog(text);
        return GitPtr();
    }
}

GitPtr SubmoduleMainWindow::git()
{
    qDebug()<<"SubmoduleMainWindow::git";
    RepositoryItem const &item = currentRepository();
    return git(item.local_dir, {}, item.ssh_key);
}

GitPtr SubmoduleMainWindow::git(const Git::SubmoduleItem &submod)
{
    if (!submod) return {};
    RepositoryItem const &item = currentRepository();
    return git(item.local_dir, submod.path, item.ssh_key);
}

void SubmoduleMainWindow::autoOpenRepository(QString dir)
{
    auto Open = [&](RepositoryItem const &item){
        setCurrentRepository(item, true);
        openRepository(false, true);
    };

    RepositoryItem const *repo = findRegisteredRepository(&dir);
    if (repo) {
        Open(*repo);
        return;
    }

    RepositoryItem newitem;
    GitPtr g = git(dir, {}, {});
    if (isValidWorkingCopy(g)) {
        ushort const *left = dir.utf16();
        ushort const *right = left + dir.size();
        if (right[-1] == '/' || right[-1] == '\\') {
            right--;
        }
        ushort const *p = right;
        while (left + 1 < p && !(p[-1] == '/' || p[-1] == '\\')) p--;
        if (p < right) {
            newitem.local_dir = dir;
            newitem.name = QString::fromUtf16(p, int(right - p));
            saveRepositoryBookmark(newitem);
            Open(newitem);
            return;
        }
    } else {
        DoYouWantToInitDialog dlg((MainWindow *)this, dir);
        if (dlg.exec() == QDialog::Accepted) {
            createRepository(dir);
        }
    }
}

bool SubmoduleMainWindow::queryCommit(const QString &id, Git::CommitItem *out)
{
    *out = Git::CommitItem();
    GitPtr g = git();
    return g->queryCommit(id, out);
}

void SubmoduleMainWindow::checkout(RepositoryWrapper2Frame *frame, QWidget *parent, const Git::CommitItem *commit, std::function<void ()> accepted_callback)
{
    if (!commit) return;

    GitPtr g = git();
    if (!isValidWorkingCopy(g)) return;

    QStringList tags;
    QStringList all_local_branches;
    QStringList local_branches;
    QStringList remote_branches;
    {
        NamedCommitList named_commits = namedCommitItems(frame, Branches | Tags | Remotes);
        for (NamedCommitItem const &item : named_commits) {
            QString name = item.name;
            if (item.id == commit->commit_id) {
                if (item.type == NamedCommitItem::Type::Tag) {
                    tags.push_back(name);
                } else if (item.type == NamedCommitItem::Type::BranchLocal || item.type == NamedCommitItem::Type::BranchRemote) {
                    int i = name.lastIndexOf('/');
                    if (i < 0 && name == "HEAD") continue;
                    if (i > 0 && name.mid(i + 1) == "HEAD") continue;
                    if (item.type == NamedCommitItem::Type::BranchLocal) {
                        local_branches.push_back(name);
                    } else if (item.type == NamedCommitItem::Type::BranchRemote) {
                        remote_branches.push_back(name);
                    }
                }
            }
            if (item.type == NamedCommitItem::Type::BranchLocal) {
                all_local_branches.push_back(name);
            }
        }
    }

    CheckoutDialog dlg(parent, tags, all_local_branches, local_branches, remote_branches);
    if (dlg.exec() == QDialog::Accepted) {
        if (accepted_callback) {
            accepted_callback();
        }
        CheckoutDialog::Operation op = dlg.operation();
        QString name = dlg.branchName();
        QString id = commit->commit_id;
        if (id.isEmpty() && !commit->parent_ids.isEmpty()) {
            id = commit->parent_ids.front();
        }
        bool ok = false;
        setLogEnabled(g, true);
        if (op == CheckoutDialog::Operation::HeadDetached) {
            if (!id.isEmpty()) {
                ok = g->git(QString("checkout \"%1\"").arg(id), true);
            }
        } else if (op == CheckoutDialog::Operation::CreateLocalBranch) {
            if (!name.isEmpty() && !id.isEmpty()) {
                ok = g->git(QString("checkout -b \"%1\" \"%2\"").arg(name).arg(id), true);
            }
        } else if (op == CheckoutDialog::Operation::ExistingLocalBranch) {
            if (!name.isEmpty()) {
                ok = g->git(QString("checkout \"%1\"").arg(name), true);
            }
        }
        if (ok) {
            openRepository(true);
        }
    }
}

void SubmoduleMainWindow::checkout(RepositoryWrapper2Frame *frame)
{
    checkout(frame, this, selectedCommitItem(frame));
}

void SubmoduleMainWindow::jumpToCommit(RepositoryWrapper2Frame *frame, QString id)
{
    GitPtr g = git();
    id = g->rev_parse(id);
    if (!id.isEmpty()) {
        int row = rowFromCommitId(frame, id);
        setCurrentLogRow(frame, row);
    }
}

Git::Object SubmoduleMainWindow::cat_file(RepositoryWrapper2Frame *frame, const QString &id)
{
    return cat_file_(frame, git(), id);
}

void SubmoduleMainWindow::addWorkingCopyDir(const QString &dir, bool open)
{
    addWorkingCopyDir(dir, QString(), open);
}

bool SubmoduleMainWindow::saveAs(RepositoryWrapper2Frame *frame, const QString &id, const QString &dstpath)
{
    if (id.startsWith(PATH_PREFIX)) {
        return saveFileAs(id.mid(1), dstpath);
    } else {
        return saveBlobAs(frame, id, dstpath);
    }
}

QString SubmoduleMainWindow::determinFileType(QByteArray in)
{
    if (in.isEmpty()) return QString();

    if (in.size() > 10) {
        if (memcmp(in.data(), "\x1f\x8b\x08", 3) == 0) { // gzip
            QBuffer buf;
            MemoryReader reader(in.data(), in.size());
            reader.open(MemoryReader::ReadOnly);
            buf.open(QBuffer::WriteOnly);
            gunzip z;
            z.set_maximul_size(100000);
            z.decode(&reader, &buf);
            in = buf.buffer();
        }
    }

    QString mimetype;
    if (!in.isEmpty()) {
        std::string s = global->filetype.mime_by_data(in.data(), in.size());
        auto i = s.find(';');
        if (i != std::string::npos) {
            s = s.substr(0, i);
        }
        mimetype = QString::fromStdString(s).trimmed();
    }
    return mimetype;
}

QList<Git::Tag> SubmoduleMainWindow::queryTagList(RepositoryWrapper2Frame *frame)
{
    QList<Git::Tag> list;
    Git::CommitItem const *commit = selectedCommitItem(frame);
    if (commit && Git::isValidID(commit->commit_id)) {
        list = findTag(frame, commit->commit_id);
    }
    return list;
}

TextEditorThemePtr SubmoduleMainWindow::themeForTextEditor()
{
    return global->theme->text_editor_theme;
}

bool SubmoduleMainWindow::isValidWorkingCopy(GitPtr g) const
{
    return g && g->isValidWorkingCopy();
}

void SubmoduleMainWindow::emitWriteLog(const QByteArray &ba)
{
    emit signalWriteLog(ba);
}

QString SubmoduleMainWindow::findFileID(RepositoryWrapper2Frame *frame, const QString &commit_id, const QString &file)
{
    return lookupFileID(getObjCache(frame), commit_id, file);
}

const Git::CommitItem *SubmoduleMainWindow::commitItem(RepositoryWrapper2Frame *frame, int row) const
{
    auto const &logs = getLogs(frame);
    if (row >= 0 && row < (int)logs.size()) {
        return &logs[row];
    }
    return nullptr;
}

QIcon SubmoduleMainWindow::committerIcon(RepositoryWrapper2Frame *frame, int row) const
{
    QIcon icon;
    if (isAvatarEnabled() && isOnlineMode()) {
        auto const &logs = getLogs(frame);
        if (row >= 0 && row < (int)logs.size()) {
            Git::CommitItem const &commit = logs[row];
            if (commit.email.indexOf('@') > 0) {
                std::string email = commit.email.toStdString();
                icon = getAvatarLoader()->fetch((RepositoryWrapperFrame *)frame, email, true); // from gavatar
            }
        }
    }
    return icon;
}

QString SubmoduleMainWindow::abbrevCommitID(const Git::CommitItem &commit)
{
    return commit.commit_id.mid(0, 7);
}

/**
 * @brief コミットログの選択が変化した
 */
void SubmoduleMainWindow::doLogCurrentItemChanged(RepositoryWrapper2Frame *frame)
{
    clearFileList(frame);

    int row = selectedLogIndex(frame);
    qDebug()<<"doLogCurrentItemChanged"<<row;
    QTableWidgetItem *item = frame->logtablewidget()->item(row, 0);
    if (item) {
        auto const &logs = getLogs(frame);
        int index = item->data(IndexRole).toInt();
        if (index < (int)logs.size()) {
            // ステータスバー更新
            updateStatusBarText(frame);
            // 少し待ってファイルリストを更新する
            postUserFunctionEvent([&](QVariant const &, void *p){
                RepositoryWrapper2Frame *frame = reinterpret_cast<RepositoryWrapper2Frame *>(p);
                updateCurrentFilesList(frame);
            }, {}, reinterpret_cast<void *>(frame), 300); // 300ms後（キーボードのオートリピート想定）
        }
    } else {
        row = -1;
    }
    updateAncestorCommitMap(frame);
    frame->logtablewidget()->viewport()->update();
}

void SubmoduleMainWindow::findNext(RepositoryWrapper2Frame *frame)
{
    if (m->search_text.isEmpty()) {
        return;
    }
    auto const &logs = getLogs(frame);
    for (int pass = 0; pass < 2; pass++) {
        int row = 0;
        if (pass == 0) {
            row = selectedLogIndex(frame);
            if (row < 0) {
                row = 0;
            } else if (m->searching) {
                row++;
            }
        }
        while (row < (int)logs.size()) {
            Git::CommitItem const commit = logs[row];
            if (!Git::isUncommited(commit)) {
                if (commit.message.indexOf(m->search_text, 0, Qt::CaseInsensitive) >= 0) {
                    bool b = frame->logtablewidget()->blockSignals(true);
                    setCurrentLogRow(frame, row);
                    frame->logtablewidget()->blockSignals(b);
                    m->searching = true;
                    return;
                }
            }
            row++;
        }
    }
}

void SubmoduleMainWindow::findText(QString const &text)
{
    m->search_text = text;
}

bool SubmoduleMainWindow::isAncestorCommit(QString const &id)
{
    auto it = m->ancestors.find(id);
    return it != m->ancestors.end();
}

void SubmoduleMainWindow::updateAncestorCommitMap(RepositoryWrapper2Frame *frame)
{
    m->ancestors.clear();

    auto const &logs = getLogs(frame);
    const int LogCount = (int)logs.size();
    const int index = selectedLogIndex(frame);
    if (index >= 0 && index < LogCount) {
        // ok
    } else {
        return;
    }

    auto *logsp = getLogsPtr(frame);
    auto LogItem = [&](int i)->Git::CommitItem &{ return logsp->at((size_t)i); };

    std::map<QString, size_t> commit_to_index_map;

    int end = LogCount;

    if (index < end) {
        for (int i = index; i < end; i++) {
            Git::CommitItem const &commit = LogItem(i);
            commit_to_index_map[commit.commit_id] = (size_t)i;
            auto *item = frame->logtablewidget()->item((int)i, 0);
            QRect r = frame->logtablewidget()->visualItemRect(item);
            if (r.y() >= frame->logtablewidget()->height()) {
                end = i + 1;
                break;
            }
        }
    }

    Git::CommitItem *item = &LogItem(index);
    if (item) {
        m->ancestors.insert(m->ancestors.end(), item->commit_id);
    }

    for (int i = index; i < end; i++) {
        Git::CommitItem *item = &LogItem(i);
        if (isAncestorCommit(item->commit_id)) {
            for (QString const &parent : item->parent_ids) {
                m->ancestors.insert(m->ancestors.end(), parent);
            }
        }
    }
}

void SubmoduleMainWindow::on_action_open_existing_working_copy_triggered()
{
    QString dir = defaultWorkingDir();
    dir = QFileDialog::getExistingDirectory(this, tr("Add existing working copy"), dir);
    qDebug()<<"on_action_open_existing_working_copy_triggered"<<dir;
    addWorkingCopyDir(dir, false);
}

void SubmoduleMainWindow::on_action_open_existing_working_copy_1_triggered()
{
    QString dir = defaultWorkingDir();
    dir = QFileDialog::getExistingDirectory(this, tr("Add existing working copy"), dir);
    qDebug()<<"on_action_open_existing_working_copy_1_triggered"<<dir;
    //showSubmoduleSubmoduleMainWindow(dir);
    //addWorkingCopyDir(dir, false);
}

void SubmoduleMainWindow::openPatchRepository()
{
    QString dir = defaultWorkingDir();
    dir = QFileDialog::getExistingDirectory(this, tr("Add existing working copy"), dir);
    qDebug()<<"on_action_open_existing_working_copy_1_triggered"<<dir;
    //showSubmoduleSubmoduleMainWindow(dir);
    //addWorkingCopyDir(dir, false);
}

void SubmoduleMainWindow::refresh()
{
    openRepository(true);
}

void SubmoduleMainWindow::writeLog_(QByteArray ba)
{
    if (!ba.isEmpty()) {
        writeLog(ba.data(), ba.size());
    }
}

void SubmoduleMainWindow::on_action_view_refresh_triggered()
{
    refresh();
}

void SubmoduleMainWindow::on_tableWidget_log_currentItemChanged(QTableWidgetItem * /*current*/, QTableWidgetItem * /*previous*/)
{
    qDebug()<<"on_tableWidget_log_currentItemChanged";
    doLogCurrentItemChanged(frame());
    m->searching = false;
}

void SubmoduleMainWindow::on_toolButton_stage_clicked()
{
    GitPtr g = git();
    if (!isValidWorkingCopy(g)) return;

    g->stage(selectedFiles());
    updateCurrentFilesList(frame());
}

void SubmoduleMainWindow::on_toolButton_unstage_clicked()
{
    GitPtr g = git();
    if (!isValidWorkingCopy(g)) return;

    g->unstage(selectedFiles());
    updateCurrentFilesList(frame());
}

void SubmoduleMainWindow::on_toolButton_select_all_clicked()
{
    if (frame()->unstagedFileslistwidget()->count() > 0) {
        frame()->unstagedFileslistwidget()->setFocus();
        frame()->unstagedFileslistwidget()->selectAll();
    } else if (frame()->stagedFileslistwidget()->count() > 0) {
        frame()->stagedFileslistwidget()->setFocus();
        frame()->stagedFileslistwidget()->selectAll();
    }
}

void SubmoduleMainWindow::on_toolButton_commit_clicked()
{
    ui->action_commit->trigger();
}



void SubmoduleMainWindow::on_action_edit_global_gitconfig_triggered()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    QString path = dir / ".gitconfig";
    editFile(path, ".gitconfig");
}

void SubmoduleMainWindow::on_action_edit_git_config_triggered()
{
    QString dir = currentWorkingCopyDir();
    QString path = dir / ".git/config";
    editFile(path, ".git/config");
}

void SubmoduleMainWindow::on_action_edit_gitignore_triggered()
{
    QString dir = currentWorkingCopyDir();
    QString path = dir / ".gitignore";
    if (editFile(path, ".gitignore")) {
        updateCurrentFilesList(frame());
    }
}

int SubmoduleMainWindow::selectedLogIndex(RepositoryWrapper2Frame *frame) const
{
    auto const &logs = getLogs(frame);
    int i = frame->logtablewidget()->currentRow();
    if (i >= 0 && i < (int)logs.size()) {
        return i;
    }
    return -1;
}

/**
 * @brief ファイル差分表示を更新する
 * @param item
 */
void SubmoduleMainWindow::updateDiffView(RepositoryWrapper2Frame *frame, QListWidgetItem *item)
{
    qDebug()<<"updateDiffView";
    clearDiffView(frame);

    m->last_selected_file_item = item;

    if (!item) return;

    int idiff = indexOfDiff(item);
    qDebug()<<"updateDiffView1";
    if (idiff >= 0 && idiff < diffResult()->size()) {
        Git::Diff const &diff = diffResult()->at(idiff);
        QString key = GitDiff::makeKey(diff);
        qDebug()<<"updateDiffView2";
        auto it = getDiffCacheMap(frame)->find(key);
        qDebug()<<"updateDiffView3";
        if (it != getDiffCacheMap(frame)->end()) {
            auto const &logs = getLogs(frame);
            qDebug()<<"updateDiffView4";
            int row = frame->logtablewidget()->currentRow();
            qDebug()<<"updateDiffView5";
            bool uncommited = (row >= 0 && row < (int)logs.size() && Git::isUncommited(logs[row]));
            qDebug()<<"updateDiffView6";
            frame->filediffwidget()->updateDiffView(it->second, uncommited);
            qDebug()<<"updateDiffView7";
        }
    }
    qDebug()<<"updateDiffView end";
}

void SubmoduleMainWindow::updateDiffView(RepositoryWrapper2Frame *frame)
{
    qDebug()<<"updateDiffView";
    updateDiffView(frame, m->last_selected_file_item);
}

void SubmoduleMainWindow::updateUnstagedFileCurrentItem(RepositoryWrapper2Frame *frame)
{
    qDebug()<<"updateUnstagedFileCurrentItem";
    updateDiffView(frame, frame->unstagedFileslistwidget()->currentItem());
    qDebug()<<"updateUnstagedFileCurrentItem end";
}

void SubmoduleMainWindow::updateStagedFileCurrentItem(RepositoryWrapper2Frame *frame)
{
    qDebug()<<"updateStagedFileCurrentItem";
    updateDiffView(frame, frame->stagedFileslistwidget()->currentItem());
}

void SubmoduleMainWindow::on_listWidget_unstaged_currentRowChanged(int /*currentRow*/)
{
    qDebug()<<"SubmoduleMainWindow::on_listWidget_unstaged_currentRowChanged";
    updateUnstagedFileCurrentItem(frame());
    qDebug()<<"SubmoduleMainWindow::on_listWidget_unstaged_currentRowChanged end";
}

void SubmoduleMainWindow::on_listWidget_staged_currentRowChanged(int /*currentRow*/)
{
    updateStagedFileCurrentItem(frame());
}

void SubmoduleMainWindow::on_listWidget_files_currentRowChanged(int /*currentRow*/)
{
    qDebug()<<"on_listWidget_files_currentRowChanged";
    updateDiffView(frame(), frame()->fileslistwidget()->currentItem());
    qDebug()<<"on_listWidget_files_currentRowChangedend";
}

void SubmoduleMainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (QApplication::modalWindow()) return;

    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
        event->accept();
    }
}

void SubmoduleMainWindow::keyPressEvent(QKeyEvent *event)
{
    int c = event->key();
    if (c == Qt::Key_T && (event->modifiers() & Qt::ControlModifier)) {
        test();
        return;
    }
    if (QApplication::focusWidget() == ui->widget_log) {

        auto write_char = [&](char c){
            if (getPtyProcess()->isRunning()) {
                getPtyProcess()->writeInput(&c, 1);
            }
        };

        auto write_text = [&](QString const &str){
            std::string s = str.toStdString();
            for (char i : s) {
                write_char(i);
            }
        };

        if (c == Qt::Key_Return || c == Qt::Key_Enter) {
            write_char('\n');
        } else {
            QString text = event->text();
            write_text(text);
        }
    }
}

void SubmoduleMainWindow::on_action_edit_settings_triggered()
{
    SettingsDialog dlg((MainWindow *)this);
    if (dlg.exec() == QDialog::Accepted) {
        ApplicationSettings const &newsettings = dlg.settings();
        setAppSettings(newsettings);
        setGitCommand(appsettings()->git_command, false);
        setGpgCommand(appsettings()->gpg_command, false);
        setSshCommand(appsettings()->ssh_command, false);
    }
}

void SubmoduleMainWindow::onCloneCompleted(bool success, QVariant const &userdata)
{
    if (success) {
        RepositoryItem r = userdata.value<RepositoryItem>();
        saveRepositoryBookmark(r);
        setCurrentRepository(r, false);
        openRepository(true);
    }
}

void SubmoduleMainWindow::onPtyProcessCompleted(bool /*ok*/, QVariant const &userdata)
{
    switch (getPtyCondition()) {
    case PtyCondition::Clone:
        onCloneCompleted(getPtyProcessOk(), userdata);
        break;
    }
    setPtyCondition(PtyCondition::None);
}

void SubmoduleMainWindow::on_action_clone_triggered()
{
    clone();
}

void SubmoduleMainWindow::on_action_about_triggered()
{
    AboutDialog dlg((MainWindow *)this);
    dlg.exec();
}

void SubmoduleMainWindow::on_toolButton_clone_clicked()
{
    ui->action_clone->trigger();
}

void SubmoduleMainWindow::on_toolButton_fetch_clicked()
{
    ui->action_fetch->trigger();
}

void SubmoduleMainWindow::clearRepoFilter()
{
    ui->lineEdit_filter->clear();
}

void SubmoduleMainWindow::appendCharToRepoFilter(ushort c)
{
    if (QChar(c).isLetter()) {
        c = QChar(c).toLower().unicode();
    }
    ui->lineEdit_filter->setText(getRepositoryFilterText() + QChar(c));
}

void SubmoduleMainWindow::backspaceRepoFilter()
{
    QString s = getRepositoryFilterText();
    int n = s.size();
    if (n > 0) {
        s = s.mid(0, n - 1);
    }
    ui->lineEdit_filter->setText(s);
}

void SubmoduleMainWindow::on_lineEdit_filter_textChanged(QString const &text)
{
    setRepositoryFilterText(text);
    updateRepositoriesList();
}

void SubmoduleMainWindow::on_toolButton_erase_filter_clicked()
{
    clearRepoFilter();
    ui->lineEdit_filter->setFocus();
}

void SubmoduleMainWindow::deleteTags(RepositoryWrapper2Frame *frame, QStringList const &tagnames)
{
    int row = frame->logtablewidget()->currentRow();

    internalDeleteTags(tagnames);

    frame->logtablewidget()->selectRow(row);
}

void SubmoduleMainWindow::revertCommit(RepositoryWrapper2Frame *frame)
{
    GitPtr g = git();
    if (!isValidWorkingCopy(g)) return;

    Git::CommitItem const *commit = selectedCommitItem(frame);
    if (commit) {
        g->revert(commit->commit_id);
        openRepository(false);
    }
}

bool SubmoduleMainWindow::addTag(RepositoryWrapper2Frame *frame, QString const &name)
{
    int row = frame->logtablewidget()->currentRow();

    bool ok = internalAddTag(frame, name);

    frame->selectLogTableRow(row);
    return ok;
}

void SubmoduleMainWindow::on_action_push_all_tags_triggered()
{
    reopenRepository(false, [&](GitPtr g){
        g->push(true);
    });
}

void SubmoduleMainWindow::on_tableWidget_log_itemDoubleClicked(QTableWidgetItem *)
{
    Git::CommitItem const *commit = selectedCommitItem(frame());
    if (commit) {
        execCommitPropertyDialog(this, frame(), commit);
    }
}

void SubmoduleMainWindow::on_listWidget_unstaged_itemDoubleClicked(QListWidgetItem * item)
{
    qDebug()<<"on_listWidget_unstaged_itemDoubleClicked";
    showObjectProperty(item);
}

void SubmoduleMainWindow::on_listWidget_staged_itemDoubleClicked(QListWidgetItem *item)
{
    qDebug()<<"on_listWidget_staged_itemDoubleClicked";
    showObjectProperty(item);
}

void SubmoduleMainWindow::on_listWidget_files_itemDoubleClicked(QListWidgetItem *item)
{
    qDebug()<<"on_listWidget_files_itemDoubleClicked";
    showObjectProperty(item);
}

QListWidgetItem *SubmoduleMainWindow::currentFileItem() const
{
    QListWidget *listwidget = nullptr;
    if (ui->stackedWidget_filelist->currentWidget() == ui->page_uncommited) {
        QWidget *w = qApp->focusWidget();
        if (w == ui->listWidget_unstaged) {
            listwidget = ui->listWidget_unstaged;
        } else if (w == ui->listWidget_staged) {
            listwidget = ui->listWidget_staged;
        }
    } else {
        listwidget = ui->listWidget_files;
    }
    if (listwidget) {
        return listwidget->currentItem();
    }
    return nullptr;
}

void SubmoduleMainWindow::on_action_set_config_user_triggered()
{
    Git::User global_user;
    Git::User repo_user;
    GitPtr g = git();
    if (isValidWorkingCopy(g)) {
        repo_user = g->getUser(Git::Source::Local);
    }
    global_user = g->getUser(Git::Source::Global);

    execSetUserDialog(global_user, repo_user, currentRepositoryName());
}

void SubmoduleMainWindow::showLogWindow(bool show)
{
    ui->dockWidget_log->setVisible(show);
}

bool SubmoduleMainWindow::isValidRemoteURL(const QString &url, const QString &sshkey)
{
    if (url.indexOf('\"') >= 0) {
        return false;
    }
    stopPtyProcess();
    GitPtr g = git({}, {}, sshkey);
    QString cmd = "ls-remote \"%1\" HEAD";
    cmd = cmd.arg(url);
    bool f = g->git(cmd, false, false, getPtyProcess());
    {
        QTime time;
        time.start();
        while (!getPtyProcess()->wait(10)) {
            if (time.elapsed() > 10000) {
                f = false;
                break;
            }
            QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        }
        stopPtyProcess();
    }
    if (f) {
        f = (getPtyProcess()->getExitCode() == 0);
    }
    QString line;
    {
        std::vector<char> v;
        getPtyProcess()->readResult(&v);
        if (!v.empty()) {
            line = QString::fromUtf8(&v[0], (int)v.size()).trimmed();
        }
    }
    if (f) {
        qDebug() << "This is a valid repository.";
        int i = -1;
        for (int j = 0; j < line.size(); j++) {
            ushort c = line.utf16()[j];
            if (QChar(c).isSpace()) {
                i = j;
                break;
            }
        }
        QString head;
        if (i == GIT_ID_LENGTH) {
            QString id = line.mid(0, i);
            QString name = line.mid(i + 1).trimmed();
            qDebug() << id << name;
            if (name == "HEAD" && Git::isValidID(id)) {
                head = id;
            }
        }
        if (head.isEmpty()) {
            qDebug() << "But HEAD not found";
        }
        return true;
    }
    qDebug() << "This is not a repository.";
    return false;
}

QStringList SubmoduleMainWindow::whichCommand_(const QString &cmdfile1, const QString &cmdfile2)
{
    QStringList list;

    if (!cmdfile1.isEmpty()){
        std::vector<std::string> vec;
        FileUtil::qwhich(cmdfile1.toStdString(), &vec);
        for (std::string const &s : vec) {
            list.push_back(QString::fromStdString(s));
        }
    }
    if (!cmdfile2.isEmpty()){
        std::vector<std::string> vec;
        FileUtil::qwhich(cmdfile2.toStdString(), &vec);
        for (std::string const &s : vec) {
            list.push_back(QString::fromStdString(s));
        }
    }

    return list;
}

QString SubmoduleMainWindow::selectCommand_(const QString &cmdname, const QStringList &cmdfiles, const QStringList &list, QString path, const std::function<void (const QString &)> &callback)
{
    QString window_title = tr("Select %1 command");
    window_title = window_title.arg(cmdfiles.front());

    SelectCommandDialog dlg((MainWindow *)this, cmdname, cmdfiles, path, list);
    dlg.setWindowTitle(window_title);
    if (dlg.exec() == QDialog::Accepted) {
        path = dlg.selectedFile();
        path = misc::normalizePathSeparator(path);
        QFileInfo info(path);
        if (info.isExecutable()) {
            callback(path);
            return path;
        }
    }
    return QString();
}

QString SubmoduleMainWindow::selectCommand_(const QString &cmdname, const QString &cmdfile, const QStringList &list, const QString &path, const std::function<void (const QString &)> &callback)
{
    QStringList cmdfiles;
    cmdfiles.push_back(cmdfile);
    return selectCommand_(cmdname, cmdfiles, list, path, callback);
}

void SubmoduleMainWindow::on_action_window_log_triggered(bool checked)
{
    showLogWindow(checked);
}

void SubmoduleMainWindow::on_action_repo_jump_triggered()
{
    GitPtr g = git();
    if (!isValidWorkingCopy(g)) return;

    NamedCommitList items = namedCommitItems(frame(), Branches | Tags | Remotes);
    {
        NamedCommitItem head;
        head.name = "HEAD";
        head.id = getHeadId();
        items.push_front(head);
    }
    JumpDialog dlg((MainWindow *)this, items);
    if (dlg.exec() == QDialog::Accepted) {
        QString text = dlg.text();
        if (text.isEmpty()) return;
        QString id = g->rev_parse(text);
        if (id.isEmpty() && Git::isValidID(text)) {
            QStringList list = findGitObject(text);
            if (list.isEmpty()) {
                QMessageBox::warning(this, tr("Jump"), QString("%1\n\n").arg(text) + tr("No such commit"));
                return;
            }
            ObjectBrowserDialog dlg2((MainWindow*)this, list);
            if (dlg2.exec() == QDialog::Accepted) {
                id = dlg2.text();
                if (id.isEmpty()) return;
            }
        }
        if (g->objectType(id) == "tag") {
            id = getObjCache(frame())->getCommitIdFromTag(id);
        }
        int row = rowFromCommitId(frame(), id);
        if (row < 0) {
            QMessageBox::warning(this, tr("Jump"), QString("%1\n(%2)\n\n").arg(text).arg(id) + tr("No such commit"));
        } else {
            setCurrentLogRow(frame(), row);
        }
    }
}

void SubmoduleMainWindow::on_action_repo_checkout_triggered()
{
    checkout(frame());
}

void SubmoduleMainWindow::on_action_delete_branch_triggered()
{
    deleteBranch(frame());
}

void SubmoduleMainWindow::on_toolButton_terminal_clicked()
{
    qDebug()<<"on_toolButton_terminal_clicked";
    openTerminal(nullptr);
}

void SubmoduleMainWindow::on_toolButton_explorer_clicked()
{
    openExplorer(nullptr);
}

void SubmoduleMainWindow::on_action_reset_HEAD_1_triggered()
{
    GitPtr g = git();
    if (!isValidWorkingCopy(g)) return;

    g->reset_head1();
    openRepository(false);
}

void SubmoduleMainWindow::on_action_create_a_repository_triggered()
{
    createRepository(QString());
}

bool SubmoduleMainWindow::isOnlineMode() const
{
    return m->is_online_mode;
}

void SubmoduleMainWindow::setRemoteOnline(bool f, bool save)
{
    m->is_online_mode = f;

    {
        QRadioButton *rb = nullptr;
        rb = f ? ui->radioButton_remote_online : ui->radioButton_remote_offline;
        rb->blockSignals(true);
        rb->click();
        rb->blockSignals(false);

        ui->action_online->setCheckable(true);
        ui->action_offline->setCheckable(true);
        ui->action_online->setChecked(f);
        ui->action_offline->setChecked(!f);

        setNetworkingCommandsEnabled(f);
    }

    if (save) {
        MySettings s;
        s.beginGroup("Remote");
        s.setValue("Online", f);
        s.endGroup();
    }
}

void SubmoduleMainWindow::on_radioButton_remote_online_clicked()
{
    setRemoteOnline(true, true);
}

void SubmoduleMainWindow::on_radioButton_remote_offline_clicked()
{
    setRemoteOnline(false, true);
}

void SubmoduleMainWindow::on_verticalScrollBar_log_valueChanged(int)
{
    ui->widget_log->refrectScrollBar();
}

void SubmoduleMainWindow::on_horizontalScrollBar_log_valueChanged(int)
{
    ui->widget_log->refrectScrollBar();
}

void SubmoduleMainWindow::on_toolButton_stop_process_clicked()
{
    abortPtyProcess();
}

void SubmoduleMainWindow::on_action_stop_process_triggered()
{
    abortPtyProcess();
}

void SubmoduleMainWindow::on_action_exit_triggered()
{
    close();
}

void SubmoduleMainWindow::on_action_reflog_triggered()
{
    GitPtr g = git();
    Git::ReflogItemList reflog;
    g->reflog(&reflog);

    ReflogWindow dlg(this, (MainWindow *)this, reflog);
    dlg.exec();
}

void SubmoduleMainWindow::blame(QListWidgetItem *item)
{
    QList<BlameItem> list;
    QString path = getFilePath(item);
    {
        GitPtr g = git();
        QByteArray ba = g->blame(path);
        if (!ba.isEmpty()) {
            char const *begin = ba.data();
            char const *end = begin + ba.size();
            list = BlameWindow::parseBlame(begin, end);
        }
    }
    if (!list.isEmpty()) {
        qApp->setOverrideCursor(Qt::WaitCursor);
        BlameWindow win((MainWindow *)this, path, list);
        qApp->restoreOverrideCursor();
        win.exec();
    }
}

void SubmoduleMainWindow::blame()
{
    blame(currentFileItem());
}

void SubmoduleMainWindow::on_action_repository_property_triggered()
{
    execRepositoryPropertyDialog(currentRepository());
}

void SubmoduleMainWindow::on_action_set_gpg_signing_triggered()
{
    GitPtr g = git();
    QString global_key_id = g->signingKey(Git::Source::Global);
    QString repository_key_id;
    if (g->isValidWorkingCopy()) {
        repository_key_id = g->signingKey(Git::Source::Local);
    }
    SetGpgSigningDialog dlg((MainWindow *)this, currentRepositoryName(), global_key_id, repository_key_id);
    if (dlg.exec() == QDialog::Accepted) {
        g->setSigningKey(dlg.id(), dlg.isGlobalChecked());
    }
}

void SubmoduleMainWindow::execAreYouSureYouWantToContinueConnectingDialog()
{
    using TheDlg = AreYouSureYouWantToContinueConnectingDialog;

    setInteractionMode(InteractionMode::Busy);

    QApplication::restoreOverrideCursor();

    TheDlg dlg((MainWindow *)this);
    if (dlg.exec() == QDialog::Accepted) {
        TheDlg::Result r = dlg.result();
        if (r == TheDlg::Result::Yes) {
            getPtyProcess()->writeInput("yes\n", 4);
        } else {
            setPtyProcessOk(false); // abort
            getPtyProcess()->writeInput("no\n", 3);
            QThread::msleep(300);
            stopPtyProcess();
        }
    } else {
        ui->widget_log->setFocus();
        setInteractionCanceled(true);
    }
    setInteractionMode(InteractionMode::Busy);
}

void SubmoduleMainWindow::deleteRemoteBranch(RepositoryWrapper2Frame *frame, const Git::CommitItem *commit)
{
    if (!commit) return;

    GitPtr g = git();
    if (!isValidWorkingCopy(g)) return;

    QStringList all_branches;
    QStringList remote_branches = remoteBranches(frame, commit->commit_id, &all_branches);
    if (remote_branches.isEmpty()) return;

    DeleteBranchDialog dlg((MainWindow *)this, true, all_branches, remote_branches);
    if (dlg.exec() == QDialog::Accepted) {
        setLogEnabled(g, true);
        QStringList names = dlg.selectedBranchNames();
        for (QString const &name : names) {
            int i = name.indexOf('/');
            if (i > 0) {
                QString remote = name.mid(0, i);
                QString branch = ':' + name.mid(i + 1);
				pushSetUpstream(true, remote, branch, false);
            }
        }
    }
}

QStringList SubmoduleMainWindow::remoteBranches(RepositoryWrapper2Frame *frame, const QString &id, QStringList *all)
{
    if (all) all->clear();

    QStringList list;

    GitPtr g = git();
    if (isValidWorkingCopy(g)) {
        NamedCommitList named_commits = namedCommitItems(frame, Branches | Remotes);
        for (NamedCommitItem const &item : named_commits) {
            if (item.id == id && !item.remote.isEmpty()) {
                list.push_back(item.remote / item.name);
            }
            if (all && !item.remote.isEmpty() && item.name != "HEAD") {
                all->push_back(item.remote / item.name);
            }
        }
    }

    return list;
}

void SubmoduleMainWindow::onLogIdle()
{
    if (interactionCanceled()) return;
    if (interactionMode() != InteractionMode::None) return;

    static char const are_you_sure_you_want_to_continue_connecting[] = "Are you sure you want to continue connecting (yes/no)?";
    static char const enter_passphrase[] = "Enter passphrase: ";
    static char const enter_passphrase_for_key[] = "Enter passphrase for key '";
    static char const fatal_authentication_failed_for[] = "fatal: Authentication failed for '";

    std::vector<char> vec;
    ui->widget_log->retrieveLastText(&vec, 100);
    if (!vec.empty()) {
        std::string line;
        size_t n = vec.size();
        size_t i = n;
        while (i > 0) {
            i--;
            if (i + 1 < n && vec[i] == '\n') {
                i++;
                line.assign(&vec[i], size_t(n - i));
                break;
            }
        }
        if (!line.empty()) {
            auto ExecLineEditDialog = [&](QWidget *parent, QString const &title, QString const &prompt, QString const &val, bool password){
                LineEditDialog dlg(parent, title, prompt, val, password);
                if (dlg.exec() == QDialog::Accepted) {
                    std::string ret = dlg.text().toStdString();
                    std::string str = ret + '\n';
                    getPtyProcess()->writeInput(str.c_str(), str.size());
                    return ret;
                }
                abortPtyProcess();
                return std::string();
            };

            auto Match = [&](char const *str){
                size_t n = strlen(str);
                if (strncmp(line.c_str(), str, n) == 0) {
                    char const *p = line.c_str() + n;
                    while (1) {
                        if (!*p) return true;
                        if (!isspace((unsigned char)*p)) break;
                        p++;
                    }
                }
                return false;
            };

            auto StartsWith = [&](char const *str){
                char const *p = line.c_str();
                while (*str) {
                    if (*p != *str) return false;
                    str++;
                    p++;
                }
                return true;
            };

            if (Match(are_you_sure_you_want_to_continue_connecting)) {
                execAreYouSureYouWantToContinueConnectingDialog();
                return;
            }

            if (line == enter_passphrase) {
                ExecLineEditDialog(this, "Passphrase", QString::fromStdString(line), QString(), true);
                return;
            }

            if (StartsWith(enter_passphrase_for_key)) {
                std::string keyfile;
                {
                    int i = strlen(enter_passphrase_for_key);
                    char const *p = line.c_str() + i;
                    char const *q = strrchr(p, ':');
                    if (q && p + 2 < q && q[-1] == '\'') {
                        keyfile.assign(p, q - 1);
                    }
                }
                if (!keyfile.empty()) {
                    if (keyfile == sshPassphraseUser() && !sshPassphrasePass().empty()) {
                        std::string text = sshPassphrasePass() + '\n';
                        getPtyProcess()->writeInput(text.c_str(), text.size());
                    } else {
                        std::string secret = ExecLineEditDialog(this, "Passphrase for key", QString::fromStdString(line), QString(), true);
                        sshSetPassphrase(keyfile, secret);
                    }
                    return;
                }
            }

            char const *begin = line.c_str();
            char const *end = line.c_str() + line.size();
            auto Input = [&](QString const &title, bool password, std::string *value){
                Q_ASSERT(value);
                std::string header = QString("%1 for '").arg(title).toStdString();
                if (strncmp(begin, header.c_str(), header.size()) == 0) {
                    QString msg;
                    if (memcmp(end - 2, "':", 2) == 0) {
                        msg = QString::fromUtf8(begin, int(end - begin - 1));
                    } else if (memcmp(end - 3, "': ", 3) == 0) {
                        msg = QString::fromUtf8(begin, int(end - begin - 2));
                    }
                    if (!msg.isEmpty()) {
                        std::string s = ExecLineEditDialog(this, title, msg, value ? QString::fromStdString(*value) : QString(), password);
                        *value = s;
                        return true;
                    }
                }
                return false;
            };
            std::string uid = httpAuthenticationUser();
            std::string pwd = httpAuthenticationPass();
            bool ok = false;
            if (Input("Username", false, &uid)) ok = true;
            if (Input("Password", true, &pwd))  ok = true;
            if (ok) {
                httpSetAuthentication(uid, pwd);
                return;
            }

            if (StartsWith(fatal_authentication_failed_for)) {
                QMessageBox::critical(this, tr("Authentication Failed"), QString::fromStdString(line));
                abortPtyProcess();
                return;
            }
        }
    }
}

void SubmoduleMainWindow::on_action_edit_tags_triggered()
{
    Git::CommitItem const *commit = selectedCommitItem(frame());
    if (commit && Git::isValidID(commit->commit_id)) {
        EditTagsDialog dlg((MainWindow *)this, commit);
        dlg.exec();
    }
}

void SubmoduleMainWindow::on_action_push_u_triggered()
{
    pushSetUpstream(false);
}

void SubmoduleMainWindow::on_action_delete_remote_branch_triggered()
{
    deleteRemoteBranch(frame(), selectedCommitItem(frame()));
}

void SubmoduleMainWindow::on_action_terminal_triggered()
{
    qDebug()<<"on_action_terminal_triggered";
    auto const *repo = &currentRepository();
    openTerminal(repo);
}

void SubmoduleMainWindow::on_action_explorer_triggered()
{
    auto const *repo = &currentRepository();
    openExplorer(repo);
}

void SubmoduleMainWindow::on_action_reset_hard_triggered()
{
    doGitCommand([&](GitPtr g){
        g->reset_hard();
    });
}

void SubmoduleMainWindow::on_action_clean_df_triggered()
{
    doGitCommand([&](GitPtr g){
        g->clean_df();
    });
}

void SubmoduleMainWindow::postOpenRepositoryFromGitHub(QString const &username, QString const &reponame)
{
    QVariantList list;
    list.push_back(username);
    list.push_back(reponame);
    postUserFunctionEvent([&](QVariant const &v, void *){
        QVariantList l = v.toList();
        QString uname = l[0].toString();
        QString rname = l[1].toString();
        CloneFromGitHubDialog dlg((MainWindow *)this, uname, rname);
        if (dlg.exec() == QDialog::Accepted) {
            clone(dlg.url());
        }
    }, QVariant(list));
}

void SubmoduleMainWindow::on_action_stash_triggered()
{
    doGitCommand([&](GitPtr g){
        g->stash();
    });
}

void SubmoduleMainWindow::on_action_stash_apply_triggered()
{
    doGitCommand([&](GitPtr g){
        g->stash_apply();
    });
}

void SubmoduleMainWindow::on_action_stash_drop_triggered()
{
    doGitCommand([&](GitPtr g){
        g->stash_drop();
    });
}

void SubmoduleMainWindow::on_action_online_triggered()
{
    ui->radioButton_remote_online->click();
}

void SubmoduleMainWindow::on_action_offline_triggered()
{
    ui->radioButton_remote_offline->click();
}

void SubmoduleMainWindow::on_action_repositories_panel_triggered()
{
    bool checked = ui->action_repositories_panel->isChecked();
    ui->stackedWidget_leftpanel->setCurrentWidget(checked ? ui->page_repos : ui->page_collapsed);
    qDebug()<<"q"<<endl;
    if (checked) {
        ui->stackedWidget_leftpanel->setFixedWidth(m->repos_panel_width);
        ui->stackedWidget_leftpanel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        ui->stackedWidget_leftpanel->setMinimumWidth(QWIDGETSIZE_MAX);
        ui->stackedWidget_leftpanel->setMaximumWidth(QWIDGETSIZE_MAX);
    } else {
        m->repos_panel_width = ui->stackedWidget_leftpanel->width();
        ui->stackedWidget_leftpanel->setFixedWidth(24);
    }
}

void SubmoduleMainWindow::on_action_find_triggered()
{
    m->searching = false;

    if (getLogs(frame()).empty()) {
        return;
    }

    FindCommitDialog dlg((MainWindow *)this, m->search_text);
    if (dlg.exec() == QDialog::Accepted) {
        m->search_text = dlg.text();
        frame()->setFocusToLogTable();
        findNext(frame());
    }
}

void SubmoduleMainWindow::on_action_find_next_triggered()
{
    if (m->search_text.isEmpty()) {
        on_action_find_triggered();
    } else {
        findNext(frame());
    }
}

void SubmoduleMainWindow::on_action_repo_jump_to_head_triggered()
{
    QString name = "HEAD";
    GitPtr g = git();
    QString id = g->rev_parse(name);
    int row = rowFromCommitId(frame(), id);
    if (row < 0) {
        qDebug() << "No such commit";
    } else {
        setCurrentLogRow(frame(), row);
    }

}

void SubmoduleMainWindow::on_action_repo_merge_triggered()
{
    merge(frame());
}

void SubmoduleMainWindow::on_action_expand_commit_log_triggered()
{
    ui->splitter_h->setSizes({10000, 1, 1});
}

void SubmoduleMainWindow::on_action_expand_file_list_triggered()
{
    ui->splitter_h->setSizes({1, 10000, 1});
}

void SubmoduleMainWindow::on_action_expand_diff_view_triggered()
{
    ui->splitter_h->setSizes({1, 1, 10000});
}

void SubmoduleMainWindow::on_action_sidebar_triggered()
{
    bool f = ui->stackedWidget_leftpanel->isVisible();
    f = !f;
    ui->stackedWidget_leftpanel->setVisible(f);
    ui->action_sidebar->setChecked(f);
}

#if 0
void SubmoduleMainWindow::on_action_wide_triggered()
{
    QWidget *w = focusWidget();

    if (w == m->focused_widget) {
        ui->splitter_h->setSizes(m->splitter_h_sizes);
        m->focused_widget = nullptr;
    } else {
        m->focused_widget = w;
        m->splitter_h_sizes = ui->splitter_h->sizes();

        if (w == frame->logtablewidget()) {
            ui->splitter_h->setSizes({10000, 1, 1});
        } else if (ui->stackedWidget_filelist->isAncestorOf(w)) {
            ui->splitter_h->setSizes({1, 10000, 1});
        } else if (ui->frame_diff_view->isAncestorOf(w)) {
            ui->splitter_h->setSizes({1, 1, 10000});
        }
    }
}
#endif

void SubmoduleMainWindow::setShowLabels(bool show, bool save)
{
    ApplicationSettings *as = appsettings();
    as->show_labels = show;

    bool b = ui->action_show_labels->blockSignals(true);
    ui->action_show_labels->setChecked(show);
    ui->action_show_labels->blockSignals(b);

    if (save) {
        saveApplicationSettings();
    }
}

bool SubmoduleMainWindow::isLabelsVisible() const
{
    return appsettings()->show_labels;
}

void SubmoduleMainWindow::on_action_show_labels_triggered()
{
    bool f = ui->action_show_labels->isChecked();
    setShowLabels(f, true);
    frame()->updateLogTableView();
}

void SubmoduleMainWindow::on_action_submodules_triggered()
{
    GitPtr g = git();
    QList<Git::SubmoduleItem> mods = g->submodules();

    std::vector<SubmodulesDialog::Submodule> mods2;
    mods2.resize((size_t)mods.size());

    for (size_t i = 0; i < (size_t)mods.size(); i++) {
        const Git::SubmoduleItem mod = mods[(int)i];
        mods2[i].submodule = mod;

        GitPtr g2 = git(g->workingDir(), mod.path, g->sshKey());
        g2->queryCommit(mod.id, &mods2[i].head);
    }

    SubmodulesDialog dlg((MainWindow *)this, mods2);
    dlg.exec();
}

void SubmoduleMainWindow::on_action_submodule_add_triggered()
{
    QString dir = currentRepository().local_dir;
    submodule_add({}, dir);
}

void SubmoduleMainWindow::on_action_submodule_update_triggered()
{
    SubmoduleUpdateDialog dlg((MainWindow *)this);
    if (dlg.exec() == QDialog::Accepted) {
        GitPtr g = git();
        Git::SubmoduleUpdateData data;
        data.init = dlg.isInit();
        data.recursive = dlg.isRecursive();
        g->submodule_update(data, getPtyProcess());
        refresh();
    }
}

/**
 * @brief アイコンの読み込みが完了した
 */
void SubmoduleMainWindow::onAvatarUpdated(RepositoryWrapper2FrameP frame)
{
    updateCommitLogTableLater(frame.pointer, 100); // コミットログを更新（100ms後）
}

#include <fcntl.h>
#include <sys/stat.h>

void SubmoduleMainWindow::on_action_create_desktop_launcher_file_triggered()
{
#ifdef Q_OS_UNIX
	QString exec = QApplication::applicationFilePath();

	QString home = QDir::home().absolutePath();
	QString icon_dir = home / ".local/share/icons/jp.soramimi/";
	QString launcher_dir = home / ".local/share/applications/";

	QString name = "jp.soramimi.Guitar";

	QString iconfile;
	QFile src(":/image/Guitar.svg");
	if (src.open(QFile::ReadOnly)) {
		QByteArray ba = src.readAll();
		src.close();
		QDir d;
		d.mkpath(icon_dir);
		iconfile = icon_dir / name + ".svg";
		QFile dst(iconfile);
		if (dst.open(QFile::WriteOnly)) {
			dst.write(ba);
			dst.close();

			d.mkpath(launcher_dir);
			QString launcher_path = launcher_dir / name + ".desktop";
			QFile out(launcher_path);
			if (out.open(QFile::WriteOnly)) {
QString data = R"---([Desktop Entry]
Type=Application
Name=Guitar
Categories=Development
Exec=%1
Icon=%2
Terminal=false
)---";
				data = data.arg(exec).arg(iconfile);
				std::string s = data.toStdString();
				out.write(s.c_str(), s.size());
				out.close();
				std::string path = launcher_path.toStdString();
				chmod(path.c_str(), 0755);
			}
		}
	}
#endif
}

void SubmoduleMainWindow::test()
{
}

