/****************************************************************************
** Meta object code from reading C++ file 'terminalWidget.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.11.3)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "terminalWidget.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'terminalWidget.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.11.3. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_PTerminal__Widget_t {
    QByteArrayData data[17];
    char stringdata0[167];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_PTerminal__Widget_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_PTerminal__Widget_t qt_meta_stringdata_PTerminal__Widget = {
    {
QT_MOC_LITERAL(0, 0, 17), // "PTerminal::Widget"
QT_MOC_LITERAL(1, 18, 15), // "termSizeChanged"
QT_MOC_LITERAL(2, 34, 0), // ""
QT_MOC_LITERAL(3, 35, 4), // "copy"
QT_MOC_LITERAL(4, 40, 5), // "paste"
QT_MOC_LITERAL(5, 46, 7), // "copyAll"
QT_MOC_LITERAL(6, 54, 15), // "clearScrollback"
QT_MOC_LITERAL(7, 70, 5), // "reset"
QT_MOC_LITERAL(8, 76, 7), // "restart"
QT_MOC_LITERAL(9, 84, 6), // "scroll"
QT_MOC_LITERAL(10, 91, 5), // "lines"
QT_MOC_LITERAL(11, 97, 12), // "scrollTermTo"
QT_MOC_LITERAL(12, 110, 6), // "lineNo"
QT_MOC_LITERAL(13, 117, 7), // "timeout"
QT_MOC_LITERAL(14, 125, 11), // "fdReadInput"
QT_MOC_LITERAL(15, 137, 12), // "fdWriteInput"
QT_MOC_LITERAL(16, 150, 16) // "fdExceptionInput"

    },
    "PTerminal::Widget\0termSizeChanged\0\0"
    "copy\0paste\0copyAll\0clearScrollback\0"
    "reset\0restart\0scroll\0lines\0scrollTermTo\0"
    "lineNo\0timeout\0fdReadInput\0fdWriteInput\0"
    "fdExceptionInput"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_PTerminal__Widget[] = {

 // content:
       7,       // revision
       0,       // classname
       0,    0, // classinfo
      13,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       1,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    0,   79,    2, 0x06 /* Public */,

 // slots: name, argc, parameters, tag, flags
       3,    0,   80,    2, 0x0a /* Public */,
       4,    0,   81,    2, 0x0a /* Public */,
       5,    0,   82,    2, 0x0a /* Public */,
       6,    0,   83,    2, 0x0a /* Public */,
       7,    0,   84,    2, 0x0a /* Public */,
       8,    0,   85,    2, 0x0a /* Public */,
       9,    1,   86,    2, 0x0a /* Public */,
      11,    1,   89,    2, 0x0a /* Public */,
      13,    0,   92,    2, 0x09 /* Protected */,
      14,    1,   93,    2, 0x09 /* Protected */,
      15,    1,   96,    2, 0x09 /* Protected */,
      16,    1,   99,    2, 0x09 /* Protected */,

 // signals: parameters
    QMetaType::Void,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int,   10,
    QMetaType::Void, QMetaType::Int,   12,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int,    2,
    QMetaType::Void, QMetaType::Int,    2,
    QMetaType::Void, QMetaType::Int,    2,

       0        // eod
};

void PTerminal::Widget::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        Widget *_t = static_cast<Widget *>(_o);
        Q_UNUSED(_t)
        switch (_id) {
        case 0: _t->termSizeChanged(); break;
        case 1: _t->copy(); break;
        case 2: _t->paste(); break;
        case 3: _t->copyAll(); break;
        case 4: _t->clearScrollback(); break;
        case 5: _t->reset(); break;
        case 6: _t->restart(); break;
        case 7: _t->scroll((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 8: _t->scrollTermTo((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 9: _t->timeout(); break;
        case 10: _t->fdReadInput((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 11: _t->fdWriteInput((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 12: _t->fdExceptionInput((*reinterpret_cast< int(*)>(_a[1]))); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (Widget::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&Widget::termSizeChanged)) {
                *result = 0;
                return;
            }
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject PTerminal::Widget::staticMetaObject = {
    { &AbstractTerminalWidget::staticMetaObject, qt_meta_stringdata_PTerminal__Widget.data,
      qt_meta_data_PTerminal__Widget,  qt_static_metacall, nullptr, nullptr}
};


const QMetaObject *PTerminal::Widget::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *PTerminal::Widget::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_PTerminal__Widget.stringdata0))
        return static_cast<void*>(this);
    return AbstractTerminalWidget::qt_metacast(_clname);
}

int PTerminal::Widget::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = AbstractTerminalWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 13)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 13;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 13)
            *reinterpret_cast<int*>(_a[0]) = -1;
        _id -= 13;
    }
    return _id;
}

// SIGNAL 0
void PTerminal::Widget::termSizeChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
