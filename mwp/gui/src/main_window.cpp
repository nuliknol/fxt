#include "main_window.h"
#include "cluster_chart_widget.h"
#include "micro_harvest_widget.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QColor>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSettings>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSet>
#include <QSpinBox>
#include <QStandardPaths>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyle>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextEdit>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include <cmath>

#ifndef GUI_BUNDLED_SAMPLE_CSV
#define GUI_BUNDLED_SAMPLE_CSV ""
#endif

namespace {
enum {
	clustering_ui_threshold_graph = 101,
	clustering_ui_sparse_graph = 102,
	clustering_ui_kmedoids = 103,
	clustering_ui_dbscan = 104
};

enum {
	job_type_graph_clustering = 1,
	job_type_word_mining = 2,
	job_type_phrase_mining = 3,
	job_type_kmedoids = 4,
	job_type_dbscan = 5,
	job_type_wave_inference = 6,
	job_type_micro_harvest_replay = 8
};

enum {
	graph_cluster_mode_legacy_connected_components = 0,
	graph_cluster_mode_connected_components = 1,
	graph_cluster_mode_label_propagation = 2,
	graph_cluster_mode_louvain = 3
};

static bool property_name_in_list(const QString &key, const char *const *items, int count)
{
	for (int i = 0; i < count; ++i) {
		if (key == QString::fromUtf8(items[i])) {
			return true;
		}
	}
	return false;
}

static bool is_relative_wave_model_name(const QString &name)
{
	return name == "relative_waves" ||
		name == "relative_waves_2_stage" ||
		name == "relative_waves_seeded" ||
		name == "relative_waves_iterative" ||
		name == "layered_waves";
}

static void populate_wave_composition_table(QTableWidget *table, const QJsonArray &child_strings)
{
	if (table == nullptr) {
		return;
	}
	table->setRowCount(child_strings.size());
	for (int i = 0; i < child_strings.size(); ++i) {
		QJsonObject child = child_strings.at(i).toObject();

		table->setItem(i, 0, new QTableWidgetItem(QString::number(child.value("child_index").toInt())));
		table->setItem(i, 1, new QTableWidgetItem(QString::number(child.value("start_candle").toInt())));
		table->setItem(i, 2, new QTableWidgetItem(QString::number(child.value("end_candle").toInt())));
		table->setItem(i, 3, new QTableWidgetItem(child.value("primitive_type").toString()));
		table->setItem(i, 4, new QTableWidgetItem(child.value("primitive_direction").toString()));
		table->setItem(i, 5, new QTableWidgetItem(QString::number(child.value("duration_candles").toInt())));
		table->setItem(i, 6, new QTableWidgetItem(QString::number(child.value("net_move_raw").toInt())));
		table->setItem(i, 7, new QTableWidgetItem(QString::number(child.value("primitive_consecutive_ratio").toInt())));
		table->setItem(i, 8, new QTableWidgetItem(QString::number(child.value("primitive_alternating_ratio").toInt())));
		table->setItem(i, 9, new QTableWidgetItem(QString::number(child.value("primitive_alternating_pair_count").toInt())));
	}
	table->resizeRowsToContents();
	table->resizeColumnsToContents();
}

static int relative_trend_quality_bucket_from_raw_gui(int raw)
{
	if (raw <= 320) {
		return 0;
	}
	if (raw <= 416) {
		return 1;
	}
	if (raw <= 512) {
		return 2;
	}
	if (raw <= 608) {
		return 3;
	}
	if (raw <= 704) {
		return 4;
	}
	if (raw <= 800) {
		return 5;
	}
	if (raw <= 896) {
		return 6;
	}
	return 7;
}

struct wave_detail_candle_row {
	int absolute_index;
	int open_pips;
	int high_pips;
	int low_pips;
	int close_pips;
	int body;
	int abs_body;
	int range;
	int upper_wick;
	int lower_wick;
	int step_delta;
	int step_abs;
	int overlap_with_prev;
	int body_lo;
	int body_hi;
	int inside_candle;
	int effective_sign;
	int tolerated_counter;
};

struct relative_metric_breakdown {
	int raw_net;
	int net_move_raw;
	int duration;
	int sign_changes;
	int directional_purity_body;
	int retracement_ratio;
	int range_growth_ratio;
	int relative_abs_body_sum_raw;
	int relative_abs_step_sum_raw;
	int relative_directional_efficiency_body;
	int relative_directional_efficiency_step;
	int relative_avg_body_efficiency;
	int relative_body_direction_share;
	int relative_upper_wick_sum_raw;
	int relative_lower_wick_sum_raw;
	int relative_total_wick_sum_raw;
	int relative_wick_to_body_ratio;
	int relative_wick_bias_up;
	int relative_wick_bias_down;
	int relative_wick_bias_aligned;
	int relative_max_pullback_raw;
	int relative_impulse_leg_raw;
	int relative_retracement_ratio;
	int relative_body_overlap_sum_raw;
	int relative_overlap_ratio;
	int relative_range_growth_ratio;
	int relative_body_growth_ratio;
	int relative_trend_quality_raw;
	int relative_trend_quality_bucket;
	int dominant_direction;
	int sign_change_density;
};

struct predictor_trace_row {
	QString name;
	QString lhs;
	QString op;
	QString rhs;
	QString value;
	int src_a;
	int src_b;
};

static QString predictor_trace_number(double value)
{
	return QString::number(value, 'f', 6);
}

static int predictor_trace_add_row(QVector<predictor_trace_row> *rows,
	const QString &name,
	const QString &lhs,
	const QString &op,
	const QString &rhs,
	double value,
	int src_a = -1,
	int src_b = -1)
{
	predictor_trace_row row;

	row.name = name;
	row.lhs = lhs;
	row.op = op;
	row.rhs = rhs;
	row.value = predictor_trace_number(value);
	row.src_a = src_a;
	row.src_b = src_b;
	rows->push_back(row);
	return rows->size() - 1;
}

static int predictor_trace_add_text_row(QVector<predictor_trace_row> *rows,
	const QString &name,
	const QString &lhs,
	const QString &op,
	const QString &rhs,
	const QString &value,
	int src_a = -1,
	int src_b = -1)
{
	predictor_trace_row row;

	row.name = name;
	row.lhs = lhs;
	row.op = op;
	row.rhs = rhs;
	row.value = value;
	row.src_a = src_a;
	row.src_b = src_b;
	rows->push_back(row);
	return rows->size() - 1;
}

static QVector<predictor_trace_row> predictor_build_trace_rows(const QJsonObject &trace_obj)
{
	QVector<predictor_trace_row> rows;
	QJsonArray families = trace_obj.value("families").toArray();
	QJsonArray score_values = trace_obj.value("score_values").toArray();
	int selected_index = trace_obj.value("selected_index").toInt();
	int base_count = trace_obj.value("base_count").toInt();
	int total = trace_obj.value("total").toInt();
	int value_count = trace_obj.value("value_count").toInt();
	double temperature = trace_obj.value("temperature").toDouble();
	int family_count = trace_obj.value("family_count").toInt();
	QVector<double> shifted_values;
	QVector<double> exp_values;
	int base_num_row;
	int base_den_row;
	int base_prob_row;
	int log_base_row;
	int evidence_row = -1;
	int base_count_row;
	int total_row;
	int value_count_row;
	int family_count_row;
	int temperature_row;
	int family_scale_row;
	int scaled_evidence_row;
	int max_score_row = -1;
	int exp_sum_row = -1;
	int selected_prob_row = -1;

	base_count_row = predictor_trace_add_row(&rows, "base_count", "input", "=", "base_count", (double)base_count);
	total_row = predictor_trace_add_row(&rows, "total", "input", "=", "total", (double)total);
	value_count_row = predictor_trace_add_row(&rows, "value_count", "input", "=", "value_count", (double)value_count);
	family_count_row = predictor_trace_add_row(&rows, "family_count", "input", "=", "family_count", (double)family_count);
	temperature_row = predictor_trace_add_row(&rows, "temperature", "input", "=", "temperature", temperature);
	base_num_row = predictor_trace_add_row(&rows, "base_num", "base_count", "+", "1", (double)base_count + 1.0, base_count_row, -1);
	base_den_row = predictor_trace_add_row(&rows, "base_den", "total", "+", "value_count", (double)total + (double)value_count, total_row, value_count_row);
	base_prob_row = predictor_trace_add_row(&rows, "base_prob", "base_num", "/", "base_den",
		((double)base_count + 1.0) / ((double)total + (double)value_count),
		base_num_row,
		base_den_row);
	log_base_row = predictor_trace_add_row(&rows, "log_base", "base_prob", "log", "e",
		log(((double)base_count + 1.0) / ((double)total + (double)value_count)),
		base_prob_row,
		-1);
	for (int i = 0; i < families.size(); ++i) {
		QJsonObject family_obj = families.at(i).toObject();
		QString prefix = QString("f%1").arg(i + 1);
		int support = family_obj.value("support").toInt();
		int target_count = family_obj.value("target_count").toInt();
		double cond_prob = family_obj.value("cond_prob").toDouble();
		double lift = family_obj.value("lift").toDouble();
		double contribution = family_obj.value("contribution").toDouble();
		int support_row;
		int target_count_row;
		int cond_num_row;
		int cond_den_row;
		int cond_prob_row;
		int lift_row;
		int weight_arg_row;
		int weight_row;
		int log_lift_row;
		int contrib_row;

		predictor_trace_add_text_row(&rows,
			prefix + "_label",
			"label",
			"=",
			QString("\"%1\"").arg(family_obj.value("label").toString()),
			family_obj.value("label").toString());
		support_row = predictor_trace_add_row(&rows, prefix + "_support", "input", "=", "support", (double)support);
		target_count_row = predictor_trace_add_row(&rows, prefix + "_target_count", "input", "=", "target_count", (double)target_count);
		cond_num_row = predictor_trace_add_row(&rows, prefix + "_cond_num", prefix + "_target_count", "+", "1", (double)target_count + 1.0, target_count_row, -1);
		cond_den_row = predictor_trace_add_row(&rows, prefix + "_cond_den", prefix + "_support", "+", "value_count", (double)support + (double)value_count, support_row, value_count_row);
		cond_prob_row = predictor_trace_add_row(&rows, prefix + "_cond_prob", prefix + "_cond_num", "/", prefix + "_cond_den", cond_prob, cond_num_row, cond_den_row);
		lift_row = predictor_trace_add_row(&rows, prefix + "_lift", prefix + "_cond_prob", "/", "base_prob", lift, cond_prob_row, base_prob_row);
		weight_arg_row = predictor_trace_add_row(&rows, prefix + "_weight_arg", prefix + "_support", "+", "1", (double)support + 1.0, support_row, -1);
		weight_row = predictor_trace_add_row(&rows, prefix + "_weight", prefix + "_weight_arg", "log", "e", log((double)support + 1.0), weight_arg_row, -1);
		log_lift_row = predictor_trace_add_row(&rows, prefix + "_log_lift", prefix + "_lift", "log", "e", log(lift), lift_row, -1);
		contrib_row = predictor_trace_add_row(&rows, prefix + "_contribution", prefix + "_weight", "*", prefix + "_log_lift", contribution, weight_row, log_lift_row);
		if (evidence_row < 0) {
			evidence_row = predictor_trace_add_row(&rows, "evidence_sum_1", prefix + "_contribution", "+", "0", contribution, contrib_row, -1);
		} else {
			evidence_row = predictor_trace_add_row(&rows,
				QString("evidence_sum_%1").arg(i + 1),
				rows.at(evidence_row).name,
				"+",
				prefix + "_contribution",
				rows.at(evidence_row).value.toDouble() + contribution,
				evidence_row,
				contrib_row);
		}
	}
	if (evidence_row < 0) {
		evidence_row = predictor_trace_add_row(&rows, "evidence_sum_0", "0", "+", "0", 0.0);
	}
	family_scale_row = predictor_trace_add_row(&rows, "family_scale", "family_count", "sqrt", "1", sqrt((double)qMax(family_count, 1)), family_count_row, -1);
	scaled_evidence_row = predictor_trace_add_row(&rows, "scaled_evidence", rows.at(evidence_row).name, "/", "family_scale",
		(family_count > 0) ? (rows.at(evidence_row).value.toDouble() / sqrt((double)family_count)) : 0.0,
		evidence_row,
		family_scale_row);
	for (int i = 0; i < score_values.size(); ++i) {
		QJsonObject score_obj = score_values.at(i).toObject();
		QString label = score_obj.value("label").toString();
		double score = score_obj.value("score").toDouble();
		double probability = score_obj.value("probability").toDouble();
		int score_row;
		int shifted_row;
		int temp_row;
		int exp_row;

		if (i == selected_index) {
			score_row = predictor_trace_add_row(&rows, QString("score_%1").arg(label), "log_base", "+", "scaled_evidence", score, log_base_row, scaled_evidence_row);
		} else {
			score_row = predictor_trace_add_row(&rows, QString("score_%1").arg(label), "raw_score", "+", "0", score);
		}
		if (max_score_row < 0) {
			max_score_row = predictor_trace_add_row(&rows, "max_score_1", rows.at(score_row).name, "max", rows.at(score_row).name, score, score_row, score_row);
		} else {
			max_score_row = predictor_trace_add_row(&rows,
				QString("max_score_%1").arg(i + 1),
				rows.at(max_score_row).name,
				"max",
				rows.at(score_row).name,
				qMax(rows.at(max_score_row).value.toDouble(), score),
				max_score_row,
				score_row);
		}
		shifted_row = predictor_trace_add_row(&rows, QString("shifted_%1").arg(label), rows.at(score_row).name, "-", "max_score", score, score_row, -1);
		temp_row = predictor_trace_add_row(&rows, QString("temp_%1").arg(label), rows.at(shifted_row).name, "/", "temperature", score / qMax(temperature, 1e-9), shifted_row, temperature_row);
		exp_row = predictor_trace_add_row(&rows, QString("exp_%1").arg(label), rows.at(temp_row).name, "exp", "1", exp(score / qMax(temperature, 1e-9)), temp_row, -1);
		if (exp_sum_row < 0) {
			exp_sum_row = predictor_trace_add_row(&rows, "exp_sum_1", rows.at(exp_row).name, "+", "0", rows.at(exp_row).value.toDouble(), exp_row, -1);
		} else {
			exp_sum_row = predictor_trace_add_row(&rows,
				QString("exp_sum_%1").arg(i + 1),
				rows.at(exp_sum_row).name,
				"+",
				rows.at(exp_row).name,
				rows.at(exp_sum_row).value.toDouble() + rows.at(exp_row).value.toDouble(),
				exp_sum_row,
				exp_row);
		}
		if (i == selected_index) {
			selected_prob_row = predictor_trace_add_row(&rows,
				QString("prob_%1").arg(label),
				rows.at(exp_row).name,
				"/",
				"exp_sum",
				probability,
				exp_row,
				-1);
		}
	}
	if (max_score_row >= 0) {
		double max_score = rows.at(max_score_row).value.toDouble();
		double exp_sum = 0.0;

		shifted_values.resize(score_values.size());
		exp_values.resize(score_values.size());

		for (int i = 0; i < score_values.size(); ++i) {
			QJsonObject score_obj = score_values.at(i).toObject();
			QString label = score_obj.value("label").toString();
			double shifted_value = score_obj.value("score").toDouble() - max_score;
			double exp_value = exp(shifted_value / qMax(temperature, 1e-9));

			shifted_values[i] = shifted_value;
			exp_values[i] = exp_value;
			exp_sum += exp_value;

			for (int row_index = 0; row_index < rows.size(); ++row_index) {
				if (rows[row_index].name == QString("shifted_%1").arg(label)) {
					rows[row_index].rhs = rows.at(max_score_row).name;
					rows[row_index].src_b = max_score_row;
					rows[row_index].value = predictor_trace_number(shifted_value);
				}
				if (rows[row_index].name == QString("temp_%1").arg(label)) {
					rows[row_index].value = predictor_trace_number(shifted_value / qMax(temperature, 1e-9));
				}
				if (rows[row_index].name == QString("exp_%1").arg(label)) {
					rows[row_index].value = predictor_trace_number(exp_value);
				}
			}
		}
		if (exp_sum_row >= 0) {
			double running_sum = 0.0;

			for (int i = 0; i < score_values.size(); ++i) {
				QString label = score_values.at(i).toObject().value("label").toString();

				running_sum += exp_values[i];
				for (int row_index = 0; row_index < rows.size(); ++row_index) {
					if (rows[row_index].name == QString("exp_sum_%1").arg(i + 1)) {
						rows[row_index].value = predictor_trace_number(running_sum);
					}
					if (rows[row_index].name == QString("prob_%1").arg(label)) {
						rows[row_index].value = predictor_trace_number((exp_sum > 0.0) ? (exp_values[i] / exp_sum) : 0.0);
					}
				}
			}
		}
	}
	if (selected_prob_row >= 0 && exp_sum_row >= 0) {
		rows[selected_prob_row].rhs = rows.at(exp_sum_row).name;
		rows[selected_prob_row].src_b = exp_sum_row;
	}
	return rows;
}

static void predictor_trace_apply_highlight(QTableWidget *table, int row)
{
	QColor normal_color = table->palette().base().color();
	QColor selected_color(220, 235, 255);
	QColor source_color(255, 245, 180);
	int src_a = -1;
	int src_b = -1;

	if (table == nullptr || row < 0 || row >= table->rowCount()) {
		return;
	}
	for (int r = 0; r < table->rowCount(); ++r) {
		for (int c = 0; c < table->columnCount(); ++c) {
			QTableWidgetItem *cell = table->item(r, c);

			if (cell != nullptr) {
				cell->setBackground(normal_color);
			}
		}
	}
	if (table->item(row, 0) != nullptr) {
		src_a = table->item(row, 0)->data(Qt::UserRole).toInt();
		src_b = table->item(row, 0)->data(Qt::UserRole + 1).toInt();
	}
	for (int c = 0; c < table->columnCount(); ++c) {
		QTableWidgetItem *cell = table->item(row, c);

		if (cell != nullptr) {
			cell->setBackground(selected_color);
		}
	}
	for (int source_row : {src_a, src_b}) {
		if (source_row < 0 || source_row >= table->rowCount()) {
			continue;
		}
		for (int c = 0; c < table->columnCount(); ++c) {
			QTableWidgetItem *cell = table->item(source_row, c);

			if (cell != nullptr) {
				cell->setBackground(source_color);
			}
		}
	}
}

static QTableWidget *predictor_trace_build_table(const QVector<predictor_trace_row> &rows, QWidget *parent)
{
	QTableWidget *table = new QTableWidget(parent);

	table->setColumnCount(5);
	table->setHorizontalHeaderLabels(QStringList({"name", "lhs", "op", "rhs", "value"}));
	table->setRowCount(rows.size());
	table->setSelectionBehavior(QAbstractItemView::SelectRows);
	table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
	table->horizontalHeader()->setStretchLastSection(false);
	for (int i = 0; i < rows.size(); ++i) {
		const predictor_trace_row &row = rows.at(i);
		QString values[5] = {row.name, row.lhs, row.op, row.rhs, row.value};

		for (int c = 0; c < 5; ++c) {
			QTableWidgetItem *item = new QTableWidgetItem(values[c]);

			item->setData(Qt::UserRole, row.src_a);
			item->setData(Qt::UserRole + 1, row.src_b);
			table->setItem(i, c, item);
		}
	}
	QObject::connect(table, &QTableWidget::itemClicked, table, [table](QTableWidgetItem *item) {
		if (item == nullptr) {
			return;
		}
		predictor_trace_apply_highlight(table, item->row());
	});
	table->resizeRowsToContents();
	table->resizeColumnsToContents();
	if (!rows.isEmpty()) {
		predictor_trace_apply_highlight(table, rows.size() - 1);
	}
	return table;
}

static int abs_i32_gui(int v)
{
	return (v >= 0) ? v : -v;
}

static int max_i32_gui(int a, int b)
{
	return (a > b) ? a : b;
}

static int min_i32_gui(int a, int b)
{
	return (a < b) ? a : b;
}

static int penalty_low_gui(int x, int a)
{
	return (x < a) ? (a - x) : 0;
}

static int penalty_high_gui(int x, int b)
{
	return (x > b) ? (x - b) : 0;
}

static QString json_value_to_text(const QJsonValue &value)
{
	if (value.isString()) {
		return value.toString();
	}
	if (value.isDouble()) {
		return QString::number(value.toDouble(), 'g', 15);
	}
	if (value.isBool()) {
		return value.toBool() ? "true" : "false";
	}
	if (value.isNull()) {
		return "null";
	}
	if (value.isUndefined()) {
		return "undefined";
	}
	if (value.isObject()) {
		return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Indented)).trimmed();
	}
	if (value.isArray()) {
		return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Indented)).trimmed();
	}
	return "";
}

static int property_value_visible_line_count(const QString &value_text)
{
	return value_text.count('\n') + 1;
}

static int property_value_viewport_height(const QWidget *widget, int visible_lines)
{
	QFontMetrics metrics(widget->font());

	return (metrics.lineSpacing() * visible_lines) + 10;
}

static void set_property_value_cell(QTableWidget *table,
	int row,
	int column,
	const QString &value_text,
	bool highlight = false,
	const QString &tool_tip = QString())
{
	QWidget *existing_widget;

	if (table == nullptr) {
		return;
	}
	existing_widget = table->cellWidget(row, column);
	if (existing_widget != nullptr) {
		table->removeCellWidget(row, column);
		existing_widget->deleteLater();
	}
	if (property_value_visible_line_count(value_text) > 5) {
		QPlainTextEdit *editor = new QPlainTextEdit(table);
		QPalette palette = editor->palette();
		int target_height;

		editor->setReadOnly(true);
		editor->setLineWrapMode(QPlainTextEdit::WidgetWidth);
		editor->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
		editor->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
		editor->setPlainText(value_text);
		editor->setToolTip(tool_tip);
		if (highlight) {
			palette.setColor(QPalette::Base, QColor(255, 249, 196));
			editor->setPalette(palette);
		}
		target_height = property_value_viewport_height(editor, 5);
		editor->setFixedHeight(target_height);
		table->setCellWidget(row, column, editor);
		if (table->rowHeight(row) < target_height) {
			table->setRowHeight(row, target_height);
		}
		return;
	}
	{
		QTableWidgetItem *item = new QTableWidgetItem(value_text);

		item->setToolTip(tool_tip);
		if (highlight) {
			item->setBackground(QColor(255, 249, 196));
		}
		table->setItem(row, column, item);
	}
}

static QString gui_settings_path()
{
	return QDir::homePath() + "/.mwp_gui.ini";
}

static int is_inside_candle_gui(const QVector<wave_detail_candle_row> &candles, int index)
{
	if (index <= 0 || index >= candles.size()) {
		return 0;
	}
	return candles[index].high_pips <= candles[index - 1].high_pips &&
		candles[index].low_pips >= candles[index - 1].low_pips;
}

static int is_tolerated_counter_trend_candle_gui(const QVector<wave_detail_candle_row> &candles,
	const QVector<int> &bodies,
	int index,
	int canonical_sign)
{
	int next_index;

	if (index <= 0 || index + 1 >= candles.size()) {
		return 0;
	}
	next_index = index + 1;
	while (next_index < bodies.size() && bodies[next_index] == 0) {
		next_index += 1;
	}
	if (next_index >= bodies.size()) {
		return 0;
	}
	if (canonical_sign > 0) {
		return bodies[index] < 0 &&
			bodies[next_index] > 0 &&
			candles[index].close_pips >= candles[index - 1].low_pips &&
			candles[next_index].close_pips > candles[index - 1].close_pips;
	}
	if (canonical_sign < 0) {
		return bodies[index] > 0 &&
			bodies[next_index] < 0 &&
			candles[index].close_pips <= candles[index - 1].high_pips &&
			candles[next_index].close_pips < candles[index - 1].close_pips;
	}
	return 0;
}

static relative_metric_breakdown compute_relative_metric_breakdown(QVector<wave_detail_candle_row> *candles,
	int norm_scale,
	int epsilon)
{
	relative_metric_breakdown out;
	QVector<int> bodies;
	int canonical_sign;
	int last_sign = 0;
	int tolerated_counter_index = -1;
	int trend_body = 0;
	int retrace_body = 0;
	int gross_move_raw = 0;
	int body_eff_sum = 0;
	int aligned_body_sum = 0;
	int counter_body_sum = 0;
	int first_half_sum = 0;
	int second_half_sum = 0;
	int first_half_body_sum = 0;
	int second_half_body_sum = 0;
	int first_half_len;
	int second_half_len;
	int relative_running_extreme;

	memset(&out, 0, sizeof(out));
	if (candles == NULL || candles->isEmpty()) {
		return out;
	}

	out.duration = candles->size();
	out.raw_net = candles->last().close_pips - candles->first().open_pips;
	out.net_move_raw = abs_i32_gui(out.raw_net);
	out.dominant_direction = (out.raw_net > 0) ? 1 : ((out.raw_net < 0) ? -1 : 0);
	canonical_sign = (out.raw_net < 0) ? -1 : 1;
	first_half_len = out.duration / 2;
	if (first_half_len == 0) {
		first_half_len = 1;
	}
	second_half_len = out.duration - first_half_len;
	if (second_half_len == 0) {
		second_half_len = 1;
	}

	bodies.reserve(candles->size());
	for (int i = 0; i < candles->size(); ++i) {
		bodies.push_back((*candles)[i].body);
		(*candles)[i].inside_candle = is_inside_candle_gui(*candles, i);
		(*candles)[i].tolerated_counter = 0;
		(*candles)[i].effective_sign = 0;
	}

	relative_running_extreme = candles->first().close_pips;
	for (int i = 0; i < candles->size(); ++i) {
		int canonical_body = bodies[i] * canonical_sign;
		int body_abs = abs_i32_gui(bodies[i]);
		int sign = 0;
		int tolerated_counter = 0;

		if (tolerated_counter_index < 0 &&
			is_tolerated_counter_trend_candle_gui(*candles, bodies, i, canonical_sign)) {
			tolerated_counter = 1;
			tolerated_counter_index = i;
			canonical_body = 0;
		}
		if (canonical_body > 0) {
			sign = 1;
			trend_body += canonical_body;
			aligned_body_sum += canonical_body;
		} else if (canonical_body < 0) {
			sign = -1;
			retrace_body += -canonical_body;
			counter_body_sum += -canonical_body;
		}
		if (!(*candles)[i].inside_candle && bodies[i] != 0 && sign != 0 && last_sign != 0 && sign != last_sign) {
			out.sign_changes += 1;
		}
		if (!(*candles)[i].inside_candle && bodies[i] != 0 && sign != 0) {
			last_sign = sign;
		}
		out.relative_abs_body_sum_raw += body_abs;
		body_eff_sum += (int)(((long long)body_abs * norm_scale) / (long long)max_i32_gui((*candles)[i].range, epsilon));
		out.relative_upper_wick_sum_raw += (*candles)[i].upper_wick;
		out.relative_lower_wick_sum_raw += (*candles)[i].lower_wick;
		gross_move_raw += abs_i32_gui(canonical_body);
		(*candles)[i].tolerated_counter = tolerated_counter;
		(*candles)[i].effective_sign = sign;
		if (i < first_half_len) {
			first_half_sum += (*candles)[i].range;
			first_half_body_sum += body_abs;
		} else {
			second_half_sum += (*candles)[i].range;
			second_half_body_sum += body_abs;
		}
		if (i > 0) {
			(*candles)[i].step_delta = (*candles)[i].close_pips - (*candles)[i - 1].close_pips;
			(*candles)[i].step_abs = abs_i32_gui((*candles)[i].step_delta);
			(*candles)[i].overlap_with_prev = max_i32_gui(0,
				min_i32_gui((*candles)[i - 1].body_hi, (*candles)[i].body_hi) -
				max_i32_gui((*candles)[i - 1].body_lo, (*candles)[i].body_lo));
			out.relative_abs_step_sum_raw += (*candles)[i].step_abs;
			out.relative_body_overlap_sum_raw += (*candles)[i].overlap_with_prev;
		}
		if (out.raw_net > 0) {
			if ((*candles)[i].close_pips > relative_running_extreme) {
				relative_running_extreme = (*candles)[i].close_pips;
			}
			if (relative_running_extreme - (*candles)[i].close_pips > out.relative_max_pullback_raw) {
				out.relative_max_pullback_raw = relative_running_extreme - (*candles)[i].close_pips;
			}
			if (relative_running_extreme - candles->first().close_pips > out.relative_impulse_leg_raw) {
				out.relative_impulse_leg_raw = relative_running_extreme - candles->first().close_pips;
			}
		} else if (out.raw_net < 0) {
			if ((*candles)[i].close_pips < relative_running_extreme) {
				relative_running_extreme = (*candles)[i].close_pips;
			}
			if ((*candles)[i].close_pips - relative_running_extreme > out.relative_max_pullback_raw) {
				out.relative_max_pullback_raw = (*candles)[i].close_pips - relative_running_extreme;
			}
			if (candles->first().close_pips - relative_running_extreme > out.relative_impulse_leg_raw) {
				out.relative_impulse_leg_raw = candles->first().close_pips - relative_running_extreme;
			}
		}
	}

	out.directional_purity_body = (int)(((long long)trend_body * norm_scale) / (long long)max_i32_gui(gross_move_raw, epsilon));
	out.retracement_ratio = (int)(((long long)retrace_body * norm_scale) / (long long)max_i32_gui(trend_body, epsilon));
	out.sign_change_density = (int)(((long long)out.sign_changes * norm_scale) / (long long)max_i32_gui(out.duration - 1, 1));
	out.relative_directional_efficiency_body = (int)(((long long)out.net_move_raw * norm_scale) / (long long)max_i32_gui(out.relative_abs_body_sum_raw, epsilon));
	out.relative_directional_efficiency_step = (int)(((long long)out.net_move_raw * norm_scale) / (long long)max_i32_gui(out.relative_abs_step_sum_raw, epsilon));
	out.relative_avg_body_efficiency = body_eff_sum / max_i32_gui(out.duration, 1);
	out.relative_body_direction_share = (int)(((long long)aligned_body_sum * norm_scale) / (long long)max_i32_gui(aligned_body_sum + counter_body_sum, epsilon));
	out.relative_wick_bias_up = (int)(((long long)(out.relative_lower_wick_sum_raw + epsilon) * norm_scale) /
		(long long)max_i32_gui(out.relative_upper_wick_sum_raw + epsilon, 1));
	out.relative_wick_bias_down = (int)(((long long)(out.relative_upper_wick_sum_raw + epsilon) * norm_scale) /
		(long long)max_i32_gui(out.relative_lower_wick_sum_raw + epsilon, 1));
	out.relative_total_wick_sum_raw = out.relative_upper_wick_sum_raw + out.relative_lower_wick_sum_raw;
	out.relative_wick_to_body_ratio = (int)(((long long)out.relative_total_wick_sum_raw * norm_scale) /
		(long long)max_i32_gui(out.relative_abs_body_sum_raw, epsilon));
	out.relative_wick_bias_aligned = (out.raw_net > 0) ? out.relative_wick_bias_up :
		((out.raw_net < 0) ? out.relative_wick_bias_down : norm_scale);
	out.relative_overlap_ratio = (int)(((long long)out.relative_body_overlap_sum_raw * norm_scale) /
		(long long)max_i32_gui(out.relative_abs_body_sum_raw, epsilon));
	out.relative_retracement_ratio = (out.raw_net == 0 || out.relative_impulse_leg_raw <= 0) ? norm_scale :
		(int)(((long long)out.relative_max_pullback_raw * norm_scale) /
			(long long)max_i32_gui(out.relative_impulse_leg_raw, epsilon));
	out.range_growth_ratio = (out.duration < 2) ? norm_scale :
		(int)(((long long)(second_half_sum / second_half_len) * norm_scale) /
			(long long)max_i32_gui(first_half_sum / first_half_len, epsilon));
	out.relative_range_growth_ratio = out.range_growth_ratio;
	out.relative_body_growth_ratio = (out.duration < 2) ? norm_scale :
		(int)(((long long)(second_half_body_sum / second_half_len) * norm_scale) /
			(long long)max_i32_gui(first_half_body_sum / first_half_len, epsilon));
	out.relative_trend_quality_raw =
		((norm_scale - min_i32_gui(out.relative_directional_efficiency_step, norm_scale)) >> 2) +
		((norm_scale - min_i32_gui(out.relative_directional_efficiency_body, norm_scale)) >> 3) +
		(out.relative_retracement_ratio >> 3) +
		(out.relative_overlap_ratio >> 3) +
		(out.relative_wick_to_body_ratio >> 3) +
		((norm_scale - min_i32_gui(out.relative_avg_body_efficiency, norm_scale)) >> 3) +
		(out.sign_change_density >> 4) +
		(penalty_low_gui(out.relative_wick_bias_aligned, norm_scale) >> 3);
	if (out.raw_net == 0) {
		out.relative_trend_quality_raw = 0x3ff;
	}
	out.relative_trend_quality_bucket = relative_trend_quality_bucket_from_raw_gui(out.relative_trend_quality_raw);
	return out;
}
}

main_window::main_window(QWidget *parent)
	: QMainWindow(parent)
{
	QWidget *root = new QWidget(this);
	QVBoxLayout *root_layout = new QVBoxLayout(root);
	QTabWidget *tabs = new QTabWidget(root);

	setWindowTitle("MT Research Client");
	resize(1280, 860);
	load_wave_property_visibility();
	load_live_property_visibility();
	m_live_candle_start = 0;
	m_live_last_ts = 0;
	m_live_last_wave_count = 0;

	tabs->addTab(build_connection_tab(), "Connection");
	tabs->addTab(build_jobs_tab(), "Jobs");
	tabs->addTab(build_patterns_tab(), "Patterns");
	tabs->addTab(build_waves_tab(), "Waves");
	tabs->addTab(build_predictor_tab(), "Predictor");
	m_micro_harvest_widget = new micro_harvest_widget(root);
	m_micro_harvest_widget->set_base_url(current_base_url());
	tabs->addTab(m_micro_harvest_widget, "Micro Harvest");
	tabs->addTab(build_live_tab(), "Live");

	m_log_text = nullptr;
	m_jobs_refresh_timer = new QTimer(this);
	m_jobs_refresh_timer->setInterval(1000);
	connect(m_jobs_refresh_timer, &QTimer::timeout, this, &main_window::on_refresh_jobs);
	m_live_refresh_timer = new QTimer(this);
	m_live_refresh_timer->setInterval(30000);
	connect(m_live_refresh_timer, &QTimer::timeout, this, &main_window::on_refresh_live);
	if (m_live_auto_refresh_check != nullptr && m_live_auto_refresh_check->isChecked()) {
		m_live_refresh_timer->start();
	}

	root_layout->addWidget(tabs, 1);
	setCentralWidget(root);
	connect(m_server_url_edit, &QLineEdit::textChanged, this, [this](const QString &text) {
		if (m_micro_harvest_widget != nullptr) {
			m_micro_harvest_widget->set_base_url(text);
		}
	});

	on_refresh_datasets();
	on_refresh_jobs();
	on_refresh_live();
}

QString main_window::current_base_url() const
{
	return m_server_url_edit->text().trimmed();
}

void main_window::append_log(const QString &msg)
{
	Q_UNUSED(msg);
}

void main_window::load_wave_property_visibility()
{
	QSettings settings(gui_settings_path(), QSettings::IniFormat);

	m_hidden_wave_properties = settings.value("waves/hidden_token_properties").toStringList();
}

void main_window::load_live_property_visibility()
{
	QSettings settings(gui_settings_path(), QSettings::IniFormat);

	m_hidden_live_properties = settings.value("live/hidden_token_properties").toStringList();
}

void main_window::save_wave_property_visibility() const
{
	QSettings settings(gui_settings_path(), QSettings::IniFormat);

	settings.setValue("waves/hidden_token_properties", m_hidden_wave_properties);
}

void main_window::save_live_property_visibility() const
{
	QSettings settings(gui_settings_path(), QSettings::IniFormat);

	settings.setValue("live/hidden_token_properties", m_hidden_live_properties);
}

void main_window::save_wave_splitter_state() const
{
	QSettings settings(gui_settings_path(), QSettings::IniFormat);

	if (m_wave_main_splitter != nullptr) {
		settings.setValue("waves/main_splitter_state", m_wave_main_splitter->saveState());
	}
	if (m_wave_content_splitter != nullptr) {
		settings.setValue("waves/content_splitter_state", m_wave_content_splitter->saveState());
	}
}

void main_window::update_wave_overlay_property_options()
{
	QString current;
	QStringList keys;

	if (m_wave_overlay_property_combo == nullptr) {
		return;
	}
	current = m_wave_overlay_property_combo->currentText();
	keys = available_wave_property_names();
	m_wave_overlay_property_combo->blockSignals(true);
	m_wave_overlay_property_combo->clear();
	m_wave_overlay_property_combo->addItem("");
	for (const QString &key : keys) {
		m_wave_overlay_property_combo->addItem(key);
	}
	{
		int index = m_wave_overlay_property_combo->findText(current);

		if (index >= 0) {
			m_wave_overlay_property_combo->setCurrentIndex(index);
		}
	}
	m_wave_overlay_property_combo->blockSignals(false);
}

QVector<cluster_chart_widget::wave_overlay_item> main_window::build_wave_overlay_items(int fetch_start, int fetch_end) const
{
	QVector<cluster_chart_widget::wave_overlay_item> out;
	QString key;

	if (m_wave_overlay_property_combo == nullptr) {
		return out;
	}
	key = m_wave_overlay_property_combo->currentText().trimmed();
	if (key.isEmpty()) {
		return out;
	}
	for (int i = 0; i < m_wave_rows.size(); ++i) {
		QJsonObject obj = m_wave_rows.at(i).toObject();
		int start_candle = obj.value("start_candle").toInt();
		int end_candle = obj.value("end_candle").toInt();
		cluster_chart_widget::wave_overlay_item item;

		if (end_candle < fetch_start || start_candle > fetch_end) {
			continue;
		}
		item.start_index = start_candle - fetch_start;
		item.end_index = end_candle - fetch_start;
		item.label = json_value_to_text(obj.value(key));
		out.push_back(item);
	}
	return out;
}

bool main_window::is_wave_property_visible(const QString &key) const
{
	return !m_hidden_wave_properties.contains(key);
}

bool main_window::is_live_property_visible(const QString &key) const
{
	return !m_hidden_live_properties.contains(key);
}

QStringList main_window::available_wave_property_names() const
{
	QSet<QString> names;

	for (int i = 0; i < m_wave_rows.size(); ++i) {
		QJsonObject obj = m_wave_rows.at(i).toObject();
		QStringList keys = obj.keys();

		for (const QString &key : keys) {
			names.insert(key);
		}
	}
	{
		QStringList keys = m_wave_summary_obj.keys();

		for (const QString &key : keys) {
			Q_UNUSED(key);
		}
	}
	{
		QStringList hidden = m_hidden_wave_properties;

		for (const QString &key : hidden) {
			names.insert(key);
		}
	}
	{
		QStringList out = names.values();

		out.sort();
		return out;
	}
}

QStringList main_window::available_live_property_names() const
{
	QSet<QString> names;

	for (int i = 0; i < m_live_waves.size(); ++i) {
		QJsonObject obj = m_live_waves.at(i).toObject();
		QStringList keys = obj.keys();

		for (const QString &key : keys) {
			names.insert(key);
		}
	}
	{
		QStringList hidden = m_hidden_live_properties;

		for (const QString &key : hidden) {
			names.insert(key);
		}
	}
	{
		QStringList out = names.values();

		out.sort();
		return out;
	}
}

void main_window::update_wave_token_properties_height(int visible_property_count)
{
	int row_count;
	int row_height = 0;
	int frame;
	int header;
	int target_height;

	if (m_wave_token_props_table == nullptr) {
		return;
	}
	row_count = (visible_property_count + 3) / 4;
	if (row_count <= 0) {
		row_count = 1;
	}
	for (int row = 0; row < row_count; ++row) {
		row_height += m_wave_token_props_table->rowHeight(row);
	}
	frame = m_wave_token_props_table->frameWidth() * 2;
	header = m_wave_token_props_table->horizontalHeader()->isVisible() ?
		m_wave_token_props_table->horizontalHeader()->height() : 0;
	target_height = row_height + header + frame + 8;
	m_wave_token_props_table->setMinimumHeight(target_height);
	m_wave_token_props_table->setMaximumHeight(QWIDGETSIZE_MAX);
	m_wave_token_props_table->updateGeometry();
}

void main_window::update_label_text(QLabel *label, const QString &text, const QString &tooltip)
{
	if (label == nullptr) {
		return;
	}
	label->setText(text);
	label->setToolTip(tooltip);
}

void main_window::set_table_headers(QTableWidget *table, const QStringList &headers)
{
	table->setColumnCount(headers.size());
	table->setHorizontalHeaderLabels(headers);
	table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
	table->setSelectionBehavior(QAbstractItemView::SelectRows);
	table->setEditTriggers(QAbstractItemView::NoEditTriggers);
}

int main_window::current_clustering_algorithm() const
{
	if (m_clustering_algorithm_group == nullptr) {
		return clustering_ui_sparse_graph;
	}
	return m_clustering_algorithm_group->checkedId();
}

QString main_window::job_type_name(int job_type) const
{
	switch (job_type) {
	case job_type_graph_clustering:
		return "graph_clustering";
	case job_type_word_mining:
		return "word_mining";
	case job_type_phrase_mining:
		return "phrase_mining";
	case job_type_kmedoids:
		return "kmedoids";
	case job_type_dbscan:
		return "dbscan";
	case job_type_wave_inference:
		return "wave_inference";
	case job_type_micro_harvest_replay:
		return "micro_harvest_replay";
	default:
		return QString("unknown(%1)").arg(job_type);
	}
}

QString main_window::job_status_name(int job_status) const
{
	switch (job_status) {
	case 1:
		return "queued";
	case 2:
		return "running";
	case 3:
		return "completed";
	case 4:
		return "failed";
	case 5:
		return "cancelled";
	default:
		return QString("unknown(%1)").arg(job_status);
	}
}

QString main_window::graph_cluster_mode_name(int cluster_mode) const
{
	switch (cluster_mode) {
	case graph_cluster_mode_legacy_connected_components:
		return "legacy_connected_components";
	case graph_cluster_mode_connected_components:
		return "connected_components";
	case graph_cluster_mode_label_propagation:
		return "label_propagation";
	case graph_cluster_mode_louvain:
		return "louvain";
	default:
		return QString("unknown(%1)").arg(cluster_mode);
	}
}

QString main_window::clustering_algorithm_name(int algorithm_type) const
{
	switch (algorithm_type) {
	case clustering_ui_threshold_graph:
		return "Threshold graph clustering";
	case clustering_ui_sparse_graph:
		return "Sparse graph clustering";
	case clustering_ui_kmedoids:
		return "K-medoids";
	case clustering_ui_dbscan:
		return "DBSCAN";
	case job_type_graph_clustering:
		return "Graph clustering";
	case job_type_kmedoids:
		return "K-medoids";
	case job_type_dbscan:
		return "DBSCAN";
	default:
		return QString("Unknown (%1)").arg(algorithm_type);
	}
}

QString main_window::clustering_result_name(int algorithm_type, int graph_cluster_mode) const
{
	if (algorithm_type == job_type_graph_clustering) {
		if (graph_cluster_mode == graph_cluster_mode_legacy_connected_components) {
			return "Threshold graph clustering";
		}
		return QString("Sparse graph clustering -> %1").arg(graph_cluster_mode_name(graph_cluster_mode));
	}
	return clustering_algorithm_name(algorithm_type);
}

void main_window::update_clustering_job_ui()
{
	int algorithm_type = current_clustering_algorithm();
	bool is_threshold = algorithm_type == clustering_ui_threshold_graph;
	bool is_graph = algorithm_type == clustering_ui_sparse_graph;
	bool is_kmedoids = algorithm_type == clustering_ui_kmedoids;
	bool is_dbscan = algorithm_type == clustering_ui_dbscan;
	QString summary = QString("Will run: %1").arg(clustering_algorithm_name(algorithm_type));
	QString intro;
	QString formula;
	QString distance_mode_name;
	QString q_label = "Pattern length (q)";
	QString q_tip = "How many candles are packed into each character block.";
	QString delta_label = "Distance tolerance";
	QString delta_tip = "Tolerance used by the distance function before two values are treated as different.";
	QString mirrored_label = "Use mirrored comparison";
	QString mirrored_tip = "Allow bullish/bearish mirrored patterns to count as similar.";
	QString upper_label = "Upper wick weight";
	QString lower_label = "Lower wick weight";
	QString body_label = "Body weight";
	QString range_label = "Range weight";
	QString feature_tip = "Relative importance of this feature inside the selected algorithm's distance function.";

	m_threshold_params_box->setVisible(is_threshold);
	m_graph_params_box->setVisible(is_graph);
	m_kmedoids_params_box->setVisible(is_kmedoids);
	m_dbscan_params_box->setVisible(is_dbscan);

	if (is_threshold) {
		distance_mode_name = (m_threshold_distance_mode_combo != nullptr) ? m_threshold_distance_mode_combo->currentData().toString() : "legacy";
		intro = "Threshold graph clustering. Every pair of blocks is compared on the GPU, and a block may join a cluster only when it is within theta of every existing cluster member. Strict composition and wick-aware modes add hard regime gates before mismatch counting.";
		formula = "Base rule: D(X,Y) = sum_i [ abs(X[i] - Y[i]) > delta ]. Cluster rule: a block joins only if D(X,Y) <= theta for every current member. Wick-aware mode also enforces wick and wick/body-ratio gates.";
		summary += QString(" -> theta=%1, distance=%2").arg(m_theta_spin->value()).arg(distance_mode_name);
		delta_label = "Coordinate mismatch tolerance";
		delta_tip = "A coordinate counts as mismatched only when its absolute difference exceeds this value.";
		upper_label = "Upper wick mismatch weight";
		lower_label = "Lower wick mismatch weight";
		body_label = "Body mismatch weight";
		range_label = "Range mismatch weight";
		m_common_params_box->setTitle("Threshold Graph Core Parameters");
		m_weights_box->setTitle("Threshold Distance Weights");
	} else if (is_graph) {
		intro = "Sparse graph clustering. Blocks are scored with the graph distance, only the nearest neighbors are kept, and the selected graph backend clusters the sparse graph.";
		formula = "Pipeline: block distance -> candidate filters -> k-nearest-neighbor graph -> graph clustering backend.";
		summary += QString(" -> %1").arg(m_graph_mode_combo->currentData().toString());
		delta_label = "Feature tolerance";
		delta_tip = "Per-feature tolerance before excess difference contributes to sparse graph distance.";
		upper_label = "Upper wick importance";
		lower_label = "Lower wick importance";
		body_label = "Body importance";
		range_label = "Range importance";
		m_common_params_box->setTitle("Sparse Graph Core Parameters");
		m_weights_box->setTitle("Sparse Graph Distance Weights");
	} else if (is_kmedoids) {
		intro = "K-medoids clustering. Real observed blocks act as cluster centers, each block is assigned to the nearest medoid, and medoids are refined over repeated iterations.";
		formula = "Pipeline: choose k medoids -> assign blocks to nearest medoid -> update medoids -> repeat.";
		delta_label = "Distance tolerance";
		delta_tip = "Tolerance used inside the medoid assignment distance.";
		upper_label = "Upper wick importance";
		lower_label = "Lower wick importance";
		body_label = "Body importance";
		range_label = "Range importance";
		m_common_params_box->setTitle("K-Medoids Core Parameters");
		m_weights_box->setTitle("K-Medoids Distance Weights");
	} else if (is_dbscan) {
		intro = "DBSCAN density clustering. Blocks become neighbors when their distance is within eps, dense regions grow into clusters, and outliers remain noise.";
		formula = "Pipeline: measure block distance -> form eps neighborhoods -> require min_pts for core points -> expand dense regions.";
		delta_label = "Distance tolerance";
		delta_tip = "Tolerance used inside the DBSCAN block distance calculation.";
		upper_label = "Upper wick importance";
		lower_label = "Lower wick importance";
		body_label = "Body importance";
		range_label = "Range importance";
		m_common_params_box->setTitle("DBSCAN Core Parameters");
		m_weights_box->setTitle("DBSCAN Distance Weights");
	} else {
		m_common_params_box->setTitle("Core Parameters");
		m_weights_box->setTitle("Distance Weights");
	}
	if (m_clustering_summary_label != nullptr) {
		m_clustering_summary_label->setText(summary);
	}
	if (m_algorithm_intro_label != nullptr) {
		m_algorithm_intro_label->setText(intro);
	}
	if (m_algorithm_formula_label != nullptr) {
		m_algorithm_formula_label->setText(formula);
	}
	update_label_text(m_q_label, q_label, q_tip);
	update_label_text(m_delta_label, delta_label, delta_tip);
	update_label_text(m_mirrored_label, mirrored_label, mirrored_tip);
	update_label_text(m_w_upper_label, upper_label, feature_tip);
	update_label_text(m_w_lower_label, lower_label, feature_tip);
	update_label_text(m_w_body_label, body_label, feature_tip);
	update_label_text(m_w_range_label, range_label, feature_tip);
}

QWidget *main_window::build_connection_tab()
{
	QWidget *tab = new QWidget(this);
	QVBoxLayout *v = new QVBoxLayout(tab);
	QGroupBox *server_box = new QGroupBox("Server", tab);
	QFormLayout *server_form = new QFormLayout(server_box);
	QPushButton *ping_btn = new QPushButton("Ping /health", server_box);

	m_server_url_edit = new QLineEdit("http://127.0.0.1", server_box);
	server_form->addRow("Base URL", m_server_url_edit);
	server_form->addRow("", ping_btn);

	QGroupBox *import_box = new QGroupBox("Dataset Import", tab);
	QGridLayout *import_grid = new QGridLayout(import_box);
	QPushButton *import_btn = new QPushButton("Import CSV", import_box);
	QPushButton *sample_btn = new QPushButton("Import Bundled Sample", import_box);
	QPushButton *refresh_btn = new QPushButton("Refresh Datasets", import_box);

	m_import_path_edit = new QLineEdit(import_box);
	m_import_path_edit->setPlaceholderText("/absolute/path/to/mt4.csv");
	m_import_pip_scale_spin = new QSpinBox(import_box);
	m_import_pip_scale_spin->setRange(1, 10000000);
	m_import_pip_scale_spin->setValue(10000);
	import_grid->addWidget(new QLabel("CSV Path", import_box), 0, 0);
	import_grid->addWidget(m_import_path_edit, 0, 1, 1, 3);
	import_grid->addWidget(new QLabel("Pip Scale", import_box), 1, 0);
	import_grid->addWidget(m_import_pip_scale_spin, 1, 1);
	import_grid->addWidget(import_btn, 1, 2);
	import_grid->addWidget(refresh_btn, 1, 3);
	import_grid->addWidget(sample_btn, 2, 2, 1, 2);

	QGroupBox *browse_box = new QGroupBox("Dataset Browser", tab);
	QVBoxLayout *browse_layout = new QVBoxLayout(browse_box);
	QHBoxLayout *browse_ctl = new QHBoxLayout();
	QPushButton *detail_btn = new QPushButton("Load Detail", browse_box);
	QPushButton *candles_btn = new QPushButton("Load Candles", browse_box);
	QPushButton *features_btn = new QPushButton("Load Features", browse_box);

	m_dataset_id_spin = new QSpinBox(browse_box);
	m_dataset_id_spin->setRange(1, 1000000);
	m_window_start_spin = new QSpinBox(browse_box);
	m_window_start_spin->setRange(0, 1000000000);
	m_window_count_spin = new QSpinBox(browse_box);
	m_window_count_spin->setRange(1, 2048);
	m_window_count_spin->setValue(128);
	browse_ctl->addWidget(new QLabel("Dataset ID", browse_box));
	browse_ctl->addWidget(m_dataset_id_spin);
	browse_ctl->addWidget(new QLabel("Start", browse_box));
	browse_ctl->addWidget(m_window_start_spin);
	browse_ctl->addWidget(new QLabel("Count", browse_box));
	browse_ctl->addWidget(m_window_count_spin);
	browse_ctl->addWidget(detail_btn);
	browse_ctl->addWidget(candles_btn);
	browse_ctl->addWidget(features_btn);

	m_dataset_table = new QTableWidget(browse_box);
	set_table_headers(m_dataset_table, {"dataset_id", "name", "candles", "features", "char_blocks", "q"});
	m_dataset_text = new QTextEdit(browse_box);
	m_dataset_text->setReadOnly(true);
	m_dataset_text->setMinimumHeight(220);

	browse_layout->addLayout(browse_ctl);
	browse_layout->addWidget(m_dataset_table, 1);
	browse_layout->addWidget(m_dataset_text, 1);

	v->addWidget(server_box);
	v->addWidget(import_box);
	v->addWidget(browse_box, 1);

	connect(ping_btn, &QPushButton::clicked, this, &main_window::on_ping_server);
	connect(import_btn, &QPushButton::clicked, this, &main_window::on_import_dataset);
	connect(sample_btn, &QPushButton::clicked, this, &main_window::on_import_sample_dataset);
	connect(refresh_btn, &QPushButton::clicked, this, &main_window::on_refresh_datasets);
	connect(detail_btn, &QPushButton::clicked, this, &main_window::on_load_dataset_detail);
	connect(candles_btn, &QPushButton::clicked, this, &main_window::on_load_candles);
	connect(features_btn, &QPushButton::clicked, this, &main_window::on_load_features);

	return tab;
}

QWidget *main_window::build_jobs_tab()
{
	QWidget *tab = new QWidget(this);
	QVBoxLayout *v = new QVBoxLayout(tab);
	QTabWidget *job_tabs = new QTabWidget(tab);
	QWidget *clustering_page = new QWidget(job_tabs);
	QWidget *mining_page = new QWidget(job_tabs);
	QWidget *wave_page = new QWidget(job_tabs);
	QWidget *queue_page = new QWidget(job_tabs);
	QVBoxLayout *clustering_layout = new QVBoxLayout(clustering_page);
	QVBoxLayout *mining_layout = new QVBoxLayout(mining_page);
	QVBoxLayout *wave_layout = new QVBoxLayout(wave_page);
	QVBoxLayout *queue_layout = new QVBoxLayout(queue_page);
	m_common_params_box = new QGroupBox("Core Parameters", tab);
	QGridLayout *common_grid = new QGridLayout(m_common_params_box);
	m_weights_box = new QGroupBox("Distance Weights", tab);
	QGridLayout *weights_grid = new QGridLayout(m_weights_box);
	QGroupBox *algorithm_box = new QGroupBox("Clustering Algorithm", tab);
	QVBoxLayout *algorithm_layout = new QVBoxLayout(algorithm_box);
	QHBoxLayout *algorithm_radio_layout = new QHBoxLayout();
	QRadioButton *threshold_radio = new QRadioButton("Threshold graph clustering", algorithm_box);
	QRadioButton *graph_radio = new QRadioButton("Sparse graph clustering", algorithm_box);
	QRadioButton *kmed_radio = new QRadioButton("K-medoids", algorithm_box);
	QRadioButton *dbscan_radio = new QRadioButton("DBSCAN", algorithm_box);
	QPushButton *run_clustering_btn = new QPushButton("Run Clustering", tab);
	QPushButton *word_btn = new QPushButton("Run Word Mining", tab);
	QPushButton *phrase_btn = new QPushButton("Run Phrase Mining", tab);
	QPushButton *wave_btn = new QPushButton("Run Wave Inference", tab);
	QPushButton *refresh_jobs_btn = new QPushButton("Refresh Jobs", tab);
	QPushButton *delete_job_btn = new QPushButton("Delete Selected Job", tab);
	QHBoxLayout *clustering_actions = new QHBoxLayout();
	QFormLayout *mining_form = new QFormLayout();
	QHBoxLayout *mining_actions = new QHBoxLayout();
	QHBoxLayout *wave_run_layout = new QHBoxLayout();

	m_job_dataset_id_spin = new QSpinBox(m_common_params_box);
	m_job_dataset_id_spin->setRange(1, 1000000);
	m_mining_job_dataset_id_spin = new QSpinBox(tab);
	m_mining_job_dataset_id_spin->setRange(1, 1000000);
	m_wave_job_dataset_id_spin = new QSpinBox(tab);
	m_wave_job_dataset_id_spin->setRange(1, 1000000);
	m_q_spin = new QSpinBox(m_common_params_box);
	m_q_spin->setRange(1, 64);
	m_q_spin->setValue(4);
	m_delta_spin = new QSpinBox(m_common_params_box);
	m_delta_spin->setRange(0, 1000000);
	m_delta_spin->setValue(4);
	m_theta_spin = new QSpinBox(tab);
	m_theta_spin->setRange(0, 1000000);
	m_theta_spin->setValue(4);
	m_delta_upper_wick_gate_spin = new QSpinBox(tab);
	m_delta_upper_wick_gate_spin->setRange(0, 1000000);
	m_delta_upper_wick_gate_spin->setValue(8);
	m_delta_lower_wick_gate_spin = new QSpinBox(tab);
	m_delta_lower_wick_gate_spin->setRange(0, 1000000);
	m_delta_lower_wick_gate_spin->setValue(8);
	m_delta_lower_ratio_gate_spin = new QSpinBox(tab);
	m_delta_lower_ratio_gate_spin->setRange(0, 1000000);
	m_delta_lower_ratio_gate_spin->setValue(25);
	m_delta_upper_ratio_gate_spin = new QSpinBox(tab);
	m_delta_upper_ratio_gate_spin->setRange(0, 1000000);
	m_delta_upper_ratio_gate_spin->setValue(25);
	m_delta_total_wick_ratio_gate_spin = new QSpinBox(tab);
	m_delta_total_wick_ratio_gate_spin->setRange(0, 1000000);
	m_delta_total_wick_ratio_gate_spin->setValue(30);
	m_k_spin = new QSpinBox(tab);
	m_k_spin->setRange(1, 4096);
	m_k_spin->setValue(8);
	m_max_iter_spin = new QSpinBox(tab);
	m_max_iter_spin->setRange(1, 10000);
	m_max_iter_spin->setValue(20);
	m_restarts_spin = new QSpinBox(tab);
	m_restarts_spin->setRange(1, 128);
	m_restarts_spin->setValue(1);
	m_eps_spin = new QSpinBox(tab);
	m_eps_spin->setRange(0, 1000000);
	m_eps_spin->setValue(4);
	m_min_pts_spin = new QSpinBox(tab);
	m_min_pts_spin->setRange(1, 1024);
	m_min_pts_spin->setValue(4);
	m_delta_body_gate_spin = new QSpinBox(tab);
	m_delta_body_gate_spin->setRange(0, 1000000);
	m_delta_body_gate_spin->setValue(8);
	m_delta_range_gate_spin = new QSpinBox(tab);
	m_delta_range_gate_spin->setRange(0, 1000000);
	m_delta_range_gate_spin->setValue(8);
	m_delta_bullcount_gate_spin = new QSpinBox(tab);
	m_delta_bullcount_gate_spin->setRange(0, 64);
	m_delta_bullcount_gate_spin->setValue(2);
	m_delta_signchange_gate_spin = new QSpinBox(tab);
	m_delta_signchange_gate_spin->setRange(0, 64);
	m_delta_signchange_gate_spin->setValue(2);
	m_graph_knn_spin = new QSpinBox(tab);
	m_graph_knn_spin->setRange(1, 4096);
	m_graph_knn_spin->setValue(8);
	m_graph_max_neighbor_distance_spin = new QDoubleSpinBox(tab);
	m_graph_max_neighbor_distance_spin->setRange(0.0, 1000000000.0);
	m_graph_max_neighbor_distance_spin->setDecimals(3);
	m_graph_max_neighbor_distance_spin->setValue(0.0);
	m_graph_mutual_knn_spin = new QSpinBox(tab);
	m_graph_mutual_knn_spin->setRange(0, 1);
	m_graph_mutual_knn_spin->setValue(1);
	m_require_same_majority_sign_spin = new QSpinBox(tab);
	m_require_same_majority_sign_spin->setRange(0, 1);
	m_require_same_majority_sign_spin->setValue(0);
	m_use_composition_spin = new QSpinBox(tab);
	m_use_composition_spin->setRange(0, 1);
	m_use_composition_spin->setValue(1);
	m_word_min_spin = new QSpinBox(tab);
	m_word_min_spin->setRange(2, 64);
	m_word_min_spin->setValue(2);
	m_word_max_spin = new QSpinBox(tab);
	m_word_max_spin->setRange(2, 128);
	m_word_max_spin->setValue(12);
	m_phrase_min_spin = new QSpinBox(tab);
	m_phrase_min_spin->setRange(2, 64);
	m_phrase_min_spin->setValue(2);
	m_phrase_max_spin = new QSpinBox(tab);
	m_phrase_max_spin->setRange(2, 128);
	m_phrase_max_spin->setValue(8);
	m_wave_lmin_spin = new QSpinBox(tab);
	m_wave_lmin_spin->setRange(1, 128);
	m_wave_lmin_spin->setValue(1);
	m_wave_lmax_spin = new QSpinBox(tab);
	m_wave_lmax_spin->setRange(1, 256);
	m_wave_lmax_spin->setValue(60);
	m_wave_context_window_spin = new QSpinBox(tab);
	m_wave_context_window_spin->setRange(1, 1024);
	m_wave_context_window_spin->setValue(512);
	m_wave_lambda_spin = new QSpinBox(tab);
	m_wave_lambda_spin->setRange(0, 1000000);
	m_wave_lambda_spin->setValue(220);
	m_wave_min_move_spin = new QSpinBox(tab);
	m_wave_min_move_spin->setRange(0, 1000000);
	m_wave_min_move_spin->setValue(0);
	m_wave_minimum_trend_pips_spin = new QSpinBox(tab);
	m_wave_minimum_trend_pips_spin->setRange(1, 1000000);
	m_wave_minimum_trend_pips_spin->setValue(7);
	m_wave_norm_scale_spin = new QSpinBox(tab);
	m_wave_norm_scale_spin->setRange(1, 1000000);
	m_wave_norm_scale_spin->setValue(1000);
	m_wave_context_floor_spin = new QSpinBox(tab);
	m_wave_context_floor_spin->setRange(1, 1000000);
	m_wave_context_floor_spin->setValue(1);
	m_wave_epsilon_spin = new QSpinBox(tab);
	m_wave_epsilon_spin->setRange(1, 1000000);
	m_wave_epsilon_spin->setValue(1);
	m_wave_weak_fit_spin = new QSpinBox(tab);
	m_wave_weak_fit_spin->setRange(0, 1000000);
	m_wave_weak_fit_spin->setValue(140);
	m_wave_boundary_low_conf_spin = new QSpinBox(tab);
	m_wave_boundary_low_conf_spin->setRange(0, 1000000);
	m_wave_boundary_low_conf_spin->setValue(30);
	m_wave_model_combo = new QComboBox(tab);
	m_wave_model_combo->addItem("legacy_waves", 0);
	m_wave_model_combo->addItem("primitive_waves", 1);
	m_wave_model_combo->addItem("relative_waves", 2);
	m_wave_model_combo->addItem("relative_waves_2_stage", 3);
	m_wave_model_combo->addItem("relative_waves_seeded", 4);
	m_wave_model_combo->addItem("relative_waves_iterative", 5);
	m_wave_model_combo->addItem("layered_waves", 6);
	m_wave_model_combo->setCurrentIndex(2);
	m_wave_trend_priority_combo = new QComboBox(tab);
	m_wave_trend_priority_combo->addItem("off", 0);
	m_wave_trend_priority_combo->addItem("normal", 1);
	m_wave_trend_priority_combo->addItem("strict", 2);
	m_wave_trend_priority_combo->setCurrentIndex(1);
	m_wave_retrace_break_spin = new QSpinBox(tab);
	m_wave_retrace_break_spin->setRange(0, 1000000);
	m_wave_retrace_break_spin->setValue(200);
	m_wave_trend_split_gain_spin = new QSpinBox(tab);
	m_wave_trend_split_gain_spin->setRange(0, 1000000);
	m_wave_trend_split_gain_spin->setValue(80);
	m_wave_compression_min_duration_spin = new QSpinBox(tab);
	m_wave_compression_min_duration_spin->setRange(1, 256);
	m_wave_compression_min_duration_spin->setValue(4);
	m_wave_expansion_min_duration_spin = new QSpinBox(tab);
	m_wave_expansion_min_duration_spin->setRange(1, 256);
	m_wave_expansion_min_duration_spin->setValue(4);
	m_wave_v_strength_spin = new QSpinBox(tab);
	m_wave_v_strength_spin->setRange(0, 1000000);
	m_wave_v_strength_spin->setValue(420);
	m_wave_v_bridge_spin = new QSpinBox(tab);
	m_wave_v_bridge_spin->setRange(0, 1000000);
	m_wave_v_bridge_spin->setValue(520);
	m_wave_v_duration_spin = new QSpinBox(tab);
	m_wave_v_duration_spin->setRange(1, 256);
	m_wave_v_duration_spin->setValue(12);
	m_wave_relative_trend_retracement_max_spin = new QSpinBox(tab);
	m_wave_relative_trend_retracement_max_spin->setRange(1, 1000000);
	m_wave_relative_trend_retracement_max_spin->setValue(300);
	m_wave_relative_strong_trend_retracement_max_spin = new QSpinBox(tab);
	m_wave_relative_strong_trend_retracement_max_spin->setRange(1, 1000000);
	m_wave_relative_strong_trend_retracement_max_spin->setValue(180);
	m_wave_relative_trend_der_step_min_spin = new QSpinBox(tab);
	m_wave_relative_trend_der_step_min_spin->setRange(1, 1000000);
	m_wave_relative_trend_der_step_min_spin->setValue(550);
	m_wave_relative_trend_der_body_min_spin = new QSpinBox(tab);
	m_wave_relative_trend_der_body_min_spin->setRange(1, 1000000);
	m_wave_relative_trend_der_body_min_spin->setValue(500);
	m_wave_relative_trend_body_eff_min_spin = new QSpinBox(tab);
	m_wave_relative_trend_body_eff_min_spin->setRange(1, 1000000);
	m_wave_relative_trend_body_eff_min_spin->setValue(450);
	m_wave_relative_trend_overlap_max_spin = new QSpinBox(tab);
	m_wave_relative_trend_overlap_max_spin->setRange(1, 1000000);
	m_wave_relative_trend_overlap_max_spin->setValue(350);
	m_wave_relative_trend_sign_flip_max_spin = new QSpinBox(tab);
	m_wave_relative_trend_sign_flip_max_spin->setRange(1, 1000000);
	m_wave_relative_trend_sign_flip_max_spin->setValue(450);
	m_wave_relative_trend_wick_bias_min_spin = new QSpinBox(tab);
	m_wave_relative_trend_wick_bias_min_spin->setRange(1, 1000000);
	m_wave_relative_trend_wick_bias_min_spin->setValue(1050);
	m_wave_relative_range_der_step_max_spin = new QSpinBox(tab);
	m_wave_relative_range_der_step_max_spin->setRange(1, 1000000);
	m_wave_relative_range_der_step_max_spin->setValue(500);
	m_wave_relative_range_body_eff_max_spin = new QSpinBox(tab);
	m_wave_relative_range_body_eff_max_spin->setRange(1, 1000000);
	m_wave_relative_range_body_eff_max_spin->setValue(500);
	m_wave_relative_range_overlap_min_spin = new QSpinBox(tab);
	m_wave_relative_range_overlap_min_spin->setRange(1, 1000000);
	m_wave_relative_range_overlap_min_spin->setValue(350);
	m_wave_relative_range_sign_flip_min_spin = new QSpinBox(tab);
	m_wave_relative_range_sign_flip_min_spin->setRange(1, 1000000);
	m_wave_relative_range_sign_flip_min_spin->setValue(400);
	m_mirrored_spin = new QSpinBox(m_common_params_box);
	m_mirrored_spin->setRange(0, 1);
	m_clustering_algorithm_group = new QButtonGroup(tab);
	m_clustering_algorithm_group->addButton(threshold_radio, clustering_ui_threshold_graph);
	m_clustering_algorithm_group->addButton(graph_radio, clustering_ui_sparse_graph);
	m_clustering_algorithm_group->addButton(kmed_radio, clustering_ui_kmedoids);
	m_clustering_algorithm_group->addButton(dbscan_radio, clustering_ui_dbscan);
	graph_radio->setChecked(true);
	m_clustering_summary_label = new QLabel(algorithm_box);
	m_algorithm_intro_label = new QLabel(algorithm_box);
	m_algorithm_intro_label->setWordWrap(true);
	m_algorithm_formula_label = new QLabel(algorithm_box);
	m_algorithm_formula_label->setWordWrap(true);
	m_graph_mode_combo = new QComboBox(tab);
	m_graph_mode_combo->addItem("label_propagation", "label_propagation");
	m_graph_mode_combo->addItem("connected_components", "connected_components");
	m_graph_mode_combo->addItem("legacy_connected_components", "legacy_connected_components");
	m_graph_mode_combo->addItem("louvain", "louvain");
	m_threshold_distance_mode_combo = new QComboBox(tab);
	m_threshold_distance_mode_combo->addItem("legacy", "legacy");
	m_threshold_distance_mode_combo->addItem("strict_composition", "strict_composition");
	m_threshold_distance_mode_combo->addItem("wick_aware", "wick_aware");
	m_distance_mode_combo = new QComboBox(tab);
	m_distance_mode_combo->addItem("feature_aware", "feature_aware");
	m_distance_mode_combo->addItem("legacy", "legacy");
	m_distance_mode_combo->addItem("strict_composition", "strict_composition");
	m_distance_mode_combo->addItem("wick_aware", "wick_aware");
	m_body_mode_combo = new QComboBox(tab);
	m_body_mode_combo->addItem("signed_body", 0);
	m_body_mode_combo->addItem("magnitude_only", 1);
	m_body_mode_combo->addItem("magnitude_sign_penalty", 2);
	m_w_upper_spin = new QDoubleSpinBox(tab);
	m_w_upper_spin->setRange(0.0, 1000.0);
	m_w_upper_spin->setValue(1.0);
	m_w_lower_spin = new QDoubleSpinBox(tab);
	m_w_lower_spin->setRange(0.0, 1000.0);
	m_w_lower_spin->setValue(1.0);
	m_w_body_spin = new QDoubleSpinBox(tab);
	m_w_body_spin->setRange(0.0, 1000.0);
	m_w_body_spin->setValue(1.0);
	m_w_range_spin = new QDoubleSpinBox(tab);
	m_w_range_spin->setRange(0.0, 1000.0);
	m_w_range_spin->setValue(1.0);
	m_body_sign_penalty_spin = new QDoubleSpinBox(tab);
	m_body_sign_penalty_spin->setRange(0.0, 1000.0);
	m_body_sign_penalty_spin->setValue(2.0);
	m_w_bullcount_spin = new QDoubleSpinBox(tab);
	m_w_bullcount_spin->setRange(0.0, 1000.0);
	m_w_bullcount_spin->setValue(1.0);
	m_w_signchanges_spin = new QDoubleSpinBox(tab);
	m_w_signchanges_spin->setRange(0.0, 1000.0);
	m_w_signchanges_spin->setValue(1.0);
	m_w_runbull_spin = new QDoubleSpinBox(tab);
	m_w_runbull_spin->setRange(0.0, 1000.0);
	m_w_runbull_spin->setValue(1.0);
	m_w_runbear_spin = new QDoubleSpinBox(tab);
	m_w_runbear_spin->setRange(0.0, 1000.0);
	m_w_runbear_spin->setValue(1.0);

	common_grid->addWidget(new QLabel("Dataset ID", m_common_params_box), 0, 0);
	common_grid->addWidget(m_job_dataset_id_spin, 0, 1);
	m_q_label = new QLabel(m_common_params_box);
	common_grid->addWidget(m_q_label, 0, 2);
	common_grid->addWidget(m_q_spin, 0, 3);
	m_delta_label = new QLabel(m_common_params_box);
	common_grid->addWidget(m_delta_spin, 0, 5);
	common_grid->addWidget(m_delta_label, 0, 4);
	m_mirrored_label = new QLabel(m_common_params_box);
	common_grid->addWidget(m_mirrored_label, 0, 6);
	common_grid->addWidget(m_mirrored_spin, 0, 7);

	m_w_upper_label = new QLabel(m_weights_box);
	weights_grid->addWidget(m_w_upper_label, 0, 0);
	weights_grid->addWidget(m_w_upper_spin, 0, 1);
	m_w_lower_label = new QLabel(m_weights_box);
	weights_grid->addWidget(m_w_lower_label, 0, 2);
	weights_grid->addWidget(m_w_lower_spin, 0, 3);
	m_w_body_label = new QLabel(m_weights_box);
	weights_grid->addWidget(m_w_body_label, 0, 4);
	weights_grid->addWidget(m_w_body_spin, 0, 5);
	m_w_range_label = new QLabel(m_weights_box);
	weights_grid->addWidget(m_w_range_label, 0, 6);
	weights_grid->addWidget(m_w_range_spin, 0, 7);

	algorithm_radio_layout->addWidget(threshold_radio);
	algorithm_radio_layout->addWidget(graph_radio);
	algorithm_radio_layout->addWidget(kmed_radio);
	algorithm_radio_layout->addWidget(dbscan_radio);
	algorithm_radio_layout->addStretch(1);
	algorithm_layout->addLayout(algorithm_radio_layout);
	algorithm_layout->addWidget(m_algorithm_intro_label);
	algorithm_layout->addWidget(m_algorithm_formula_label);
	algorithm_layout->addWidget(m_clustering_summary_label);

	m_threshold_params_box = new QGroupBox("Threshold Graph Parameters", tab);
	{
		QGridLayout *threshold_grid = new QGridLayout(m_threshold_params_box);
		threshold_grid->addWidget(new QLabel("Distance family", m_threshold_params_box), 0, 0);
		threshold_grid->addWidget(m_threshold_distance_mode_combo, 0, 1);
		threshold_grid->addWidget(new QLabel("Maximum block mismatch count (theta)", m_threshold_params_box), 0, 2);
		threshold_grid->addWidget(m_theta_spin, 0, 3);
		threshold_grid->addWidget(new QLabel("Threshold mode keeps strict all-pairs membership. Strict composition and wick-aware modes add hard regime gates before mismatch counting.", m_threshold_params_box), 0, 4, 1, 4);
		threshold_grid->addWidget(new QLabel("Max upper-wick sum difference", m_threshold_params_box), 1, 0);
		threshold_grid->addWidget(m_delta_upper_wick_gate_spin, 1, 1);
		threshold_grid->addWidget(new QLabel("Max lower-wick sum difference", m_threshold_params_box), 1, 2);
		threshold_grid->addWidget(m_delta_lower_wick_gate_spin, 1, 3);
		threshold_grid->addWidget(new QLabel("Max lower wick/body ratio diff (%)", m_threshold_params_box), 2, 0);
		threshold_grid->addWidget(m_delta_lower_ratio_gate_spin, 2, 1);
		threshold_grid->addWidget(new QLabel("Max upper wick/body ratio diff (%)", m_threshold_params_box), 2, 2);
		threshold_grid->addWidget(m_delta_upper_ratio_gate_spin, 2, 3);
		threshold_grid->addWidget(new QLabel("Max total wick/body ratio diff (%)", m_threshold_params_box), 2, 4);
		threshold_grid->addWidget(m_delta_total_wick_ratio_gate_spin, 2, 5);
	}

	m_graph_params_box = new QGroupBox("Sparse Graph Parameters", tab);
	{
		QGridLayout *graph_grid = new QGridLayout(m_graph_params_box);
		graph_grid->addWidget(new QLabel("Distance family", m_graph_params_box), 0, 0);
		graph_grid->addWidget(m_distance_mode_combo, 0, 1);
		graph_grid->addWidget(new QLabel("Final graph backend", m_graph_params_box), 0, 2);
		graph_grid->addWidget(m_graph_mode_combo, 0, 3);
		graph_grid->addWidget(new QLabel("Neighbors per block", m_graph_params_box), 1, 0);
		graph_grid->addWidget(m_graph_knn_spin, 1, 1);
		graph_grid->addWidget(new QLabel("Require mutual neighbors", m_graph_params_box), 1, 2);
		graph_grid->addWidget(m_graph_mutual_knn_spin, 1, 3);
		graph_grid->addWidget(new QLabel("Maximum neighbor distance", m_graph_params_box), 1, 4);
		graph_grid->addWidget(m_graph_max_neighbor_distance_spin, 1, 5);
		graph_grid->addWidget(new QLabel("Body comparison mode", m_graph_params_box), 2, 0);
		graph_grid->addWidget(m_body_mode_combo, 2, 1);
		graph_grid->addWidget(new QLabel("Body sign penalty", m_graph_params_box), 2, 2);
		graph_grid->addWidget(m_body_sign_penalty_spin, 2, 3);
		graph_grid->addWidget(new QLabel("Require same dominant direction", m_graph_params_box), 2, 4);
		graph_grid->addWidget(m_require_same_majority_sign_spin, 2, 5);
		graph_grid->addWidget(new QLabel("Use composition penalties", m_graph_params_box), 2, 6);
		graph_grid->addWidget(m_use_composition_spin, 2, 7);
		graph_grid->addWidget(new QLabel("Max net-body difference", m_graph_params_box), 3, 0);
		graph_grid->addWidget(m_delta_body_gate_spin, 3, 1);
		graph_grid->addWidget(new QLabel("Max range difference", m_graph_params_box), 3, 2);
		graph_grid->addWidget(m_delta_range_gate_spin, 3, 3);
		graph_grid->addWidget(new QLabel("Max bullish-count difference", m_graph_params_box), 3, 4);
		graph_grid->addWidget(m_delta_bullcount_gate_spin, 3, 5);
		graph_grid->addWidget(new QLabel("Max sign-change difference", m_graph_params_box), 3, 6);
		graph_grid->addWidget(m_delta_signchange_gate_spin, 3, 7);
		graph_grid->addWidget(new QLabel("Bull-count penalty weight", m_graph_params_box), 4, 0);
		graph_grid->addWidget(m_w_bullcount_spin, 4, 1);
		graph_grid->addWidget(new QLabel("Sign-change penalty weight", m_graph_params_box), 4, 2);
		graph_grid->addWidget(m_w_signchanges_spin, 4, 3);
		graph_grid->addWidget(new QLabel("Bull-run penalty weight", m_graph_params_box), 4, 4);
		graph_grid->addWidget(m_w_runbull_spin, 4, 5);
		graph_grid->addWidget(new QLabel("Bear-run penalty weight", m_graph_params_box), 4, 6);
		graph_grid->addWidget(m_w_runbear_spin, 4, 7);
	}

	m_kmedoids_params_box = new QGroupBox("K-medoids Parameters", tab);
	{
		QGridLayout *kmed_grid = new QGridLayout(m_kmedoids_params_box);
		kmed_grid->addWidget(new QLabel("Number of clusters (k)", m_kmedoids_params_box), 0, 0);
		kmed_grid->addWidget(m_k_spin, 0, 1);
		kmed_grid->addWidget(new QLabel("Refinement iterations", m_kmedoids_params_box), 0, 2);
		kmed_grid->addWidget(m_max_iter_spin, 0, 3);
		kmed_grid->addWidget(new QLabel("Initialization restarts", m_kmedoids_params_box), 0, 4);
		kmed_grid->addWidget(m_restarts_spin, 0, 5);
	}

	m_dbscan_params_box = new QGroupBox("DBSCAN Parameters", tab);
	{
		QGridLayout *dbscan_grid = new QGridLayout(m_dbscan_params_box);
		dbscan_grid->addWidget(new QLabel("Neighborhood radius (eps)", m_dbscan_params_box), 0, 0);
		dbscan_grid->addWidget(m_eps_spin, 0, 1);
		dbscan_grid->addWidget(new QLabel("Minimum neighbors (min_pts)", m_dbscan_params_box), 0, 2);
		dbscan_grid->addWidget(m_min_pts_spin, 0, 3);
	}

	clustering_actions->addWidget(run_clustering_btn);
	clustering_actions->addWidget(refresh_jobs_btn);
	clustering_actions->addStretch(1);

	mining_form->addRow("Dataset ID", m_mining_job_dataset_id_spin);
	mining_form->addRow("Word min length", m_word_min_spin);
	mining_form->addRow("Word max length", m_word_max_spin);
	mining_form->addRow("Phrase min length", m_phrase_min_spin);
	mining_form->addRow("Phrase max length", m_phrase_max_spin);
	mining_actions->addWidget(word_btn);
	mining_actions->addWidget(phrase_btn);
	mining_actions->addStretch(1);

	m_wave_common_box = new QGroupBox("Wave Run Setup", wave_page);
	{
		QFormLayout *wave_common_form = new QFormLayout(m_wave_common_box);
		wave_common_form->addRow("Dataset ID (dataset_id)", m_wave_job_dataset_id_spin);
		wave_common_form->addRow("Model (wave_model)", m_wave_model_combo);
		wave_common_form->addRow("Min token candles (lmin_candles)", m_wave_lmin_spin);
		wave_common_form->addRow("Max token candles (lmax_candles)", m_wave_lmax_spin);
		wave_common_form->addRow("Context window (regime_context_window)", m_wave_context_window_spin);
		wave_common_form->addRow("Min wave move pips (min_wave_move_pips)", m_wave_min_move_spin);
		wave_common_form->addRow("Minimum pips for trend (minimum_pips_for_trend)", m_wave_minimum_trend_pips_spin);
		wave_common_form->addRow("Segment penalty lambda (segment_penalty_lambda)", m_wave_lambda_spin);
		wave_common_form->addRow("Norm scale (norm_scale)", m_wave_norm_scale_spin);
		wave_common_form->addRow("Context range floor (context_range_floor)", m_wave_context_floor_spin);
		wave_common_form->addRow("Epsilon (epsilon)", m_wave_epsilon_spin);
		wave_common_form->addRow("Weak fit threshold (weak_fit_threshold)", m_wave_weak_fit_spin);
		wave_common_form->addRow("Boundary low-conf threshold (boundary_low_conf_threshold)", m_wave_boundary_low_conf_spin);
	}

	m_wave_cleanup_box = new QGroupBox("Legacy And Primitive Cleanup", wave_page);
	{
		QFormLayout *wave_cleanup_form = new QFormLayout(m_wave_cleanup_box);
		wave_cleanup_form->addRow("Trend priority (trend_priority_mode)", m_wave_trend_priority_combo);
		wave_cleanup_form->addRow("Retrace break threshold (retrace_break_threshold)", m_wave_retrace_break_spin);
		wave_cleanup_form->addRow("Trend split gain min (trend_split_gain_min)", m_wave_trend_split_gain_spin);
		wave_cleanup_form->addRow("Compression min duration (compression_min_duration)", m_wave_compression_min_duration_spin);
		wave_cleanup_form->addRow("Expansion min duration (expansion_min_duration)", m_wave_expansion_min_duration_spin);
		wave_cleanup_form->addRow("V reversal min strength (v_reversal_min_trend_strength)", m_wave_v_strength_spin);
		wave_cleanup_form->addRow("V reversal max bridge chop (v_reversal_max_bridge_chop)", m_wave_v_bridge_spin);
		wave_cleanup_form->addRow("V reversal max duration (v_reversal_max_duration)", m_wave_v_duration_spin);
	}

	m_wave_relative_box = new QGroupBox("Relative Waves Parameters", wave_page);
	{
		QFormLayout *wave_relative_form = new QFormLayout(m_wave_relative_box);
		wave_relative_form->addRow("Trend retracement max (relative_trend_retracement_max)", m_wave_relative_trend_retracement_max_spin);
		wave_relative_form->addRow("Strong trend retracement max (relative_strong_trend_retracement_max)", m_wave_relative_strong_trend_retracement_max_spin);
		wave_relative_form->addRow("Trend DER step min (relative_trend_der_step_min)", m_wave_relative_trend_der_step_min_spin);
		wave_relative_form->addRow("Trend DER body min (relative_trend_der_body_min)", m_wave_relative_trend_der_body_min_spin);
		wave_relative_form->addRow("Trend body efficiency min (relative_trend_body_eff_min)", m_wave_relative_trend_body_eff_min_spin);
		wave_relative_form->addRow("Trend overlap max (relative_trend_overlap_max)", m_wave_relative_trend_overlap_max_spin);
		wave_relative_form->addRow("Trend sign-flip max (relative_trend_sign_flip_max)", m_wave_relative_trend_sign_flip_max_spin);
		wave_relative_form->addRow("Trend wick bias min (relative_trend_wick_bias_min)", m_wave_relative_trend_wick_bias_min_spin);
		wave_relative_form->addRow("Range DER step max (relative_range_der_step_max)", m_wave_relative_range_der_step_max_spin);
		wave_relative_form->addRow("Range body efficiency max (relative_range_body_eff_max)", m_wave_relative_range_body_eff_max_spin);
		wave_relative_form->addRow("Range overlap min (relative_range_overlap_min)", m_wave_relative_range_overlap_min_spin);
		wave_relative_form->addRow("Range sign-flip min (relative_range_sign_flip_min)", m_wave_relative_range_sign_flip_min_spin);
	}

	wave_run_layout->addWidget(wave_btn);
	wave_run_layout->addStretch(1);

	m_jobs_table = new QTableWidget(tab);
	set_table_headers(m_jobs_table, {"job_id", "dataset_id", "job_type", "status", "progress", "error"});
	job_tabs->addTab(clustering_page, "Clustering");
	job_tabs->addTab(mining_page, "Mining");
	job_tabs->addTab(wave_page, "Waves");
	job_tabs->addTab(queue_page, "Queue");

	clustering_layout->addWidget(m_common_params_box);
	clustering_layout->addWidget(m_weights_box);
	clustering_layout->addWidget(algorithm_box);
	clustering_layout->addWidget(m_threshold_params_box);
	clustering_layout->addWidget(m_graph_params_box);
	clustering_layout->addWidget(m_kmedoids_params_box);
	clustering_layout->addWidget(m_dbscan_params_box);
	clustering_layout->addLayout(clustering_actions);
	clustering_layout->addStretch(1);

	{
		QGroupBox *mining_box = new QGroupBox("Mining Jobs", mining_page);
		QVBoxLayout *mining_box_layout = new QVBoxLayout(mining_box);
		mining_box_layout->addLayout(mining_form);
		mining_box_layout->addLayout(mining_actions);
		mining_layout->addWidget(mining_box);
		mining_layout->addStretch(1);
	}

	wave_layout->addWidget(m_wave_common_box);
	wave_layout->addWidget(m_wave_relative_box);
	wave_layout->addWidget(m_wave_cleanup_box);
	wave_layout->addLayout(wave_run_layout);
	wave_layout->addStretch(1);

	{
		QHBoxLayout *queue_actions = new QHBoxLayout();
		queue_actions->addWidget(refresh_jobs_btn);
		queue_actions->addWidget(delete_job_btn);
		queue_actions->addStretch(1);
		queue_layout->addLayout(queue_actions);
	}
	queue_layout->addWidget(m_jobs_table, 1);

	v->addWidget(job_tabs, 1);

	connect(m_clustering_algorithm_group, &QButtonGroup::idClicked, this, &main_window::on_clustering_algorithm_changed);
	connect(m_graph_mode_combo, &QComboBox::currentTextChanged, this, &main_window::on_clustering_algorithm_changed);
	connect(m_threshold_distance_mode_combo, &QComboBox::currentTextChanged, this, &main_window::on_clustering_algorithm_changed);
	connect(m_theta_spin, qOverload<int>(&QSpinBox::valueChanged), this, &main_window::on_clustering_algorithm_changed);
	connect(m_wave_model_combo, &QComboBox::currentTextChanged, this, &main_window::update_wave_job_ui);
	connect(run_clustering_btn, &QPushButton::clicked, this, &main_window::on_submit_clustering_job);
	connect(word_btn, &QPushButton::clicked, this, &main_window::on_submit_word_job);
	connect(phrase_btn, &QPushButton::clicked, this, &main_window::on_submit_phrase_job);
	connect(wave_btn, &QPushButton::clicked, this, &main_window::on_submit_wave_job);
	connect(refresh_jobs_btn, &QPushButton::clicked, this, &main_window::on_refresh_jobs);
	connect(delete_job_btn, &QPushButton::clicked, this, &main_window::on_delete_selected_job);
	update_clustering_job_ui();
	update_wave_job_ui();

	return tab;
}

QWidget *main_window::build_patterns_tab()
{
	QWidget *tab = new QWidget(this);
	QVBoxLayout *v = new QVBoxLayout(tab);
	QHBoxLayout *ctl = new QHBoxLayout();
	QPushButton *clusters_btn = new QPushButton("Load Clusters", tab);
	QPushButton *members_btn = new QPushButton("Load Members", tab);
	QPushButton *words_btn = new QPushButton("Load Words", tab);
	QPushButton *phrases_btn = new QPushButton("Load Phrases", tab);
	QHBoxLayout *view_mode_layout = new QHBoxLayout();

	m_patterns_dataset_id_spin = new QSpinBox(tab);
	m_patterns_dataset_id_spin->setRange(1, 1000000);
	m_cluster_member_start_spin = new QSpinBox(tab);
	m_cluster_member_start_spin->setRange(0, 1000000);
	m_cluster_member_count_spin = new QSpinBox(tab);
	m_cluster_member_count_spin->setRange(1, 256);
	m_cluster_member_count_spin->setValue(128);

	ctl->addWidget(new QLabel("dataset_id", tab));
	ctl->addWidget(m_patterns_dataset_id_spin);
	ctl->addWidget(new QLabel("member_start", tab));
	ctl->addWidget(m_cluster_member_start_spin);
	ctl->addWidget(new QLabel("member_count", tab));
	ctl->addWidget(m_cluster_member_count_spin);
	ctl->addWidget(clusters_btn);
	ctl->addWidget(members_btn);
	ctl->addWidget(words_btn);
	ctl->addWidget(phrases_btn);
	ctl->addStretch(1);

	m_clusters_table = new QTableWidget(tab);
	set_table_headers(m_clusters_table, {"cluster_id", "size", "representative_index", "score"});
	m_cluster_members_table = new QTableWidget(tab);
	set_table_headers(m_cluster_members_table, {"member_index", "block_index", "start_candle", "end_candle", "start_ts", "end_ts"});
	m_words_table = new QTableWidget(tab);
	set_table_headers(m_words_table, {"word_id", "length_chars", "frequency"});
	m_words_table->setMinimumHeight(120);
	m_words_table->setMaximumHeight(180);
	m_phrases_table = new QTableWidget(tab);
	set_table_headers(m_phrases_table, {"phrase_id", "token_len", "frequency", "avg_duration_chars", "avg_duration_candles"});
	m_phrases_table->setMinimumHeight(120);
	m_phrases_table->setMaximumHeight(180);
	m_cluster_chart = new cluster_chart_widget(tab);
	m_individual_view_radio = new QRadioButton("Individual Block View", tab);
	m_group_view_radio = new QRadioButton("Group Block View", tab);
	m_individual_view_radio->setChecked(true);
	m_group_prev_page_btn = new QPushButton("Previous Page", tab);
	m_group_next_page_btn = new QPushButton("Next Page", tab);
	m_pattern_view_stack = new QStackedWidget(tab);
	m_group_page_start = 0;
	m_group_total_members = 0;
	m_cluster_feature_text = new QTextEdit(tab);
	m_cluster_feature_text->setReadOnly(true);
	m_cluster_feature_text->setMinimumHeight(150);
	m_cluster_feature_text->setMinimumWidth(260);
	m_cluster_feature_text->setMaximumWidth(360);
	m_cluster_feature_table = new QTableWidget(tab);
	set_table_headers(m_cluster_feature_table, {"offset", "upper", "lower", "body", "range", "abs_body"});
	m_cluster_feature_table->setMinimumWidth(260);
	m_cluster_feature_table->setMaximumWidth(360);

	QSplitter *top_split = new QSplitter(Qt::Horizontal, tab);
	QSplitter *left_split = new QSplitter(Qt::Vertical, top_split);
	QSplitter *bottom_split = new QSplitter(Qt::Horizontal, tab);
	QWidget *feature_panel = new QWidget(tab);
	QVBoxLayout *feature_layout = new QVBoxLayout(feature_panel);
	QWidget *individual_view_page = new QWidget(tab);
	QHBoxLayout *individual_view_layout = new QHBoxLayout(individual_view_page);
	QWidget *group_view_page = new QWidget(tab);
	QVBoxLayout *group_view_page_layout = new QVBoxLayout(group_view_page);
	QHBoxLayout *group_paging_layout = new QHBoxLayout();
	QGridLayout *group_view_layout = new QGridLayout(group_view_page);

	view_mode_layout->addWidget(m_individual_view_radio);
	view_mode_layout->addWidget(m_group_view_radio);
	view_mode_layout->addStretch(1);

	left_split->addWidget(m_clusters_table);
	left_split->addWidget(m_cluster_members_table);
	left_split->setStretchFactor(0, 1);
	left_split->setStretchFactor(1, 2);
	feature_layout->addWidget(new QLabel("Selected Block Features", feature_panel));
	feature_layout->addWidget(m_cluster_feature_text);
	feature_layout->addWidget(m_cluster_feature_table, 1);
	feature_layout->setContentsMargins(0, 0, 0, 0);
	feature_panel->setMinimumWidth(280);
	feature_panel->setMaximumWidth(380);
	individual_view_layout->setContentsMargins(0, 0, 0, 0);
	individual_view_layout->addWidget(feature_panel);
	individual_view_layout->addWidget(m_cluster_chart, 1);
	group_paging_layout->addWidget(m_group_prev_page_btn);
	group_paging_layout->addWidget(m_group_next_page_btn);
	group_paging_layout->addStretch(1);
	for (int i = 0; i < 18; ++i) {
		cluster_chart_widget *chart = new cluster_chart_widget(group_view_page);

		chart->set_interactive(false);
		chart->set_fixed_window_count(40);
		chart->set_dim_non_focus(true);
		chart->set_footer_visible(false);
		chart->setMinimumHeight(120);
		m_group_charts.push_back(chart);
		group_view_layout->addWidget(chart, i / 6, i % 6);
		connect(chart, &cluster_chart_widget::chart_clicked, this, &main_window::on_group_chart_clicked);
	}
	group_view_page_layout->setContentsMargins(0, 0, 0, 0);
	group_view_page_layout->addLayout(group_paging_layout);
	group_view_page_layout->addLayout(group_view_layout, 1);
	group_view_layout->setContentsMargins(0, 0, 0, 0);
	group_view_layout->setHorizontalSpacing(6);
	group_view_layout->setVerticalSpacing(6);
	m_pattern_view_stack->addWidget(individual_view_page);
	m_pattern_view_stack->addWidget(group_view_page);
	top_split->addWidget(left_split);
	top_split->addWidget(m_pattern_view_stack);
	top_split->setStretchFactor(0, 1);
	top_split->setStretchFactor(1, 3);

	bottom_split->addWidget(m_words_table);
	bottom_split->addWidget(m_phrases_table);
	bottom_split->setStretchFactor(0, 1);
	bottom_split->setStretchFactor(1, 1);

	v->addLayout(ctl);
	v->addLayout(view_mode_layout);
	v->addWidget(top_split, 4);
	v->addWidget(bottom_split, 1);

	connect(clusters_btn, &QPushButton::clicked, this, &main_window::on_load_clusters);
	connect(members_btn, &QPushButton::clicked, this, &main_window::on_load_cluster_members);
	connect(words_btn, &QPushButton::clicked, this, &main_window::on_load_words);
	connect(phrases_btn, &QPushButton::clicked, this, &main_window::on_load_phrases);
	connect(m_clusters_table, &QTableWidget::itemSelectionChanged, this, &main_window::on_cluster_row_changed);
	connect(m_cluster_members_table, &QTableWidget::itemSelectionChanged, this, &main_window::on_cluster_member_row_changed);
	connect(m_individual_view_radio, &QRadioButton::toggled, this, &main_window::on_view_mode_changed);
	connect(m_group_view_radio, &QRadioButton::toggled, this, &main_window::on_view_mode_changed);
	connect(m_group_prev_page_btn, &QPushButton::clicked, this, &main_window::on_group_prev_page);
	connect(m_group_next_page_btn, &QPushButton::clicked, this, &main_window::on_group_next_page);

	return tab;
}

QWidget *main_window::build_waves_tab()
{
	QWidget *tab = new QWidget(this);
	QVBoxLayout *v = new QVBoxLayout(tab);
	QHBoxLayout *ctl = new QHBoxLayout();
	QSplitter *main_split = new QSplitter(Qt::Vertical, tab);
	QSplitter *split = new QSplitter(Qt::Horizontal, tab);
	QWidget *chart_panel = new QWidget(tab);
	QVBoxLayout *chart_layout = new QVBoxLayout(chart_panel);
	QPushButton *load_btn = new QPushButton("Load Waves", tab);
	QPushButton *load_lowest_err_btn = new QPushButton("Load Lowest Err", tab);

	m_wave_dataset_id_spin = new QSpinBox(tab);
	m_wave_dataset_id_spin->setRange(1, 1000000);
	m_wave_start_spin = new QSpinBox(tab);
	m_wave_start_spin->setRange(0, 1000000);
	m_wave_count_spin = new QSpinBox(tab);
	m_wave_count_spin->setRange(1, 1024);
	m_wave_count_spin->setValue(64);
	m_wave_filter_combo = new QComboBox(tab);
	m_wave_model_name = "legacy_waves";
	m_wave_main_splitter = main_split;
	m_wave_content_splitter = split;
	update_wave_filter_options();
	m_waves_table = new QTableWidget(tab);
	update_wave_table_headers();
	m_waves_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
	m_waves_table->horizontalHeader()->setStretchLastSection(false);
	m_waves_table->horizontalHeader()->setMinimumSectionSize(24);
	m_waves_table->resizeColumnsToContents();
	m_wave_chart = new cluster_chart_widget(tab);
	m_wave_chart_status_label = new QLabel("Candle index: -", tab);
	m_wave_show_seed_check = new QCheckBox("Show Seed", tab);
	m_wave_show_seed_check->setChecked(true);
	m_wave_property_visibility_btn = new QPushButton("Visible Properties", tab);
	m_wave_penalty_trace_btn = new QPushButton("Show Penalty Calculation", tab);
	m_wave_overlay_property_combo = new QComboBox(tab);
	m_wave_overlay_property_combo->addItem("");
	m_wave_overlay_property_combo->setMinimumContentsLength(24);
	m_wave_overlay_property_combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	m_wave_info_tabs = new QTabWidget(tab);
	m_wave_info_tabs->setMinimumHeight(120);
	m_wave_summary_text = new QTextEdit(tab);
	m_wave_summary_text->setReadOnly(true);
	m_wave_token_props_table = new QTableWidget(tab);
	m_wave_layer1_table = new QTableWidget(tab);
	set_table_headers(m_wave_token_props_table, {"Property", "Value", "Property", "Value", "Property", "Value", "Property", "Value"});
	m_wave_token_props_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
	m_wave_token_props_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
	m_wave_token_props_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
	m_wave_token_props_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
	m_wave_token_props_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
	m_wave_token_props_table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
	m_wave_token_props_table->horizontalHeader()->setSectionResizeMode(6, QHeaderView::ResizeToContents);
	m_wave_token_props_table->horizontalHeader()->setSectionResizeMode(7, QHeaderView::Stretch);
	m_wave_token_props_table->verticalHeader()->setVisible(false);
	m_wave_token_props_table->horizontalHeader()->setVisible(false);
	m_wave_token_props_table->setSelectionBehavior(QAbstractItemView::SelectItems);
	set_table_headers(m_wave_layer1_table, {"child", "start", "end", "type", "dir", "duration", "net_move", "cons_ratio", "alt_ratio", "alt_pairs"});
	m_wave_layer1_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
	m_wave_layer1_table->horizontalHeader()->setStretchLastSection(false);
	m_wave_layer1_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_wave_layer1_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_wave_info_tabs->addTab(m_wave_summary_text, "Dataset Statistics");
	m_wave_info_tabs->addTab(m_wave_token_props_table, "Token Properties");
	{
		QWidget *layer1_tab = new QWidget(tab);
		QVBoxLayout *layer1_layout = new QVBoxLayout(layer1_tab);
		QHBoxLayout *layer1_actions = new QHBoxLayout();

		layer1_layout->setContentsMargins(0, 0, 0, 0);
		layer1_actions->addWidget(m_wave_penalty_trace_btn);
		layer1_actions->addStretch(1);
		layer1_layout->addLayout(layer1_actions);
		layer1_layout->addWidget(m_wave_layer1_table, 1);
		m_wave_info_tabs->addTab(layer1_tab, "Layer 1 Strings");
	}
	m_waves_table->setMinimumHeight(120);
	m_wave_chart->setMinimumHeight(120);
	chart_layout->setContentsMargins(0, 0, 0, 0);
	chart_layout->setSpacing(2);
	chart_layout->addWidget(m_wave_chart, 1);
	chart_layout->addWidget(m_wave_chart_status_label, 0);

	ctl->addWidget(new QLabel("dataset_id", tab));
	ctl->addWidget(m_wave_dataset_id_spin);
	ctl->addWidget(new QLabel("start", tab));
	ctl->addWidget(m_wave_start_spin);
	ctl->addWidget(new QLabel("count", tab));
	ctl->addWidget(m_wave_count_spin);
	ctl->addWidget(new QLabel("class", tab));
	ctl->addWidget(m_wave_filter_combo);
	ctl->addWidget(load_btn);
	ctl->addWidget(load_lowest_err_btn);
	ctl->addWidget(m_wave_property_visibility_btn);
	ctl->addWidget(new QLabel("Chart Overlay", tab));
	ctl->addWidget(m_wave_overlay_property_combo);
	ctl->addWidget(m_wave_show_seed_check);
	ctl->addStretch(1);

	split->addWidget(m_waves_table);
	split->addWidget(chart_panel);
	split->setChildrenCollapsible(false);
	split->setStretchFactor(0, 2);
	split->setStretchFactor(1, 3);
	main_split->addWidget(m_wave_info_tabs);
	main_split->addWidget(split);
	main_split->setChildrenCollapsible(false);
	main_split->setStretchFactor(0, 1);
	main_split->setStretchFactor(1, 3);
	{
		QSettings settings(gui_settings_path(), QSettings::IniFormat);
		QByteArray main_state = settings.value("waves/main_splitter_state").toByteArray();
		QByteArray content_state = settings.value("waves/content_splitter_state").toByteArray();

		if (!content_state.isEmpty()) {
			split->restoreState(content_state);
		}
		if (!main_state.isEmpty()) {
			main_split->restoreState(main_state);
		}
	}

	v->addLayout(ctl);
	v->addWidget(main_split, 1);

	connect(load_btn, &QPushButton::clicked, this, &main_window::on_load_waves);
	connect(load_lowest_err_btn, &QPushButton::clicked, this, &main_window::on_load_waves_lowest_err);
	connect(m_wave_show_seed_check, &QCheckBox::toggled, this, [this](bool) {
		on_wave_row_changed();
	});
	connect(m_wave_property_visibility_btn, &QPushButton::clicked, this, &main_window::on_configure_wave_properties);
	connect(m_wave_penalty_trace_btn, &QPushButton::clicked, this, &main_window::on_show_wave_penalty_calculation);
	connect(m_wave_overlay_property_combo, &QComboBox::currentTextChanged, this, [this](const QString &) {
		on_wave_row_changed();
	});
	connect(m_wave_chart, &cluster_chart_widget::candle_hovered, this, [this](int candle_index) {
		if (m_wave_chart_status_label == nullptr) {
			return;
		}
		if (candle_index >= 0) {
			m_wave_chart_status_label->setText(QString("Candle index: %1").arg(candle_index));
		} else {
			m_wave_chart_status_label->setText("Candle index: -");
		}
	});
	connect(main_split, &QSplitter::splitterMoved, this, [this](int, int) {
		save_wave_splitter_state();
	});
	connect(split, &QSplitter::splitterMoved, this, [this](int, int) {
		save_wave_splitter_state();
	});
	connect(m_wave_filter_combo, &QComboBox::currentTextChanged, this, &main_window::on_wave_filter_changed);
	connect(m_waves_table, &QTableWidget::itemSelectionChanged, this, &main_window::on_wave_row_changed);
	connect(m_wave_token_props_table, &QTableWidget::itemDoubleClicked, this, &main_window::on_wave_property_item_double_clicked);
	return tab;
}

QWidget *main_window::build_predictor_tab()
{
	QWidget *tab = new QWidget(this);
	QVBoxLayout *v = new QVBoxLayout(tab);
	QHBoxLayout *ctl = new QHBoxLayout();
	QSplitter *main_split = new QSplitter(Qt::Horizontal, tab);
	QWidget *left_panel = new QWidget(tab);
	QVBoxLayout *left_layout = new QVBoxLayout(left_panel);
	QSplitter *left_split = new QSplitter(Qt::Vertical, left_panel);
	QWidget *charts_panel = new QWidget(tab);
	QVBoxLayout *charts_layout = new QVBoxLayout(charts_panel);
	QSplitter *charts_split = new QSplitter(Qt::Vertical, charts_panel);
	QWidget *predicted_panel = new QWidget(charts_panel);
	QVBoxLayout *predicted_layout = new QVBoxLayout(predicted_panel);
	QWidget *actual_panel = new QWidget(charts_panel);
	QVBoxLayout *actual_layout = new QVBoxLayout(actual_panel);
	QWidget *horizons_panel = new QWidget(tab);
	QVBoxLayout *horizons_layout = new QVBoxLayout(horizons_panel);
	QPushButton *load_btn = new QPushButton("Load Predictor Waves", tab);
	QPushButton *base_probs_btn = new QPushButton("Base Probabilities", tab);

	m_predictor_dataset_id_spin = new QSpinBox(tab);
	m_predictor_dataset_id_spin->setRange(1, 1000000);
	m_predictor_start_spin = new QSpinBox(tab);
	m_predictor_start_spin->setRange(0, 1000000);
	m_predictor_count_spin = new QSpinBox(tab);
	m_predictor_count_spin->setRange(1, 1024);
	m_predictor_count_spin->setValue(96);
	m_predictor_input_len_spin = new QSpinBox(tab);
	m_predictor_input_len_spin->setRange(1, 8);
	m_predictor_input_len_spin->setValue(8);
	m_predictor_horizon_spin = new QSpinBox(tab);
	m_predictor_horizon_spin->setRange(1, 8);
	m_predictor_horizon_spin->setValue(3);
	m_predictor_waves_table = new QTableWidget(tab);
	set_table_headers(m_predictor_waves_table, {"row", "start", "end", "class", "dir", "net_move", "duration"});
	m_predictor_waves_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
	m_predictor_waves_table->horizontalHeader()->setStretchLastSection(false);
	m_predictor_waves_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_predictor_waves_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_predictor_horizons_table = new QTableWidget(tab);
	set_table_headers(m_predictor_horizons_table, {"h", "actual", "pred_class", "p(class)", "pred_dir", "p(dir)", "move", "exp_move", "support"});
	m_predictor_horizons_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
	m_predictor_horizons_table->horizontalHeader()->setStretchLastSection(false);
	m_predictor_horizons_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_predictor_horizons_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_predictor_chart = new cluster_chart_widget(tab);
	m_predictor_actual_chart = new cluster_chart_widget(tab);
	m_predictor_chart_status_label = new QLabel("Predicted chart candle index: -", tab);
	m_predictor_actual_status_label = new QLabel("Actual chart candle index: -", tab);
	m_predictor_summary_text = new QTextEdit(tab);
	m_predictor_signal_text = new QTextEdit(tab);
	m_predictor_summary_text->setReadOnly(true);
	m_predictor_signal_text->setReadOnly(true);
	m_predictor_chart->setMinimumHeight(260);
	m_predictor_actual_chart->setMinimumHeight(260);
	predicted_layout->setContentsMargins(0, 0, 0, 0);
	actual_layout->setContentsMargins(0, 0, 0, 0);
	predicted_layout->setSpacing(2);
	actual_layout->setSpacing(2);
	predicted_layout->addWidget(new QLabel("Predicted Future Window", tab));
	predicted_layout->addWidget(m_predictor_chart, 1);
	predicted_layout->addWidget(m_predictor_chart_status_label, 0);
	actual_layout->addWidget(new QLabel("Anchor Wave Window", tab));
	actual_layout->addWidget(m_predictor_actual_chart, 1);
	actual_layout->addWidget(m_predictor_actual_status_label, 0);
	charts_split->addWidget(predicted_panel);
	charts_split->addWidget(actual_panel);
	charts_split->setChildrenCollapsible(false);
	charts_split->setStretchFactor(0, 1);
	charts_split->setStretchFactor(1, 1);
	charts_layout->setContentsMargins(0, 0, 0, 0);
	charts_layout->addWidget(charts_split, 1);

	{
		QWidget *summary_panel = new QWidget(left_panel);
		QVBoxLayout *summary_layout = new QVBoxLayout(summary_panel);

		summary_layout->setContentsMargins(0, 0, 0, 0);
		summary_layout->addWidget(new QLabel("Prediction Summary", tab));
		summary_layout->addWidget(m_predictor_summary_text, 1);
		summary_layout->addWidget(new QLabel("Strongest Signals", tab));
		summary_layout->addWidget(m_predictor_signal_text, 1);
		left_split->addWidget(m_predictor_waves_table);
		left_split->addWidget(horizons_panel);
		left_split->addWidget(summary_panel);
		left_split->setChildrenCollapsible(false);
		left_split->setStretchFactor(0, 1);
		left_split->setStretchFactor(1, 1);
		left_split->setStretchFactor(2, 1);
		left_layout->setContentsMargins(0, 0, 0, 0);
		left_layout->addWidget(left_split, 1);
	}

	horizons_layout->setContentsMargins(0, 0, 0, 0);
	horizons_layout->addWidget(new QLabel("Horizon Predictions", tab));
	horizons_layout->addWidget(m_predictor_horizons_table, 1);

	ctl->addWidget(new QLabel("dataset_id", tab));
	ctl->addWidget(m_predictor_dataset_id_spin);
	ctl->addWidget(new QLabel("start", tab));
	ctl->addWidget(m_predictor_start_spin);
	ctl->addWidget(new QLabel("count", tab));
	ctl->addWidget(m_predictor_count_spin);
	ctl->addWidget(new QLabel("input_len", tab));
	ctl->addWidget(m_predictor_input_len_spin);
	ctl->addWidget(new QLabel("horizon", tab));
	ctl->addWidget(m_predictor_horizon_spin);
	ctl->addWidget(load_btn);
	ctl->addWidget(base_probs_btn);
	ctl->addStretch(1);

	main_split->addWidget(left_panel);
	main_split->addWidget(charts_panel);
	main_split->setChildrenCollapsible(false);
	main_split->setStretchFactor(0, 2);
	main_split->setStretchFactor(1, 5);
	v->addLayout(ctl);
	v->addWidget(main_split, 1);

	connect(load_btn, &QPushButton::clicked, this, &main_window::on_load_predictor_waves);
	connect(base_probs_btn, &QPushButton::clicked, this, &main_window::on_show_predictor_base_probabilities);
	connect(m_predictor_waves_table, &QTableWidget::itemSelectionChanged, this, &main_window::on_predictor_row_changed);
	connect(m_predictor_horizons_table, &QTableWidget::itemDoubleClicked, this, &main_window::on_predictor_horizon_item_double_clicked);
	connect(m_predictor_input_len_spin, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) {
		update_predictor_view();
	});
	connect(m_predictor_horizon_spin, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) {
		update_predictor_view();
	});
	connect(m_predictor_chart, &cluster_chart_widget::candle_hovered, this, [this](int candle_index) {
		if (m_predictor_chart_status_label == nullptr) {
			return;
		}
		if (candle_index >= 0) {
			m_predictor_chart_status_label->setText(QString("Predicted chart candle index: %1").arg(candle_index));
		} else {
			m_predictor_chart_status_label->setText("Predicted chart candle index: -");
		}
	});
	connect(m_predictor_actual_chart, &cluster_chart_widget::candle_hovered, this, [this](int candle_index) {
		if (m_predictor_actual_status_label == nullptr) {
			return;
		}
		if (candle_index >= 0) {
			m_predictor_actual_status_label->setText(QString("Actual chart candle index: %1").arg(candle_index));
		} else {
			m_predictor_actual_status_label->setText("Actual chart candle index: -");
		}
	});
	return tab;
}

QWidget *main_window::build_live_tab()
{
	QWidget *tab = new QWidget(this);
	QVBoxLayout *layout = new QVBoxLayout(tab);
	QHBoxLayout *controls = new QHBoxLayout();
	QSplitter *main_split = new QSplitter(Qt::Horizontal, tab);
	QWidget *left_panel = new QWidget(tab);
	QVBoxLayout *left_layout = new QVBoxLayout(left_panel);
	QSplitter *left_split = new QSplitter(Qt::Vertical, left_panel);
	QWidget *chart_panel = new QWidget(tab);
	QVBoxLayout *chart_layout = new QVBoxLayout(chart_panel);

	m_live_stream_combo = new QComboBox(tab);
	m_live_candle_count_spin = new QSpinBox(tab);
	m_live_wave_count_spin = new QSpinBox(tab);
	m_live_auto_refresh_check = new QCheckBox("auto refresh", tab);
	m_live_refresh_btn = new QPushButton("Refresh Live", tab);
	m_live_recompute_btn = new QPushButton("Recompute Waves", tab);
	m_live_reset_btn = new QPushButton("Reset Stream", tab);
	m_live_property_visibility_btn = new QPushButton("Property Visibility", tab);
	m_live_waves_table = new QTableWidget(tab);
	m_live_info_tabs = new QTabWidget(tab);
	m_live_properties_table = new QTableWidget(tab);
	m_live_composition_table = new QTableWidget(tab);
	m_live_chart = new cluster_chart_widget(tab);
	m_live_status_label = new QLabel("Live stream: -", tab);
	m_live_chart_status_label = new QLabel("Live chart candle index: -", tab);
	m_live_summary_text = new QTextEdit(tab);

	m_live_candle_count_spin->setRange(50, 5000);
	m_live_candle_count_spin->setValue(100);
	m_live_wave_count_spin->setRange(10, 1000);
	m_live_wave_count_spin->setValue(128);
	m_live_auto_refresh_check->setChecked(true);
	m_live_summary_text->setReadOnly(true);
	m_live_chart->setMinimumHeight(500);
	set_table_headers(m_live_waves_table, {"token", "start", "end", "class", "dir", "net_move", "duration", "fit_error"});
	m_live_waves_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
	m_live_waves_table->horizontalHeader()->setStretchLastSection(false);
	m_live_waves_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_live_waves_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	set_table_headers(m_live_properties_table, {"property", "value", ""});
	m_live_properties_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
	m_live_properties_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
	m_live_properties_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
	m_live_properties_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	set_table_headers(m_live_composition_table, {"child", "start", "end", "type", "dir", "duration", "net_move", "cons_ratio", "alt_ratio", "alt_pairs"});
	m_live_composition_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
	m_live_composition_table->horizontalHeader()->setStretchLastSection(false);
	m_live_composition_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_live_composition_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_live_info_tabs->addTab(m_live_properties_table, "Properties");
	m_live_info_tabs->addTab(m_live_composition_table, "Composition");

	controls->addWidget(new QLabel("stream", tab));
	controls->addWidget(m_live_stream_combo, 1);
	controls->addWidget(new QLabel("candles", tab));
	controls->addWidget(m_live_candle_count_spin);
	controls->addWidget(new QLabel("waves", tab));
	controls->addWidget(m_live_wave_count_spin);
	controls->addWidget(m_live_auto_refresh_check);
	controls->addWidget(m_live_refresh_btn);
	controls->addWidget(m_live_recompute_btn);
	controls->addWidget(m_live_reset_btn);
	controls->addWidget(m_live_property_visibility_btn);

	left_layout->setContentsMargins(0, 0, 0, 0);
	left_split->addWidget(m_live_waves_table);
	left_split->addWidget(m_live_info_tabs);
	left_split->setChildrenCollapsible(false);
	left_split->setStretchFactor(0, 3);
	left_split->setStretchFactor(1, 2);
	left_layout->addWidget(left_split, 1);
	left_layout->addWidget(m_live_status_label);
	left_layout->addWidget(new QLabel("Live Stream Summary", tab));
	left_layout->addWidget(m_live_summary_text, 0);

	chart_layout->setContentsMargins(0, 0, 0, 0);
	chart_layout->addWidget(new QLabel("Last Closed Candles", tab));
	chart_layout->addWidget(m_live_chart, 1);
	chart_layout->addWidget(m_live_chart_status_label, 0);

	main_split->addWidget(left_panel);
	main_split->addWidget(chart_panel);
	main_split->setChildrenCollapsible(false);
	main_split->setStretchFactor(0, 2);
	main_split->setStretchFactor(1, 5);
	layout->addLayout(controls);
	layout->addWidget(main_split, 1);

	connect(m_live_refresh_btn, &QPushButton::clicked, this, &main_window::on_refresh_live);
	connect(m_live_recompute_btn, &QPushButton::clicked, this, &main_window::on_recompute_live);
	connect(m_live_reset_btn, &QPushButton::clicked, this, &main_window::on_reset_live);
	connect(m_live_property_visibility_btn, &QPushButton::clicked, this, &main_window::on_configure_live_properties);
	connect(m_live_stream_combo, qOverload<int>(&QComboBox::currentIndexChanged), this, &main_window::on_live_stream_changed);
	connect(m_live_waves_table, &QTableWidget::itemSelectionChanged, this, &main_window::on_live_wave_row_changed);
	connect(m_live_auto_refresh_check, &QCheckBox::toggled, this, [this](bool enabled) {
		if (m_live_refresh_timer == nullptr) {
			return;
		}
		if (enabled) {
			m_live_refresh_timer->start();
		} else {
			m_live_refresh_timer->stop();
		}
	});
	connect(m_live_chart, &cluster_chart_widget::candle_hovered, this, [this](int candle_index) {
		if (m_live_chart_status_label == nullptr) {
			return;
		}
		if (candle_index >= 0) {
			m_live_chart_status_label->setText(QString("Live chart candle index: %1").arg(candle_index));
		} else {
			m_live_chart_status_label->setText("Live chart candle index: -");
		}
	});
	return tab;
}

void main_window::on_show_predictor_base_probabilities()
{
	QJsonDocument doc;
	QString err;
	int dataset_id = m_predictor_dataset_id_spin->value();
	QDialog dialog(this);
	QVBoxLayout *layout = new QVBoxLayout(&dialog);
	QTextEdit *text = new QTextEdit(&dialog);
	QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);

	m_api.set_base_url(current_base_url());
	if (!m_api.get(QString("/datasets/%1/wave_predictor_base_probs").arg(dataset_id), &doc, &err)) {
		append_log(QString("predictor base probabilities load failed: %1").arg(err));
		QMessageBox::warning(this, "Predictor", "Base probabilities load failed: " + err);
		return;
	}

	dialog.setWindowTitle(QString("Predictor Base Probabilities - dataset %1").arg(dataset_id));
	dialog.resize(920, 700);
	text->setReadOnly(true);
	text->setPlainText(QString::fromUtf8(doc.toJson(QJsonDocument::Indented)));
	layout->addWidget(text, 1);
	layout->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	dialog.exec();
}

void main_window::on_predictor_horizon_item_double_clicked(QTableWidgetItem *item)
{
	QJsonArray horizons = m_predictor_doc_obj.value("horizons").toArray();
	QJsonObject horizon_obj;
	QJsonObject trace_obj;
	QDialog dialog(this);
	QVBoxLayout *layout;
	QLabel *info_label;
	QTabWidget *tabs;
	QDialogButtonBox *buttons;
	struct target_spec {
		const char *key;
		const char *label;
	} specs[] = {
		{"class", "Class"},
		{"direction", "Direction"},
		{"move", "Net Move"}
	};

	if (item == nullptr || item->row() < 0 || item->row() >= horizons.size()) {
		return;
	}
	horizon_obj = horizons.at(item->row()).toObject();
	trace_obj = horizon_obj.value("trace").toObject();
	if (trace_obj.isEmpty()) {
		QMessageBox::warning(this, "Predictor Trace", "Trace data is not present in this predictor response.");
		return;
	}
	dialog.setWindowTitle(QString("Predictor Trace: horizon %1").arg(horizon_obj.value("horizon").toInt()));
	dialog.resize(1200, 760);
	layout = new QVBoxLayout(&dialog);
	info_label = new QLabel("Click a row to highlight the direct source rows used to calculate it.", &dialog);
	tabs = new QTabWidget(&dialog);
	buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
	layout->addWidget(info_label);
	for (const target_spec &spec : specs) {
		QJsonObject target_trace = trace_obj.value(spec.key).toObject();
		QWidget *tab = new QWidget(&dialog);
		QVBoxLayout *tab_layout = new QVBoxLayout(tab);
		QLabel *summary_label;
		QTableWidget *table;
		QVector<predictor_trace_row> rows;

		if (target_trace.isEmpty()) {
			continue;
		}
		rows = predictor_build_trace_rows(target_trace);
		summary_label = new QLabel(
			QString("Selected: %1 | families=%2 | support=%3")
				.arg(target_trace.value("selected_label").toString())
				.arg(target_trace.value("family_count").toInt())
				.arg(target_trace.value("total").toInt()),
			tab);
		table = predictor_trace_build_table(rows, tab);
		tab_layout->addWidget(summary_label);
		tab_layout->addWidget(table, 1);
		tabs->addTab(tab, QString::fromUtf8(spec.label));
	}
	layout->addWidget(tabs, 1);
	layout->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	dialog.exec();
}

void main_window::on_ping_server()
{
	QJsonDocument doc;
	QString err;

	m_api.set_base_url(current_base_url());
	if (!m_api.get("/health", &doc, &err)) {
		append_log(QString("ping failed: %1").arg(err));
		QMessageBox::warning(this, "Server", "Ping failed: " + err);
		return;
	}
	append_log("ping ok: " + QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
}

void main_window::on_import_dataset()
{
	QJsonDocument doc;
	QString err;
	QString path = m_import_path_edit->text().trimmed();
	int pip_scale = m_import_pip_scale_spin->value();

	if (path.isEmpty()) {
		QMessageBox::warning(this, "Import", "CSV path is required");
		return;
	}
	m_api.set_base_url(current_base_url());
	if (!m_api.post(QString("/datasets/import?path=%1&pip_scale=%2").arg(path).arg(pip_scale), &doc, &err)) {
		append_log(QString("import failed: %1").arg(err));
		QMessageBox::warning(this, "Import", "Import failed: " + err);
		return;
	}
	append_log("import result: " + QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
	on_refresh_datasets();
}

void main_window::on_import_sample_dataset()
{
	QJsonDocument doc;
	QString err;
	QString path = QString::fromUtf8(GUI_BUNDLED_SAMPLE_CSV).trimmed();

	if (path.isEmpty()) {
		QMessageBox::warning(this, "Sample Import", "Bundled sample path is not configured");
		return;
	}
	m_import_path_edit->setText(path);
	m_api.set_base_url(current_base_url());
	if (!m_api.post(QString("/datasets/import?path=%1&pip_scale=%2").arg(path).arg(m_import_pip_scale_spin->value()), &doc, &err)) {
		append_log(QString("sample import failed: %1").arg(err));
		QMessageBox::warning(this, "Sample Import", "Import failed: " + err);
		return;
	}
	if (doc.object().contains("dataset_id")) {
		int dataset_id = doc.object().value("dataset_id").toInt();

		m_dataset_id_spin->setValue(dataset_id);
		m_job_dataset_id_spin->setValue(dataset_id);
		if (m_mining_job_dataset_id_spin != nullptr) {
			m_mining_job_dataset_id_spin->setValue(dataset_id);
		}
		if (m_wave_job_dataset_id_spin != nullptr) {
			m_wave_job_dataset_id_spin->setValue(dataset_id);
		}
		m_patterns_dataset_id_spin->setValue(dataset_id);
		if (m_wave_dataset_id_spin != nullptr) {
			m_wave_dataset_id_spin->setValue(dataset_id);
		}
	}
	append_log("sample import result: " + QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
	on_refresh_datasets();
}

void main_window::on_refresh_datasets()
{
	QJsonDocument doc;
	QString err;
	QJsonArray rows;

	m_api.set_base_url(current_base_url());
	if (!m_api.get("/datasets", &doc, &err)) {
		append_log(QString("datasets load failed: %1").arg(err));
		return;
	}
	rows = doc.object().value("datasets").toArray();
	m_dataset_table->setRowCount(rows.size());
	for (int i = 0; i < rows.size(); ++i) {
		QJsonObject o = rows.at(i).toObject();
		m_dataset_table->setItem(i, 0, new QTableWidgetItem(QString::number(o.value("dataset_id").toInt())));
		m_dataset_table->setItem(i, 1, new QTableWidgetItem(o.value("name").toString()));
		m_dataset_table->setItem(i, 2, new QTableWidgetItem(QString::number(o.value("candles").toInt())));
		m_dataset_table->setItem(i, 3, new QTableWidgetItem(QString::number(o.value("features").toInt())));
		m_dataset_table->setItem(i, 4, new QTableWidgetItem(QString::number(o.value("char_blocks").toInt())));
		m_dataset_table->setItem(i, 5, new QTableWidgetItem(QString::number(o.value("q").toInt())));
	}
	append_log(QString("datasets refreshed: %1").arg(rows.size()));
}

void main_window::on_load_dataset_detail()
{
	QJsonDocument doc;
	QString err;
	int dataset_id = m_dataset_id_spin->value();

	m_api.set_base_url(current_base_url());
	if (!m_api.get(QString("/datasets/%1").arg(dataset_id), &doc, &err)) {
		append_log(QString("dataset detail failed: %1").arg(err));
		return;
	}
	m_dataset_text->setPlainText(QString::fromUtf8(doc.toJson(QJsonDocument::Indented)));
}

void main_window::on_load_candles()
{
	QJsonDocument doc;
	QString err;
	int dataset_id = m_dataset_id_spin->value();
	int start = m_window_start_spin->value();
	int count = m_window_count_spin->value();

	m_api.set_base_url(current_base_url());
	if (!m_api.get(QString("/datasets/%1/candles?start=%2&count=%3").arg(dataset_id).arg(start).arg(count), &doc, &err)) {
		append_log(QString("candles load failed: %1").arg(err));
		return;
	}
	m_dataset_text->setPlainText(QString::fromUtf8(doc.toJson(QJsonDocument::Indented)));
}

void main_window::on_load_features()
{
	QJsonDocument doc;
	QString err;
	int dataset_id = m_dataset_id_spin->value();
	int start = m_window_start_spin->value();
	int count = m_window_count_spin->value();

	m_api.set_base_url(current_base_url());
	if (!m_api.get(QString("/datasets/%1/features?start=%2&count=%3").arg(dataset_id).arg(start).arg(count), &doc, &err)) {
		append_log(QString("features load failed: %1").arg(err));
		return;
	}
	m_dataset_text->setPlainText(QString::fromUtf8(doc.toJson(QJsonDocument::Indented)));
}

void main_window::on_submit_clustering_job()
{
	QJsonDocument doc;
	QString err;
	QString path;
	QString request_name;
	int algorithm_type = current_clustering_algorithm();

	if (algorithm_type == clustering_ui_threshold_graph) {
		path = QString("/jobs/character-clustering?dataset_id=%1&q=%2&distance_mode=%3&delta=%4&theta=%5&cluster_mode=legacy_connected_components&mirrored=%6&weighted=1&delta_upper_wick_gate=%7&delta_lower_wick_gate=%8&delta_lower_ratio_gate=%9&delta_upper_ratio_gate=%10&delta_total_wick_ratio_gate=%11&w_upper=%12&w_lower=%13&w_body=%14&w_range=%15")
			.arg(m_job_dataset_id_spin->value())
			.arg(m_q_spin->value())
			.arg(m_threshold_distance_mode_combo->currentData().toString())
			.arg(m_delta_spin->value())
			.arg(m_theta_spin->value())
			.arg(m_mirrored_spin->value())
			.arg(m_delta_upper_wick_gate_spin->value())
			.arg(m_delta_lower_wick_gate_spin->value())
			.arg(m_delta_lower_ratio_gate_spin->value())
			.arg(m_delta_upper_ratio_gate_spin->value())
			.arg(m_delta_total_wick_ratio_gate_spin->value())
			.arg(m_w_upper_spin->value())
			.arg(m_w_lower_spin->value())
			.arg(m_w_body_spin->value())
			.arg(m_w_range_spin->value());
		request_name = "threshold graph clustering";
	} else if (algorithm_type == clustering_ui_sparse_graph) {
		path = QString("/jobs/character-clustering?dataset_id=%1&q=%2&distance_mode=%3&delta=%4&delta_body_gate=%5&delta_range_gate=%6&delta_bullcount_gate=%7&delta_signchange_gate=%8&delta_upper_wick_gate=%9&delta_lower_wick_gate=%10&delta_lower_ratio_gate=%11&delta_upper_ratio_gate=%12&delta_total_wick_ratio_gate=%13&k_neighbors=%14&use_mutual_knn=%15&max_neighbor_distance=%16&cluster_mode=%17&body_mode=%18&body_sign_penalty=%19&require_same_majority_sign=%20&use_composition=%21&mirrored=%22&weighted=1&w_upper=%23&w_lower=%24&w_body=%25&w_range=%26&w_bullcount=%27&w_signchanges=%28&w_runbull=%29&w_runbear=%30")
			.arg(m_job_dataset_id_spin->value())
			.arg(m_q_spin->value())
			.arg(m_distance_mode_combo->currentData().toString())
			.arg(m_delta_spin->value())
			.arg(m_delta_body_gate_spin->value())
			.arg(m_delta_range_gate_spin->value())
			.arg(m_delta_bullcount_gate_spin->value())
			.arg(m_delta_signchange_gate_spin->value())
			.arg(m_delta_upper_wick_gate_spin->value())
			.arg(m_delta_lower_wick_gate_spin->value())
			.arg(m_delta_lower_ratio_gate_spin->value())
			.arg(m_delta_upper_ratio_gate_spin->value())
			.arg(m_delta_total_wick_ratio_gate_spin->value())
			.arg(m_graph_knn_spin->value())
			.arg(m_graph_mutual_knn_spin->value())
			.arg(m_graph_max_neighbor_distance_spin->value(), 0, 'f', 3)
			.arg(m_graph_mode_combo->currentData().toString())
			.arg(m_body_mode_combo->currentData().toInt())
			.arg(m_body_sign_penalty_spin->value(), 0, 'f', 3)
			.arg(m_require_same_majority_sign_spin->value())
			.arg(m_use_composition_spin->value())
			.arg(m_mirrored_spin->value())
			.arg(m_w_upper_spin->value())
			.arg(m_w_lower_spin->value())
			.arg(m_w_body_spin->value())
			.arg(m_w_range_spin->value())
			.arg(m_w_bullcount_spin->value())
			.arg(m_w_signchanges_spin->value())
			.arg(m_w_runbull_spin->value())
			.arg(m_w_runbear_spin->value());
		request_name = "sparse graph clustering";
	} else if (algorithm_type == clustering_ui_kmedoids) {
		path = QString("/jobs/kmedoids?dataset_id=%1&q=%2&delta=%3&k=%4&max_iter=%5&restarts=%6&mirrored=%7&weighted=%8&w_upper=%9&w_lower=%10&w_body=%11&w_range=%12")
			.arg(m_job_dataset_id_spin->value())
			.arg(m_q_spin->value())
			.arg(m_delta_spin->value())
			.arg(m_k_spin->value())
			.arg(m_max_iter_spin->value())
			.arg(m_restarts_spin->value())
			.arg(m_mirrored_spin->value())
			.arg(1)
			.arg(m_w_upper_spin->value())
			.arg(m_w_lower_spin->value())
			.arg(m_w_body_spin->value())
			.arg(m_w_range_spin->value());
		request_name = "kmedoids";
	} else if (algorithm_type == clustering_ui_dbscan) {
		path = QString("/jobs/dbscan?dataset_id=%1&q=%2&delta=%3&eps=%4&min_pts=%5&mirrored=%6&weighted=%7&w_upper=%8&w_lower=%9&w_body=%10&w_range=%11")
			.arg(m_job_dataset_id_spin->value())
			.arg(m_q_spin->value())
			.arg(m_delta_spin->value())
			.arg(m_eps_spin->value())
			.arg(m_min_pts_spin->value())
			.arg(m_mirrored_spin->value())
			.arg(1)
			.arg(m_w_upper_spin->value())
			.arg(m_w_lower_spin->value())
			.arg(m_w_body_spin->value())
			.arg(m_w_range_spin->value());
		request_name = "dbscan";
	} else {
		append_log(QString("clustering job submit failed: unsupported algorithm %1").arg(algorithm_type));
		return;
	}

	m_api.set_base_url(current_base_url());
	if (!m_api.post(path, &doc, &err)) {
		append_log(QString("%1 job submit failed: %2").arg(request_name).arg(err));
		return;
	}
	append_log(QString("%1 job submit: ").arg(request_name) + QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
	on_refresh_jobs();
}

void main_window::on_clustering_algorithm_changed()
{
	update_clustering_job_ui();
}

void main_window::update_wave_job_ui()
{
	bool relative_mode = (m_wave_model_combo != nullptr &&
		(m_wave_model_combo->currentData().toInt() == 2 ||
		 m_wave_model_combo->currentData().toInt() == 3 ||
		 m_wave_model_combo->currentData().toInt() == 4 ||
		 m_wave_model_combo->currentData().toInt() == 5 ||
		 m_wave_model_combo->currentData().toInt() == 6));

	if (m_wave_relative_box != nullptr) {
		m_wave_relative_box->setVisible(relative_mode);
	}
	if (m_wave_cleanup_box != nullptr) {
		m_wave_cleanup_box->setVisible(!relative_mode);
	}
}

void main_window::on_submit_word_job()
{
	QJsonDocument doc;
	QString err;
	QString path = QString("/jobs/word-mining?dataset_id=%1&min_len=%2&max_len=%3&use_rle=0")
		.arg(m_mining_job_dataset_id_spin->value())
		.arg(m_word_min_spin->value())
		.arg(m_word_max_spin->value());

	m_api.set_base_url(current_base_url());
	if (!m_api.post(path, &doc, &err)) {
		append_log(QString("word job submit failed: %1").arg(err));
		return;
	}
	append_log("word job submit: " + QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
	on_refresh_jobs();
}

void main_window::on_submit_phrase_job()
{
	QJsonDocument doc;
	QString err;
	QString path = QString("/jobs/phrase-mining?dataset_id=%1&min_len=%2&max_len=%3&use_rle=1")
		.arg(m_mining_job_dataset_id_spin->value())
		.arg(m_phrase_min_spin->value())
		.arg(m_phrase_max_spin->value());

	m_api.set_base_url(current_base_url());
	if (!m_api.post(path, &doc, &err)) {
		append_log(QString("phrase job submit failed: %1").arg(err));
		return;
	}
	append_log("phrase job submit: " + QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
	on_refresh_jobs();
}

void main_window::on_submit_wave_job()
{
	QJsonDocument doc;
	QString err;
	QString path = QString("/jobs/wave-inference?dataset_id=%1&wave_model=%2&lmin_candles=%3&lmax_candles=%4&regime_context_window=%5&regime_context_mode=%6&min_wave_move_pips=%7&norm_scale=%8&context_range_floor=%9&epsilon=%10&segment_penalty_lambda=%11&weak_fit_threshold=%12&boundary_low_conf_threshold=%13&trend_priority_mode=%14&retrace_break_threshold=%15&trend_split_gain_min=%16&compression_min_duration=%17&expansion_min_duration=%18&v_reversal_min_trend_strength=%19&v_reversal_max_bridge_chop=%20&v_reversal_max_duration=%21&relative_trend_retracement_max=%22&relative_strong_trend_retracement_max=%23&relative_trend_der_step_min=%24&relative_trend_der_body_min=%25&relative_trend_body_eff_min=%26&relative_trend_overlap_max=%27&relative_trend_sign_flip_max=%28&relative_trend_wick_bias_min=%29&relative_range_der_step_max=%30&relative_range_body_eff_max=%31&relative_range_overlap_min=%32&relative_range_sign_flip_min=%33")
		.arg(m_wave_job_dataset_id_spin->value())
		.arg(m_wave_model_combo->currentData().toInt())
		.arg(m_wave_lmin_spin->value())
		.arg(m_wave_lmax_spin->value())
		.arg(m_wave_context_window_spin->value())
		.arg(1)
		.arg(m_wave_min_move_spin->value())
		.arg(m_wave_norm_scale_spin->value())
		.arg(m_wave_context_floor_spin->value())
		.arg(m_wave_epsilon_spin->value())
		.arg(m_wave_lambda_spin->value())
		.arg(m_wave_weak_fit_spin->value())
		.arg(m_wave_boundary_low_conf_spin->value())
		.arg(m_wave_trend_priority_combo->currentData().toInt())
		.arg(m_wave_retrace_break_spin->value())
		.arg(m_wave_trend_split_gain_spin->value())
		.arg(m_wave_compression_min_duration_spin->value())
		.arg(m_wave_expansion_min_duration_spin->value())
		.arg(m_wave_v_strength_spin->value())
		.arg(m_wave_v_bridge_spin->value())
		.arg(m_wave_v_duration_spin->value())
		.arg(m_wave_relative_trend_retracement_max_spin->value())
		.arg(m_wave_relative_strong_trend_retracement_max_spin->value())
		.arg(m_wave_relative_trend_der_step_min_spin->value())
		.arg(m_wave_relative_trend_der_body_min_spin->value())
		.arg(m_wave_relative_trend_body_eff_min_spin->value())
		.arg(m_wave_relative_trend_overlap_max_spin->value())
		.arg(m_wave_relative_trend_sign_flip_max_spin->value())
		.arg(m_wave_relative_trend_wick_bias_min_spin->value())
		.arg(m_wave_relative_range_der_step_max_spin->value())
		.arg(m_wave_relative_range_body_eff_max_spin->value())
		.arg(m_wave_relative_range_overlap_min_spin->value())
		.arg(m_wave_relative_range_sign_flip_min_spin->value());
	path += QString("&minimum_pips_for_trend=%1").arg(m_wave_minimum_trend_pips_spin->value());

	m_api.set_base_url(current_base_url());
	if (!m_api.post(path, &doc, &err)) {
		append_log(QString("wave job submit failed: %1").arg(err));
		return;
	}
	append_log("wave job submit: " + QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
	on_refresh_jobs();
}

void main_window::on_refresh_jobs()
{
	QJsonDocument doc;
	QString err;
	QJsonArray rows;
	bool has_active_jobs = false;

	m_api.set_base_url(current_base_url());
	if (!m_api.get("/jobs", &doc, &err)) {
		update_job_polling(false);
		append_log(QString("jobs load failed: %1").arg(err));
		return;
	}
	rows = doc.object().value("jobs").toArray();
	m_jobs_table->setRowCount(rows.size());
	for (int i = 0; i < rows.size(); ++i) {
		QJsonObject o = rows.at(i).toObject();
		int status = o.value("status").toInt();
		if (status == 1 || status == 2) {
			has_active_jobs = true;
		}
		m_jobs_table->setItem(i, 0, new QTableWidgetItem(QString::number(o.value("job_id").toVariant().toLongLong())));
		m_jobs_table->setItem(i, 1, new QTableWidgetItem(QString::number(o.value("dataset_id").toInt())));
		m_jobs_table->setItem(i, 2, new QTableWidgetItem(job_type_name(o.value("job_type").toInt())));
		m_jobs_table->setItem(i, 3, new QTableWidgetItem(QString("%1 (%2)").arg(job_status_name(status)).arg(status)));
		m_jobs_table->setItem(i, 4, new QTableWidgetItem(QString::number(o.value("progress").toDouble(), 'f', 3)));
		m_jobs_table->setItem(i, 5, new QTableWidgetItem(o.value("error").toString()));
	}
	update_job_polling(has_active_jobs);
	append_log(QString("jobs refreshed: %1").arg(rows.size()));
}

void main_window::on_delete_selected_job()
{
	QJsonDocument doc;
	QString err;
	int row = m_jobs_table != nullptr ? m_jobs_table->currentRow() : -1;
	QTableWidgetItem *job_item;
	bool ok = false;
	qulonglong job_id;

	if (row < 0) {
		QMessageBox::information(this, "Jobs", "Select a job first.");
		return;
	}
	job_item = m_jobs_table->item(row, 0);
	if (job_item == nullptr) {
		QMessageBox::information(this, "Jobs", "Selected row has no job id.");
		return;
	}
	job_id = job_item->text().toULongLong(&ok);
	if (!ok || job_id == 0) {
		QMessageBox::warning(this, "Jobs", "Invalid job id.");
		return;
	}
	if (QMessageBox::question(this,
			"Delete Job",
			QString("Delete job %1 from queue history? Dataset results will be kept.")
				.arg(job_id)) != QMessageBox::Yes) {
		return;
	}
	m_api.set_base_url(current_base_url());
	if (!m_api.post(QString("/jobs/%1/delete").arg(job_id), &doc, &err)) {
		append_log(QString("job delete failed: %1").arg(err));
		QMessageBox::warning(this, "Jobs", "Delete failed: " + err);
		return;
	}
	append_log("job deleted: " + QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
	on_refresh_jobs();
}

void main_window::update_job_polling(bool has_active_jobs)
{
	if (has_active_jobs) {
		if (!m_jobs_refresh_timer->isActive()) {
			m_jobs_refresh_timer->start();
		}
		return;
	}
	if (m_jobs_refresh_timer->isActive()) {
		m_jobs_refresh_timer->stop();
	}
}

void main_window::on_load_clusters()
{
	QJsonDocument doc;
	QString err;
	int dataset_id = m_patterns_dataset_id_spin->value();
	QJsonArray rows;

	m_api.set_base_url(current_base_url());
	if (!m_api.get(QString("/datasets/%1/clusters").arg(dataset_id), &doc, &err)) {
		append_log(QString("clusters load failed: %1").arg(err));
		return;
	}
	rows = doc.object().value("clusters").toArray();
	m_clusters_table->setRowCount(rows.size());
	for (int i = 0; i < rows.size(); ++i) {
		QJsonObject o = rows.at(i).toObject();
		m_clusters_table->setItem(i, 0, new QTableWidgetItem(QString::number(o.value("cluster_id").toInt())));
		m_clusters_table->setItem(i, 1, new QTableWidgetItem(QString::number(o.value("size").toInt())));
		m_clusters_table->setItem(i, 2, new QTableWidgetItem(QString::number(o.value("representative_index").toInt())));
		m_clusters_table->setItem(i, 3, new QTableWidgetItem(QString::number(o.value("score").toDouble(), 'f', 3)));
	}
	if (doc.object().contains("algorithm_type")) {
		append_log(QString("clusters algorithm: %1")
			.arg(clustering_result_name(
				doc.object().value("algorithm_type").toInt(),
				doc.object().value("graph_cluster_mode").toInt())));
	}
	if (doc.object().contains("graph_diagnostics")) {
		QJsonObject diag = doc.object().value("graph_diagnostics").toObject();
		append_log(QString("clusters loaded: algorithm=%1 mode=%2 edges=%3 largest_component_fraction=%4 percolated=%5")
			.arg(clustering_result_name(
				doc.object().value("algorithm_type").toInt(),
				doc.object().value("graph_cluster_mode").toInt()))
			.arg(graph_cluster_mode_name(doc.object().value("graph_cluster_mode").toInt()))
			.arg(diag.value("n_edges").toInt())
			.arg(diag.value("largest_component_fraction").toDouble(), 0, 'f', 3)
			.arg(diag.value("percolated").toBool() ? "true" : "false"));
	} else {
		append_log(QString("clusters loaded: algorithm=%1 count=%2")
			.arg(clustering_result_name(
				doc.object().value("algorithm_type").toInt(),
				doc.object().value("graph_cluster_mode").toInt()))
			.arg(rows.size()));
	}
	m_cluster_members_table->setRowCount(0);
	m_cluster_chart->clear();
	m_cluster_feature_text->clear();
	m_cluster_feature_table->setRowCount(0);
	m_group_page_start = 0;
	m_group_total_members = 0;
	m_group_preview_members.clear();
	refresh_group_block_view();
}

void main_window::on_load_cluster_members()
{
	QJsonDocument doc;
	QString err;
	int dataset_id = m_patterns_dataset_id_spin->value();
	int row = m_clusters_table->currentRow();
	int cluster_id;
	int member_start = m_cluster_member_start_spin->value();
	int member_count = m_cluster_member_count_spin->value();
	QJsonArray rows;

	if (row < 0 || m_clusters_table->item(row, 0) == nullptr) {
		append_log("cluster members load skipped: no cluster selected");
		return;
	}
	cluster_id = m_clusters_table->item(row, 0)->text().toInt();
	m_api.set_base_url(current_base_url());
	if (!m_api.get(QString("/datasets/%1/clusters/%2/members?start=%3&count=%4")
			.arg(dataset_id)
			.arg(cluster_id)
			.arg(member_start)
			.arg(member_count),
		&doc,
		&err)) {
		append_log(QString("cluster members load failed: %1").arg(err));
		return;
	}
	rows = doc.object().value("members").toArray();
	m_cluster_members_table->setRowCount(rows.size());
	for (int i = 0; i < rows.size(); ++i) {
		QJsonObject o = rows.at(i).toObject();

		m_cluster_members_table->setItem(i, 0, new QTableWidgetItem(QString::number(o.value("member_index").toInt())));
		m_cluster_members_table->setItem(i, 1, new QTableWidgetItem(QString::number(o.value("object_id").toInt())));
		m_cluster_members_table->setItem(i, 2, new QTableWidgetItem(QString::number(o.value("start_candle").toInt())));
		m_cluster_members_table->setItem(i, 3, new QTableWidgetItem(QString::number(o.value("end_candle").toInt())));
		m_cluster_members_table->setItem(i, 4, new QTableWidgetItem(QString::number(o.value("start_ts").toInt())));
		m_cluster_members_table->setItem(i, 5, new QTableWidgetItem(QString::number(o.value("end_ts").toInt())));
	}
	append_log(QString("cluster members loaded: cluster=%1 count=%2 total=%3 q=%4")
		.arg(cluster_id)
		.arg(rows.size())
		.arg(doc.object().value("total").toInt())
		.arg(doc.object().value("q").toInt()));
	m_group_page_start = 0;
	m_group_total_members = doc.object().value("total").toInt();
	m_group_preview_members.clear();
	refresh_group_block_view();
	if (!rows.isEmpty()) {
		m_cluster_members_table->selectRow(0);
		on_cluster_member_row_changed();
	} else {
		m_cluster_chart->clear();
		m_cluster_feature_text->clear();
		m_cluster_feature_table->setRowCount(0);
		refresh_group_block_view();
	}
}

void main_window::on_cluster_row_changed()
{
	if (m_clusters_table->currentRow() >= 0) {
		on_load_cluster_members();
	}
}

void main_window::on_cluster_member_row_changed()
{
	int row = m_cluster_members_table->currentRow();

	if (row < 0 || m_cluster_members_table->item(row, 2) == nullptr || m_cluster_members_table->item(row, 3) == nullptr) {
		refresh_group_block_view();
		return;
	}
	load_cluster_feature_panel(m_patterns_dataset_id_spin->value(),
		m_cluster_members_table->item(row, 2)->text().toInt(),
		m_cluster_members_table->item(row, 3)->text().toInt(),
		m_cluster_members_table->item(row, 1) != nullptr ? m_cluster_members_table->item(row, 1)->text().toInt() : -1);
	load_cluster_chart(m_patterns_dataset_id_spin->value(),
		m_cluster_members_table->item(row, 2)->text().toInt(),
		m_cluster_members_table->item(row, 3)->text().toInt());
	refresh_group_block_view();
}

void main_window::on_view_mode_changed()
{
	if (m_pattern_view_stack == nullptr || m_individual_view_radio == nullptr) {
		return;
	}
	m_pattern_view_stack->setCurrentIndex(m_individual_view_radio->isChecked() ? 0 : 1);
	if (m_group_view_radio != nullptr && m_group_view_radio->isChecked()) {
		refresh_group_block_view();
	}
}

void main_window::on_group_chart_clicked(int member_row)
{
	if (member_row < 0 || member_row >= m_group_preview_members.size()) {
		return;
	}
	{
		const group_preview_member &member = m_group_preview_members[member_row];

		if (m_individual_view_radio != nullptr) {
			m_individual_view_radio->setChecked(true);
		}
		for (int row = 0; row < m_cluster_members_table->rowCount(); ++row) {
			QTableWidgetItem *item = m_cluster_members_table->item(row, 0);

			if (item != nullptr && item->text().toInt() == member.member_index) {
				m_cluster_members_table->selectRow(row);
				break;
			}
		}
		load_cluster_feature_panel(m_patterns_dataset_id_spin->value(),
			member.start_candle,
			member.end_candle,
			member.block_index);
		load_cluster_chart(m_patterns_dataset_id_spin->value(),
			member.start_candle,
			member.end_candle);
	}
}

void main_window::on_group_prev_page()
{
	int page_size = m_group_charts.size();

	if (page_size <= 0) {
		return;
	}
	m_group_page_start = qMax(0, m_group_page_start - page_size);
	refresh_group_block_view();
}

void main_window::on_group_next_page()
{
	int page_size = m_group_charts.size();
	int row_count = m_cluster_members_table != nullptr ? m_cluster_members_table->rowCount() : 0;

	if (page_size <= 0 || row_count <= 0) {
		return;
	}
	if (m_group_page_start + page_size < row_count) {
		m_group_page_start += page_size;
	}
	refresh_group_block_view();
}

void main_window::on_load_words()
{
	QJsonDocument doc;
	QString err;
	int dataset_id = m_patterns_dataset_id_spin->value();
	QJsonArray rows;

	m_api.set_base_url(current_base_url());
	if (!m_api.get(QString("/datasets/%1/words").arg(dataset_id), &doc, &err)) {
		append_log(QString("words load failed: %1").arg(err));
		return;
	}
	rows = doc.object().value("words").toArray();
	m_words_table->setRowCount(rows.size());
	for (int i = 0; i < rows.size(); ++i) {
		QJsonObject o = rows.at(i).toObject();
		m_words_table->setItem(i, 0, new QTableWidgetItem(QString::number(o.value("word_id").toInt())));
		m_words_table->setItem(i, 1, new QTableWidgetItem(QString::number(o.value("length_chars").toInt())));
		m_words_table->setItem(i, 2, new QTableWidgetItem(QString::number(o.value("frequency").toInt())));
	}
}

void main_window::on_load_phrases()
{
	QJsonDocument doc;
	QString err;
	int dataset_id = m_patterns_dataset_id_spin->value();
	QJsonArray rows;

	m_api.set_base_url(current_base_url());
	if (!m_api.get(QString("/datasets/%1/phrases").arg(dataset_id), &doc, &err)) {
		append_log(QString("phrases load failed: %1").arg(err));
		return;
	}
	rows = doc.object().value("phrases").toArray();
	m_phrases_table->setRowCount(rows.size());
	for (int i = 0; i < rows.size(); ++i) {
		QJsonObject o = rows.at(i).toObject();
		m_phrases_table->setItem(i, 0, new QTableWidgetItem(QString::number(o.value("phrase_id").toInt())));
		m_phrases_table->setItem(i, 1, new QTableWidgetItem(QString::number(o.value("token_len").toInt())));
		m_phrases_table->setItem(i, 2, new QTableWidgetItem(QString::number(o.value("frequency").toInt())));
		m_phrases_table->setItem(i, 3, new QTableWidgetItem(QString::number(o.value("avg_duration_chars").toDouble(), 'f', 3)));
		m_phrases_table->setItem(i, 4, new QTableWidgetItem(QString::number(o.value("avg_duration_candles").toDouble(), 'f', 3)));
	}
}

void main_window::on_load_waves()
{
	load_waves(false);
}

void main_window::on_load_waves_lowest_err()
{
	load_waves(true);
}

void main_window::on_load_predictor_waves()
{
	load_predictor_waves();
}

void main_window::on_refresh_live()
{
	load_live_status();
	load_live_stream_data();
}

void main_window::on_recompute_live()
{
	QJsonDocument doc;
	QString err;
	QString symbol = current_live_symbol();
	QString timeframe = current_live_timeframe();
	QString symbol_q = QString::fromUtf8(QUrl::toPercentEncoding(symbol));
	QString timeframe_q = QString::fromUtf8(QUrl::toPercentEncoding(timeframe));
	int old_timeout;

	if (symbol.isEmpty() || timeframe.isEmpty()) {
		QMessageBox::warning(this, "Live", "No live stream is selected.");
		return;
	}
	m_api.set_base_url(current_base_url());
	old_timeout = m_api.timeout_ms();
	m_api.set_timeout_ms(120000);
	if (!m_api.post(QString("/live/recompute?symbol=%1&timeframe=%2").arg(symbol_q).arg(timeframe_q), &doc, &err)) {
		m_api.set_timeout_ms(old_timeout);
		append_log(QString("live recompute failed: %1").arg(err));
		QMessageBox::warning(this, "Live", "Recompute failed: " + err);
		return;
	}
	m_api.set_timeout_ms(old_timeout);
	if (m_live_summary_text != nullptr) {
		m_live_summary_text->setPlainText(QString::fromUtf8(doc.toJson(QJsonDocument::Indented)));
	}
	load_live_status();
	load_live_stream_data();
}

void main_window::on_reset_live()
{
	QJsonDocument doc;
	QString err;
	QString symbol = current_live_symbol();
	QString timeframe = current_live_timeframe();
	QString symbol_q = QString::fromUtf8(QUrl::toPercentEncoding(symbol));
	QString timeframe_q = QString::fromUtf8(QUrl::toPercentEncoding(timeframe));
	QMessageBox::StandardButton answer;

	if (symbol.isEmpty() || timeframe.isEmpty()) {
		QMessageBox::warning(this, "Live", "No live stream is selected.");
		return;
	}
	answer = QMessageBox::question(this,
		"Reset Live Stream",
		QString("Delete local live candles and waves for %1 %2?").arg(symbol).arg(timeframe),
		QMessageBox::Yes | QMessageBox::No,
		QMessageBox::No);
	if (answer != QMessageBox::Yes) {
		return;
	}
	m_api.set_base_url(current_base_url());
	if (!m_api.post(QString("/live/reset?symbol=%1&timeframe=%2").arg(symbol_q).arg(timeframe_q), &doc, &err)) {
		append_log(QString("live reset failed: %1").arg(err));
		QMessageBox::warning(this, "Live", "Reset failed: " + err);
		return;
	}
	m_live_waves = QJsonArray();
	if (m_live_waves_table != nullptr) {
		m_live_waves_table->setRowCount(0);
	}
	if (m_live_properties_table != nullptr) {
		m_live_properties_table->setRowCount(0);
	}
	if (m_live_chart != nullptr) {
		m_live_chart->clear();
	}
	if (m_live_chart_status_label != nullptr) {
		m_live_chart_status_label->setText("Live chart candle index: -");
	}
	load_live_status();
	load_live_stream_data();
}

void main_window::on_live_stream_changed()
{
	m_live_last_key.clear();
	m_live_last_ts = 0;
	m_live_last_wave_count = 0;
	load_live_stream_data();
}

void main_window::on_live_wave_row_changed()
{
	int row = m_live_waves_table != nullptr ? m_live_waves_table->currentRow() : -1;
	QJsonObject token;

	update_live_properties(row);
	update_live_composition(row);
	if (m_live_chart == nullptr) {
		return;
	}
	if (row < 0 || row >= m_live_waves.size()) {
		m_live_chart->set_focus_range(-1, -1);
		return;
	}
	token = m_live_waves.at(row).toObject();
	m_live_chart->set_focus_range(token.value("start_candle").toInt() - m_live_candle_start,
		token.value("end_candle").toInt() - m_live_candle_start);
}

QString main_window::current_live_symbol() const
{
	QString data;
	int sep;

	if (m_live_stream_combo == nullptr || m_live_stream_combo->currentIndex() < 0) {
		return QString();
	}
	data = m_live_stream_combo->currentData().toString();
	sep = data.indexOf('|');
	if (sep < 0) {
		return QString();
	}
	return data.left(sep);
}

QString main_window::current_live_timeframe() const
{
	QString data;
	int sep;

	if (m_live_stream_combo == nullptr || m_live_stream_combo->currentIndex() < 0) {
		return QString();
	}
	data = m_live_stream_combo->currentData().toString();
	sep = data.indexOf('|');
	if (sep < 0) {
		return QString();
	}
	return data.mid(sep + 1);
}

void main_window::load_live_status()
{
	QJsonDocument doc;
	QString err;
	QString current_key;

	if (m_live_stream_combo == nullptr) {
		return;
	}
	current_key = m_live_stream_combo->currentData().toString();
	m_api.set_base_url(current_base_url());
	if (!m_api.get("/live/status", &doc, &err)) {
		append_log(QString("live status load failed: %1").arg(err));
		return;
	}
	m_live_streams = doc.object().value("streams").toArray();
	m_live_stream_combo->blockSignals(true);
	m_live_stream_combo->clear();
	for (int i = 0; i < m_live_streams.size(); ++i) {
		QJsonObject stream = m_live_streams.at(i).toObject();
		QString symbol = stream.value("symbol").toString();
		QString timeframe = stream.value("timeframe").toString();
		QString key = symbol + "|" + timeframe;
		QString status = stream.value("recompute_status").toString("empty");

		m_live_stream_combo->addItem(QString("%1 %2  candles=%3 waves=%4 %5")
			.arg(symbol)
			.arg(timeframe)
			.arg(stream.value("candle_count").toInt())
			.arg(stream.value("wave_count").toInt())
			.arg(status), key);
	}
	if (!current_key.isEmpty()) {
		int index = m_live_stream_combo->findData(current_key);

		if (index >= 0) {
			m_live_stream_combo->setCurrentIndex(index);
		}
	}
	m_live_stream_combo->blockSignals(false);
}

void main_window::load_live_stream_data()
{
	QJsonDocument doc;
	QString err;
	QString symbol = current_live_symbol();
	QString timeframe = current_live_timeframe();
	QString live_key = symbol + "|" + timeframe;
	QString symbol_q = QString::fromUtf8(QUrl::toPercentEncoding(symbol));
	QString timeframe_q = QString::fromUtf8(QUrl::toPercentEncoding(timeframe));
	int wave_count = m_live_wave_count_spin != nullptr ? m_live_wave_count_spin->value() : 128;
	int candle_count = m_live_candle_count_spin != nullptr ? m_live_candle_count_spin->value() : 100;
	int previous_selected_token = -1;
	int selected_row_after_refresh = -1;
	int summary_last_ts = 0;
	int summary_wave_count = 0;
	QJsonObject snapshot_obj;
	QJsonObject candles_obj;
	QJsonArray candle_rows;
	QVector<cluster_chart_widget::candle_item> candles;
	QVector<cluster_chart_widget::wave_overlay_item> overlays;
	int candle_start = 0;
	bool waves_stale = false;

	if (m_live_waves_table != nullptr && m_live_waves_table->currentRow() >= 0 && m_live_waves_table->currentRow() < m_live_waves.size()) {
		previous_selected_token = m_live_waves.at(m_live_waves_table->currentRow()).toObject().value("token_index").toInt(-1);
	}
	if (symbol.isEmpty() || timeframe.isEmpty()) {
		if (m_live_waves_table != nullptr) {
			m_live_waves_table->setRowCount(0);
		}
		if (m_live_properties_table != nullptr) {
			m_live_properties_table->setRowCount(0);
		}
		if (m_live_chart != nullptr) {
			m_live_chart->clear();
		}
		return;
	}
	m_api.set_base_url(current_base_url());
	if (!m_api.get(QString("/live/snapshot?symbol=%1&timeframe=%2&candle_count=%3&wave_count=%4")
			.arg(symbol_q)
			.arg(timeframe_q)
			.arg(candle_count)
			.arg(wave_count),
		&doc,
		&err)) {
		append_log(QString("live snapshot load failed: %1").arg(err));
		return;
	}
	snapshot_obj = doc.object();
	{
		QJsonObject summary_obj = snapshot_obj.value("summary").toObject();

		waves_stale = summary_obj.value("waves_stale").toInt() != 0;
		summary_last_ts = summary_obj.value("last_ts").toInt();
		summary_wave_count = summary_obj.value("wave_count").toInt();
		if (m_live_status_label != nullptr) {
			m_live_status_label->setText(QString("Live stream: %1 %2 | %3 | candles=%4 waves=%5 | last_ts=%6 recompute_ts=%7 | batch %8/%9 changed")
				.arg(summary_obj.value("symbol").toString())
				.arg(summary_obj.value("timeframe").toString())
				.arg(summary_obj.value("recompute_status").toString("empty"))
				.arg(summary_obj.value("candle_count").toInt())
				.arg(summary_obj.value("wave_count").toInt())
				.arg(summary_obj.value("last_ts").toInt())
				.arg(summary_obj.value("last_recompute_ts").toInt())
				.arg(summary_obj.value("last_batch_changed").toInt())
				.arg(summary_obj.value("last_batch_input").toInt()));
			m_live_status_label->setStyleSheet(waves_stale ?
				"QLabel { color: #8a4b00; font-weight: 600; }" :
				"QLabel { color: #0b5f2a; font-weight: 600; }");
		}
	}
	candles_obj = snapshot_obj.value("candles").toObject();
	candle_rows = candles_obj.value("candles").toArray();
	candle_start = candles_obj.value("start").toInt();
	m_live_candle_start = candle_start;
	m_live_waves = snapshot_obj.value("waves").toObject().value("waves").toArray();
	refresh_live_wave_table();
	if (m_live_summary_text != nullptr) {
		QString summary_text = QString::fromUtf8(doc.toJson(QJsonDocument::Indented));

		if (m_live_summary_text->toPlainText() != summary_text) {
			m_live_summary_text->setPlainText(summary_text);
		}
	}
	candles.reserve(candle_rows.size());
	for (int i = 0; i < candle_rows.size(); ++i) {
		QJsonObject o = candle_rows.at(i).toObject();
		cluster_chart_widget::candle_item item;

		item.candle_index = candle_start + i;
		item.ts_epoch = o.value("ts_epoch").toInt();
		item.open_pips = o.value("open_pips").toInt();
		item.high_pips = o.value("high_pips").toInt();
		item.low_pips = o.value("low_pips").toInt();
		item.close_pips = o.value("close_pips").toInt();
		candles.push_back(item);
	}
	for (int i = 0; !waves_stale && i < m_live_waves.size(); ++i) {
		QJsonObject token = m_live_waves.at(i).toObject();
		int start = token.value("start_candle").toInt();
		int end = token.value("end_candle").toInt();
		cluster_chart_widget::wave_overlay_item item;

		if (end < candle_start || start >= candle_start + candle_rows.size()) {
			continue;
		}
		item.start_index = start - candle_start;
		item.end_index = end - candle_start;
		item.label = QString("%1 %2")
			.arg(token.value("class_name").toString())
			.arg(token.value("primitive_direction").toString());
		overlays.push_back(item);
	}
	if (m_live_chart != nullptr) {
		m_live_chart->set_candles(candles, -1, -1);
		m_live_chart->set_focus_range(-1, -1);
		m_live_chart->set_seed_focus(-1, -1);
		m_live_chart->set_wave_overlay(overlays);
	}
		if (m_live_waves_table != nullptr) {
			m_live_waves_table->setEnabled(!waves_stale);
		}
		if (m_live_info_tabs != nullptr) {
			m_live_info_tabs->setEnabled(!waves_stale);
		}
	if (!waves_stale && !m_live_waves.isEmpty()) {
		bool frontier_changed = (m_live_last_key != live_key ||
			m_live_last_ts != summary_last_ts ||
			m_live_last_wave_count != summary_wave_count);

		if (frontier_changed) {
			selected_row_after_refresh = m_live_waves.size() - 1;
		} else if (previous_selected_token >= 0) {
			for (int i = 0; i < m_live_waves.size(); ++i) {
				if (m_live_waves.at(i).toObject().value("token_index").toInt(-1) == previous_selected_token) {
					selected_row_after_refresh = i;
					break;
				}
			}
		}
		if (selected_row_after_refresh < 0) {
			selected_row_after_refresh = m_live_waves.size() - 1;
		}
		m_live_waves_table->selectRow(selected_row_after_refresh);
		update_live_properties(selected_row_after_refresh);
		on_live_wave_row_changed();
		} else {
			if (m_live_waves_table != nullptr) {
				m_live_waves_table->clearSelection();
			}
			update_live_properties(-1);
			update_live_composition(-1);
		}
	m_live_last_key = live_key;
	m_live_last_ts = summary_last_ts;
	m_live_last_wave_count = summary_wave_count;
}

void main_window::refresh_live_wave_table()
{
	if (m_live_waves_table == nullptr) {
		return;
	}
	m_live_waves_table->setRowCount(m_live_waves.size());
	for (int i = 0; i < m_live_waves.size(); ++i) {
		QJsonObject token = m_live_waves.at(i).toObject();

		m_live_waves_table->setItem(i, 0, new QTableWidgetItem(QString::number(token.value("token_index").toInt(i))));
		m_live_waves_table->setItem(i, 1, new QTableWidgetItem(QString::number(token.value("start_candle").toInt())));
		m_live_waves_table->setItem(i, 2, new QTableWidgetItem(QString::number(token.value("end_candle").toInt())));
		m_live_waves_table->setItem(i, 3, new QTableWidgetItem(token.value("class_name").toString()));
		m_live_waves_table->setItem(i, 4, new QTableWidgetItem(token.value("primitive_direction").toString()));
		m_live_waves_table->setItem(i, 5, new QTableWidgetItem(QString::number(token.value("net_move_raw").toInt())));
		m_live_waves_table->setItem(i, 6, new QTableWidgetItem(QString::number(token.value("duration_candles").toInt())));
		m_live_waves_table->setItem(i, 7, new QTableWidgetItem(QString::number(token.value("fit_error").toInt())));
	}
	m_live_waves_table->resizeRowsToContents();
	m_live_waves_table->resizeColumnsToContents();
}

void main_window::update_live_properties(int row)
{
	QJsonObject token;
	QStringList keys;
	QStringList visible_keys;

	if (m_live_properties_table == nullptr) {
		return;
	}
	if (row < 0 || row >= m_live_waves.size()) {
		m_live_properties_table->setRowCount(0);
		return;
	}
	token = m_live_waves.at(row).toObject();
	keys = token.keys();
	keys.sort();
	for (const QString &key : keys) {
		if (is_live_property_visible(key)) {
			visible_keys.push_back(key);
		}
	}
	m_live_properties_table->setRowCount(visible_keys.size());
	for (int i = 0; i < visible_keys.size(); ++i) {
		QString key = visible_keys.at(i);
		QPushButton *hide_btn = new QPushButton("x", m_live_properties_table);

		hide_btn->setText("");
		hide_btn->setIcon(style()->standardIcon(QStyle::SP_DialogCloseButton));
		hide_btn->setFlat(true);
		hide_btn->setMaximumWidth(24);
		hide_btn->setStyleSheet("QPushButton { color: #b00020; }");
		hide_btn->setToolTip(QString("Hide property '%1'").arg(key));
		connect(hide_btn, &QPushButton::clicked, this, [this, key]() {
			if (!m_hidden_live_properties.contains(key)) {
				m_hidden_live_properties.push_back(key);
				m_hidden_live_properties.sort();
				save_live_property_visibility();
			}
			update_live_properties(m_live_waves_table != nullptr ? m_live_waves_table->currentRow() : -1);
		});

		m_live_properties_table->setItem(i, 0, new QTableWidgetItem(key));
		set_property_value_cell(m_live_properties_table, i, 1, json_value_to_text(token.value(key)));
		m_live_properties_table->setCellWidget(i, 2, hide_btn);
	}
	m_live_properties_table->resizeRowsToContents();
	m_live_properties_table->resizeColumnsToContents();
}

void main_window::update_live_composition(int row)
{
	QJsonArray child_strings;

	if (m_live_composition_table == nullptr) {
		return;
	}
	m_live_composition_table->setRowCount(0);
	if (row < 0 || row >= m_live_waves.size()) {
		return;
	}
	child_strings = m_live_waves.at(row).toObject().value("child_strings").toArray();
	populate_wave_composition_table(m_live_composition_table, child_strings);
}

void main_window::load_live_chart(const QString &symbol, const QString &timeframe, int candle_count)
{
	QJsonDocument doc;
	QString err;
	QString symbol_q = QString::fromUtf8(QUrl::toPercentEncoding(symbol));
	QString timeframe_q = QString::fromUtf8(QUrl::toPercentEncoding(timeframe));
	QVector<cluster_chart_widget::candle_item> candles;
	QVector<cluster_chart_widget::wave_overlay_item> overlays;
	int total_candles = 0;
	int fetch_start = 0;

	for (int i = 0; i < m_live_streams.size(); ++i) {
		QJsonObject stream = m_live_streams.at(i).toObject();

		if (stream.value("symbol").toString() == symbol && stream.value("timeframe").toString() == timeframe) {
			total_candles = stream.value("candle_count").toInt();
			break;
		}
	}
	if (total_candles > candle_count) {
		fetch_start = total_candles - candle_count;
	}
	m_api.set_base_url(current_base_url());
	if (!m_api.get(QString("/live/candles?symbol=%1&timeframe=%2&start=%3&count=%4")
			.arg(symbol_q)
			.arg(timeframe_q)
			.arg(fetch_start)
			.arg(candle_count),
		&doc,
		&err)) {
		append_log(QString("live chart load failed: %1").arg(err));
		return;
	}
	QJsonArray rows = doc.object().value("candles").toArray();

	candles.reserve(rows.size());
	for (int i = 0; i < rows.size(); ++i) {
		QJsonObject o = rows.at(i).toObject();
		cluster_chart_widget::candle_item item;

		item.candle_index = fetch_start + i;
		item.ts_epoch = o.value("ts_epoch").toInt();
		item.open_pips = o.value("open_pips").toInt();
		item.high_pips = o.value("high_pips").toInt();
		item.low_pips = o.value("low_pips").toInt();
		item.close_pips = o.value("close_pips").toInt();
		candles.push_back(item);
	}
	for (int i = 0; i < m_live_waves.size(); ++i) {
		QJsonObject token = m_live_waves.at(i).toObject();
		int start = token.value("start_candle").toInt();
		int end = token.value("end_candle").toInt();
		cluster_chart_widget::wave_overlay_item item;

		if (end < fetch_start || start >= fetch_start + rows.size()) {
			continue;
		}
		item.start_index = start - fetch_start;
		item.end_index = end - fetch_start;
		item.label = QString("%1 %2")
			.arg(token.value("class_name").toString())
			.arg(token.value("primitive_direction").toString());
		overlays.push_back(item);
	}
	if (m_live_chart != nullptr) {
		m_live_candle_start = fetch_start;
		m_live_chart->set_candles(candles, -1, -1);
		m_live_chart->set_focus_range(-1, -1);
		m_live_chart->set_seed_focus(-1, -1);
		m_live_chart->set_wave_overlay(overlays);
	}
}

void main_window::load_waves(bool order_by_fit_error)
{
	QJsonDocument doc;
	QJsonDocument summary_doc;
	QString err;
	int dataset_id = m_wave_dataset_id_spin->value();
	int start = m_wave_start_spin->value();
	int count = m_wave_count_spin->value();
	QJsonArray rows;
	QString desired_filter = m_wave_filter_combo != nullptr ? m_wave_filter_combo->currentText() : "all";

	m_api.set_base_url(current_base_url());
	{
		QString path = QString("/datasets/%1/waves?start=%2&count=%3").arg(dataset_id).arg(start).arg(count);

		if (order_by_fit_error) {
			path += "&order_by=fit_error";
		}
		if (!m_api.get(path, &summary_doc, &err)) {
			append_log(QString("waves load failed: %1").arg(err));
			QMessageBox::warning(this, "Waves", "Load failed: " + err);
			return;
		}
	}
	m_wave_model_name = summary_doc.object().value("params").toObject().value("wave_model").toString();
	if (m_wave_model_name.isEmpty()) {
		m_wave_model_name = "legacy_waves";
	}
	update_wave_filter_options();
	{
		QString active_filter = m_wave_filter_combo != nullptr ? m_wave_filter_combo->currentText() : "all";

		if (desired_filter != "all" && active_filter == desired_filter) {
			QString path = QString("/datasets/%1/waves?start=%2&count=%3&class_name=%4")
				.arg(dataset_id)
				.arg(start)
				.arg(count)
				.arg(active_filter);

			if (order_by_fit_error) {
				path += "&order_by=fit_error";
			}
			if (!m_api.get(path, &doc, &err)) {
				append_log(QString("waves load failed: %1").arg(err));
				QMessageBox::warning(this, "Waves", "Load failed: " + err);
				return;
			}
		} else {
			doc = summary_doc;
		}
	}
	m_wave_summary_obj = doc.object();
	update_wave_table_headers();
	rows = doc.object().value("waves").toArray();
	m_wave_rows = rows;
	update_wave_overlay_property_options();
	refresh_wave_table();
	m_wave_summary_text->setPlainText(QString("Active model: %1\n\n%2")
		.arg(m_wave_model_name)
		.arg(QString::fromUtf8(doc.toJson(QJsonDocument::Indented))));
	append_log(QString("waves loaded: dataset=%1 count=%2 total=%3")
		.arg(dataset_id)
		.arg(rows.size())
		.arg(doc.object().value("total").toInt()));
	if (!rows.isEmpty()) {
		m_waves_table->selectRow(0);
		on_wave_row_changed();
	} else {
		update_wave_token_properties(-1);
		update_wave_layer1_strings(-1);
		if (m_wave_chart != nullptr) {
			m_wave_chart->clear();
		}
	}
}

void main_window::load_predictor_waves()
{
	QJsonDocument doc;
	QString err;
	int dataset_id = m_predictor_dataset_id_spin->value();
	int start = m_predictor_start_spin->value();
	int count = m_predictor_count_spin->value();
	QString path = QString("/datasets/%1/waves?start=%2&count=%3")
		.arg(dataset_id)
		.arg(start)
		.arg(count);

	m_api.set_base_url(current_base_url());
	if (!m_api.get(path, &doc, &err)) {
		append_log(QString("predictor waves load failed: %1").arg(err));
		QMessageBox::warning(this, "Predictor", "Load failed: " + err);
		return;
	}
	m_predictor_rows = doc.object().value("waves").toArray();
	refresh_predictor_table();
	m_predictor_summary_text->setPlainText(QString::fromUtf8(doc.toJson(QJsonDocument::Indented)));
	m_predictor_signal_text->clear();
	append_log(QString("predictor waves loaded: dataset=%1 count=%2")
		.arg(dataset_id)
		.arg(m_predictor_rows.size()));
	if (!m_predictor_rows.isEmpty()) {
		m_predictor_waves_table->selectRow(0);
		update_predictor_view();
	} else {
		if (m_predictor_chart != nullptr) {
			m_predictor_chart->clear();
		}
		if (m_predictor_actual_chart != nullptr) {
			m_predictor_actual_chart->clear();
		}
		m_predictor_horizons_table->setRowCount(0);
	}
}

void main_window::refresh_predictor_table()
{
	if (m_predictor_waves_table == nullptr) {
		return;
	}
	m_predictor_waves_table->setRowCount(m_predictor_rows.size());
	for (int i = 0; i < m_predictor_rows.size(); ++i) {
		QJsonObject token = m_predictor_rows.at(i).toObject();
		QString class_name = token.value("class_name").toString();
		QString direction_name = token.value("direction").toString();
		QTableWidgetItem *row_item = new QTableWidgetItem(QString::number(m_predictor_start_spin->value() + i));

		row_item->setData(Qt::UserRole, i);
		row_item->setData(Qt::UserRole + 1, m_predictor_start_spin->value() + i);
		m_predictor_waves_table->setItem(i, 0, row_item);
		m_predictor_waves_table->setItem(i, 1, new QTableWidgetItem(QString::number(token.value("start_candle").toInt())));
		m_predictor_waves_table->setItem(i, 2, new QTableWidgetItem(QString::number(token.value("end_candle").toInt())));
		m_predictor_waves_table->setItem(i, 3, new QTableWidgetItem(class_name));
		m_predictor_waves_table->setItem(i, 4, new QTableWidgetItem(direction_name));
		m_predictor_waves_table->setItem(i, 5, new QTableWidgetItem(QString::number(token.value("net_move_raw").toInt())));
		m_predictor_waves_table->setItem(i, 6, new QTableWidgetItem(QString::number(token.value("duration_candles").toInt())));
	}
	m_predictor_waves_table->resizeRowsToContents();
	m_predictor_waves_table->resizeColumnsToContents();
}

int main_window::current_predictor_source_row() const
{
	int table_row;
	QTableWidgetItem *item;
	bool ok = false;

	if (m_predictor_waves_table == nullptr) {
		return -1;
	}
	table_row = m_predictor_waves_table->currentRow();
	if (table_row < 0) {
		return -1;
	}
	item = m_predictor_waves_table->item(table_row, 0);
	if (item == nullptr) {
		return -1;
	}
	table_row = item->data(Qt::UserRole).toInt(&ok);
	return ok ? table_row : -1;
}

void main_window::on_predictor_row_changed()
{
	update_predictor_view();
}

void main_window::update_predictor_view()
{
	int row = current_predictor_source_row();
	int anchor_token = -1;
	QJsonDocument doc;
	QString err;
	int start_candle;
	int end_candle;
	int seed_start_candle = -1;
	int seed_end_candle = -1;
	QJsonArray horizons;
	QVector<cluster_chart_widget::wave_overlay_item> overlay_items;
	QStringList signal_lines;
	int future_start = -1;
	int future_end = -1;

	if (row < 0 || row >= m_predictor_rows.size()) {
		if (m_predictor_chart != nullptr) {
			m_predictor_chart->clear();
		}
		if (m_predictor_actual_chart != nullptr) {
			m_predictor_actual_chart->clear();
		}
		if (m_predictor_horizons_table != nullptr) {
			m_predictor_horizons_table->setRowCount(0);
		}
		return;
	}
	start_candle = m_predictor_rows.at(row).toObject().value("start_candle").toInt();
	end_candle = m_predictor_rows.at(row).toObject().value("end_candle").toInt();
	if (m_predictor_waves_table != nullptr && m_predictor_waves_table->currentRow() >= 0) {
		QTableWidgetItem *item = m_predictor_waves_table->item(m_predictor_waves_table->currentRow(), 0);

		if (item != nullptr) {
			anchor_token = item->data(Qt::UserRole + 1).toInt();
		}
	}
	if (anchor_token < 0) {
		return;
	}
	m_api.set_base_url(current_base_url());
	if (!m_api.get(QString("/datasets/%1/wave_predictor?anchor_token=%2&input_len=%3&horizon=%4")
			.arg(m_predictor_dataset_id_spin->value())
			.arg(anchor_token)
			.arg(m_predictor_input_len_spin->value())
			.arg(m_predictor_horizon_spin->value()),
		&doc,
		&err)) {
		append_log(QString("wave predictor load failed: %1").arg(err));
		QMessageBox::warning(this, "Predictor", "Predictor load failed: " + err);
		return;
	}
	m_predictor_doc_obj = doc.object();
	horizons = m_predictor_doc_obj.value("horizons").toArray();
	m_predictor_horizons_table->setRowCount(horizons.size());
	for (int i = 0; i < horizons.size(); ++i) {
		QJsonObject horizon_obj = horizons.at(i).toObject();
		QJsonObject actual_future = horizon_obj.value("actual_future").toObject();
		QString actual_text = QString("%1 / %2")
			.arg(actual_future.value("class_name").toString())
			.arg(actual_future.value("direction").toString());
		QString pred_class = horizon_obj.value("predicted_class").toString();
		QString pred_dir = horizon_obj.value("predicted_direction").toString();
		cluster_chart_widget::wave_overlay_item item;
		QJsonArray class_signals = horizon_obj.value("top_class_signals").toArray();
		QJsonArray dir_signals = horizon_obj.value("top_direction_signals").toArray();

		m_predictor_horizons_table->setItem(i, 0, new QTableWidgetItem(QString::number(horizon_obj.value("horizon").toInt())));
		m_predictor_horizons_table->setItem(i, 1, new QTableWidgetItem(actual_text));
		m_predictor_horizons_table->setItem(i, 2, new QTableWidgetItem(pred_class));
		m_predictor_horizons_table->setItem(i, 3, new QTableWidgetItem(QString::number(horizon_obj.value("predicted_class_probability").toDouble(), 'f', 4)));
		m_predictor_horizons_table->setItem(i, 4, new QTableWidgetItem(pred_dir));
		m_predictor_horizons_table->setItem(i, 5, new QTableWidgetItem(QString::number(horizon_obj.value("predicted_direction_probability").toDouble(), 'f', 4)));
		m_predictor_horizons_table->setItem(i, 6, new QTableWidgetItem(horizon_obj.value("predicted_move_bucket").toString()));
		m_predictor_horizons_table->setItem(i, 7, new QTableWidgetItem(QString::number(horizon_obj.value("expected_net_move_raw").toDouble(), 'f', 2)));
		m_predictor_horizons_table->setItem(i, 8, new QTableWidgetItem(QString::number(horizon_obj.value("support").toInt())));
		item.start_index = actual_future.value("start_candle").toInt();
		item.end_index = actual_future.value("end_candle").toInt();
		item.label = QString("h%1 %2 %3%%\n%4 %5%%")
			.arg(horizon_obj.value("horizon").toInt())
			.arg(pred_class)
			.arg(horizon_obj.value("predicted_class_probability").toDouble() * 100.0, 0, 'f', 0)
			.arg(pred_dir)
			.arg(horizon_obj.value("predicted_direction_probability").toDouble() * 100.0, 0, 'f', 0);
		overlay_items.push_back(item);
		if (future_start < 0 || item.start_index < future_start) {
			future_start = item.start_index;
		}
		if (future_end < 0 || item.end_index > future_end) {
			future_end = item.end_index;
		}
		signal_lines << QString("h%1 class -> %2").arg(horizon_obj.value("horizon").toInt()).arg(pred_class);
		for (const QJsonValue &signal_value : class_signals) {
			QJsonObject signal_obj = signal_value.toObject();

			signal_lines << QString("  %1 | support=%2 lift=%3 contrib=%4")
				.arg(signal_obj.value("feature").toString())
				.arg(signal_obj.value("support").toInt())
				.arg(QString::number(signal_obj.value("lift").toDouble(), 'f', 3))
				.arg(QString::number(signal_obj.value("contribution").toDouble(), 'f', 3));
		}
		signal_lines << QString("h%1 direction -> %2").arg(horizon_obj.value("horizon").toInt()).arg(pred_dir);
		for (const QJsonValue &signal_value : dir_signals) {
			QJsonObject signal_obj = signal_value.toObject();

			signal_lines << QString("  %1 | support=%2 lift=%3 contrib=%4")
				.arg(signal_obj.value("feature").toString())
				.arg(signal_obj.value("support").toInt())
				.arg(QString::number(signal_obj.value("lift").toDouble(), 'f', 3))
				.arg(QString::number(signal_obj.value("contribution").toDouble(), 'f', 3));
		}
	}
	m_predictor_horizons_table->resizeRowsToContents();
	m_predictor_horizons_table->resizeColumnsToContents();
	m_predictor_summary_text->setPlainText(QString::fromUtf8(doc.toJson(QJsonDocument::Indented)));
	m_predictor_signal_text->setPlainText(signal_lines.join("\n"));
	load_chart_into(m_predictor_actual_chart,
		m_predictor_dataset_id_spin->value(),
		start_candle,
		end_candle,
		seed_start_candle,
		seed_end_candle);
	if (future_start >= 0 && future_end >= future_start) {
		load_chart_into(m_predictor_chart,
			m_predictor_dataset_id_spin->value(),
			future_start,
			future_end,
			-1,
			-1,
			&overlay_items);
	} else if (m_predictor_chart != nullptr) {
		m_predictor_chart->clear();
	}
}

void main_window::on_wave_filter_changed()
{
	on_load_waves();
}

void main_window::on_wave_row_changed()
{
	int row = current_wave_source_row();
	int start_candle;
	int end_candle;
	int seed_start_candle;
	int seed_end_candle;

	if (row < 0) {
		update_wave_token_properties(-1);
		update_wave_layer1_strings(-1);
		if (m_wave_chart != nullptr) {
			m_wave_chart->set_wave_overlay(QVector<cluster_chart_widget::wave_overlay_item>());
			m_wave_chart->clear();
		}
		return;
	}
	start_candle = m_waves_table->item(row, 1)->text().toInt();
	end_candle = m_waves_table->item(row, 2)->text().toInt();
	seed_start_candle = start_candle;
	seed_end_candle = end_candle;
	if (row >= 0 && row < m_wave_rows.size()) {
		QJsonObject token = m_wave_rows.at(row).toObject();

		if ((m_wave_model_name == "relative_waves_seeded" ||
			 m_wave_model_name == "relative_waves_iterative") &&
			m_wave_show_seed_check != nullptr &&
			m_wave_show_seed_check->isChecked()) {
			seed_start_candle = token.value("seed_start_candle").toInt(start_candle);
			seed_end_candle = token.value("seed_end_candle").toInt(end_candle);
		} else {
			seed_start_candle = -1;
			seed_end_candle = -1;
		}
	}
	update_wave_token_properties(row);
	update_wave_layer1_strings(row);
	load_chart_into(m_wave_chart,
		m_wave_dataset_id_spin->value(),
		start_candle,
		end_candle,
		seed_start_candle,
		seed_end_candle);
}

int main_window::current_wave_source_row() const
{
	int table_row;
	QTableWidgetItem *item;
	bool ok = false;

	if (m_waves_table == nullptr) {
		return -1;
	}
	table_row = m_waves_table->currentRow();
	if (table_row < 0) {
		return -1;
	}
	item = m_waves_table->item(table_row, 0);
	if (item == nullptr) {
		return -1;
	}
	table_row = item->data(Qt::UserRole).toInt(&ok);
	return ok ? table_row : -1;
}

void main_window::update_wave_token_properties(int row)
{
	int visible_property_count = 0;

	if (m_wave_token_props_table == nullptr) {
		return;
	}
	m_wave_token_props_table->setRowCount(0);
	if (row < 0 || row >= m_wave_rows.size()) {
		update_wave_token_properties_height(0);
		return;
	}
	{
		QJsonObject obj = m_wave_rows.at(row).toObject();
		QStringList keys = obj.keys();
		QStringList visible_keys;
		int pair_count_per_row = 4;
		int row_count;

		keys.sort();
		for (const QString &key : keys) {
			if (is_wave_property_visible(key)) {
				visible_keys.push_back(key);
			}
		}
		visible_property_count = visible_keys.size();
		row_count = (visible_property_count + pair_count_per_row - 1) / pair_count_per_row;
		m_wave_token_props_table->setRowCount(row_count);
		for (int i = 0; i < visible_keys.size(); ++i) {
			QJsonValue value = obj.value(visible_keys.at(i));
			QString value_text;
				int table_row = i / pair_count_per_row;
				int column_base = (i % pair_count_per_row) * 2;
				bool relevant = is_wave_property_relevant(visible_keys.at(i));
				QTableWidgetItem *key_item;

				value_text = json_value_to_text(value);
				key_item = new QTableWidgetItem(visible_keys.at(i));
				key_item->setData(Qt::UserRole, visible_keys.at(i));
				key_item->setData(Qt::UserRole + 1, relevant);
				if (relevant) {
					QColor bg(255, 249, 196);

					key_item->setBackground(bg);
					key_item->setToolTip("Double-click to inspect how this value was produced.");
				}
				m_wave_token_props_table->setItem(table_row, column_base, key_item);
				set_property_value_cell(m_wave_token_props_table,
					table_row,
					column_base + 1,
					value_text,
					relevant,
					relevant ? "Double-click the property name to inspect how this value was produced." : QString());
			}
		}
	m_wave_token_props_table->resizeRowsToContents();
	update_wave_token_properties_height(visible_property_count);
}

void main_window::update_wave_layer1_strings(int row)
{
	QJsonArray child_strings;

	if (m_wave_layer1_table == nullptr) {
		return;
	}
	m_wave_layer1_table->setRowCount(0);
	if (row < 0 || row >= m_wave_rows.size()) {
		return;
	}
	child_strings = m_wave_rows.at(row).toObject().value("child_strings").toArray();
	populate_wave_composition_table(m_wave_layer1_table, child_strings);
}

void main_window::on_show_wave_penalty_calculation()
{
	int row = current_wave_source_row();
	QDialog dialog(this);
	QVBoxLayout *layout;
	QTabWidget *tabs;
	QTextEdit *summary_text;
	QTextEdit *trend_text;
	QTextEdit *range_text;
	QDialogButtonBox *buttons;
	QJsonObject token;
	QJsonObject trace;
	QJsonObject summary_obj;
	QJsonArray trend_terms;
	QJsonArray range_terms;

	if (row < 0 || row >= m_wave_rows.size()) {
		QMessageBox::information(this, "Penalty Calculation", "Select a wave first.");
		return;
	}
	token = m_wave_rows.at(row).toObject();
	trace = token.value("penalty_trace").toObject();
	if (trace.isEmpty()) {
		QMessageBox::information(this, "Penalty Calculation", "No penalty trace is available for this wave.");
		return;
	}
	dialog.setWindowTitle(QString("Penalty Calculation: token %1").arg(token.value("token_id").toInt()));
	dialog.resize(900, 700);
	layout = new QVBoxLayout(&dialog);
	tabs = new QTabWidget(&dialog);
	summary_text = new QTextEdit(&dialog);
	trend_text = new QTextEdit(&dialog);
	range_text = new QTextEdit(&dialog);
	buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
	summary_text->setReadOnly(true);
	trend_text->setReadOnly(true);
	range_text->setReadOnly(true);
	summary_obj.insert("model", trace.value("model"));
	summary_obj.insert("chosen_class", trace.value("chosen_class"));
	summary_obj.insert("chosen_error", trace.value("chosen_error"));
	summary_obj.insert("second_best_error", trace.value("second_best_error"));
	summary_obj.insert("dp_bias", trace.value("dp_bias"));
	summary_obj.insert("metrics", trace.value("metrics"));
	trend_terms = trace.value("trend_terms").toArray();
	range_terms = trace.value("range_terms").toArray();
	summary_text->setPlainText(QString::fromUtf8(QJsonDocument(summary_obj).toJson(QJsonDocument::Indented)));
	trend_text->setPlainText(QString::fromUtf8(QJsonDocument(trend_terms).toJson(QJsonDocument::Indented)));
	range_text->setPlainText(QString::fromUtf8(QJsonDocument(range_terms).toJson(QJsonDocument::Indented)));
	tabs->addTab(summary_text, "Summary");
	tabs->addTab(trend_text, "Trend");
	tabs->addTab(range_text, "Range");
	layout->addWidget(tabs, 1);
	layout->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	dialog.exec();
}

void main_window::on_configure_wave_properties()
{
	QDialog dialog(this);
	QVBoxLayout *layout = new QVBoxLayout(&dialog);
	QScrollArea *scroll = new QScrollArea(&dialog);
	QWidget *content = new QWidget(scroll);
	QGridLayout *grid = new QGridLayout(content);
	QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	QPushButton *show_all_btn = new QPushButton("Show All", &dialog);
	QPushButton *hide_all_btn = new QPushButton("Hide All", &dialog);
	QStringList keys = available_wave_property_names();
	QVector<QCheckBox *> checks;

	dialog.setWindowTitle("Visible Token Properties");
	scroll->setWidgetResizable(true);
	scroll->setWidget(content);
	for (int i = 0; i < keys.size(); ++i) {
		QCheckBox *check = new QCheckBox(keys.at(i), content);
		int row = i / 3;
		int col = i % 3;

		check->setChecked(is_wave_property_visible(keys.at(i)));
		grid->addWidget(check, row, col);
		checks.push_back(check);
	}
	{
		QHBoxLayout *btn_row = new QHBoxLayout();

		btn_row->addWidget(show_all_btn);
		btn_row->addWidget(hide_all_btn);
		btn_row->addStretch(1);
		layout->addLayout(btn_row);
	}
	layout->addWidget(scroll, 1);
	layout->addWidget(buttons);
	connect(show_all_btn, &QPushButton::clicked, &dialog, [&checks]() {
		for (QCheckBox *check : checks) {
			if (check != nullptr) {
				check->setChecked(true);
			}
		}
	});
	connect(hide_all_btn, &QPushButton::clicked, &dialog, [&checks]() {
		for (QCheckBox *check : checks) {
			if (check != nullptr) {
				check->setChecked(false);
			}
		}
	});
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	if (dialog.exec() != QDialog::Accepted) {
		return;
	}
	m_hidden_wave_properties.clear();
	for (int i = 0; i < keys.size() && i < checks.size(); ++i) {
		if (checks.at(i) != nullptr && !checks.at(i)->isChecked()) {
			m_hidden_wave_properties.push_back(keys.at(i));
		}
	}
	save_wave_property_visibility();
	update_wave_token_properties(m_waves_table != nullptr ? m_waves_table->currentRow() : -1);
}

void main_window::on_configure_live_properties()
{
	QDialog dialog(this);
	QVBoxLayout *layout = new QVBoxLayout(&dialog);
	QScrollArea *scroll = new QScrollArea(&dialog);
	QWidget *content = new QWidget(scroll);
	QGridLayout *grid = new QGridLayout(content);
	QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	QPushButton *show_all_btn = new QPushButton("Show All", &dialog);
	QPushButton *hide_all_btn = new QPushButton("Hide All", &dialog);
	QStringList keys = available_live_property_names();
	QVector<QCheckBox *> checks;

	dialog.setWindowTitle("Live Property Visibility");
	scroll->setWidgetResizable(true);
	scroll->setWidget(content);
	for (int i = 0; i < keys.size(); ++i) {
		QCheckBox *check = new QCheckBox(keys.at(i), content);
		int row = i / 3;
		int col = i % 3;

		check->setChecked(is_live_property_visible(keys.at(i)));
		grid->addWidget(check, row, col);
		checks.push_back(check);
	}
	{
		QHBoxLayout *btn_row = new QHBoxLayout();

		btn_row->addWidget(show_all_btn);
		btn_row->addWidget(hide_all_btn);
		btn_row->addStretch(1);
		layout->addLayout(btn_row);
	}
	layout->addWidget(scroll, 1);
	layout->addWidget(buttons);
	connect(show_all_btn, &QPushButton::clicked, &dialog, [&checks]() {
		for (QCheckBox *check : checks) {
			if (check != nullptr) {
				check->setChecked(true);
			}
		}
	});
	connect(hide_all_btn, &QPushButton::clicked, &dialog, [&checks]() {
		for (QCheckBox *check : checks) {
			if (check != nullptr) {
				check->setChecked(false);
			}
		}
	});
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	if (dialog.exec() != QDialog::Accepted) {
		return;
	}
	m_hidden_live_properties.clear();
	for (int i = 0; i < keys.size() && i < checks.size(); ++i) {
		if (checks.at(i) != nullptr && !checks.at(i)->isChecked()) {
			m_hidden_live_properties.push_back(keys.at(i));
		}
	}
	m_hidden_live_properties.sort();
	save_live_property_visibility();
	update_live_properties(m_live_waves_table != nullptr ? m_live_waves_table->currentRow() : -1);
}

void main_window::on_wave_property_item_double_clicked(QTableWidgetItem *item)
{
	int row;
	QString key;

	if (item == NULL) {
		return;
	}
	key = item->data(Qt::UserRole).toString();
	if (key.isEmpty() || !item->data(Qt::UserRole + 1).toBool()) {
		return;
	}
	row = current_wave_source_row();
	if (row < 0 || row >= m_wave_rows.size()) {
		return;
	}
	show_wave_property_detail_dialog(key, m_wave_rows.at(row).toObject());
}

void main_window::show_wave_property_detail_dialog(const QString &key, const QJsonObject &token)
{
	QJsonObject params = m_wave_summary_obj.value("params").toObject();
	QJsonDocument doc;
	QString err;
	QJsonArray candle_rows;
	QVector<wave_detail_candle_row> detail_rows;
	relative_metric_breakdown calc;
	QString formula_text;
	QStringList input_names;
	QStringList input_values;
	QStringList input_contribs;
	int dataset_id = m_wave_dataset_id_spin != NULL ? m_wave_dataset_id_spin->value() : 0;
	int start_candle = token.value("start_candle").toInt();
	int end_candle = token.value("end_candle").toInt();
	int norm_scale;
	int epsilon;
	int min_wave_move_pips;
	int weak_fit_threshold;
	QDialog dialog(this);
	QVBoxLayout *layout;
	QLabel *summary_label;
	QTextEdit *formula_view;
	QTableWidget *input_table;
	QTableWidget *candle_table;
	QDialogButtonBox *buttons;

	if (params.isEmpty()) {
		QJsonParseError parse_err;
		QJsonDocument parsed = QJsonDocument::fromJson(m_wave_summary_text->toPlainText().toUtf8(), &parse_err);

		if (parse_err.error == QJsonParseError::NoError && parsed.isObject()) {
			params = parsed.object().value("params").toObject();
		}
	}
	norm_scale = params.value("norm_scale").toInt(1000);
	epsilon = params.value("epsilon").toInt(1);
	min_wave_move_pips = params.value("min_wave_move_pips").toInt(0);
	weak_fit_threshold = params.value("weak_fit_threshold").toInt(140);

	m_api.set_base_url(current_base_url());
	if (!m_api.get(QString("/datasets/%1/candles?start=%2&count=%3")
			.arg(dataset_id)
			.arg(start_candle)
			.arg(end_candle - start_candle + 1),
		&doc,
		&err)) {
		QMessageBox::warning(this, "Token Property", "Unable to load token candles: " + err);
		return;
	}
	candle_rows = doc.object().value("candles").toArray();
	for (int i = 0; i < candle_rows.size(); ++i) {
		QJsonObject o = candle_rows.at(i).toObject();
		wave_detail_candle_row row;

		memset(&row, 0, sizeof(row));
		row.absolute_index = start_candle + i;
		row.open_pips = o.value("open_pips").toInt();
		row.high_pips = o.value("high_pips").toInt();
		row.low_pips = o.value("low_pips").toInt();
		row.close_pips = o.value("close_pips").toInt();
		row.body = row.close_pips - row.open_pips;
		row.abs_body = abs_i32_gui(row.body);
		row.range = row.high_pips - row.low_pips;
		row.body_lo = min_i32_gui(row.open_pips, row.close_pips);
		row.body_hi = max_i32_gui(row.open_pips, row.close_pips);
		row.upper_wick = row.high_pips - row.body_hi;
		row.lower_wick = row.body_lo - row.low_pips;
		detail_rows.push_back(row);
	}
	calc = compute_relative_metric_breakdown(&detail_rows, norm_scale, epsilon);

	auto add_input = [&](const QString &name, const QString &value, const QString &contrib = QString()) {
		input_names.push_back(name);
		input_values.push_back(value);
		input_contribs.push_back(contrib);
	};
	auto tok_i = [&](const char *name, int default_value = 0) {
		return token.value(QString::fromUtf8(name)).toInt(default_value);
	};
	auto score_breakdown = [&](int net_move_raw,
		int signed_net_move_raw,
		int der_step,
		int der_body,
		int retracement_ratio,
		int body_eff,
		int overlap_ratio,
		int sign_density,
		int wick_bias_aligned,
		int *out_strong,
		int *out_weak,
		int *out_trend,
		int *out_range) {
		int strong = 0;
		int weak = 0;
		int range = 0;
		int move_penalty = penalty_low_gui(net_move_raw, min_wave_move_pips);

		strong += 4 * penalty_low_gui(der_step, params.value("relative_trend_der_step_min").toInt(550) + 120);
		strong += 3 * penalty_low_gui(der_body, params.value("relative_trend_der_body_min").toInt(500) + 80);
		strong += 4 * penalty_high_gui(retracement_ratio, params.value("relative_strong_trend_retracement_max").toInt(180));
		strong += 3 * penalty_low_gui(body_eff, params.value("relative_trend_body_eff_min").toInt(450) + 60);
		strong += 3 * penalty_high_gui(overlap_ratio, max_i32_gui(params.value("relative_trend_overlap_max").toInt(350) - 100, 0));
		strong += 2 * penalty_high_gui(sign_density, max_i32_gui(params.value("relative_trend_sign_flip_max").toInt(450) - 120, 0));
		strong += penalty_low_gui(wick_bias_aligned, params.value("relative_trend_wick_bias_min").toInt(1050));

		weak += 4 * penalty_low_gui(der_step, params.value("relative_trend_der_step_min").toInt(550));
		weak += 3 * penalty_low_gui(der_body, params.value("relative_trend_der_body_min").toInt(500));
		weak += 4 * penalty_high_gui(retracement_ratio, params.value("relative_trend_retracement_max").toInt(300));
		weak += 3 * penalty_low_gui(body_eff, params.value("relative_trend_body_eff_min").toInt(450));
		weak += 3 * penalty_high_gui(overlap_ratio, params.value("relative_trend_overlap_max").toInt(350));
		weak += 2 * penalty_high_gui(sign_density, params.value("relative_trend_sign_flip_max").toInt(450));
		weak += penalty_low_gui(wick_bias_aligned, params.value("relative_trend_wick_bias_min").toInt(1050));

		range += 4 * penalty_high_gui(der_step, params.value("relative_range_der_step_max").toInt(500));
		range += 3 * penalty_high_gui(body_eff, params.value("relative_range_body_eff_max").toInt(500));
		range += 3 * penalty_low_gui(overlap_ratio, params.value("relative_range_overlap_min").toInt(350));
		range += 2 * penalty_low_gui(sign_density, params.value("relative_range_sign_flip_min").toInt(400));
		range += 2 * penalty_low_gui(retracement_ratio, params.value("relative_trend_retracement_max").toInt(300));
		range += penalty_low_gui(norm_scale - wick_bias_aligned, 0);
		range += move_penalty;

		*out_strong = strong;
		*out_weak = weak;
		*out_trend = (signed_net_move_raw == 0) ? 0x3fffffff : (min_i32_gui(strong, weak) + move_penalty);
		*out_range = range;
	};

	formula_text = QString("Clicked property: %1\nToken %2, candles %3..%4, class=%5")
		.arg(key)
		.arg(token.value("token_index").toInt())
		.arg(start_candle)
		.arg(end_candle)
		.arg(token.value("class_name").toString());

	if (is_relative_wave_model_name(m_wave_model_name)) {
		if (key == "relative_overlap_ratio") {
			formula_text += "\n\nrelative_overlap_ratio = relative_body_overlap_sum_raw * norm_scale / max(relative_abs_body_sum_raw, epsilon)";
			add_input("relative_body_overlap_sum_raw", QString::number(tok_i("relative_body_overlap_sum_raw")));
			add_input("relative_abs_body_sum_raw", QString::number(tok_i("relative_abs_body_sum_raw")));
			add_input("norm_scale", QString::number(norm_scale));
			add_input("epsilon", QString::number(epsilon));
			add_input("recomputed", QString::number((tok_i("relative_body_overlap_sum_raw") * norm_scale) / max_i32_gui(tok_i("relative_abs_body_sum_raw"), epsilon)));
		} else if (key == "relative_body_overlap_sum_raw") {
			formula_text += "\n\nrelative_body_overlap_sum_raw = sum of positive body overlap between each adjacent candle pair.";
			add_input("sum(overlap_with_prev)", QString::number(calc.relative_body_overlap_sum_raw));
		} else if (key == "relative_abs_step_sum_raw") {
			formula_text += "\n\nrelative_abs_step_sum_raw = sum(abs(close_i - close_(i-1))) over the token.";
			add_input("sum(step_abs)", QString::number(calc.relative_abs_step_sum_raw));
		} else if (key == "relative_abs_body_sum_raw") {
			formula_text += "\n\nrelative_abs_body_sum_raw = sum(abs(close_i - open_i)) over all candles in the token.";
			add_input("sum(abs_body)", QString::number(calc.relative_abs_body_sum_raw));
		} else if (key == "relative_directional_efficiency_step") {
			formula_text += "\n\nrelative_directional_efficiency_step = net_move_raw * norm_scale / max(relative_abs_step_sum_raw, epsilon)";
			add_input("net_move_raw", QString::number(tok_i("net_move_raw")));
			add_input("relative_abs_step_sum_raw", QString::number(tok_i("relative_abs_step_sum_raw")));
			add_input("norm_scale", QString::number(norm_scale));
			add_input("epsilon", QString::number(epsilon));
			add_input("recomputed", QString::number((tok_i("net_move_raw") * norm_scale) / max_i32_gui(tok_i("relative_abs_step_sum_raw"), epsilon)));
		} else if (key == "relative_directional_efficiency_body") {
			formula_text += "\n\nrelative_directional_efficiency_body = net_move_raw * norm_scale / max(relative_abs_body_sum_raw, epsilon)";
			add_input("net_move_raw", QString::number(tok_i("net_move_raw")));
			add_input("relative_abs_body_sum_raw", QString::number(tok_i("relative_abs_body_sum_raw")));
			add_input("norm_scale", QString::number(norm_scale));
			add_input("epsilon", QString::number(epsilon));
			add_input("recomputed", QString::number((tok_i("net_move_raw") * norm_scale) / max_i32_gui(tok_i("relative_abs_body_sum_raw"), epsilon)));
		} else if (key == "relative_avg_body_efficiency") {
			formula_text += "\n\nrelative_avg_body_efficiency = average(abs(body_i) * norm_scale / max(range_i, epsilon)) over token candles.";
			add_input("body_efficiency_sum", QString::number(calc.relative_avg_body_efficiency * max_i32_gui(calc.duration, 1)));
			add_input("duration", QString::number(calc.duration));
			add_input("recomputed", QString::number(calc.relative_avg_body_efficiency));
		} else if (key == "relative_body_direction_share") {
			formula_text += "\n\nrelative_body_direction_share = aligned_body_sum * norm_scale / max(aligned_body_sum + counter_body_sum, epsilon).";
			add_input("recomputed", QString::number(calc.relative_body_direction_share));
		} else if (key == "relative_upper_wick_sum_raw" || key == "relative_lower_wick_sum_raw") {
			formula_text += "\n\nThis value is a straight sum of per-candle wick lengths across the token.";
			add_input("recomputed", QString::number((key == "relative_upper_wick_sum_raw") ? calc.relative_upper_wick_sum_raw : calc.relative_lower_wick_sum_raw));
		} else if (key == "relative_total_wick_sum_raw") {
			formula_text += "\n\nrelative_total_wick_sum_raw = relative_upper_wick_sum_raw + relative_lower_wick_sum_raw";
			add_input("relative_upper_wick_sum_raw", QString::number(calc.relative_upper_wick_sum_raw));
			add_input("relative_lower_wick_sum_raw", QString::number(calc.relative_lower_wick_sum_raw));
			add_input("recomputed", QString::number(calc.relative_total_wick_sum_raw));
		} else if (key == "relative_wick_to_body_ratio") {
			formula_text += "\n\nrelative_wick_to_body_ratio = relative_total_wick_sum_raw * norm_scale / max(relative_abs_body_sum_raw, epsilon)";
			add_input("relative_total_wick_sum_raw", QString::number(calc.relative_total_wick_sum_raw));
			add_input("relative_abs_body_sum_raw", QString::number(calc.relative_abs_body_sum_raw));
			add_input("norm_scale", QString::number(norm_scale));
			add_input("epsilon", QString::number(epsilon));
			add_input("recomputed", QString::number(calc.relative_wick_to_body_ratio));
		} else if (key == "relative_wick_bias_up" || key == "relative_wick_bias_down" || key == "relative_wick_bias_aligned") {
			formula_text += "\n\nWick bias ratios use the summed upper and lower wick lengths with epsilon protection.";
			add_input("upper_wick_sum_raw", QString::number(calc.relative_upper_wick_sum_raw));
			add_input("lower_wick_sum_raw", QString::number(calc.relative_lower_wick_sum_raw));
			add_input("norm_scale", QString::number(norm_scale));
			add_input("epsilon", QString::number(epsilon));
			add_input("wick_bias_up", QString::number(calc.relative_wick_bias_up));
			add_input("wick_bias_down", QString::number(calc.relative_wick_bias_down));
			add_input("wick_bias_aligned", QString::number(calc.relative_wick_bias_aligned));
		} else if (key == "relative_max_pullback_raw" || key == "relative_impulse_leg_raw" || key == "relative_retracement_ratio") {
			formula_text += "\n\nPullback and impulse are computed from the running close-based extreme in the dominant direction.";
			add_input("relative_max_pullback_raw", QString::number(calc.relative_max_pullback_raw));
			add_input("relative_impulse_leg_raw", QString::number(calc.relative_impulse_leg_raw));
			add_input("relative_retracement_ratio", QString::number(calc.relative_retracement_ratio));
			add_input("norm_scale", QString::number(norm_scale));
			add_input("epsilon", QString::number(epsilon));
		} else if (key == "relative_range_growth_ratio" || key == "relative_body_growth_ratio") {
			formula_text += "\n\nGrowth ratios compare average second-half range/body size versus first-half average, scaled by norm_scale.";
			add_input("relative_range_growth_ratio", QString::number(calc.relative_range_growth_ratio));
			add_input("relative_body_growth_ratio", QString::number(calc.relative_body_growth_ratio));
		} else if (key == "sign_change_density") {
			formula_text += "\n\nsign_change_density = sign_changes * norm_scale / max(duration - 1, 1), using effective body signs after ignoring inside/doji candles and tolerated counter candles.";
			add_input("sign_changes", QString::number(calc.sign_changes));
			add_input("duration", QString::number(calc.duration));
			add_input("norm_scale", QString::number(norm_scale));
			add_input("recomputed", QString::number(calc.sign_change_density));
		} else if (key == "relative_net_move_raw" || key == "net_move_raw" || key == "dominant_direction" || key == "duration_candles") {
			formula_text += "\n\nThis value is produced directly from the token span endpoints.";
			add_input("start_open", QString::number(detail_rows.isEmpty() ? 0 : detail_rows.first().open_pips));
			add_input("end_close", QString::number(detail_rows.isEmpty() ? 0 : detail_rows.last().close_pips));
			add_input("relative_net_move_raw", QString::number(calc.raw_net));
			add_input("net_move_raw", QString::number(calc.net_move_raw));
			add_input("dominant_direction", QString::number(calc.dominant_direction));
			add_input("duration_candles", QString::number(calc.duration));
		} else if (key == "fit_error" || key == "class_name" || key == "class_id" || key == "second_best_error" || key == "ambiguity_margin" || key == "weak_fit") {
			int strong_error;
			int weak_error;
			int trend_error;
			int range_error;
			int best_error;
			int second_error;

			score_breakdown(tok_i("net_move_raw"),
				tok_i("relative_net_move_raw"),
				tok_i("relative_directional_efficiency_step"),
				tok_i("relative_directional_efficiency_body"),
				tok_i("relative_retracement_ratio"),
				tok_i("relative_avg_body_efficiency"),
				tok_i("relative_overlap_ratio"),
				tok_i("sign_change_density"),
				tok_i("relative_wick_bias_aligned"),
				&strong_error,
				&weak_error,
				&trend_error,
				&range_error);
			best_error = min_i32_gui(trend_error, range_error);
			second_error = max_i32_gui(trend_error, range_error);
			formula_text += "\n\nFor relative wave models, fit_error is the winning class error. The dialog below shows the exact trend/range error terms used by the scorer.";
			add_input("trend_strong_error", QString::number(strong_error));
			add_input("trend_weak_error", QString::number(weak_error));
			add_input("trend_error", QString::number(trend_error));
			add_input("range_error", QString::number(range_error));
			add_input("best_fit", QString::number(best_error));
			add_input("second_best_error", QString::number(second_error));
			add_input("ambiguity_margin", QString::number(second_error - best_error));
			add_input("weak_fit_threshold", QString::number(weak_fit_threshold));
			add_input("weak_fit", (best_error > weak_fit_threshold) ? "true" : "false");

			add_input("trend.der_step", QString("%1 vs min %2").arg(tok_i("relative_directional_efficiency_step")).arg(params.value("relative_trend_der_step_min").toInt(550)),
				QString::number(4 * penalty_low_gui(tok_i("relative_directional_efficiency_step"), params.value("relative_trend_der_step_min").toInt(550))));
			add_input("trend.der_body", QString("%1 vs min %2").arg(tok_i("relative_directional_efficiency_body")).arg(params.value("relative_trend_der_body_min").toInt(500)),
				QString::number(3 * penalty_low_gui(tok_i("relative_directional_efficiency_body"), params.value("relative_trend_der_body_min").toInt(500))));
			add_input("trend.retrace", QString("%1 vs max %2").arg(tok_i("relative_retracement_ratio")).arg(params.value("relative_trend_retracement_max").toInt(300)),
				QString::number(4 * penalty_high_gui(tok_i("relative_retracement_ratio"), params.value("relative_trend_retracement_max").toInt(300))));
			add_input("trend.body_eff", QString("%1 vs min %2").arg(tok_i("relative_avg_body_efficiency")).arg(params.value("relative_trend_body_eff_min").toInt(450)),
				QString::number(3 * penalty_low_gui(tok_i("relative_avg_body_efficiency"), params.value("relative_trend_body_eff_min").toInt(450))));
			add_input("trend.overlap", QString("%1 vs max %2").arg(tok_i("relative_overlap_ratio")).arg(params.value("relative_trend_overlap_max").toInt(350)),
				QString::number(3 * penalty_high_gui(tok_i("relative_overlap_ratio"), params.value("relative_trend_overlap_max").toInt(350))));
			add_input("trend.sign_flips", QString("%1 vs max %2").arg(tok_i("sign_change_density")).arg(params.value("relative_trend_sign_flip_max").toInt(450)),
				QString::number(2 * penalty_high_gui(tok_i("sign_change_density"), params.value("relative_trend_sign_flip_max").toInt(450))));
			add_input("trend.wick_bias", QString("%1 vs min %2").arg(tok_i("relative_wick_bias_aligned")).arg(params.value("relative_trend_wick_bias_min").toInt(1050)),
				QString::number(penalty_low_gui(tok_i("relative_wick_bias_aligned"), params.value("relative_trend_wick_bias_min").toInt(1050))));
			add_input("trend_quality_raw", QString::number(calc.relative_trend_quality_raw));
			add_input("trend_quality_bucket", QString::number(calc.relative_trend_quality_bucket));
		} else if (key == "relative_trend_quality_raw" || key == "relative_trend_quality_bucket") {
			int context_range = tok_i("context_range", 1);
			int net_move_norm = (tok_i("net_move_raw") * norm_scale) / max_i32_gui(context_range, 1);
			int quality_raw =
				((norm_scale - min_i32_gui(tok_i("relative_directional_efficiency_step"), norm_scale)) >> 2) +
				((norm_scale - min_i32_gui(tok_i("relative_directional_efficiency_body"), norm_scale)) >> 3) +
				(tok_i("relative_retracement_ratio") >> 3) +
				(tok_i("relative_overlap_ratio") >> 3) +
				(tok_i("relative_wick_to_body_ratio") >> 3) +
				((norm_scale - min_i32_gui(tok_i("relative_avg_body_efficiency"), norm_scale)) >> 3) +
				(tok_i("sign_change_density") >> 4) +
				(penalty_low_gui(tok_i("relative_wick_bias_aligned"), norm_scale) >> 3) +
				(penalty_low_gui(net_move_norm, max_i32_gui(norm_scale / 4, 1)) >> 1);

			if (tok_i("relative_net_move_raw") == 0) {
				quality_raw = 0x3ff;
			}
			formula_text += "\n\nrelative_trend_quality_raw is a continuous trend-quality penalty. Lower is better.\nrelative_trend_quality_bucket uses calibrated bands: <=320 -> 0, <=416 -> 1, <=512 -> 2, <=608 -> 3, <=704 -> 4, <=800 -> 5, <=896 -> 6, else 7.";
			add_input("relative_directional_efficiency_step", QString::number(calc.relative_directional_efficiency_step));
			add_input("relative_directional_efficiency_body", QString::number(calc.relative_directional_efficiency_body));
			add_input("relative_retracement_ratio", QString::number(calc.relative_retracement_ratio));
			add_input("relative_overlap_ratio", QString::number(calc.relative_overlap_ratio));
			add_input("relative_wick_to_body_ratio", QString::number(calc.relative_wick_to_body_ratio));
			add_input("relative_avg_body_efficiency", QString::number(calc.relative_avg_body_efficiency));
			add_input("sign_change_density", QString::number(calc.sign_change_density));
			add_input("relative_wick_bias_aligned", QString::number(calc.relative_wick_bias_aligned));
			add_input("net_move_norm", QString::number(net_move_norm));
			add_input("recomputed_raw", QString::number(quality_raw));
			add_input("recomputed_bucket", QString::number(relative_trend_quality_bucket_from_raw_gui(quality_raw)));
		} else if (key == "knn_class_agreement" || key == "knn_neighbor_purity" ||
			key == "knn_distance_continuation" || key == "knn_distance_exhaustion" ||
			key == "knn_majority_label" || key == "knn_margin") {
			formula_text += "\n\nThese fields are produced by the post-segmentation k-NN pass over final token feature vectors.\nknn_class_agreement = fraction of nearest neighbors that share the token class.\nknn_neighbor_purity = fraction of neighbors belonging to the neighborhood majority label.\nknn_distance_continuation / exhaustion = average distance to the nearest continuation / exhaustion reference cases.\nknn_majority_label = class id of the local neighbor majority.\nknn_margin = knn_distance_exhaustion - knn_distance_continuation; positive means closer to continuation.";
			add_input("k_neighbors", QString::number(8));
			add_input("knn_class_agreement", json_value_to_text(token.value("knn_class_agreement")));
			add_input("knn_neighbor_purity", json_value_to_text(token.value("knn_neighbor_purity")));
			add_input("knn_distance_continuation", json_value_to_text(token.value("knn_distance_continuation")));
			add_input("knn_distance_exhaustion", json_value_to_text(token.value("knn_distance_exhaustion")));
			add_input("knn_majority_label", json_value_to_text(token.value("knn_majority_label")));
			add_input("knn_margin", json_value_to_text(token.value("knn_margin")));
		} else if (key == "boundary_confidence") {
			QJsonDocument neighbor_doc;
			QJsonArray waves;
			int token_index = token.value("token_index").toInt();

			formula_text += "\n\nboundary_confidence = merged_best_fit - (current_fit_error + next_fit_error)\nwhere merged_best_fit is recomputed on the merged span with the next token in original token order.";
			if (m_api.get(QString("/datasets/%1/waves?start=%2&count=2").arg(dataset_id).arg(token_index), &neighbor_doc, &err)) {
				waves = neighbor_doc.object().value("waves").toArray();
				if (waves.size() >= 2) {
					QJsonObject next_token = waves.at(1).toObject();
					QJsonDocument merged_doc;
					QVector<wave_detail_candle_row> merged_rows;
					relative_metric_breakdown merged_calc;
					int strong_error;
					int weak_error;
					int trend_error;
					int range_error;

					if (m_api.get(QString("/datasets/%1/candles?start=%2&count=%3")
							.arg(dataset_id)
							.arg(start_candle)
							.arg(next_token.value("end_candle").toInt() - start_candle + 1),
						&merged_doc,
						&err)) {
						QJsonArray merged_candles = merged_doc.object().value("candles").toArray();

						for (int i = 0; i < merged_candles.size(); ++i) {
							QJsonObject o = merged_candles.at(i).toObject();
							wave_detail_candle_row row;

							memset(&row, 0, sizeof(row));
							row.absolute_index = start_candle + i;
							row.open_pips = o.value("open_pips").toInt();
							row.high_pips = o.value("high_pips").toInt();
							row.low_pips = o.value("low_pips").toInt();
							row.close_pips = o.value("close_pips").toInt();
							row.body = row.close_pips - row.open_pips;
							row.abs_body = abs_i32_gui(row.body);
							row.range = row.high_pips - row.low_pips;
							row.body_lo = min_i32_gui(row.open_pips, row.close_pips);
							row.body_hi = max_i32_gui(row.open_pips, row.close_pips);
							row.upper_wick = row.high_pips - row.body_hi;
							row.lower_wick = row.body_lo - row.low_pips;
							merged_rows.push_back(row);
						}
						merged_calc = compute_relative_metric_breakdown(&merged_rows, norm_scale, epsilon);
						score_breakdown(merged_calc.net_move_raw,
							merged_calc.raw_net,
							merged_calc.relative_directional_efficiency_step,
							merged_calc.relative_directional_efficiency_body,
							merged_calc.relative_retracement_ratio,
							merged_calc.relative_avg_body_efficiency,
							merged_calc.relative_overlap_ratio,
							merged_calc.sign_change_density,
							merged_calc.relative_wick_bias_aligned,
							&strong_error,
							&weak_error,
							&trend_error,
							&range_error);
						add_input("current_fit_error", QString::number(tok_i("fit_error")));
						add_input("next_token_index", QString::number(next_token.value("token_index").toInt()));
						add_input("next_fit_error", QString::number(next_token.value("fit_error").toInt()));
						add_input("merged_trend_error", QString::number(trend_error));
						add_input("merged_range_error", QString::number(range_error));
						add_input("merged_best_fit", QString::number(min_i32_gui(trend_error, range_error)));
						add_input("recomputed_boundary_confidence",
							QString::number(min_i32_gui(trend_error, range_error) - (tok_i("fit_error") + next_token.value("fit_error").toInt())));
					}
				}
			}
		}
	}

	if (input_names.isEmpty()) {
		formula_text += "\n\nNo specialized formula view is implemented for this property yet. The dialog still shows the token candles and the saved token/run values so you can inspect the underlying data.";
		add_input("token_value", json_value_to_text(token.value(key)));
		add_input("norm_scale", QString::number(norm_scale));
		add_input("epsilon", QString::number(epsilon));
	}

	dialog.setWindowTitle(QString("Token Property Detail: %1").arg(key));
	dialog.resize(1500, 900);
	layout = new QVBoxLayout(&dialog);
	summary_label = new QLabel(QString("Token %1, dataset %2, candles %3..%4, selected class %5")
		.arg(token.value("token_index").toInt())
		.arg(dataset_id)
		.arg(start_candle)
		.arg(end_candle)
		.arg(token.value("class_name").toString()), &dialog);
	summary_label->setWordWrap(true);
	formula_view = new QTextEdit(&dialog);
	formula_view->setReadOnly(true);
	formula_view->setPlainText(formula_text);
	formula_view->setMinimumHeight(140);
	input_table = new QTableWidget(&dialog);
	input_table->setColumnCount(3);
	input_table->setHorizontalHeaderLabels({"Field", "Value", "Contribution"});
	input_table->setRowCount(input_names.size());
	input_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	input_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	for (int i = 0; i < input_names.size(); ++i) {
		input_table->setItem(i, 0, new QTableWidgetItem(input_names.at(i)));
		input_table->setItem(i, 1, new QTableWidgetItem(input_values.at(i)));
		input_table->setItem(i, 2, new QTableWidgetItem(input_contribs.value(i)));
	}
	input_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
	input_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
	input_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
	input_table->resizeRowsToContents();

	candle_table = new QTableWidget(&dialog);
	candle_table->setColumnCount(15);
	candle_table->setHorizontalHeaderLabels({"idx", "open", "high", "low", "close", "body", "abs_body", "range", "up_wick", "low_wick", "step_delta", "step_abs", "overlap_prev", "inside", "eff_sign"});
	candle_table->setRowCount(detail_rows.size());
	candle_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	candle_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	for (int i = 0; i < detail_rows.size(); ++i) {
		const wave_detail_candle_row &row = detail_rows.at(i);

		candle_table->setItem(i, 0, new QTableWidgetItem(QString::number(row.absolute_index)));
		candle_table->setItem(i, 1, new QTableWidgetItem(QString::number(row.open_pips)));
		candle_table->setItem(i, 2, new QTableWidgetItem(QString::number(row.high_pips)));
		candle_table->setItem(i, 3, new QTableWidgetItem(QString::number(row.low_pips)));
		candle_table->setItem(i, 4, new QTableWidgetItem(QString::number(row.close_pips)));
		candle_table->setItem(i, 5, new QTableWidgetItem(QString::number(row.body)));
		candle_table->setItem(i, 6, new QTableWidgetItem(QString::number(row.abs_body)));
		candle_table->setItem(i, 7, new QTableWidgetItem(QString::number(row.range)));
		candle_table->setItem(i, 8, new QTableWidgetItem(QString::number(row.upper_wick)));
		candle_table->setItem(i, 9, new QTableWidgetItem(QString::number(row.lower_wick)));
		candle_table->setItem(i, 10, new QTableWidgetItem(QString::number(row.step_delta)));
		candle_table->setItem(i, 11, new QTableWidgetItem(QString::number(row.step_abs)));
		candle_table->setItem(i, 12, new QTableWidgetItem(QString::number(row.overlap_with_prev)));
		candle_table->setItem(i, 13, new QTableWidgetItem(QString::number(row.inside_candle)));
		candle_table->setItem(i, 14, new QTableWidgetItem(QString("%1%2").arg(row.effective_sign).arg(row.tolerated_counter ? " (tol)" : "")));
	}
	candle_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
	candle_table->horizontalHeader()->setStretchLastSection(false);
	candle_table->resizeRowsToContents();

	buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);

	layout->addWidget(summary_label);
	layout->addWidget(formula_view);
	layout->addWidget(input_table, 1);
	layout->addWidget(candle_table, 3);
	layout->addWidget(buttons);
	dialog.exec();
}

bool main_window::is_wave_property_relevant(const QString &key) const
{
	static const char *const common_items[] = {
		"seed_pass",
		"layered_child_start_index",
		"layered_child_end_index",
		"layered_sequential_count",
		"layered_alternating_count",
		"layered_sequential_candles",
		"layered_alternating_candles",
		"layered_sequential_net_move_sum_raw",
		"layered_alternating_net_move_sum_raw",
		"layered_directional_agreement",
		"child_strings",
		"class_id",
		"class_name",
		"fit_error",
		"second_best_error",
		"ambiguity_margin",
		"duration_candles",
		"net_move_raw",
		"knn_class_agreement",
		"knn_neighbor_purity",
		"knn_distance_continuation",
		"knn_distance_exhaustion",
		"knn_majority_label",
		"knn_margin",
		"dominant_direction",
		"boundary_confidence",
		"weak_fit"
	};
	static const char *const primitive_items[] = {
		"primitive_type",
		"primitive_direction",
		"primitive_consecutive_ratio",
		"primitive_alternating_ratio",
		"primitive_max_consecutive_run",
		"primitive_max_alternating_run",
		"primitive_counter_body_ratio",
		"primitive_minority_body_ratio",
		"directional_purity_body",
		"sign_change_density",
		"retracement_ratio"
	};
	static const char *const legacy_items[] = {
		"directional_purity_body",
		"sign_change_density",
		"retracement_ratio",
		"range_growth_ratio",
		"context_range"
	};
	static const char *const relative_items[] = {
		"relative_net_move_raw",
		"relative_abs_body_sum_raw",
		"relative_abs_step_sum_raw",
		"relative_directional_efficiency_body",
		"relative_directional_efficiency_step",
		"relative_avg_body_efficiency",
		"relative_body_direction_share",
		"relative_upper_wick_sum_raw",
		"relative_lower_wick_sum_raw",
		"relative_total_wick_sum_raw",
		"relative_wick_to_body_ratio",
		"relative_wick_bias_up",
		"relative_wick_bias_down",
		"relative_wick_bias_aligned",
		"relative_max_pullback_raw",
		"relative_impulse_leg_raw",
		"relative_retracement_ratio",
		"relative_body_overlap_sum_raw",
		"relative_overlap_ratio",
		"relative_range_growth_ratio",
		"relative_body_growth_ratio",
		"relative_trend_quality_raw",
		"relative_trend_quality_bucket",
		"sign_change_density"
	};

	if (property_name_in_list(key, common_items, (int)(sizeof(common_items) / sizeof(common_items[0])))) {
		return true;
	}
	if (m_wave_model_name == "primitive_waves") {
		return property_name_in_list(key, primitive_items, (int)(sizeof(primitive_items) / sizeof(primitive_items[0])));
	}
	if (is_relative_wave_model_name(m_wave_model_name)) {
		return property_name_in_list(key, relative_items, (int)(sizeof(relative_items) / sizeof(relative_items[0])));
	}
	return property_name_in_list(key, legacy_items, (int)(sizeof(legacy_items) / sizeof(legacy_items[0])));
}

void main_window::refresh_wave_table()
{
	QString selected_filter = m_wave_filter_combo != nullptr ? m_wave_filter_combo->currentText() : "all";
	bool primitive_mode = (m_wave_model_name == "primitive_waves");
	bool relative_mode = is_relative_wave_model_name(m_wave_model_name);
	bool layered_mode = (m_wave_model_name == "layered_waves");
	int out_row = 0;

	if (m_waves_table == nullptr) {
		return;
	}
	m_waves_table->setRowCount(0);
	for (int i = 0; i < m_wave_rows.size(); ++i) {
		QJsonObject o = m_wave_rows.at(i).toObject();
		QString class_name = o.value("class_name").toString();
		QString primitive_type = o.value("primitive_type").toString();

		if (selected_filter != "all") {
			if (primitive_mode) {
				if (primitive_type != selected_filter) {
					continue;
				}
			} else if (class_name != selected_filter) {
				continue;
			}
		}
		m_waves_table->insertRow(out_row);
		{
			QTableWidgetItem *token_item = new QTableWidgetItem(QString::number(o.value("token_index").toInt()));
			token_item->setData(Qt::UserRole, i);
			m_waves_table->setItem(out_row, 0, token_item);
		}
		m_waves_table->setItem(out_row, 1, new QTableWidgetItem(QString::number(o.value("start_candle").toInt())));
		m_waves_table->setItem(out_row, 2, new QTableWidgetItem(QString::number(o.value("end_candle").toInt())));
		if (primitive_mode) {
			m_waves_table->setItem(out_row, 3, new QTableWidgetItem(primitive_type));
			m_waves_table->setItem(out_row, 4, new QTableWidgetItem(o.value("primitive_direction").toString()));
			m_waves_table->setItem(out_row, 5, new QTableWidgetItem(QString::number(o.value("primitive_consecutive_ratio").toInt())));
			m_waves_table->setItem(out_row, 6, new QTableWidgetItem(QString::number(o.value("primitive_alternating_ratio").toInt())));
			m_waves_table->setItem(out_row, 7, new QTableWidgetItem(QString::number(o.value("primitive_max_consecutive_run").toInt())));
			m_waves_table->setItem(out_row, 8, new QTableWidgetItem(QString::number(o.value("primitive_max_alternating_run").toInt())));
			m_waves_table->setItem(out_row, 9, new QTableWidgetItem(QString::number(o.value("fit_error").toInt())));
			m_waves_table->setItem(out_row, 10, new QTableWidgetItem(QString::number(o.value("ambiguity_margin").toInt())));
			m_waves_table->setItem(out_row, 11, new QTableWidgetItem(QString::number(o.value("dominant_direction").toInt())));
			m_waves_table->setItem(out_row, 12, new QTableWidgetItem(QString::number(o.value("duration_candles").toInt())));
			m_waves_table->setItem(out_row, 13, new QTableWidgetItem(QString::number(o.value("net_move_raw").toInt())));
			m_waves_table->setItem(out_row, 14, new QTableWidgetItem(QString::number(o.value("boundary_confidence").toInt())));
			m_waves_table->setItem(out_row, 15, new QTableWidgetItem(o.value("separator_debug").toString()));
		} else if (layered_mode) {
			m_waves_table->setItem(out_row, 3, new QTableWidgetItem(class_name));
			m_waves_table->setItem(out_row, 4, new QTableWidgetItem(o.value("primitive_direction").toString()));
			m_waves_table->setItem(out_row, 5, new QTableWidgetItem(QString::number(o.value("fit_error").toInt())));
			m_waves_table->setItem(out_row, 6, new QTableWidgetItem(QString::number(o.value("layered_sequential_count").toInt())));
			m_waves_table->setItem(out_row, 7, new QTableWidgetItem(QString::number(o.value("layered_alternating_count").toInt())));
			m_waves_table->setItem(out_row, 8, new QTableWidgetItem(QString::number(o.value("layered_sequential_candles").toInt())));
			m_waves_table->setItem(out_row, 9, new QTableWidgetItem(QString::number(o.value("layered_alternating_candles").toInt())));
			m_waves_table->setItem(out_row, 10, new QTableWidgetItem(QString::number(o.value("layered_directional_agreement").toInt())));
			m_waves_table->setItem(out_row, 11, new QTableWidgetItem(QString::number(o.value("relative_directional_efficiency_step").toInt())));
			m_waves_table->setItem(out_row, 12, new QTableWidgetItem(QString::number(o.value("relative_overlap_ratio").toInt())));
			m_waves_table->setItem(out_row, 13, new QTableWidgetItem(QString::number(o.value("duration_candles").toInt())));
			m_waves_table->setItem(out_row, 14, new QTableWidgetItem(QString::number(o.value("net_move_raw").toInt())));
			m_waves_table->setItem(out_row, 15, new QTableWidgetItem(QString("%1..%2")
				.arg(o.value("layered_child_start_index").toInt())
				.arg(o.value("layered_child_end_index").toInt())));
		} else if (relative_mode) {
			m_waves_table->setItem(out_row, 3, new QTableWidgetItem(class_name));
			m_waves_table->setItem(out_row, 4, new QTableWidgetItem(o.value("primitive_direction").toString()));
			m_waves_table->setItem(out_row, 5, new QTableWidgetItem(QString::number(o.value("fit_error").toInt())));
			m_waves_table->setItem(out_row, 6, new QTableWidgetItem(QString::number(o.value("relative_directional_efficiency_step").toInt())));
			m_waves_table->setItem(out_row, 7, new QTableWidgetItem(QString::number(o.value("relative_directional_efficiency_body").toInt())));
			m_waves_table->setItem(out_row, 8, new QTableWidgetItem(QString::number(o.value("relative_retracement_ratio").toInt())));
			m_waves_table->setItem(out_row, 9, new QTableWidgetItem(QString::number(o.value("relative_overlap_ratio").toInt())));
			m_waves_table->setItem(out_row, 10, new QTableWidgetItem(QString::number(o.value("relative_avg_body_efficiency").toInt())));
			m_waves_table->setItem(out_row, 11, new QTableWidgetItem(QString::number(o.value("relative_wick_bias_aligned").toInt())));
			m_waves_table->setItem(out_row, 12, new QTableWidgetItem(QString::number(o.value("ambiguity_margin").toInt())));
			m_waves_table->setItem(out_row, 13, new QTableWidgetItem(QString::number(o.value("duration_candles").toInt())));
			m_waves_table->setItem(out_row, 14, new QTableWidgetItem(QString::number(o.value("net_move_raw").toInt())));
			m_waves_table->setItem(out_row, 15, new QTableWidgetItem(QString::number(o.value("relative_max_pullback_raw").toInt())));
			m_waves_table->setItem(out_row, 16, new QTableWidgetItem(QString::number(o.value("relative_impulse_leg_raw").toInt())));
			m_waves_table->setItem(out_row, 17, new QTableWidgetItem(QString::number(o.value("relative_body_growth_ratio").toInt())));
			m_waves_table->setItem(out_row, 18, new QTableWidgetItem(QString::number(o.value("boundary_confidence").toInt())));
		} else {
			m_waves_table->setItem(out_row, 3, new QTableWidgetItem(class_name));
			m_waves_table->setItem(out_row, 4, new QTableWidgetItem(primitive_type));
			m_waves_table->setItem(out_row, 5, new QTableWidgetItem(o.value("primitive_direction").toString()));
			m_waves_table->setItem(out_row, 6, new QTableWidgetItem(QString::number(o.value("primitive_consecutive_ratio").toInt())));
			m_waves_table->setItem(out_row, 7, new QTableWidgetItem(QString::number(o.value("primitive_alternating_ratio").toInt())));
			m_waves_table->setItem(out_row, 8, new QTableWidgetItem(QString::number(o.value("fit_error").toInt())));
			m_waves_table->setItem(out_row, 9, new QTableWidgetItem(QString::number(o.value("ambiguity_margin").toInt())));
			m_waves_table->setItem(out_row, 10, new QTableWidgetItem(QString::number(o.value("dominant_direction").toInt())));
			m_waves_table->setItem(out_row, 11, new QTableWidgetItem(QString::number(o.value("duration_candles").toInt())));
			m_waves_table->setItem(out_row, 12, new QTableWidgetItem(QString::number(o.value("net_move_raw").toInt())));
			m_waves_table->setItem(out_row, 13, new QTableWidgetItem(QString::number(o.value("boundary_confidence").toInt())));
			m_waves_table->setItem(out_row, 14, new QTableWidgetItem(o.value("separator_debug").toString()));
		}
		out_row += 1;
	}
	if (out_row > 0) {
		m_waves_table->selectRow(0);
	} else if (m_wave_chart != nullptr) {
		m_wave_chart->clear();
	}
}

void main_window::update_wave_filter_options()
{
	QString current_text;
	bool primitive_mode = (m_wave_model_name == "primitive_waves");
	bool relative_mode = is_relative_wave_model_name(m_wave_model_name);

	if (m_wave_filter_combo == nullptr) {
		return;
	}
	current_text = m_wave_filter_combo->currentText();
	m_wave_filter_combo->blockSignals(true);
	m_wave_filter_combo->clear();
	m_wave_filter_combo->addItem("all");
	if (primitive_mode) {
		m_wave_filter_combo->addItem("sequential_string");
		m_wave_filter_combo->addItem("alternating_string");
		m_wave_filter_combo->addItem("separator_candle");
	} else if (relative_mode) {
		m_wave_filter_combo->addItem("trend");
		m_wave_filter_combo->addItem("range");
	} else {
		m_wave_filter_combo->addItem("strong_trend");
		m_wave_filter_combo->addItem("weak_trend");
		m_wave_filter_combo->addItem("retracement");
		m_wave_filter_combo->addItem("alternating_chop");
		m_wave_filter_combo->addItem("mixed_compression");
		m_wave_filter_combo->addItem("volatile_expansion");
		m_wave_filter_combo->addItem("v_shaped_reversal");
	}
	{
		int index = m_wave_filter_combo->findText(current_text);

		if (index < 0) {
			index = 0;
		}
		m_wave_filter_combo->setCurrentIndex(index);
	}
	m_wave_filter_combo->blockSignals(false);
}

void main_window::update_wave_table_headers()
{
	if (m_waves_table == nullptr) {
		return;
	}
	if (m_wave_model_name == "primitive_waves") {
		set_table_headers(m_waves_table, {"token", "start", "end", "primitive", "prim_dir", "cons_ratio", "alt_ratio", "max_cons_run", "max_alt_run", "fit_error", "ambiguity", "direction", "duration", "net_move", "boundary_conf", "sep_dbg"});
	} else if (m_wave_model_name == "layered_waves") {
		set_table_headers(m_waves_table, {"token", "start", "end", "class", "dir", "fit_error", "seq_n", "alt_n", "seq_c", "alt_c", "seq_agree", "der_step", "overlap", "duration", "net_move", "child_idx"});
	} else if (is_relative_wave_model_name(m_wave_model_name)) {
		set_table_headers(m_waves_table, {"token", "start", "end", "class", "dir", "fit_error", "der_step", "der_body", "retrace", "overlap", "body_eff", "wick_bias", "ambiguity", "duration", "net_move", "max_pullback", "impulse_leg", "body_growth", "boundary_conf"});
	} else {
		set_table_headers(m_waves_table, {"token", "start", "end", "class", "primitive", "prim_dir", "cons_ratio", "alt_ratio", "fit_error", "ambiguity", "direction", "duration", "net_move", "boundary_conf", "sep_dbg"});
	}
	m_waves_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
	m_waves_table->horizontalHeader()->setStretchLastSection(false);
	m_waves_table->horizontalHeader()->setMinimumSectionSize(24);
	m_waves_table->resizeColumnsToContents();
	update_wave_table_tooltips();
}

void main_window::update_wave_table_tooltips()
{
	QStringList tips;
	int i;

	if (m_waves_table == nullptr) {
		return;
	}
	if (m_wave_model_name == "primitive_waves") {
		tips << "Token index inside the loaded wave result. Example: 24 means the 25th token in the current dataset result."
			<< "Start candle index of the token. Example: 492 means the token begins at candle 492."
			<< "End candle index of the token. Example: 519 means the token ends at candle 519."
			<< "Primitive label emitted by the primitive wave model. sequential_string marks same-direction runs, alternating_string marks mixed flip-flop spans, and separator_candle marks a one-candle bridge or pivot that should stay isolated."
			<< "Dominant token direction. bull means upward net move, bear means downward net move, neutral means no clear direction."
			<< "Consecutive close-step ratio, scaled by 1000. Example: 900 means about 90% of non-flat close-to-close steps follow the token direction. Higher values push the model toward consecutive/trend interpretations."
			<< "Alternating close-step ratio, scaled by 1000. Example: 450 means about 45% of non-flat steps alternate sign versus the previous step. Higher values push the model toward alternating/compression labels and away from consecutive strings."
			<< "Longest consecutive same-direction run length inside the token. Example: 6 means six candles in the longest uninterrupted same-direction run. Higher values support consecutive/trend classification."
			<< "Longest alternating run length inside the token. Example: 5 means the longest flip-flop sequence spans five candles. Higher values support alternating/chop style classification."
			<< "Best fit error for the emitted token class. Lower is better. Near 0 means the token matches the chosen rule set well; large values mean weak fit."
			<< "Gap between best and second-best class errors. Higher means the decision is clearer; low values mean the token is ambiguous and close to another class."
			<< "Encoded dominant direction used by the trainer. 1 = bull, -1 = bear, 0 = neutral."
			<< "Token length in candles. Example: 28 means the token covers 28 candles."
			<< "Absolute net move across the token, in raw pip units used by the trainer. Larger values strengthen directional/trend interpretations."
			<< "Boundary confidence for the split after this token. Positive values mean the current boundary is better than merging neighboring tokens; low or negative values mean the split is weak."
			<< "Separator provenance markers. L means a separator candle was absorbed on the left edge, R means absorbed on the right edge, LR means both, and - means no separator was absorbed.";
	} else if (m_wave_model_name == "layered_waves") {
		tips << "Token index inside the loaded layered-waves result."
			<< "Start candle index of the layer-2 wave."
			<< "End candle index of the layer-2 wave."
			<< "Layer-2 class. trend means the covered child strings are more sequential and directionally aligned; range means alternating strings dominate."
			<< "Dominant direction of the full layer-2 wave."
			<< "Layered DP fit error. Lower is better."
			<< "Count of child sequential_string tokens inside this layer-2 wave."
			<< "Count of child alternating_string tokens inside this layer-2 wave."
			<< "Total candles covered by child sequential_string tokens."
			<< "Total candles covered by child alternating_string tokens."
			<< "Directional agreement among child sequential strings, scaled by 1000."
			<< "Directional efficiency versus close-to-close path, scaled by 1000."
			<< "Adjacent body-overlap ratio, scaled by 1000."
			<< "Wave length in candles."
			<< "Absolute net move across the wave, in raw pip units."
			<< "Inclusive child-string index range inside the preserved layer-1 string array.";
	} else if (is_relative_wave_model_name(m_wave_model_name)) {
		tips << "Token index inside the loaded wave result."
			<< "Start candle index of the token."
			<< "End candle index of the token."
			<< "Relative-waves class. trend means the span stays directionally efficient with limited retracement; range means drift/chop dominates."
			<< "Dominant token direction. bull means upward net move, bear means downward net move, neutral means no clear direction."
			<< "Best fit error for the emitted relative class. Lower is better."
			<< "Directional efficiency versus close-to-close path, scaled by 1000. Higher values mean cleaner directional travel."
			<< "Directional efficiency versus total candle body sum, scaled by 1000. Higher values mean most body activity supports the net move."
			<< "Max retracement divided by impulse leg, scaled by 1000. Lower values are healthier for trend classification."
			<< "Adjacent body-overlap ratio, scaled by 1000. Higher values indicate sloppy overlap/chop and push the span toward range."
			<< "Average per-candle body-to-range efficiency, scaled by 1000. Higher values mean candles spend more of their range in the body."
			<< "Direction-aligned wick support, scaled by 1000. Higher values mean wick structure supports the dominant direction."
			<< "Gap between best and second-best class errors. Higher means the class decision is clearer."
			<< "Token length in candles."
			<< "Absolute net move across the token, in raw pip units used by the trainer."
			<< "Largest close-based pullback seen inside the token, in raw pip units."
			<< "Largest favorable impulse leg reached inside the token, in raw pip units."
			<< "Late-half body size divided by early-half body size, scaled by 1000."
			<< "Boundary confidence for the split after this token.";
	} else {
		tips << "Token index inside the loaded wave result. Example: 24 means the 25th token in the current dataset result."
			<< "Start candle index of the token. Example: 492 means the token begins at candle 492."
			<< "End candle index of the token. Example: 519 means the token ends at candle 519."
			<< "High-level wave class chosen by the legacy-style output layer. Examples: strong_trend, weak_trend, retracement, mixed_compression. This is the class used in summaries and counts."
			<< "Primitive label derived from the token internals. In legacy mode it is informational; in primitive mode it is the primary interpretation."
			<< "Dominant token direction. bull means upward net move, bear means downward net move, neutral means no clear direction."
			<< "Consecutive close-step ratio, scaled by 1000. Example: 900 means about 90% of non-flat close-to-close steps follow the token direction."
			<< "Alternating close-step ratio, scaled by 1000. Example: 450 means about 45% of non-flat steps alternate sign versus the previous step."
			<< "Best fit error for the emitted token class. Lower is better. Near 0 means the token matches the chosen class rules well."
			<< "Gap between best and second-best class errors. Higher means the class decision is clearer; low values indicate ambiguity."
			<< "Encoded dominant direction used by the trainer. 1 = bull, -1 = bear, 0 = neutral."
			<< "Token length in candles. Example: 28 means the token covers 28 candles."
			<< "Absolute net move across the token, in raw pip units used by the trainer."
			<< "Boundary confidence for the split after this token. Positive values mean the current boundary is better than merging neighboring tokens."
			<< "Separator provenance markers. L means a separator candle was absorbed on the left edge, R means absorbed on the right edge, LR means both, and - means no separator was absorbed.";
	}
	for (i = 0; i < m_waves_table->columnCount() && i < tips.size(); ++i) {
		QTableWidgetItem *item = m_waves_table->horizontalHeaderItem(i);

		if (item != nullptr) {
			item->setToolTip(tips.at(i));
			item->setStatusTip(tips.at(i));
			item->setWhatsThis(tips.at(i));
		}
	}
}

void main_window::load_chart_into(cluster_chart_widget *chart, int dataset_id, int start_candle, int end_candle, int seed_start_candle, int seed_end_candle, const QVector<cluster_chart_widget::wave_overlay_item> *overlay_items)
{
	QJsonDocument doc;
	QString err;
	QJsonArray rows;
	QVector<cluster_chart_widget::candle_item> candles;
	int context = 256;
	int fetch_start = start_candle - context;
	int fetch_end = end_candle + context;
	int max_count = 512;
	int desired_count;

	if (chart == nullptr) {
		return;
	}
	if (fetch_start < 0) {
		fetch_start = 0;
	}
	desired_count = fetch_end - fetch_start + 1;
	if (desired_count > max_count) {
		int focus_center = (start_candle + end_candle) / 2;

		fetch_start = focus_center - (max_count / 2);
		if (fetch_start < 0) {
			fetch_start = 0;
		}
		fetch_end = fetch_start + max_count - 1;
		if (fetch_end < end_candle) {
			fetch_end = end_candle;
			fetch_start = qMax(0, fetch_end - max_count + 1);
		}
	}
	m_api.set_base_url(current_base_url());
	if (!m_api.get(QString("/datasets/%1/candles?start=%2&count=%3").arg(dataset_id).arg(fetch_start).arg(fetch_end - fetch_start + 1), &doc, &err)) {
		append_log(QString("cluster chart load failed: %1").arg(err));
		return;
	}
	rows = doc.object().value("candles").toArray();
	candles.reserve(rows.size());
	for (int i = 0; i < rows.size(); ++i) {
		QJsonObject o = rows.at(i).toObject();
		cluster_chart_widget::candle_item item;

		item.candle_index = fetch_start + i;
		item.ts_epoch = o.value("ts_epoch").toInt();
		item.open_pips = o.value("open_pips").toInt();
		item.high_pips = o.value("high_pips").toInt();
		item.low_pips = o.value("low_pips").toInt();
		item.close_pips = o.value("close_pips").toInt();
		candles.push_back(item);
	}
	chart->set_candles(candles, start_candle - fetch_start, end_candle - fetch_start);
	if (seed_start_candle >= 0 && seed_end_candle >= seed_start_candle) {
		chart->set_seed_focus(seed_start_candle - fetch_start, seed_end_candle - fetch_start);
	} else {
		chart->set_seed_focus(-1, -1);
	}
	if (overlay_items != nullptr) {
		chart->set_wave_overlay(*overlay_items);
	} else if (chart == m_wave_chart) {
		chart->set_wave_overlay(build_wave_overlay_items(fetch_start, fetch_end));
	} else {
		chart->set_wave_overlay(QVector<cluster_chart_widget::wave_overlay_item>());
	}
	append_log(QString("cluster chart loaded: candles=%1 focus=%2..%3")
		.arg(candles.size())
		.arg(start_candle)
		.arg(end_candle));
}

void main_window::load_cluster_chart(int dataset_id, int start_candle, int end_candle)
{
	load_chart_into(m_cluster_chart, dataset_id, start_candle, end_candle);
}

void main_window::refresh_group_block_view()
{
	QJsonDocument doc;
	QString err;
	QJsonArray rows;
	int selected_row = m_cluster_members_table != nullptr ? m_cluster_members_table->currentRow() : -1;
	int cluster_row = m_clusters_table != nullptr ? m_clusters_table->currentRow() : -1;
	int cluster_id = -1;
	int start_member = 0;
	int dataset_id = m_patterns_dataset_id_spin != nullptr ? m_patterns_dataset_id_spin->value() : 0;
	int page_size = m_group_charts.size();

	if (m_group_charts.isEmpty()) {
		return;
	}
	if (cluster_row >= 0 && m_clusters_table->item(cluster_row, 0) != nullptr) {
		cluster_id = m_clusters_table->item(cluster_row, 0)->text().toInt();
	}
	if (cluster_id < 0 || dataset_id <= 0) {
		m_group_total_members = 0;
		m_group_preview_members.clear();
		for (cluster_chart_widget *chart : m_group_charts) {
			if (chart != nullptr) {
				chart->set_click_payload(-1);
				chart->clear();
			}
		}
		if (m_group_prev_page_btn != nullptr) {
			m_group_prev_page_btn->setEnabled(false);
		}
		if (m_group_next_page_btn != nullptr) {
			m_group_next_page_btn->setEnabled(false);
		}
		return;
	}

	if (m_group_view_radio != nullptr && m_group_view_radio->isChecked()) {
		start_member = qMax(0, m_group_page_start);
	} else {
		if (selected_row >= 0 && m_cluster_members_table->item(selected_row, 0) != nullptr) {
			int selected_member_index = m_cluster_members_table->item(selected_row, 0)->text().toInt();

			start_member = qMax(0, selected_member_index - (page_size / 2));
		} else {
			start_member = qMax(0, m_group_page_start);
		}
	}
	m_api.set_base_url(current_base_url());
	if (!m_api.get(QString("/datasets/%1/clusters/%2/members?start=%3&count=%4")
			.arg(dataset_id)
			.arg(cluster_id)
			.arg(start_member)
			.arg(page_size),
		&doc,
		&err)) {
		append_log(QString("group preview load failed: %1").arg(err));
		return;
	}
	rows = doc.object().value("members").toArray();
	m_group_total_members = doc.object().value("total").toInt();
	m_group_page_start = start_member;
	m_group_preview_members.clear();
	if (m_group_prev_page_btn != nullptr) {
		m_group_prev_page_btn->setEnabled(m_group_page_start > 0);
	}
	if (m_group_next_page_btn != nullptr) {
		m_group_next_page_btn->setEnabled(m_group_total_members > (m_group_page_start + page_size));
	}

	for (int i = 0; i < m_group_charts.size(); ++i) {
		cluster_chart_widget *chart = m_group_charts[i];

		if (chart == nullptr) {
			continue;
		}
		if (i >= rows.size()) {
			chart->set_click_payload(-1);
			chart->clear();
			continue;
		}

		{
			QJsonObject member_obj = rows.at(i).toObject();
			QJsonDocument doc;
			QString err;
			QJsonArray candle_rows;
			QVector<cluster_chart_widget::candle_item> candles;
			group_preview_member member;
			int block_start;
			int block_end;
			int block_span;
			int target_count;
			int focus_center;
			int fetch_start;

			member.member_index = member_obj.value("member_index").toInt();
			member.block_index = member_obj.value("object_id").toInt();
			member.start_candle = member_obj.value("start_candle").toInt();
			member.end_candle = member_obj.value("end_candle").toInt();
			block_start = member.start_candle;
			block_end = member.end_candle;
			block_span = block_end - block_start + 1;
			target_count = qMax(40, block_span);
			focus_center = (block_start + block_end) / 2;
			fetch_start = qMax(0, focus_center - (target_count / 2));

			m_api.set_base_url(current_base_url());
			if (!m_api.get(QString("/datasets/%1/candles?start=%2&count=%3").arg(dataset_id).arg(fetch_start).arg(target_count), &doc, &err)) {
				chart->set_click_payload(-1);
				chart->clear();
				continue;
			}
			candle_rows = doc.object().value("candles").toArray();
			candles.reserve(candle_rows.size());
			for (int j = 0; j < candle_rows.size(); ++j) {
				QJsonObject o = candle_rows.at(j).toObject();
				cluster_chart_widget::candle_item item;

				item.candle_index = fetch_start + j;
				item.ts_epoch = o.value("ts_epoch").toInt();
				item.open_pips = o.value("open_pips").toInt();
				item.high_pips = o.value("high_pips").toInt();
				item.low_pips = o.value("low_pips").toInt();
				item.close_pips = o.value("close_pips").toInt();
				candles.push_back(item);
			}
			m_group_preview_members.push_back(member);
			chart->set_click_payload(i);
			chart->set_candles(candles, block_start - fetch_start, block_end - fetch_start);
		}
	}
}

void main_window::load_cluster_feature_panel(int dataset_id, int start_candle, int end_candle, int block_index)
{
	QJsonDocument feature_doc;
	QJsonDocument candle_doc;
	QString err;
	QJsonArray feature_rows;
	QJsonArray candle_rows;
	QStringList summary_lines;
	QStringList vector_parts;
	int q = end_candle - start_candle + 1;
	int total_range = 0;
	int gross_body = 0;
	int net_body = 0;
	int upper_sum = 0;
	int lower_sum = 0;
	int bull_count = 0;
	int bear_count = 0;
	int sign_changes = 0;
	int max_bull_run = 0;
	int max_bear_run = 0;
	int bull_run = 0;
	int bear_run = 0;
	int last_sign = 0;
	int lower_ratio = 0;
	int upper_ratio = 0;
	int total_wick_ratio = 0;
	QString distance_mode = (m_threshold_distance_mode_combo != nullptr)
		? m_threshold_distance_mode_combo->currentData().toString()
		: QString("legacy");
	auto html_line = [](const QString &label, const QString &value, bool highlight) {
		if (highlight) {
			return QString("<span style=\"color:#b00020;\"><b>%1:</b> %2</span>").arg(label, value);
		}
		return QString("<b>%1:</b> %2").arg(label, value);
	};

	m_cluster_feature_text->clear();
	m_cluster_feature_table->setRowCount(0);

	m_api.set_base_url(current_base_url());
	if (!m_api.get(QString("/datasets/%1/features?start=%2&count=%3").arg(dataset_id).arg(start_candle).arg(q), &feature_doc, &err)) {
		append_log(QString("cluster feature load failed: %1").arg(err));
		return;
	}
	if (!m_api.get(QString("/datasets/%1/candles?start=%2&count=%3").arg(dataset_id).arg(start_candle).arg(q), &candle_doc, &err)) {
		append_log(QString("cluster candle detail load failed: %1").arg(err));
		return;
	}

	feature_rows = feature_doc.object().value("features").toArray();
	candle_rows = candle_doc.object().value("candles").toArray();
	m_cluster_feature_table->setRowCount(feature_rows.size());
	for (int i = 0; i < feature_rows.size(); ++i) {
		QJsonObject o = feature_rows.at(i).toObject();
		int upper = o.value("u").toInt();
		int lower = o.value("l").toInt();
		int body = o.value("b").toInt();
		int range = o.value("r").toInt();
		int sign = 0;

		m_cluster_feature_table->setItem(i, 0, new QTableWidgetItem(QString::number(i)));
		m_cluster_feature_table->setItem(i, 1, new QTableWidgetItem(QString::number(upper)));
		m_cluster_feature_table->setItem(i, 2, new QTableWidgetItem(QString::number(lower)));
		m_cluster_feature_table->setItem(i, 3, new QTableWidgetItem(QString::number(body)));
		m_cluster_feature_table->setItem(i, 4, new QTableWidgetItem(QString::number(range)));
		m_cluster_feature_table->setItem(i, 5, new QTableWidgetItem(QString::number(qAbs(body))));

		total_range += range;
		gross_body += qAbs(body);
		net_body += body;
		upper_sum += upper;
		lower_sum += lower;
		if (body > 0) {
			sign = 1;
			bull_count += 1;
			bull_run += 1;
			bear_run = 0;
		} else if (body < 0) {
			sign = -1;
			bear_count += 1;
			bear_run += 1;
			bull_run = 0;
		} else {
			bull_run = 0;
			bear_run = 0;
		}
		if (sign != 0 && last_sign != 0 && sign != last_sign) {
			sign_changes += 1;
		}
		if (sign != 0) {
			last_sign = sign;
		}
		if (bull_run > max_bull_run) {
			max_bull_run = bull_run;
		}
		if (bear_run > max_bear_run) {
			max_bear_run = bear_run;
		}
		vector_parts << QString("%1,%2,%3,%4").arg(upper).arg(lower).arg(body).arg(range);
	}
	lower_ratio = (gross_body > 0) ? (100 * lower_sum) / gross_body : 0;
	upper_ratio = (gross_body > 0) ? (100 * upper_sum) / gross_body : 0;
	total_wick_ratio = (gross_body > 0) ? (100 * (upper_sum + lower_sum)) / gross_body : 0;

	summary_lines << html_line("block_index", QString::number(block_index), false);
	summary_lines << html_line("start_candle", QString::number(start_candle), false);
	summary_lines << html_line("end_candle", QString::number(end_candle), false);
	if (!candle_rows.isEmpty()) {
		QJsonObject start_obj = candle_rows.first().toObject();
		QJsonObject end_obj = candle_rows.last().toObject();
		summary_lines << html_line("start_ts_epoch", QString::number(start_obj.value("ts_epoch").toInt()), false);
		summary_lines << html_line("end_ts_epoch", QString::number(end_obj.value("ts_epoch").toInt()), false);
	}
	summary_lines << html_line("q", QString::number(q), false);
	summary_lines << html_line("distance_mode (current threshold controls)", distance_mode, true);
	summary_lines << html_line("delta (current controls)", QString::number(m_delta_spin->value()), false);
	summary_lines << html_line("theta (current controls)", QString::number(m_theta_spin->value()), false);
	summary_lines << html_line("block_total_range", QString::number(total_range), false);
	summary_lines << html_line("block_avg_range", QString::number(q > 0 ? (double)total_range / (double)q : 0.0, 'f', 3), false);
	summary_lines << html_line("block_gross_body", QString::number(gross_body), false);
	summary_lines << html_line("block_net_body", QString::number(net_body), false);
	summary_lines << html_line("block_upper_sum", QString::number(upper_sum), true);
	summary_lines << html_line("block_lower_sum", QString::number(lower_sum), true);
	summary_lines << html_line("lower_wick_body_ratio_pct", QString::number(lower_ratio), true);
	summary_lines << html_line("upper_wick_body_ratio_pct", QString::number(upper_ratio), true);
	summary_lines << html_line("total_wick_body_ratio_pct", QString::number(total_wick_ratio), true);
	summary_lines << html_line("gate_upper_wick_diff (current controls)", QString::number(m_delta_upper_wick_gate_spin->value()), true);
	summary_lines << html_line("gate_lower_wick_diff (current controls)", QString::number(m_delta_lower_wick_gate_spin->value()), true);
	summary_lines << html_line("gate_lower_ratio_diff_pct (current controls)", QString::number(m_delta_lower_ratio_gate_spin->value()), true);
	summary_lines << html_line("gate_upper_ratio_diff_pct (current controls)", QString::number(m_delta_upper_ratio_gate_spin->value()), true);
	summary_lines << html_line("gate_total_wick_ratio_diff_pct (current controls)", QString::number(m_delta_total_wick_ratio_gate_spin->value()), true);
	summary_lines << html_line("bull_count", QString::number(bull_count), false);
	summary_lines << html_line("bear_count", QString::number(bear_count), false);
	summary_lines << html_line("sign_changes", QString::number(sign_changes), false);
	summary_lines << html_line("max_bull_run", QString::number(max_bull_run), false);
	summary_lines << html_line("max_bear_run", QString::number(max_bear_run), false);
	summary_lines << html_line("encoded_block_vector", QString("[%1]").arg(vector_parts.join(" | ")), false);
	m_cluster_feature_text->setHtml(summary_lines.join("<br/>"));
	append_log(QString("cluster features loaded: block=%1 q=%2").arg(block_index).arg(q));
}
