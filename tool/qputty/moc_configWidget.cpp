/****************************************************************************
** Meta object code from reading C++ file 'configWidget.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.11.3)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "configWidget.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'configWidget.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.11.3. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_ConfigWidget_t {
    QByteArrayData data[17];
    char stringdata0[184];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_ConfigWidget_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_ConfigWidget_t qt_meta_stringdata_ConfigWidget = {
    {
QT_MOC_LITERAL(0, 0, 12), // "ConfigWidget"
QT_MOC_LITERAL(1, 13, 10), // "changePage"
QT_MOC_LITERAL(2, 24, 0), // ""
QT_MOC_LITERAL(3, 25, 16), // "QTreeWidgetItem*"
QT_MOC_LITERAL(4, 42, 4), // "item"
QT_MOC_LITERAL(5, 47, 4), // "last"
QT_MOC_LITERAL(6, 52, 12), // "buttonToggle"
QT_MOC_LITERAL(7, 65, 7), // "checked"
QT_MOC_LITERAL(8, 73, 13), // "buttonClicked"
QT_MOC_LITERAL(9, 87, 16), // "QAbstractButton*"
QT_MOC_LITERAL(10, 104, 6), // "button"
QT_MOC_LITERAL(11, 111, 13), // "doubleClicked"
QT_MOC_LITERAL(12, 125, 11), // "textChanged"
QT_MOC_LITERAL(13, 137, 15), // "selectionChange"
QT_MOC_LITERAL(14, 153, 10), // "selectFile"
QT_MOC_LITERAL(15, 164, 8), // "QWidget*"
QT_MOC_LITERAL(16, 173, 10) // "selectFont"

    },
    "ConfigWidget\0changePage\0\0QTreeWidgetItem*\0"
    "item\0last\0buttonToggle\0checked\0"
    "buttonClicked\0QAbstractButton*\0button\0"
    "doubleClicked\0textChanged\0selectionChange\0"
    "selectFile\0QWidget*\0selectFont"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_ConfigWidget[] = {

 // content:
       7,       // revision
       0,       // classname
       0,    0, // classinfo
       9,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

 // slots: name, argc, parameters, tag, flags
       1,    2,   59,    2, 0x09 /* Protected */,
       6,    1,   64,    2, 0x09 /* Protected */,
       8,    0,   67,    2, 0x09 /* Protected */,
       8,    1,   68,    2, 0x09 /* Protected */,
      11,    0,   71,    2, 0x09 /* Protected */,
      12,    1,   72,    2, 0x09 /* Protected */,
      13,    0,   75,    2, 0x09 /* Protected */,
      14,    1,   76,    2, 0x09 /* Protected */,
      16,    1,   79,    2, 0x09 /* Protected */,

 // slots: parameters
    QMetaType::Void, 0x80000000 | 3, 0x80000000 | 3,    4,    5,
    QMetaType::Void, QMetaType::Bool,    7,
    QMetaType::Void,
    QMetaType::Void, 0x80000000 | 9,   10,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString,    2,
    QMetaType::Void,
    QMetaType::Void, 0x80000000 | 15,    2,
    QMetaType::Void, 0x80000000 | 15,    2,

       0        // eod
};

void ConfigWidget::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        ConfigWidget *_t = static_cast<ConfigWidget *>(_o);
        Q_UNUSED(_t)
        switch (_id) {
        case 0: _t->changePage((*reinterpret_cast< QTreeWidgetItem*(*)>(_a[1])),(*reinterpret_cast< QTreeWidgetItem*(*)>(_a[2]))); break;
        case 1: _t->buttonToggle((*reinterpret_cast< bool(*)>(_a[1]))); break;
        case 2: _t->buttonClicked(); break;
        case 3: _t->buttonClicked((*reinterpret_cast< QAbstractButton*(*)>(_a[1]))); break;
        case 4: _t->doubleClicked(); break;
        case 5: _t->textChanged((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 6: _t->selectionChange(); break;
        case 7: _t->selectFile((*reinterpret_cast< QWidget*(*)>(_a[1]))); break;
        case 8: _t->selectFont((*reinterpret_cast< QWidget*(*)>(_a[1]))); break;
        default: ;
        }
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        switch (_id) {
        default: *reinterpret_cast<int*>(_a[0]) = -1; break;
        case 7:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<int*>(_a[0]) = -1; break;
            case 0:
                *reinterpret_cast<int*>(_a[0]) = qRegisterMetaType< QWidget* >(); break;
            }
            break;
        case 8:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<int*>(_a[0]) = -1; break;
            case 0:
                *reinterpret_cast<int*>(_a[0]) = qRegisterMetaType< QWidget* >(); break;
            }
            break;
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject ConfigWidget::staticMetaObject = {
    { &QWidget::staticMetaObject, qt_meta_stringdata_ConfigWidget.data,
      qt_meta_data_ConfigWidget,  qt_static_metacall, nullptr, nullptr}
};


const QMetaObject *ConfigWidget::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *ConfigWidget::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_ConfigWidget.stringdata0))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int ConfigWidget::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 9)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 9;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 9)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 9;
    }
    return _id;
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
