/****************************************************************************
** Meta object code from reading C++ file 'SimulationControlWidget.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.13)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "SimulationControlWidget.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'SimulationControlWidget.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.13. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_SimulationControlWidget_t {
    QByteArrayData data[10];
    char stringdata0[157];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_SimulationControlWidget_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_SimulationControlWidget_t qt_meta_stringdata_SimulationControlWidget = {
    {
QT_MOC_LITERAL(0, 0, 23), // "SimulationControlWidget"
QT_MOC_LITERAL(1, 24, 15), // "start_requested"
QT_MOC_LITERAL(2, 40, 0), // ""
QT_MOC_LITERAL(3, 41, 15), // "pause_requested"
QT_MOC_LITERAL(4, 57, 15), // "reset_requested"
QT_MOC_LITERAL(5, 73, 14), // "step_requested"
QT_MOC_LITERAL(6, 88, 17), // "run_100_requested"
QT_MOC_LITERAL(7, 106, 21), // "copy_params_requested"
QT_MOC_LITERAL(8, 128, 16), // "interval_changed"
QT_MOC_LITERAL(9, 145, 11) // "interval_ms"

    },
    "SimulationControlWidget\0start_requested\0"
    "\0pause_requested\0reset_requested\0"
    "step_requested\0run_100_requested\0"
    "copy_params_requested\0interval_changed\0"
    "interval_ms"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_SimulationControlWidget[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       7,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       7,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    0,   49,    2, 0x06 /* Public */,
       3,    0,   50,    2, 0x06 /* Public */,
       4,    0,   51,    2, 0x06 /* Public */,
       5,    0,   52,    2, 0x06 /* Public */,
       6,    0,   53,    2, 0x06 /* Public */,
       7,    0,   54,    2, 0x06 /* Public */,
       8,    1,   55,    2, 0x06 /* Public */,

 // signals: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int,    9,

       0        // eod
};

void SimulationControlWidget::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<SimulationControlWidget *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->start_requested(); break;
        case 1: _t->pause_requested(); break;
        case 2: _t->reset_requested(); break;
        case 3: _t->step_requested(); break;
        case 4: _t->run_100_requested(); break;
        case 5: _t->copy_params_requested(); break;
        case 6: _t->interval_changed((*reinterpret_cast< int(*)>(_a[1]))); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (SimulationControlWidget::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&SimulationControlWidget::start_requested)) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (SimulationControlWidget::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&SimulationControlWidget::pause_requested)) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (SimulationControlWidget::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&SimulationControlWidget::reset_requested)) {
                *result = 2;
                return;
            }
        }
        {
            using _t = void (SimulationControlWidget::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&SimulationControlWidget::step_requested)) {
                *result = 3;
                return;
            }
        }
        {
            using _t = void (SimulationControlWidget::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&SimulationControlWidget::run_100_requested)) {
                *result = 4;
                return;
            }
        }
        {
            using _t = void (SimulationControlWidget::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&SimulationControlWidget::copy_params_requested)) {
                *result = 5;
                return;
            }
        }
        {
            using _t = void (SimulationControlWidget::*)(int );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&SimulationControlWidget::interval_changed)) {
                *result = 6;
                return;
            }
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject SimulationControlWidget::staticMetaObject = { {
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_meta_stringdata_SimulationControlWidget.data,
    qt_meta_data_SimulationControlWidget,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *SimulationControlWidget::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *SimulationControlWidget::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_SimulationControlWidget.stringdata0))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int SimulationControlWidget::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 7)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 7;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 7)
            *reinterpret_cast<int*>(_a[0]) = -1;
        _id -= 7;
    }
    return _id;
}

// SIGNAL 0
void SimulationControlWidget::start_requested()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void SimulationControlWidget::pause_requested()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}

// SIGNAL 2
void SimulationControlWidget::reset_requested()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}

// SIGNAL 3
void SimulationControlWidget::step_requested()
{
    QMetaObject::activate(this, &staticMetaObject, 3, nullptr);
}

// SIGNAL 4
void SimulationControlWidget::run_100_requested()
{
    QMetaObject::activate(this, &staticMetaObject, 4, nullptr);
}

// SIGNAL 5
void SimulationControlWidget::copy_params_requested()
{
    QMetaObject::activate(this, &staticMetaObject, 5, nullptr);
}

// SIGNAL 6
void SimulationControlWidget::interval_changed(int _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 6, _a);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
