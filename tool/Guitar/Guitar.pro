
QT += core gui widgets svg network
win32:QT += winextras

CONFIG(debug,debug|release):TARGET = Guitard
CONFIG(release,debug|release):TARGET = Guitar
TEMPLATE = app

CONFIG += c++1z nostrip debug_info

DESTDIR = $$PWD/_bin

TRANSLATIONS = $$PWD/src/resources/translations/Guitar_ja.ts
TRANSLATIONS += $$PWD/src/resources/translations/Guitar_ru.ts
TRANSLATIONS += $$PWD/src/resources/translations/Guitar_es.ts
TRANSLATIONS += $$PWD/src/resources/translations/Guitar_zh-CN.ts
TRANSLATIONS += $$PWD/src/resources/translations/Guitar_zh-TW.ts

DEFINES += APP_GUITAR

DEFINES += HAVE_POSIX_OPENPT
macx:DEFINES += HAVE_SYS_TIME_H
macx:DEFINES += HAVE_UTMPX

gcc:QMAKE_CXXFLAGS += -Wall -Wextra -Werror=return-type -Werror=trigraphs -Wno-switch -Wno-reorder
linux:QMAKE_RPATHDIR += $ORIGIN
macx:QMAKE_RPATHDIR += @executable_path/../Frameworks

INCLUDEPATH += $$PWD/src
INCLUDEPATH += $$PWD/src/coloredit
INCLUDEPATH += $$PWD/src/texteditor

win32:INCLUDEPATH += $$PWD/misc/winpty/include

# winpty x86
#win32:LIBS += $$PWD/misc/winpty/ia32/lib/winpty.lib
# winpty x86_64
win32:LIBS += $$PWD/misc/winpty/x64/lib/winpty.lib

# OpenSSL

linux {
	static_link_openssl {
		LIBS += $$OPENSSL_LIB_DIR/libssl.a $$OPENSSL_LIB_DIR/libcrypto.a -ldl
	} else {
		LIBS += -lssl -lcrypto
	}
}
haiku:LIBS += -lssl -lcrypto -lnetwork
macx:INCLUDEPATH += /usr/local/include
macx:LIBS += /usr/local/lib/libssl.a /usr/local/lib/libcrypto.a

win32:msvc {
	INCLUDEPATH += $$PWD/../zlib

	# OpenSSL x86
#	INCLUDEPATH += "C:\Program Files (x86)\OpenSSL\include"
#	LIBS += "-LC:\Program Files (x86)\OpenSSL\lib"
	# OpenSSL x86_64
	INCLUDEPATH += "C:\Program Files\OpenSSL\include"
	LIBS += "-LC:\Program Files\OpenSSL\lib"

	# OpenSSL 1.0
#	LIBS += -llibeay32 -lssleay32
	# OpenSSL 1.1
	LIBS += -llibcrypto -llibssl
}

win32:gcc {
	INCLUDEPATH += C:\Qt\Tools\mingw530_32\opt\include
	LIBS += -LC:\Qt\Tools\mingw530_32\opt\lib
	LIBS += -lcrypto -lssl
}

# execute 'ruby prepare.rb' automatically

prepare.target = prepare
prepare.commands = cd $$PWD && ruby -W0 prepare.rb
QMAKE_EXTRA_TARGETS += prepare
PRE_TARGETDEPS += prepare


# zlib

win32:msvc {
	CONFIG(debug, debug|release):LIBS += $$PWD/_bin/libz.lib
	CONFIG(release, debug|release):LIBS += $$PWD/_bin/libz.lib
}

win32:gcc {
	CONFIG(debug, debug|release):LIBS += $$PWD/_bin/liblibz.a
	CONFIG(release, debug|release):LIBS += $$PWD/_bin/liblibz.a
}

!haiku {
	use_system_zlib {
		unix:LIBS += -lz
	} else {
		unix:CONFIG(debug, debug|release):LIBS += $$PWD/_bin/libzd.a
		unix:CONFIG(release, debug|release):LIBS += $$PWD/_bin/libz.a
	}
}

haiku:LIBS += -lz

# filetype library

INCLUDEPATH += $$PWD/filetype/file/src
SOURCES += filetype/filetype.cpp \
	src/common/base64.cpp

#

win32 {
	LIBS += -ladvapi32 -lshell32 -luser32 -lws2_32
	RC_FILE = win.rc
	QMAKE_SUBSYSTEM_SUFFIX=,5.01
}

macx {
	QMAKE_INFO_PLIST = Info.plist
	ICON += src/resources/Guitar.icns
	t.path=Contents/Resources
	QMAKE_BUNDLE_DATA += t
}

SOURCES += \
	src/AboutDialog.cpp \
	src/AbstractProcess.cpp \
	src/AbstractSettingForm.cpp \
	src/ApplicationGlobal.cpp \
	src/AreYouSureYouWantToContinueConnectingDialog.cpp \
	src/AvatarLoader.cpp \
	src/BasicRepositoryDialog.cpp \
	src/BigDiffWindow.cpp \
        src/BigDiff2Window.cpp \
	src/BlameWindow.cpp \
    src/BranchLabel.cpp \
	src/CheckoutDialog.cpp \
	src/CherryPickDialog.cpp \
	src/ClearButton.cpp \
	src/CloneDialog.cpp \
	src/CloneFromGitHubDialog.cpp \
    src/ColorButton.cpp \
	src/CommitDialog.cpp \
	src/CommitExploreWindow.cpp \
	src/CommitPropertyDialog.cpp \
	src/CommitViewWindow.cpp \
	src/ConfigCredentialHelperDialog.cpp \
	src/ConfigSigningDialog.cpp \
	src/CreateRepositoryDialog.cpp \
	src/DeleteBranchDialog.cpp \
	src/DeleteTagsDialog.cpp \
	src/DialogHeaderFrame.cpp \
	src/DirectoryLineEdit.cpp \
	src/DoYouWantToInitDialog.cpp \
	src/EditGitIgnoreDialog.cpp \
	src/EditRemoteDialog.cpp \
	src/EditTagsDialog.cpp \
	src/ExperimentDialog.cpp \
	src/FileDiffSliderWidget.cpp \
	src/FileDiffWidget.cpp \
        src/FileDiff2Widget.cpp \
	src/FileHistoryWindow.cpp \
	src/FilePropertyDialog.cpp \
	src/FileUtil.cpp \
	src/FileViewWidget.cpp \
        src/FileView2Widget.cpp \
	src/FilesListWidget.cpp \
	src/FindCommitDialog.cpp \
	src/Git.cpp \
	src/GitDiff.cpp \
	src/GitHubAPI.cpp \
	src/GitObjectManager.cpp \
	src/GitPack.cpp \
	src/GitPackIdxV2.cpp \
	src/HyperLinkLabel.cpp \
	src/ImageViewWidget.cpp \
        src/ImageView2Widget.cpp \
	src/InputNewTagDialog.cpp \
	src/JumpDialog.cpp \
	src/Languages.cpp \
	src/LineEditDialog.cpp \
	src/LocalSocketReader.cpp \
	src/LogTableWidget.cpp \
        src/LogTable2Widget.cpp \
	src/MainWindow.cpp \
	src/MaximizeButton.cpp \
	src/MemoryReader.cpp \
	src/MenuButton.cpp \
	src/MergeDialog.cpp \
	src/MyImageViewWidget.cpp \
	src/MyProcess.cpp \
	src/MySettings.cpp \
	src/MyTableWidgetDelegate.cpp \
	src/MyTextEditorWidget.cpp \
	src/MyToolButton.cpp \
        src/MyTool2Button.cpp \
	src/ObjectBrowserDialog.cpp \
	src/Photoshop.cpp \
	src/PushDialog.cpp \
	src/ReadOnlyLineEdit.cpp \
	src/ReadOnlyPlainTextEdit.cpp \
	src/ReflogWindow.cpp \
	src/RemoteAdvancedOptionWidget.cpp \
	src/RemoteRepositoriesTableWidget.cpp \
	src/RepositoriesTreeWidget.cpp \
	src/RepositoryData.cpp \
	src/RepositoryInfoFrame.cpp \
	src/RepositoryLineEdit.cpp \
	src/RepositoryPropertyDialog.cpp \
    src/RepositoryWrapperFrame.cpp \
    src/RepositoryWrapper2Frame.cpp \
	src/SearchFromGitHubDialog.cpp \
	src/SelectCommandDialog.cpp \
	src/SelectGpgKeyDialog.cpp \
	src/SelectItemDialog.cpp \
	src/SetGlobalUserDialog.cpp \
	src/SetGpgSigningDialog.cpp \
	src/SetRemoteUrlDialog.cpp \
	src/SetUserDialog.cpp \
	src/SettingBehaviorForm.cpp \
	src/SettingExampleForm.cpp \
	src/SettingGeneralForm.cpp \
	src/SettingNetworkForm.cpp \
	src/SettingPrograms2Form.cpp \
	src/SettingProgramsForm.cpp \
    src/SettingVisualForm.cpp \
	src/SettingsDialog.cpp \
	src/StatusLabel.cpp \
    src/SubmoduleAddDialog.cpp \
    src/SubmoduleMainWindow.cpp \
    src/SubmoduleUpdateDialog.cpp \
    src/SubmodulesDialog.cpp \
	src/Terminal.cpp \
	src/TextEditDialog.cpp \
	src/Theme.cpp \
    src/UserEvent.cpp \
	src/WelcomeWizardDialog.cpp \
	src/charvec.cpp \
    src/coloredit/ColorDialog.cpp \
    src/coloredit/ColorEditWidget.cpp \
    src/coloredit/ColorPreviewWidget.cpp \
    src/coloredit/ColorSlider.cpp \
    src/coloredit/ColorSquareWidget.cpp \
    src/coloredit/RingSlider.cpp \
	src/common/joinpath.cpp \
	src/common/misc.cpp \
	src/darktheme/DarkStyle.cpp \
	src/darktheme/NinePatch.cpp \
	src/darktheme/StandardStyle.cpp \
	src/darktheme/TraditionalWindowsStyleTreeControl.cpp \
	src/gpg.cpp \
	src/gunzip.cpp \
	src/main.cpp\
	src/texteditor/AbstractCharacterBasedApplication.cpp \
	src/texteditor/InputMethodPopup.cpp \
	src/texteditor/TextEditorTheme.cpp \
	src/texteditor/TextEditorWidget.cpp \
	src/texteditor/UnicodeWidth.cpp \
	src/texteditor/unicode.cpp \
	src/urlencode.cpp \
	src/webclient.cpp \

HEADERS  += \
	filetype/filetype.h \
	src/AboutDialog.h \
	src/AbstractProcess.h \
	src/AbstractSettingForm.h \
	src/ApplicationGlobal.h \
	src/AreYouSureYouWantToContinueConnectingDialog.h \
	src/AvatarLoader.h \
	src/BasicRepositoryDialog.h \
	src/BigDiffWindow.h \
        src/BigDiff2Window.h \
	src/BlameWindow.h \
	src/BranchLabel.h \
	src/CheckoutDialog.h \
	src/CherryPickDialog.h \
	src/ClearButton.h \
	src/CloneDialog.h \
	src/CloneFromGitHubDialog.h \
	src/ColorButton.h \
	src/CommitDialog.h \
	src/CommitExploreWindow.h \
	src/CommitPropertyDialog.h \
	src/CommitViewWindow.h \
	src/ConfigCredentialHelperDialog.h \
	src/ConfigSigningDialog.h \
	src/CreateRepositoryDialog.h \
	src/Debug.h \
	src/DeleteBranchDialog.h \
	src/DeleteTagsDialog.h \
	src/DialogHeaderFrame.h \
	src/DirectoryLineEdit.h \
	src/DoYouWantToInitDialog.h \
	src/EditGitIgnoreDialog.h \
	src/EditRemoteDialog.h \
	src/EditTagsDialog.h \
	src/ExperimentDialog.h \
	src/FileDiffSliderWidget.h \
	src/FileDiffWidget.h \
        src/FileDiff2Widget.h \
	src/FileHistoryWindow.h \
	src/FilePropertyDialog.h \
	src/FileUtil.h \
	src/FilesListWidget.h \
	src/FindCommitDialog.h \
	src/Git.h \
	src/GitDiff.h \
	src/GitHubAPI.h \
	src/GitObjectManager.h \
	src/GitPack.h \
	src/GitPackIdxV2.h \
	src/HyperLinkLabel.h \
	src/ImageViewWidget.h \
        src/ImageView2Widget.h \
	src/InputNewTagDialog.h \
	src/JumpDialog.h \
	src/Languages.h \
	src/LineEditDialog.h \
	src/LocalSocketReader.h \
	src/LogTableWidget.h \
        src/LogTable2Widget.h \
	src/MainWindow.h \
	src/MaximizeButton.h \
	src/MemoryReader.h \
	src/MenuButton.h \
	src/MergeDialog.h \
	src/MyImageViewWidget.h \
	src/MyProcess.h \
	src/MySettings.h \
	src/MyTableWidgetDelegate.h \
	src/MyTextEditorWidget.h \
	src/MyToolButton.h \
        src/MyTool2Button.h \
	src/ObjectBrowserDialog.h \
	src/Photoshop.h \
	src/PushDialog.h \
	src/ReadOnlyLineEdit.h \
	src/ReadOnlyPlainTextEdit.h \
	src/ReflogWindow.h \
	src/RemoteAdvancedOptionWidget.h \
	src/RemoteRepositoriesTableWidget.h \
	src/RepositoriesTreeWidget.h \
	src/RepositoryData.h \
	src/RepositoryInfoFrame.h \
	src/RepositoryLineEdit.h \
	src/RepositoryPropertyDialog.h \
	src/RepositoryWrapperFrame.h \
        src/RepositoryWrapper2Frame.h \
	src/SearchFromGitHubDialog.h \
	src/SelectCommandDialog.h \
	src/SelectGpgKeyDialog.h \
	src/SelectItemDialog.h \
	src/SetGlobalUserDialog.h \
	src/SetGpgSigningDialog.h \
	src/SetRemoteUrlDialog.h \
	src/SetUserDialog.h \
	src/SettingBehaviorForm.h \
	src/SettingExampleForm.h \
	src/SettingGeneralForm.h \
	src/SettingNetworkForm.h \
	src/SettingPrograms2Form.h \
	src/SettingProgramsForm.h \
	src/SettingVisualForm.h \
	src/SettingsDialog.h \
	src/StatusLabel.h \
	src/SubmoduleAddDialog.h \
	src/SubmoduleMainWindow.h \
	src/SubmoduleUpdateDialog.h \
	src/SubmodulesDialog.h \
	src/Terminal.h \
	src/TextEditDialog.h \
	src/Theme.h \
	src/UserEvent.h \
	src/WelcomeWizardDialog.h \
	src/charvec.h \
	src/coloredit/ColorDialog.h \
	src/coloredit/ColorEditWidget.h \
	src/coloredit/ColorPreviewWidget.h \
	src/coloredit/ColorSlider.h \
	src/coloredit/ColorSquareWidget.h \
	src/coloredit/RingSlider.h \
	src/common/base64.h \
	src/common/joinpath.h \
	src/common/misc.h \
	src/darktheme/DarkStyle.h \
	src/darktheme/NinePatch.h \
	src/darktheme/StandardStyle.h \
	src/darktheme/TraditionalWindowsStyleTreeControl.h \
	src/gpg.h \
	src/gunzip.h \
	src/main.h \
	src/platform.h \
	src/texteditor/AbstractCharacterBasedApplication.h \
	src/texteditor/InputMethodPopup.h \
	src/texteditor/TextEditorTheme.h \
	src/texteditor/TextEditorWidget.h \
	src/texteditor/UnicodeWidth.h \
        src/texteditor/unicode.h \
	src/urlencode.h \
	src/webclient.h

HEADERS += src/version.h

FORMS    += \
	src/AboutDialog.ui \
	src/AreYouSureYouWantToContinueConnectingDialog.ui \
	src/BigDiffWindow.ui \
        src/BigDiff2Window.ui \
	src/BlameWindow.ui \
	src/CheckoutDialog.ui \
	src/CherryPickDialog.ui \
	src/CloneDialog.ui \
	src/CloneFromGitHubDialog.ui \
	src/CommitDialog.ui \
	src/CommitExploreWindow.ui \
	src/CommitPropertyDialog.ui \
	src/CommitViewWindow.ui \
	src/ConfigCredentialHelperDialog.ui \
	src/ConfigSigningDialog.ui \
	src/CreateRepositoryDialog.ui \
	src/DeleteBranchDialog.ui \
	src/DeleteTagsDialog.ui \
	src/DoYouWantToInitDialog.ui \
	src/EditGitIgnoreDialog.ui \
	src/EditRemoteDialog.ui \
	src/EditTagsDialog.ui \
	src/ExperimentDialog.ui \
	src/FileDiffWidget.ui \
        src/FileDiff2Widget.ui \
	src/FileHistoryWindow.ui \
	src/FilePropertyDialog.ui \
	src/FindCommitDialog.ui \
	src/InputNewTagDialog.ui \
	src/JumpDialog.ui \
	src/LineEditDialog.ui \
	src/MainWindow.ui \
	src/MergeDialog.ui \
	src/ObjectBrowserDialog.ui \
	src/PushDialog.ui \
	src/ReflogWindow.ui \
	src/RemoteAdvancedOptionWidget.ui \
	src/RepositoryPropertyDialog.ui \
	src/SearchFromGitHubDialog.ui \
	src/SelectCommandDialog.ui \
	src/SelectGpgKeyDialog.ui \
	src/SelectItemDialog.ui \
	src/SetGlobalUserDialog.ui \
	src/SetGpgSigningDialog.ui \
	src/SetRemoteUrlDialog.ui \
	src/SetUserDialog.ui \
	src/SettingBehaviorForm.ui \
	src/SettingExampleForm.ui \
	src/SettingGeneralForm.ui \
	src/SettingNetworkForm.ui \
	src/SettingPrograms2Form.ui \
	src/SettingProgramsForm.ui \
	src/SettingVisualForm.ui \
	src/SettingsDialog.ui \
	src/SubmoduleAddDialog.ui \
	src/SubmoduleMainWindow.ui \
	src/SubmoduleUpdateDialog.ui \
	src/SubmodulesDialog.ui \
	src/TextEditDialog.ui \
	src/WelcomeWizardDialog.ui \
	src/coloredit/ColorDialog.ui \
	src/coloredit/ColorEditWidget.ui

RESOURCES += \
	src/resources/resources.qrc

unix {
	SOURCES += \
		src/unix/UnixProcess.cpp \
		src/unix/UnixPtyProcess.cpp
	HEADERS += \
		src/unix/UnixProcess.h \
		src/unix/UnixPtyProcess.h
}

win32 {
	SOURCES += \
        src/win32/Win32Process.cpp \
        src/win32/Win32PtyProcess.cpp \
        src/win32/event.cpp \
        src/win32/thread.cpp \
        src/win32/win32.cpp

	HEADERS += \
        src/win32/Win32Process.h \
        src/win32/Win32PtyProcess.h \
        src/win32/event.h \
        src/win32/mutex.h \
        src/win32/thread.h \
        src/win32/win32.h
}

include(filetype/filetype.pri)


