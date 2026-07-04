/****************************************************************************
** Meta object code from reading C++ file 'mf_view.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.13)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "include/mf_view.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'mf_view.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.13. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_MfView_t {
    QByteArrayData data[15];
    char stringdata0[151];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_MfView_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_MfView_t qt_meta_stringdata_MfView = {
    {
QT_MOC_LITERAL(0, 0, 6), // "MfView"
QT_MOC_LITERAL(1, 7, 13), // "candleClicked"
QT_MOC_LITERAL(2, 21, 0), // ""
QT_MOC_LITERAL(3, 22, 6), // "Candle"
QT_MOC_LITERAL(4, 29, 6), // "candle"
QT_MOC_LITERAL(5, 36, 10), // "layerIndex"
QT_MOC_LITERAL(6, 47, 11), // "candleIndex"
QT_MOC_LITERAL(7, 59, 12), // "viewScrolled"
QT_MOC_LITERAL(8, 72, 17), // "firstVisibleIndex"
QT_MOC_LITERAL(9, 90, 13), // "candleHovered"
QT_MOC_LITERAL(10, 104, 13), // "pointSelected"
QT_MOC_LITERAL(11, 118, 7), // "int64_t"
QT_MOC_LITERAL(12, 126, 9), // "timestamp"
QT_MOC_LITERAL(13, 136, 8), // "scrollTo"
QT_MOC_LITERAL(14, 145, 5) // "index"

    },
    "MfView\0candleClicked\0\0Candle\0candle\0"
    "layerIndex\0candleIndex\0viewScrolled\0"
    "firstVisibleIndex\0candleHovered\0"
    "pointSelected\0int64_t\0timestamp\0"
    "scrollTo\0index"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_MfView[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       5,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       4,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    3,   39,    2, 0x06 /* Public */,
       7,    1,   46,    2, 0x06 /* Public */,
       9,    3,   49,    2, 0x06 /* Public */,
      10,    2,   56,    2, 0x06 /* Public */,

 // slots: name, argc, parameters, tag, flags
      13,    1,   61,    2, 0x0a /* Public */,

 // signals: parameters
    QMetaType::Void, 0x80000000 | 3, QMetaType::Int, QMetaType::Int,    4,    5,    6,
    QMetaType::Void, QMetaType::Int,    8,
    QMetaType::Void, 0x80000000 | 3, QMetaType::Int, QMetaType::Int,    4,    5,    6,
    QMetaType::Void, QMetaType::Int, 0x80000000 | 11,    6,   12,

 // slots: parameters
    QMetaType::Void, QMetaType::Int,   14,

       0        // eod
};

void MfView::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<MfView *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->candleClicked((*reinterpret_cast< const Candle(*)>(_a[1])),(*reinterpret_cast< int(*)>(_a[2])),(*reinterpret_cast< int(*)>(_a[3]))); break;
        case 1: _t->viewScrolled((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 2: _t->candleHovered((*reinterpret_cast< const Candle(*)>(_a[1])),(*reinterpret_cast< int(*)>(_a[2])),(*reinterpret_cast< int(*)>(_a[3]))); break;
        case 3: _t->pointSelected((*reinterpret_cast< int(*)>(_a[1])),(*reinterpret_cast< int64_t(*)>(_a[2]))); break;
        case 4: _t->scrollTo((*reinterpret_cast< int(*)>(_a[1]))); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (MfView::*)(const Candle & , int , int );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&MfView::candleClicked)) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (MfView::*)(int );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&MfView::viewScrolled)) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (MfView::*)(const Candle & , int , int );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&MfView::candleHovered)) {
                *result = 2;
                return;
            }
        }
        {
            using _t = void (MfView::*)(int , int64_t );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&MfView::pointSelected)) {
                *result = 3;
                return;
            }
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject MfView::staticMetaObject = { {
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_meta_stringdata_MfView.data,
    qt_meta_data_MfView,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *MfView::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *MfView::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_MfView.stringdata0))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int MfView::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 5)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 5;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 5)
            *reinterpret_cast<int*>(_a[0]) = -1;
        _id -= 5;
    }
    return _id;
}

// SIGNAL 0
void MfView::candleClicked(const Candle & _t1, int _t2, int _t3)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t3))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void MfView::viewScrolled(int _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}

// SIGNAL 2
void MfView::candleHovered(const Candle & _t1, int _t2, int _t3)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t3))) };
    QMetaObject::activate(this, &staticMetaObject, 2, _a);
}

// SIGNAL 3
void MfView::pointSelected(int _t1, int64_t _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 3, _a);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
