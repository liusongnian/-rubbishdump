#include "Terminal.h"
#include "ApplicationGlobal.h"
#include <QFileInfo>

#include "ApplicationGlobal.h"

#ifdef Q_OS_WIN

#include <QDir>
#include <windows.h>

void Terminal::open(QString const &dir, QString const &ssh_key)
{
	QString cmd = global->appsettings.terminal_command;
	QDir::setCurrent(dir);
	if (dir.indexOf('\"') < 0 && QFileInfo(dir).isDir()) {
		PROCESS_INFORMATION pi;
		STARTUPINFO si;

		ZeroMemory(&si, sizeof(si));
		si.cb = sizeof(si);

		CreateProcess(nullptr, (wchar_t *)cmd.utf16(), nullptr, nullptr, FALSE, NORMAL_PRIORITY_CLASS, nullptr, nullptr, &si, &pi);

		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);
	}
}

#else

void Terminal::open(QString const &dir, QString const &ssh_key)
{
	if (dir.indexOf('\"') < 0 && QFileInfo(dir).isDir()) {
		QString term = global->appsettings.terminal_command;
		if (term.isEmpty()) {
			term = ApplicationSettings::defaultSettings().terminal_command;
		}
		QString cmd = "/bin/sh -c \"cd \\\"%1\\\" && %2\" &";
		cmd = cmd.arg(dir).arg(term);
		if (!global->appsettings.ssh_command.isEmpty() && !ssh_key.isEmpty()) {
			QString env = QString("GIT_SSH_COMMAND=\"%1 -i %2\" ").arg(global->appsettings.ssh_command).arg(ssh_key);
			cmd = env + cmd;
		}
		int r = system(cmd.toStdString().c_str());
		(void)r;
	}
}

#endif
