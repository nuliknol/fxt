#ifndef CLUSTER_CHART_WIDGET_H
#define CLUSTER_CHART_WIDGET_H

#include <QVector>
#include <QWidget>

class cluster_chart_widget : public QWidget
{
	Q_OBJECT

public:
	struct candle_item {
		int candle_index;
		int ts_epoch;
		int open_pips;
		int high_pips;
		int low_pips;
		int close_pips;
	};

	struct wave_overlay_item {
		int start_index;
		int end_index;
		QString label;
	};

	struct micro_harvest_island_overlay_item {
		int start_index;
		int end_index;
		int low_pips;
		int high_pips;
		bool selected;
	};

	struct micro_harvest_trade_overlay_item {
		int open_index;
		int close_index;
		int open_price_pips;
		int close_price_pips;
		int direction;
		bool closed;
	};

	explicit cluster_chart_widget(QWidget *parent = nullptr);

	void clear();
	void set_candles(const QVector<candle_item> &candles, int focus_start, int focus_end);
	void set_focus_range(int focus_start, int focus_end);
	void set_seed_focus(int seed_start, int seed_end);
	void set_wave_overlay(const QVector<wave_overlay_item> &items);
	void set_micro_harvest_overlays(const QVector<micro_harvest_island_overlay_item> &islands,
		const QVector<micro_harvest_trade_overlay_item> &trades);
	void set_interactive(bool interactive);
	void set_fixed_window_count(int count);
	void set_dim_non_focus(bool enabled);
	void set_footer_visible(bool visible);
	void set_click_payload(int value);

protected:
	void paintEvent(QPaintEvent *event) override;
	void wheelEvent(QWheelEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void leaveEvent(QEvent *event) override;

private:
	QVector<candle_item> m_candles;
	int m_focus_start;
	int m_focus_end;
	int m_seed_start;
	int m_seed_end;
	QVector<wave_overlay_item> m_wave_overlay_items;
	QVector<micro_harvest_island_overlay_item> m_micro_harvest_islands;
	QVector<micro_harvest_trade_overlay_item> m_micro_harvest_trades;
	int m_view_start;
	int m_view_end;
	int m_fixed_window_count;
	int m_click_payload;
	int m_drag_last_x;
	bool m_interactive;
	bool m_dim_non_focus;
	bool m_footer_visible;
	bool m_drag_panning;

signals:
	void chart_clicked(int payload);
	void candle_hovered(int candle_index);
};

#endif
