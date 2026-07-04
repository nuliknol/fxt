/****************************************************************************
** Meta object code from reading C++ file 'hmm_client.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.13)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "include/hmm_client.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#include <QtCore/QVector>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'hmm_client.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.13. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_HmmClient_t {
    QByteArrayData data[30];
    char stringdata0[408];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_HmmClient_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_HmmClient_t qt_meta_stringdata_HmmClient = {
    {
QT_MOC_LITERAL(0, 0, 9), // "HmmClient"
QT_MOC_LITERAL(1, 10, 9), // "connected"
QT_MOC_LITERAL(2, 20, 0), // ""
QT_MOC_LITERAL(3, 21, 12), // "disconnected"
QT_MOC_LITERAL(4, 34, 15), // "connectionError"
QT_MOC_LITERAL(5, 50, 5), // "error"
QT_MOC_LITERAL(6, 56, 15), // "trainingStarted"
QT_MOC_LITERAL(7, 72, 16), // "trainingProgress"
QT_MOC_LITERAL(8, 89, 9), // "iteration"
QT_MOC_LITERAL(9, 99, 13), // "maxIterations"
QT_MOC_LITERAL(10, 113, 13), // "logLikelihood"
QT_MOC_LITERAL(11, 127, 11), // "improvement"
QT_MOC_LITERAL(12, 139, 10), // "elapsedSec"
QT_MOC_LITERAL(13, 150, 16), // "trainingComplete"
QT_MOC_LITERAL(14, 167, 7), // "success"
QT_MOC_LITERAL(15, 175, 15), // "totalIterations"
QT_MOC_LITERAL(16, 191, 18), // "finalLogLikelihood"
QT_MOC_LITERAL(17, 210, 9), // "trainTime"
QT_MOC_LITERAL(18, 220, 9), // "modelPath"
QT_MOC_LITERAL(19, 230, 13), // "trainingError"
QT_MOC_LITERAL(20, 244, 19), // "predictionsReceived"
QT_MOC_LITERAL(21, 264, 10), // "startIndex"
QT_MOC_LITERAL(22, 275, 27), // "QVector<HmmPredictedCandle>"
QT_MOC_LITERAL(23, 303, 11), // "predictions"
QT_MOC_LITERAL(24, 315, 14), // "inferenceError"
QT_MOC_LITERAL(25, 330, 11), // "onConnected"
QT_MOC_LITERAL(26, 342, 14), // "onDisconnected"
QT_MOC_LITERAL(27, 357, 11), // "onReadyRead"
QT_MOC_LITERAL(28, 369, 7), // "onError"
QT_MOC_LITERAL(29, 377, 30) // "QLocalSocket::LocalSocketError"

    },
    "HmmClient\0connected\0\0disconnected\0"
    "connectionError\0error\0trainingStarted\0"
    "trainingProgress\0iteration\0maxIterations\0"
    "logLikelihood\0improvement\0elapsedSec\0"
    "trainingComplete\0success\0totalIterations\0"
    "finalLogLikelihood\0trainTime\0modelPath\0"
    "trainingError\0predictionsReceived\0"
    "startIndex\0QVector<HmmPredictedCandle>\0"
    "predictions\0inferenceError\0onConnected\0"
    "onDisconnected\0onReadyRead\0onError\0"
    "QLocalSocket::LocalSocketError"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_HmmClient[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
      13,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       9,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    0,   79,    2, 0x06 /* Public */,
       3,    0,   80,    2, 0x06 /* Public */,
       4,    1,   81,    2, 0x06 /* Public */,
       6,    0,   84,    2, 0x06 /* Public */,
       7,    5,   85,    2, 0x06 /* Public */,
      13,    5,   96,    2, 0x06 /* Public */,
      19,    1,  107,    2, 0x06 /* Public */,
      20,    2,  110,    2, 0x06 /* Public */,
      24,    1,  115,    2, 0x06 /* Public */,

 // slots: name, argc, parameters, tag, flags
      25,    0,  118,    2, 0x08 /* Private */,
      26,    0,  119,    2, 0x08 /* Private */,
      27,    0,  120,    2, 0x08 /* Private */,
      28,    1,  121,    2, 0x08 /* Private */,

 // signals: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString,    5,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int, QMetaType::Int, QMetaType::Double, QMetaType::Double, QMetaType::Double,    8,    9,   10,   11,   12,
    QMetaType::Void, QMetaType::Bool, QMetaType::Int, QMetaType::Double, QMetaType::Double, QMetaType::QString,   14,   15,   16,   17,   18,
    QMetaType::Void, QMetaType::QString,    5,
    QMetaType::Void, QMetaType::Int, 0x80000000 | 22,   21,   23,
    QMetaType::Void, QMetaType::QString,    5,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, 0x80000000 | 29,    5,

       0        // eod
};

void HmmClient::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<HmmClient *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->connected(); break;
        case 1: _t->disconnected(); break;
        case 2: _t->connectionError((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 3: _t->trainingStarted(); break;
        case 4: _t->trainingProgress((*reinterpret_cast< int(*)>(_a[1])),(*reinterpret_cast< int(*)>(_a[2])),(*reinterpret_cast< double(*)>(_a[3])),(*reinterpret_cast< double(*)>(_a[4])),(*reinterpret_cast< double(*)>(_a[5]))); break;
        case 5: _t->trainingComplete((*reinterpret_cast< bool(*)>(_a[1])),(*reinterpret_cast< int(*)>(_a[2])),(*reinterpret_cast< double(*)>(_a[3])),(*reinterpret_cast< double(*)>(_a[4])),(*reinterpret_cast< const QString(*)>(_a[5]))); break;
        case 6: _t->trainingError((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 7: _t->predictionsReceived((*reinterpret_cast< int(*)>(_a[1])),(*reinterpret_cast< const QVector<HmmPredictedCandle>(*)>(_a[2]))); break;
        case 8: _t->inferenceError((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 9: _t->onConnected(); break;
        case 10: _t->onDisconnected(); break;
        case 11: _t->onReadyRead(); break;
        case 12: _t->onError((*reinterpret_cast< QLocalSocket::LocalSocketError(*)>(_a[1]))); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (HmmClient::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&HmmClient::connected)) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (HmmClient::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&HmmClient::disconnected)) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (HmmClient::*)(const QString & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&HmmClient::connectionError)) {
                *result = 2;
                return;
            }
        }
        {
            using _t = void (HmmClient::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&HmmClient::trainingStarted)) {
                *result = 3;
                return;
            }
        }
        {
            using _t = void (HmmClient::*)(int , int , double , double , double );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&HmmClient::trainingProgress)) {
                *result = 4;
                return;
            }
        }
        {
            using _t = void (HmmClient::*)(bool , int , double , double , const QString & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&HmmClient::trainingComplete)) {
                *result = 5;
                return;
            }
        }
        {
            using _t = void (HmmClient::*)(const QString & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&HmmClient::trainingError)) {
                *result = 6;
                return;
            }
        }
        {
            using _t = void (HmmClient::*)(int , const QVector<HmmPredictedCandle> & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&HmmClient::predictionsReceived)) {
                *result = 7;
                return;
            }
        }
        {
            using _t = void (HmmClient::*)(const QString & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&HmmClient::inferenceError)) {
                *result = 8;
                return;
            }
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject HmmClient::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_meta_stringdata_HmmClient.data,
    qt_meta_data_HmmClient,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *HmmClient::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *HmmClient::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_HmmClient.stringdata0))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int HmmClient::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
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
void HmmClient::connected()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void HmmClient::disconnected()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}

// SIGNAL 2
void HmmClient::connectionError(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 2, _a);
}

// SIGNAL 3
void HmmClient::trainingStarted()
{
    QMetaObject::activate(this, &staticMetaObject, 3, nullptr);
}

// SIGNAL 4
void HmmClient::trainingProgress(int _t1, int _t2, double _t3, double _t4, double _t5)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t3))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t4))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t5))) };
    QMetaObject::activate(this, &staticMetaObject, 4, _a);
}

// SIGNAL 5
void HmmClient::trainingComplete(bool _t1, int _t2, double _t3, double _t4, const QString & _t5)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t3))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t4))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t5))) };
    QMetaObject::activate(this, &staticMetaObject, 5, _a);
}

// SIGNAL 6
void HmmClient::trainingError(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 6, _a);
}

// SIGNAL 7
void HmmClient::predictionsReceived(int _t1, const QVector<HmmPredictedCandle> & _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 7, _a);
}

// SIGNAL 8
void HmmClient::inferenceError(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 8, _a);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
