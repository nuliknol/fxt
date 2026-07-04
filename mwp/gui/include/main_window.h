#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include "api_client.h"
#include "cluster_chart_widget.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QMainWindow>
#include <QVector>

class QLineEdit;
class QTextEdit;
class QCheckBox;
class QSpinBox;
class QDoubleSpinBox;
class QComboBox;
class QLabel;
class QGroupBox;
class QButtonGroup;
class QRadioButton;
class QStackedWidget;
class QTabWidget;
class QTableWidget;
class QPushButton;
class QTimer;
class QTableWidgetItem;
class QStringList;
class QComboBox;
class QSplitter;
class micro_harvest_widget;

class main_window : public QMainWindow
{
	Q_OBJECT

public:
	explicit main_window(QWidget *parent = nullptr);

	struct group_preview_member {
		int member_index;
		int block_index;
		int start_candle;
		int end_candle;
	};

private slots:
	void on_ping_server();
	void on_import_dataset();
	void on_import_sample_dataset();
	void on_refresh_datasets();
	void on_load_dataset_detail();
	void on_load_candles();
	void on_load_features();

	void on_submit_clustering_job();
	void on_submit_word_job();
	void on_submit_phrase_job();
	void on_submit_wave_job();
	void on_refresh_jobs();
	void on_delete_selected_job();
	void on_clustering_algorithm_changed();

	void on_load_clusters();
	void on_load_cluster_members();
	void on_cluster_row_changed();
	void on_cluster_member_row_changed();
	void on_load_words();
	void on_load_phrases();
	void on_load_waves();
	void on_load_waves_lowest_err();
	void on_load_predictor_waves();
	void on_predictor_row_changed();
	void on_predictor_horizon_item_double_clicked(QTableWidgetItem *item);
	void on_show_predictor_base_probabilities();
	void on_refresh_live();
	void on_recompute_live();
	void on_reset_live();
	void on_live_stream_changed();
	void on_live_wave_row_changed();
	void on_configure_live_properties();
	void on_wave_filter_changed();
	void on_wave_row_changed();
	void on_wave_property_item_double_clicked(QTableWidgetItem *item);
	void on_configure_wave_properties();
	void on_show_wave_penalty_calculation();
	void on_view_mode_changed();
	void on_group_chart_clicked(int member_row);
	void on_group_prev_page();
	void on_group_next_page();

private:
	QString current_base_url() const;
	void append_log(const QString &msg);
	void set_table_headers(QTableWidget *table, const QStringList &headers);
	void load_chart_into(cluster_chart_widget *chart, int dataset_id, int start_candle, int end_candle, int seed_start_candle = -1, int seed_end_candle = -1, const QVector<cluster_chart_widget::wave_overlay_item> *overlay_items = nullptr);
	void load_cluster_chart(int dataset_id, int start_candle, int end_candle);
	void load_cluster_feature_panel(int dataset_id, int start_candle, int end_candle, int block_index);
	void refresh_group_block_view();
	int current_clustering_algorithm() const;
	void update_clustering_job_ui();
	QString job_type_name(int job_type) const;
	QString job_status_name(int job_status) const;
	QString graph_cluster_mode_name(int cluster_mode) const;
	QString clustering_algorithm_name(int algorithm_type) const;
	QString clustering_result_name(int algorithm_type, int graph_cluster_mode) const;
	void update_label_text(QLabel *label, const QString &text, const QString &tooltip);
	void refresh_wave_table();
	void update_wave_filter_options();
	void update_wave_table_headers();
	void update_wave_table_tooltips();
	void update_job_polling(bool has_active_jobs);
	void update_wave_token_properties(int row);
	void update_wave_layer1_strings(int row);
	void update_wave_token_properties_height(int visible_property_count);
	bool is_wave_property_relevant(const QString &key) const;
	void update_wave_job_ui();
	void load_waves(bool order_by_fit_error);
	void show_wave_property_detail_dialog(const QString &key, const QJsonObject &token);
	QStringList available_wave_property_names() const;
	bool is_wave_property_visible(const QString &key) const;
	void load_wave_property_visibility();
	void save_wave_property_visibility() const;
	void update_wave_overlay_property_options();
	QVector<cluster_chart_widget::wave_overlay_item> build_wave_overlay_items(int fetch_start, int fetch_end) const;
	void save_wave_splitter_state() const;
	int current_wave_source_row() const;
	void load_predictor_waves();
	void refresh_predictor_table();
	int current_predictor_source_row() const;
	void update_predictor_view();
	void load_live_status();
	void load_live_stream_data();
	void refresh_live_wave_table();
	void update_live_properties(int row);
	void update_live_composition(int row);
	void load_live_chart(const QString &symbol, const QString &timeframe, int candle_count);
	QString current_live_symbol() const;
	QString current_live_timeframe() const;
	bool is_live_property_visible(const QString &key) const;
	QStringList available_live_property_names() const;
	void load_live_property_visibility();
	void save_live_property_visibility() const;

	QWidget *build_connection_tab();
	QWidget *build_jobs_tab();
	QWidget *build_patterns_tab();
	QWidget *build_waves_tab();
	QWidget *build_predictor_tab();
	QWidget *build_live_tab();

	api_client m_api;

	QLineEdit *m_server_url_edit;
	QLineEdit *m_import_path_edit;
	QSpinBox *m_import_pip_scale_spin;
	QSpinBox *m_dataset_id_spin;
	QSpinBox *m_window_start_spin;
	QSpinBox *m_window_count_spin;
	QTextEdit *m_dataset_text;
	QTableWidget *m_dataset_table;

	QSpinBox *m_job_dataset_id_spin;
	QSpinBox *m_mining_job_dataset_id_spin;
	QSpinBox *m_wave_job_dataset_id_spin;
	QSpinBox *m_q_spin;
	QSpinBox *m_delta_spin;
	QSpinBox *m_theta_spin;
	QSpinBox *m_k_spin;
	QSpinBox *m_max_iter_spin;
	QSpinBox *m_restarts_spin;
	QSpinBox *m_eps_spin;
	QSpinBox *m_min_pts_spin;
	QSpinBox *m_delta_upper_wick_gate_spin;
	QSpinBox *m_delta_lower_wick_gate_spin;
	QSpinBox *m_delta_lower_ratio_gate_spin;
	QSpinBox *m_delta_upper_ratio_gate_spin;
	QSpinBox *m_delta_total_wick_ratio_gate_spin;
	QSpinBox *m_delta_body_gate_spin;
	QSpinBox *m_delta_range_gate_spin;
	QSpinBox *m_delta_bullcount_gate_spin;
	QSpinBox *m_delta_signchange_gate_spin;
	QSpinBox *m_graph_knn_spin;
	QDoubleSpinBox *m_graph_max_neighbor_distance_spin;
	QSpinBox *m_graph_mutual_knn_spin;
	QSpinBox *m_require_same_majority_sign_spin;
	QSpinBox *m_use_composition_spin;
	QSpinBox *m_word_min_spin;
	QSpinBox *m_word_max_spin;
	QSpinBox *m_phrase_min_spin;
	QSpinBox *m_phrase_max_spin;
	QSpinBox *m_wave_lmin_spin;
	QSpinBox *m_wave_lmax_spin;
	QSpinBox *m_wave_context_window_spin;
	QSpinBox *m_wave_lambda_spin;
	QSpinBox *m_wave_min_move_spin;
	QSpinBox *m_wave_minimum_trend_pips_spin;
	QSpinBox *m_wave_norm_scale_spin;
	QSpinBox *m_wave_context_floor_spin;
	QSpinBox *m_wave_epsilon_spin;
	QSpinBox *m_wave_weak_fit_spin;
	QSpinBox *m_wave_boundary_low_conf_spin;
	QComboBox *m_wave_model_combo;
	QComboBox *m_wave_trend_priority_combo;
	QSpinBox *m_wave_retrace_break_spin;
	QSpinBox *m_wave_trend_split_gain_spin;
	QSpinBox *m_wave_compression_min_duration_spin;
	QSpinBox *m_wave_expansion_min_duration_spin;
	QSpinBox *m_wave_v_strength_spin;
	QSpinBox *m_wave_v_bridge_spin;
	QSpinBox *m_wave_v_duration_spin;
	QSpinBox *m_wave_relative_trend_retracement_max_spin;
	QSpinBox *m_wave_relative_strong_trend_retracement_max_spin;
	QSpinBox *m_wave_relative_trend_der_step_min_spin;
	QSpinBox *m_wave_relative_trend_der_body_min_spin;
	QSpinBox *m_wave_relative_trend_body_eff_min_spin;
	QSpinBox *m_wave_relative_trend_overlap_max_spin;
	QSpinBox *m_wave_relative_trend_sign_flip_max_spin;
	QSpinBox *m_wave_relative_trend_wick_bias_min_spin;
	QSpinBox *m_wave_relative_range_der_step_max_spin;
	QSpinBox *m_wave_relative_range_body_eff_max_spin;
	QSpinBox *m_wave_relative_range_overlap_min_spin;
	QSpinBox *m_wave_relative_range_sign_flip_min_spin;
	QSpinBox *m_mirrored_spin;
	QButtonGroup *m_clustering_algorithm_group;
	QGroupBox *m_common_params_box;
	QGroupBox *m_weights_box;
	QLabel *m_clustering_summary_label;
	QLabel *m_algorithm_intro_label;
	QLabel *m_algorithm_formula_label;
	QLabel *m_q_label;
	QLabel *m_delta_label;
	QLabel *m_mirrored_label;
	QLabel *m_w_upper_label;
	QLabel *m_w_lower_label;
	QLabel *m_w_body_label;
	QLabel *m_w_range_label;
	QGroupBox *m_threshold_params_box;
	QGroupBox *m_graph_params_box;
	QGroupBox *m_kmedoids_params_box;
	QGroupBox *m_dbscan_params_box;
	QGroupBox *m_wave_common_box;
	QGroupBox *m_wave_cleanup_box;
	QGroupBox *m_wave_relative_box;
	QComboBox *m_threshold_distance_mode_combo;
	QComboBox *m_graph_mode_combo;
	QComboBox *m_distance_mode_combo;
	QComboBox *m_body_mode_combo;
	QDoubleSpinBox *m_w_upper_spin;
	QDoubleSpinBox *m_w_lower_spin;
	QDoubleSpinBox *m_w_body_spin;
	QDoubleSpinBox *m_w_range_spin;
	QDoubleSpinBox *m_body_sign_penalty_spin;
	QDoubleSpinBox *m_w_bullcount_spin;
	QDoubleSpinBox *m_w_signchanges_spin;
	QDoubleSpinBox *m_w_runbull_spin;
	QDoubleSpinBox *m_w_runbear_spin;
	QTableWidget *m_jobs_table;
	QTimer *m_jobs_refresh_timer;

	QSpinBox *m_patterns_dataset_id_spin;
	QSpinBox *m_cluster_member_start_spin;
	QSpinBox *m_cluster_member_count_spin;
	QTableWidget *m_clusters_table;
	QTableWidget *m_cluster_members_table;
	QTableWidget *m_words_table;
	QTableWidget *m_phrases_table;
	QRadioButton *m_individual_view_radio;
	QRadioButton *m_group_view_radio;
	QPushButton *m_group_prev_page_btn;
	QPushButton *m_group_next_page_btn;
	QStackedWidget *m_pattern_view_stack;
	cluster_chart_widget *m_cluster_chart;
	QVector<cluster_chart_widget *> m_group_charts;
	QVector<group_preview_member> m_group_preview_members;
	QTextEdit *m_cluster_feature_text;
	QTableWidget *m_cluster_feature_table;
	int m_group_page_start;
	int m_group_total_members;

	QTextEdit *m_log_text;

	QSpinBox *m_wave_dataset_id_spin;
	QSpinBox *m_wave_start_spin;
	QSpinBox *m_wave_count_spin;
	QComboBox *m_wave_filter_combo;
	QTableWidget *m_waves_table;
	QSplitter *m_wave_main_splitter;
	QSplitter *m_wave_content_splitter;
	cluster_chart_widget *m_wave_chart;
	QLabel *m_wave_chart_status_label;
	QCheckBox *m_wave_show_seed_check;
	QPushButton *m_wave_property_visibility_btn;
	QPushButton *m_wave_penalty_trace_btn;
	QComboBox *m_wave_overlay_property_combo;
	QTabWidget *m_wave_info_tabs;
	QTextEdit *m_wave_summary_text;
	QTableWidget *m_wave_token_props_table;
	QTableWidget *m_wave_layer1_table;
	QJsonArray m_wave_rows;
	QJsonObject m_wave_summary_obj;
	QString m_wave_model_name;
	QStringList m_hidden_wave_properties;

	QSpinBox *m_predictor_dataset_id_spin;
	QSpinBox *m_predictor_start_spin;
	QSpinBox *m_predictor_count_spin;
	QSpinBox *m_predictor_input_len_spin;
	QSpinBox *m_predictor_horizon_spin;
	QTableWidget *m_predictor_waves_table;
	QTableWidget *m_predictor_horizons_table;
	cluster_chart_widget *m_predictor_chart;
	cluster_chart_widget *m_predictor_actual_chart;
	QLabel *m_predictor_chart_status_label;
	QLabel *m_predictor_actual_status_label;
	QTextEdit *m_predictor_summary_text;
	QTextEdit *m_predictor_signal_text;
	QJsonArray m_predictor_rows;
	QJsonObject m_predictor_doc_obj;
	micro_harvest_widget *m_micro_harvest_widget;

	QComboBox *m_live_stream_combo;
	QSpinBox *m_live_candle_count_spin;
	QSpinBox *m_live_wave_count_spin;
	QCheckBox *m_live_auto_refresh_check;
	QPushButton *m_live_refresh_btn;
	QPushButton *m_live_recompute_btn;
	QPushButton *m_live_reset_btn;
	QPushButton *m_live_property_visibility_btn;
	QTimer *m_live_refresh_timer;
	QTableWidget *m_live_waves_table;
	QTabWidget *m_live_info_tabs;
	QTableWidget *m_live_properties_table;
	QTableWidget *m_live_composition_table;
	cluster_chart_widget *m_live_chart;
	QLabel *m_live_status_label;
	QLabel *m_live_chart_status_label;
	QTextEdit *m_live_summary_text;
	QJsonArray m_live_streams;
	QJsonArray m_live_waves;
	int m_live_candle_start;
	int m_live_last_ts;
	int m_live_last_wave_count;
	QString m_live_last_key;
	QStringList m_hidden_live_properties;
};

#endif
