/****************************************************************************
** Meta object code from reading C++ file 'main_window.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.13)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../../include/main_window.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'main_window.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.13. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_main_window_t {
    QByteArrayData data[46];
    char stringdata0[975];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_main_window_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_main_window_t qt_meta_stringdata_main_window = {
    {
QT_MOC_LITERAL(0, 0, 11), // "main_window"
QT_MOC_LITERAL(1, 12, 14), // "on_ping_server"
QT_MOC_LITERAL(2, 27, 0), // ""
QT_MOC_LITERAL(3, 28, 17), // "on_import_dataset"
QT_MOC_LITERAL(4, 46, 24), // "on_import_sample_dataset"
QT_MOC_LITERAL(5, 71, 19), // "on_refresh_datasets"
QT_MOC_LITERAL(6, 91, 22), // "on_load_dataset_detail"
QT_MOC_LITERAL(7, 114, 15), // "on_load_candles"
QT_MOC_LITERAL(8, 130, 16), // "on_load_features"
QT_MOC_LITERAL(9, 147, 24), // "on_submit_clustering_job"
QT_MOC_LITERAL(10, 172, 18), // "on_submit_word_job"
QT_MOC_LITERAL(11, 191, 20), // "on_submit_phrase_job"
QT_MOC_LITERAL(12, 212, 18), // "on_submit_wave_job"
QT_MOC_LITERAL(13, 231, 15), // "on_refresh_jobs"
QT_MOC_LITERAL(14, 247, 22), // "on_delete_selected_job"
QT_MOC_LITERAL(15, 270, 31), // "on_clustering_algorithm_changed"
QT_MOC_LITERAL(16, 302, 16), // "on_load_clusters"
QT_MOC_LITERAL(17, 319, 23), // "on_load_cluster_members"
QT_MOC_LITERAL(18, 343, 22), // "on_cluster_row_changed"
QT_MOC_LITERAL(19, 366, 29), // "on_cluster_member_row_changed"
QT_MOC_LITERAL(20, 396, 13), // "on_load_words"
QT_MOC_LITERAL(21, 410, 15), // "on_load_phrases"
QT_MOC_LITERAL(22, 426, 13), // "on_load_waves"
QT_MOC_LITERAL(23, 440, 24), // "on_load_waves_lowest_err"
QT_MOC_LITERAL(24, 465, 23), // "on_load_predictor_waves"
QT_MOC_LITERAL(25, 489, 24), // "on_predictor_row_changed"
QT_MOC_LITERAL(26, 514, 40), // "on_predictor_horizon_item_dou..."
QT_MOC_LITERAL(27, 555, 17), // "QTableWidgetItem*"
QT_MOC_LITERAL(28, 573, 4), // "item"
QT_MOC_LITERAL(29, 578, 36), // "on_show_predictor_base_probab..."
QT_MOC_LITERAL(30, 615, 15), // "on_refresh_live"
QT_MOC_LITERAL(31, 631, 17), // "on_recompute_live"
QT_MOC_LITERAL(32, 649, 13), // "on_reset_live"
QT_MOC_LITERAL(33, 663, 22), // "on_live_stream_changed"
QT_MOC_LITERAL(34, 686, 24), // "on_live_wave_row_changed"
QT_MOC_LITERAL(35, 711, 28), // "on_configure_live_properties"
QT_MOC_LITERAL(36, 740, 22), // "on_wave_filter_changed"
QT_MOC_LITERAL(37, 763, 19), // "on_wave_row_changed"
QT_MOC_LITERAL(38, 783, 36), // "on_wave_property_item_double_..."
QT_MOC_LITERAL(39, 820, 28), // "on_configure_wave_properties"
QT_MOC_LITERAL(40, 849, 32), // "on_show_wave_penalty_calculation"
QT_MOC_LITERAL(41, 882, 20), // "on_view_mode_changed"
QT_MOC_LITERAL(42, 903, 22), // "on_group_chart_clicked"
QT_MOC_LITERAL(43, 926, 10), // "member_row"
QT_MOC_LITERAL(44, 937, 18), // "on_group_prev_page"
QT_MOC_LITERAL(45, 956, 18) // "on_group_next_page"

    },
    "main_window\0on_ping_server\0\0"
    "on_import_dataset\0on_import_sample_dataset\0"
    "on_refresh_datasets\0on_load_dataset_detail\0"
    "on_load_candles\0on_load_features\0"
    "on_submit_clustering_job\0on_submit_word_job\0"
    "on_submit_phrase_job\0on_submit_wave_job\0"
    "on_refresh_jobs\0on_delete_selected_job\0"
    "on_clustering_algorithm_changed\0"
    "on_load_clusters\0on_load_cluster_members\0"
    "on_cluster_row_changed\0"
    "on_cluster_member_row_changed\0"
    "on_load_words\0on_load_phrases\0"
    "on_load_waves\0on_load_waves_lowest_err\0"
    "on_load_predictor_waves\0"
    "on_predictor_row_changed\0"
    "on_predictor_horizon_item_double_clicked\0"
    "QTableWidgetItem*\0item\0"
    "on_show_predictor_base_probabilities\0"
    "on_refresh_live\0on_recompute_live\0"
    "on_reset_live\0on_live_stream_changed\0"
    "on_live_wave_row_changed\0"
    "on_configure_live_properties\0"
    "on_wave_filter_changed\0on_wave_row_changed\0"
    "on_wave_property_item_double_clicked\0"
    "on_configure_wave_properties\0"
    "on_show_wave_penalty_calculation\0"
    "on_view_mode_changed\0on_group_chart_clicked\0"
    "member_row\0on_group_prev_page\0"
    "on_group_next_page"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_main_window[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
      41,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

 // slots: name, argc, parameters, tag, flags
       1,    0,  219,    2, 0x08 /* Private */,
       3,    0,  220,    2, 0x08 /* Private */,
       4,    0,  221,    2, 0x08 /* Private */,
       5,    0,  222,    2, 0x08 /* Private */,
       6,    0,  223,    2, 0x08 /* Private */,
       7,    0,  224,    2, 0x08 /* Private */,
       8,    0,  225,    2, 0x08 /* Private */,
       9,    0,  226,    2, 0x08 /* Private */,
      10,    0,  227,    2, 0x08 /* Private */,
      11,    0,  228,    2, 0x08 /* Private */,
      12,    0,  229,    2, 0x08 /* Private */,
      13,    0,  230,    2, 0x08 /* Private */,
      14,    0,  231,    2, 0x08 /* Private */,
      15,    0,  232,    2, 0x08 /* Private */,
      16,    0,  233,    2, 0x08 /* Private */,
      17,    0,  234,    2, 0x08 /* Private */,
      18,    0,  235,    2, 0x08 /* Private */,
      19,    0,  236,    2, 0x08 /* Private */,
      20,    0,  237,    2, 0x08 /* Private */,
      21,    0,  238,    2, 0x08 /* Private */,
      22,    0,  239,    2, 0x08 /* Private */,
      23,    0,  240,    2, 0x08 /* Private */,
      24,    0,  241,    2, 0x08 /* Private */,
      25,    0,  242,    2, 0x08 /* Private */,
      26,    1,  243,    2, 0x08 /* Private */,
      29,    0,  246,    2, 0x08 /* Private */,
      30,    0,  247,    2, 0x08 /* Private */,
      31,    0,  248,    2, 0x08 /* Private */,
      32,    0,  249,    2, 0x08 /* Private */,
      33,    0,  250,    2, 0x08 /* Private */,
      34,    0,  251,    2, 0x08 /* Private */,
      35,    0,  252,    2, 0x08 /* Private */,
      36,    0,  253,    2, 0x08 /* Private */,
      37,    0,  254,    2, 0x08 /* Private */,
      38,    1,  255,    2, 0x08 /* Private */,
      39,    0,  258,    2, 0x08 /* Private */,
      40,    0,  259,    2, 0x08 /* Private */,
      41,    0,  260,    2, 0x08 /* Private */,
      42,    1,  261,    2, 0x08 /* Private */,
      44,    0,  264,    2, 0x08 /* Private */,
      45,    0,  265,    2, 0x08 /* Private */,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, 0x80000000 | 27,   28,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, 0x80000000 | 27,   28,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int,   43,
    QMetaType::Void,
    QMetaType::Void,

       0        // eod
};

void main_window::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<main_window *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->on_ping_server(); break;
        case 1: _t->on_import_dataset(); break;
        case 2: _t->on_import_sample_dataset(); break;
        case 3: _t->on_refresh_datasets(); break;
        case 4: _t->on_load_dataset_detail(); break;
        case 5: _t->on_load_candles(); break;
        case 6: _t->on_load_features(); break;
        case 7: _t->on_submit_clustering_job(); break;
        case 8: _t->on_submit_word_job(); break;
        case 9: _t->on_submit_phrase_job(); break;
        case 10: _t->on_submit_wave_job(); break;
        case 11: _t->on_refresh_jobs(); break;
        case 12: _t->on_delete_selected_job(); break;
        case 13: _t->on_clustering_algorithm_changed(); break;
        case 14: _t->on_load_clusters(); break;
        case 15: _t->on_load_cluster_members(); break;
        case 16: _t->on_cluster_row_changed(); break;
        case 17: _t->on_cluster_member_row_changed(); break;
        case 18: _t->on_load_words(); break;
        case 19: _t->on_load_phrases(); break;
        case 20: _t->on_load_waves(); break;
        case 21: _t->on_load_waves_lowest_err(); break;
        case 22: _t->on_load_predictor_waves(); break;
        case 23: _t->on_predictor_row_changed(); break;
        case 24: _t->on_predictor_horizon_item_double_clicked((*reinterpret_cast< QTableWidgetItem*(*)>(_a[1]))); break;
        case 25: _t->on_show_predictor_base_probabilities(); break;
        case 26: _t->on_refresh_live(); break;
        case 27: _t->on_recompute_live(); break;
        case 28: _t->on_reset_live(); break;
        case 29: _t->on_live_stream_changed(); break;
        case 30: _t->on_live_wave_row_changed(); break;
        case 31: _t->on_configure_live_properties(); break;
        case 32: _t->on_wave_filter_changed(); break;
        case 33: _t->on_wave_row_changed(); break;
        case 34: _t->on_wave_property_item_double_clicked((*reinterpret_cast< QTableWidgetItem*(*)>(_a[1]))); break;
        case 35: _t->on_configure_wave_properties(); break;
        case 36: _t->on_show_wave_penalty_calculation(); break;
        case 37: _t->on_view_mode_changed(); break;
        case 38: _t->on_group_chart_clicked((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 39: _t->on_group_prev_page(); break;
        case 40: _t->on_group_next_page(); break;
        default: ;
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject main_window::staticMetaObject = { {
    QMetaObject::SuperData::link<QMainWindow::staticMetaObject>(),
    qt_meta_stringdata_main_window.data,
    qt_meta_data_main_window,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *main_window::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *main_window::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_main_window.stringdata0))
        return static_cast<void*>(this);
    return QMainWindow::qt_metacast(_clname);
}

int main_window::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QMainWindow::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 41)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 41;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 41)
            *reinterpret_cast<int*>(_a[0]) = -1;
        _id -= 41;
    }
    return _id;
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
