/****************************************************************************
** Meta object code from reading C++ file 'terminalText.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.11.3)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "terminalText.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'terminalText.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.11.3. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_PTerminal__Text_t {
    QByteArrayData data[15];
    char stringdata0[81];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_PTerminal__Text_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_PTerminal__Text_t qt_meta_stringdata_PTerminal__Text = {
    {
QT_MOC_LITERAL(0, 0, 15), // "PTerminal::Text"
QT_MOC_LITERAL(1, 16, 3), // "LDC"
QT_MOC_LITERAL(2, 20, 5), // "begin"
QT_MOC_LITERAL(3, 26, 2), // "lh"
QT_MOC_LITERAL(4, 29, 2), // "lv"
QT_MOC_LITERAL(5, 32, 4), // "ldlr"
QT_MOC_LITERAL(6, 37, 4), // "ldll"
QT_MOC_LITERAL(7, 42, 4), // "lulr"
QT_MOC_LITERAL(8, 47, 4), // "lull"
QT_MOC_LITERAL(9, 52, 4), // "lvlr"
QT_MOC_LITERAL(10, 57, 4), // "lvll"
QT_MOC_LITERAL(11, 62, 4), // "lhld"
QT_MOC_LITERAL(12, 67, 4), // "lhlu"
QT_MOC_LITERAL(13, 72, 4), // "lhlv"
QT_MOC_LITERAL(14, 77, 3) // "end"

    },
    "PTerminal::Text\0LDC\0begin\0lh\0lv\0ldlr\0"
    "ldll\0lulr\0lull\0lvlr\0lvll\0lhld\0lhlu\0"
    "lhlv\0end"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_PTerminal__Text[] = {

 // content:
       7,       // revision
       0,       // classname
       0,    0, // classinfo
       0,    0, // methods
       0,    0, // properties
       1,   14, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

 // enums: name, flags, count, data
       1, 0x0,   13,   18,

 // enum data: key, value
       2, uint(PTerminal::Text::begin),
       3, uint(PTerminal::Text::lh),
       4, uint(PTerminal::Text::lv),
       5, uint(PTerminal::Text::ldlr),
       6, uint(PTerminal::Text::ldll),
       7, uint(PTerminal::Text::lulr),
       8, uint(PTerminal::Text::lull),
       9, uint(PTerminal::Text::lvlr),
      10, uint(PTerminal::Text::lvll),
      11, uint(PTerminal::Text::lhld),
      12, uint(PTerminal::Text::lhlu),
      13, uint(PTerminal::Text::lhlv),
      14, uint(PTerminal::Text::end),

       0        // eod
};

void PTerminal::Text::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    Q_UNUSED(_o);
    Q_UNUSED(_id);
    Q_UNUSED(_c);
    Q_UNUSED(_a);
}

QT_INIT_METAOBJECT const QMetaObject PTerminal::Text::staticMetaObject = {
    { &QObject::staticMetaObject, qt_meta_stringdata_PTerminal__Text.data,
      qt_meta_data_PTerminal__Text,  qt_static_metacall, nullptr, nullptr}
};


const QMetaObject *PTerminal::Text::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *PTerminal::Text::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_PTerminal__Text.stringdata0))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int PTerminal::Text::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    return _id;
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
