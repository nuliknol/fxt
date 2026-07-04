/****************************************************************************
** Meta object code from reading C++ file 'mainwindow.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.13)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "include/mainwindow.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#include <QtCore/QVector>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'mainwindow.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.13. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_MainWindow_t {
    QByteArrayData data[52];
    char stringdata0[733];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_MainWindow_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_MainWindow_t qt_meta_stringdata_MainWindow = {
    {
QT_MOC_LITERAL(0, 0, 10), // "MainWindow"
QT_MOC_LITERAL(1, 11, 15), // "onCandleClicked"
QT_MOC_LITERAL(2, 27, 0), // ""
QT_MOC_LITERAL(3, 28, 6), // "Candle"
QT_MOC_LITERAL(4, 35, 1), // "c"
QT_MOC_LITERAL(5, 37, 10), // "layerIndex"
QT_MOC_LITERAL(6, 48, 11), // "candleIndex"
QT_MOC_LITERAL(7, 60, 14), // "onViewScrolled"
QT_MOC_LITERAL(8, 75, 17), // "firstVisibleIndex"
QT_MOC_LITERAL(9, 93, 15), // "onCandleHovered"
QT_MOC_LITERAL(10, 109, 10), // "onOpenFile"
QT_MOC_LITERAL(11, 120, 18), // "onLayerItemClicked"
QT_MOC_LITERAL(12, 139, 16), // "QListWidgetItem*"
QT_MOC_LITERAL(13, 156, 4), // "item"
QT_MOC_LITERAL(14, 161, 13), // "onRemoveLayer"
QT_MOC_LITERAL(15, 175, 16), // "onConfigureLayer"
QT_MOC_LITERAL(16, 192, 13), // "onMoveLayerUp"
QT_MOC_LITERAL(17, 206, 15), // "onMoveLayerDown"
QT_MOC_LITERAL(18, 222, 15), // "onSetStartPoint"
QT_MOC_LITERAL(19, 238, 13), // "onSetEndPoint"
QT_MOC_LITERAL(20, 252, 15), // "onStartTraining"
QT_MOC_LITERAL(21, 268, 14), // "onStopTraining"
QT_MOC_LITERAL(22, 283, 18), // "onHmmPointSelected"
QT_MOC_LITERAL(23, 302, 7), // "int64_t"
QT_MOC_LITERAL(24, 310, 9), // "timestamp"
QT_MOC_LITERAL(25, 320, 14), // "onHmmConnected"
QT_MOC_LITERAL(26, 335, 17), // "onHmmDisconnected"
QT_MOC_LITERAL(27, 353, 20), // "onHmmConnectionError"
QT_MOC_LITERAL(28, 374, 5), // "error"
QT_MOC_LITERAL(29, 380, 18), // "onModelTypeChanged"
QT_MOC_LITERAL(30, 399, 5), // "index"
QT_MOC_LITERAL(31, 405, 20), // "onHmmTrainingStarted"
QT_MOC_LITERAL(32, 426, 21), // "onHmmTrainingProgress"
QT_MOC_LITERAL(33, 448, 9), // "iteration"
QT_MOC_LITERAL(34, 458, 13), // "maxIterations"
QT_MOC_LITERAL(35, 472, 13), // "logLikelihood"
QT_MOC_LITERAL(36, 486, 11), // "improvement"
QT_MOC_LITERAL(37, 498, 10), // "elapsedSec"
QT_MOC_LITERAL(38, 509, 21), // "onHmmTrainingComplete"
QT_MOC_LITERAL(39, 531, 7), // "success"
QT_MOC_LITERAL(40, 539, 15), // "totalIterations"
QT_MOC_LITERAL(41, 555, 18), // "finalLogLikelihood"
QT_MOC_LITERAL(42, 574, 9), // "trainTime"
QT_MOC_LITERAL(43, 584, 9), // "modelPath"
QT_MOC_LITERAL(44, 594, 18), // "onHmmTrainingError"
QT_MOC_LITERAL(45, 613, 14), // "onLoadHmmModel"
QT_MOC_LITERAL(46, 628, 14), // "onSaveHmmModel"
QT_MOC_LITERAL(47, 643, 21), // "onPredictionsReceived"
QT_MOC_LITERAL(48, 665, 10), // "startIndex"
QT_MOC_LITERAL(49, 676, 27), // "QVector<HmmPredictedCandle>"
QT_MOC_LITERAL(50, 704, 11), // "predictions"
QT_MOC_LITERAL(51, 716, 16) // "onInferenceError"

    },
    "MainWindow\0onCandleClicked\0\0Candle\0c\0"
    "layerIndex\0candleIndex\0onViewScrolled\0"
    "firstVisibleIndex\0onCandleHovered\0"
    "onOpenFile\0onLayerItemClicked\0"
    "QListWidgetItem*\0item\0onRemoveLayer\0"
    "onConfigureLayer\0onMoveLayerUp\0"
    "onMoveLayerDown\0onSetStartPoint\0"
    "onSetEndPoint\0onStartTraining\0"
    "onStopTraining\0onHmmPointSelected\0"
    "int64_t\0timestamp\0onHmmConnected\0"
    "onHmmDisconnected\0onHmmConnectionError\0"
    "error\0onModelTypeChanged\0index\0"
    "onHmmTrainingStarted\0onHmmTrainingProgress\0"
    "iteration\0maxIterations\0logLikelihood\0"
    "improvement\0elapsedSec\0onHmmTrainingComplete\0"
    "success\0totalIterations\0finalLogLikelihood\0"
    "trainTime\0modelPath\0onHmmTrainingError\0"
    "onLoadHmmModel\0onSaveHmmModel\0"
    "onPredictionsReceived\0startIndex\0"
    "QVector<HmmPredictedCandle>\0predictions\0"
    "onInferenceError"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_MainWindow[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
      26,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

 // slots: name, argc, parameters, tag, flags
       1,    3,  144,    2, 0x08 /* Private */,
       7,    1,  151,    2, 0x08 /* Private */,
       9,    3,  154,    2, 0x08 /* Private */,
      10,    0,  161,    2, 0x08 /* Private */,
      11,    1,  162,    2, 0x08 /* Private */,
      14,    0,  165,    2, 0x08 /* Private */,
      15,    0,  166,    2, 0x08 /* Private */,
      16,    0,  167,    2, 0x08 /* Private */,
      17,    0,  168,    2, 0x08 /* Private */,
      18,    0,  169,    2, 0x08 /* Private */,
      19,    0,  170,    2, 0x08 /* Private */,
      20,    0,  171,    2, 0x08 /* Private */,
      21,    0,  172,    2, 0x08 /* Private */,
      22,    2,  173,    2, 0x08 /* Private */,
      25,    0,  178,    2, 0x08 /* Private */,
      26,    0,  179,    2, 0x08 /* Private */,
      27,    1,  180,    2, 0x08 /* Private */,
      29,    1,  183,    2, 0x08 /* Private */,
      31,    0,  186,    2, 0x08 /* Private */,
      32,    5,  187,    2, 0x08 /* Private */,
      38,    5,  198,    2, 0x08 /* Private */,
      44,    1,  209,    2, 0x08 /* Private */,
      45,    0,  212,    2, 0x08 /* Private */,
      46,    0,  213,    2, 0x08 /* Private */,
      47,    2,  214,    2, 0x08 /* Private */,
      51,    1,  219,    2, 0x08 /* Private */,

 // slots: parameters
    QMetaType::Void, 0x80000000 | 3, QMetaType::Int, QMetaType::Int,    4,    5,    6,
    QMetaType::Void, QMetaType::Int,    8,
    QMetaType::Void, 0x80000000 | 3, QMetaType::Int, QMetaType::Int,    4,    5,    6,
    QMetaType::Void,
    QMetaType::Void, 0x80000000 | 12,   13,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int, 0x80000000 | 23,    6,   24,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString,   28,
    QMetaType::Void, QMetaType::Int,   30,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int, QMetaType::Int, QMetaType::Double, QMetaType::Double, QMetaType::Double,   33,   34,   35,   36,   37,
    QMetaType::Void, QMetaType::Bool, QMetaType::Int, QMetaType::Double, QMetaType::Double, QMetaType::QString,   39,   40,   41,   42,   43,
    QMetaType::Void, QMetaType::QString,   28,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int, 0x80000000 | 49,   48,   50,
    QMetaType::Void, QMetaType::QString,   28,

       0        // eod
};

void MainWindow::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<MainWindow *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->onCandleClicked((*reinterpret_cast< const Candle(*)>(_a[1])),(*reinterpret_cast< int(*)>(_a[2])),(*reinterpret_cast< int(*)>(_a[3]))); break;
        case 1: _t->onViewScrolled((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 2: _t->onCandleHovered((*reinterpret_cast< const Candle(*)>(_a[1])),(*reinterpret_cast< int(*)>(_a[2])),(*reinterpret_cast< int(*)>(_a[3]))); break;
        case 3: _t->onOpenFile(); break;
        case 4: _t->onLayerItemClicked((*reinterpret_cast< QListWidgetItem*(*)>(_a[1]))); break;
        case 5: _t->onRemoveLayer(); break;
        case 6: _t->onConfigureLayer(); break;
        case 7: _t->onMoveLayerUp(); break;
        case 8: _t->onMoveLayerDown(); break;
        case 9: _t->onSetStartPoint(); break;
        case 10: _t->onSetEndPoint(); break;
        case 11: _t->onStartTraining(); break;
        case 12: _t->onStopTraining(); break;
        case 13: _t->onHmmPointSelected((*reinterpret_cast< int(*)>(_a[1])),(*reinterpret_cast< int64_t(*)>(_a[2]))); break;
        case 14: _t->onHmmConnected(); break;
        case 15: _t->onHmmDisconnected(); break;
        case 16: _t->onHmmConnectionError((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 17: _t->onModelTypeChanged((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 18: _t->onHmmTrainingStarted(); break;
        case 19: _t->onHmmTrainingProgress((*reinterpret_cast< int(*)>(_a[1])),(*reinterpret_cast< int(*)>(_a[2])),(*reinterpret_cast< double(*)>(_a[3])),(*reinterpret_cast< double(*)>(_a[4])),(*reinterpret_cast< double(*)>(_a[5]))); break;
        case 20: _t->onHmmTrainingComplete((*reinterpret_cast< bool(*)>(_a[1])),(*reinterpret_cast< int(*)>(_a[2])),(*reinterpret_cast< double(*)>(_a[3])),(*reinterpret_cast< double(*)>(_a[4])),(*reinterpret_cast< const QString(*)>(_a[5]))); break;
        case 21: _t->onHmmTrainingError((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 22: _t->onLoadHmmModel(); break;
        case 23: _t->onSaveHmmModel(); break;
        case 24: _t->onPredictionsReceived((*reinterpret_cast< int(*)>(_a[1])),(*reinterpret_cast< const QVector<HmmPredictedCandle>(*)>(_a[2]))); break;
        case 25: _t->onInferenceError((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        default: ;
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject MainWindow::staticMetaObject = { {
    QMetaObject::SuperData::link<QMainWindow::staticMetaObject>(),
    qt_meta_stringdata_MainWindow.data,
    qt_meta_data_MainWindow,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *MainWindow::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *MainWindow::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_MainWindow.stringdata0))
        return static_cast<void*>(this);
    return QMainWindow::qt_metacast(_clname);
}

int MainWindow::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QMainWindow::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 26)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 26;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 26)
            *reinterpret_cast<int*>(_a[0]) = -1;
        _id -= 26;
    }
    return _id;
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
