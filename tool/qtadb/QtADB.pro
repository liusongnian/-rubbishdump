#
#Copyright 2010-20XX by Jakub Motyczko
#
#   Licensed under the Apache License, Version 2.0 (the "License");
#   you may not use this file except in compliance with the License.
#   You may obtain a copy of the License at
#
#       http://www.apache.org/licenses/LICENSE-2.0
#
#   Unless required by applicable law or agreed to in writing, software
#   distributed under the License is distributed on an "AS IS" BASIS,
#   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#   See the License for the specific language governing permissions and
#   limitations under the License.
#
#   @author Jakub Motyczko
# -------------------------------------------------
QT += network core widgets  xlsx
#QT += declarative
QT += quick
CONFIG(debug,debug|release):TARGET = QtADBD
CONFIG(release,debug|release):TARGET = QtADB

CONFIG(debug, debug|release){
    DEFINES -= QT_NO_DEBUG_OUTPUT
}
else{
    DEFINES += QT_NO_DEBUG_OUTPUT
}

CONFIG += c++1z nostrip debug_info console
QT_LOGGING_TO_CONSOLE=1

TEMPLATE = app

SOURCES += main.cpp \
    ./dialogs/mainwindow.cpp \
    ./dialogs/dialogkopiuj.cpp \
    ./classes/phone.cpp \
    ./classes/computer.cpp \
    ./threads/screenshotthread.cpp \
    ./dialogs/connectWifi.cpp \
    ./dialogs/aboutdialog.cpp \
    ./dialogs/appdialog.cpp \
    ./classes/application.cpp \
    ./classes/updateapp.cpp \
    ./dialogs/appinfo.cpp \
    widgets/filewidget.cpp \
    widgets/appwidget.cpp \
    widgets/recoverywidget.cpp \
    widgets/fastbootwidget.cpp \
    widgets/screenshotwidget.cpp \
    widgets/phoneinfowidget.cpp \
    widgets/settingswidget.cpp \
    widgets/shellwidget.cpp \
    widgets/messagewidget.cpp \
    widgets/contactwidget.cpp \
    classes/models/apptablemodel.cpp \
    classes/models/filetablemodel.cpp \
    classes/models/backuptablemodel.cpp \
    classes/animation.cpp \
    classes/models/messagemodel.cpp \
    classes/models/messagethreadmodel.cpp \
    classes/models/contactmodel.cpp \
    dialogs/logcatdialog.cpp \
    classes/models/logcatmodel.cpp \
    classes/ecwin7.cpp \
    classes/mytableview.cpp \
    dialogs/registerdialog.cpp \
    abstractProcess/unix/UnixProcess.cpp \
    abstractProcess/unix/UnixPtyProcess.cpp \
    abstractProcess/AbstractProcess.cpp \
    abstractProcess/MyProcess.cpp \
    tinymix/readtinymix.cpp \
    asoundinfo/asoundinfo.cpp \
    json/cJSON.c \
    json/cJSON_Utils.c \
    widgets/Asoundinfowidget.cpp \
    widgets/DepartNodeItem.cpp \
    widgets/EmployeeNodeItem.cpp \
    widgets/StreamNodeItem.cpp \
    external/tinyxml2/tinyxml2.cpp \
    libutils/VectorImpl.cpp \
    libutils/SharedBuffer.cpp \
    libutils/String8.cpp \
    libutils/String16.cpp \
    libutils/Unicode.cpp \
    asoundtrack/asoundtrack.cpp \
    asoundtrack/audiohardware/common/aud_drv/audio_hw_hal.cpp \
    asoundtrack/libmediahelper/TypeConverter.cpp \
    asoundtrack/libmediahelper/AudioValidator.cpp \
    asoundtrack/libmediahelper/AudioParameter.cpp \
    asoundtrack/audiohardware/common/V3/aud_drv/AudioALSAHardware.cpp \
    asoundtrack/audiohardware/common/V3/aud_drv/AudioALSADeviceParser.cpp
HEADERS += ./dialogs/mainwindow.h \
    ./dialogs/dialogkopiuj.h \
    ./classes/phone.h \
    ./classes/computer.h \
    ./threads/screenshotthread.h \
    ./dialogs/connectWifi.h \
    ./dialogs/aboutdialog.h \
    ./dialogs/appdialog.h \
    ./classes/application.h \
    ./classes/updateapp.h \
    ./dialogs/appinfo.h \
    widgets/filewidget.h \
    widgets/appwidget.h \
    widgets/recoverywidget.h \
    widgets/fastbootwidget.h \
    widgets/screenshotwidget.h \
    widgets/phoneinfowidget.h \
    widgets/settingswidget.h \
    widgets/shellwidget.h \
    widgets/messagewidget.h \
    widgets/contactwidget.h \
    classes/models/apptablemodel.h \
    classes/models/filetablemodel.h \
    classes/models/backuptablemodel.h \
    classes/animation.h \
    classes/models/messagemodel.h \
    classes/models/messagethreadmodel.h \
    classes/models/contactmodel.h \
    dialogs/logcatdialog.h \
    classes/models/logcatmodel.h \
    classes/ecwin7.h \
    classes/mytableview.h \
    dialogs/registerdialog.h \
    abstractProcess/unix/UnixProcess.h \
    abstractProcess/unix/UnixPtyProcess.h \
    abstractProcess/AbstractProcess.h \
    abstractProcess/MyProcess.h \
    tinymix/readtinymix.h \
    asoundinfo/asoundinfo.h \
    json/cJSON.h \
    json/cJSON_Utils.h \
    asoundtrack/asoundtrack.h \
    widgets/Asoundinfowidget.h \
    widgets/DepartNodeItem.h \
    widgets/EmployeeNodeItem.h \
    widgets/StreamNodeItem.h \
    external/tinyxml2/tinyxml2.h \
    asoundtrack/audiohardware/common/include/KeyedVector.h \
    asoundtrack/audiohardware/common/include/audio.h \
    asoundtrack/audiohardware/common/include/hardware.h \
    asoundtrack/audiohardware/common/include/hardware/audio_mtk.h \
    asoundtrack/audiohardware/common/V3/include/AudioType.h \
    asoundtrack/audiohardware/common/V3/include/AudioTypeExt.h \
    asoundtrack/audiohardware/common/V3/include/AudioDeviceInt.h \
    asoundtrack/audiohardware/common/include/hardware_legacy/AudioMTKHardwareInterface.h \
    asoundtrack/audiohardware/common/include/AudioParameter.h \
    asoundtrack/audiohardware/common/aud_drv/audio_hw_hal.h \
    libutils/include/utils/VectorImpl.h \
    libutils/SharedBuffer.h \
    libutils/include/utils/String8.h \
    libutils/include/utils/String16.h \
    asoundtrack/libmediahelper/include/media/convert.h \
    asoundtrack/libmediahelper/include/media/TypeConverter.h \
    asoundtrack/libmediahelper/include/media/AudioValidator.h \
    asoundtrack/libmediahelper/include/media/AudioParameter.h \
    asoundtrack/audiohardware/common/include/system/audio.h \
    asoundtrack/audiohardware/common/include/system/audio.h \
    asoundtrack/audiohardware/common/V3/include/AudioALSAHardware.h
FORMS += ./dialogs/mainwindow.ui \
    ./dialogs/dialogkopiuj.ui \
    ./dialogs/connectWifi.ui \
    ./dialogs/aboutdialog.ui \
    ./dialogs/appdialog.ui \
    ./dialogs/appinfo.ui \
    widgets/filewidget.ui \
    widgets/appwidget.ui \
    widgets/recoverywidget.ui \
    widgets/fastbootwidget.ui \
    widgets/screenshotwidget.ui \
    widgets/phoneinfowidget.ui \
    widgets/settingswidget.ui \
    widgets/shellwidget.ui \
    widgets/messagewidget.ui \
    widgets/contactwidget.ui \
    dialogs/logcatdialog.ui \
    dialogs/registerdialog.ui \
    tinymix/readtinymix.ui \
    asoundinfo/asoundinfo.ui \
    asoundtrack/asoundtrack.ui \
    widgets/asoundinfowidget_1.ui \
    widgets/Asoundinfowidget.ui \
    widgets/EmployeeNodeItem.ui \
    widgets/StreamNodeItem.ui
LANGUAGES = en pl el es it nl cs de hu sv ja ar ru pt sr zh_CN zh_TW

# parameters: var, prepend, append
defineReplace(prependAll) {
 for(a,$$1):result += $$2$${a}$$3
 return($$result)
}

#TRANSLATIONS = $$prependAll(LANGUAGES, $$PWD/languages/qtadb_, .ts)
TRANSLATIONS = $$prependAll(LANGUAGES, languages/qtadb_, .ts)

TRANSLATIONS_FILES =

qtPrepareTool(LRELEASE, lrelease)
for(tsfile, TRANSLATIONS) {
 #qmfile = $$shadowed($$tsfile)
 qmfile = $$relative_path($$tsfile)
 qmfile ~= s,.ts$,.qm,
 qmdir = $$dirname(qmfile)
 !exists($$qmdir) {
    mkpath($$qmdir)|error("Aborting.")
 }
 command = $$LRELEASE -removeidentical $$tsfile -qm $$qmfile
 system($$command)|error("Failed to run: $$command")
 TRANSLATIONS_FILES += $$qmfile
}

RC_FILE = ikonka.rc
RESOURCES += zasoby.qrc
OTHER_FILES += otherFiles/changes.txt \
    otherFiles/todo.txt \
    otherFiles/busybox

OTHER_FILES += \
    qml/messageView.qml \
    qml/messages/ThreadList.qml \
    qml/messages/MessageList.qml \
    qml/messages/delegates/MessageDelegate.qml \
    qml/messages/delegates/ThreadDelegate.qml \
    qml/messages/delegates/ScrollBar.qml \
    qml/messages/delegates/Button.qml \
    qml/messages/delegates/SendMessage.qml \
    qml/messages/NewMessage.qml \
    qml/messages/delegates/ContactDelegate.qml \
    qml/messages/ContactList.qml \
    qml/messages/delegates/ThreadContextMenu.qml

win32 {
LIBS += libole32
}

mac {
QMAKE_INFO_PLIST = QtADB.plist
ICON = images/android.icns
BUSYBOX.files = otherFiles/busybox
BUSYBOX.path = Contents/Resources
QMAKE_BUNDLE_DATA += BUSYBOX
}

#tutaj i w ecwin7.h
