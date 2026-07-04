#include "cluster_chart_widget.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QWheelEvent>
#include <QtGlobal>

cluster_chart_widget::cluster_chart_widget(QWidget *parent)
	: QWidget(parent),
	  m_focus_start(-1),
	  m_focus_end(-1),
	  m_seed_start(-1),
	  m_seed_end(-1),
	  m_view_start(0),
	  m_view_end(-1),
	  m_fixed_window_count(0),
	  m_click_payload(-1),
	  m_drag_last_x(0),
	  m_interactive(true),
	  m_dim_non_focus(false),
	  m_footer_visible(true),
	  m_drag_panning(false)
{
	setMinimumHeight(260);
	setMouseTracking(true);
}

void cluster_chart_widget::clear()
{
	m_candles.clear();
	m_focus_start = -1;
	m_focus_end = -1;
	m_seed_start = -1;
	m_seed_end = -1;
	m_wave_overlay_items.clear();
	m_micro_harvest_islands.clear();
	m_micro_harvest_trades.clear();
	m_view_start = 0;
	m_view_end = -1;
	m_click_payload = -1;
	m_drag_last_x = 0;
	m_drag_panning = false;
	update();
}

void cluster_chart_widget::set_candles(const QVector<candle_item> &candles, int focus_start, int focus_end)
{
	m_candles = candles;
	m_focus_start = focus_start;
	m_focus_end = focus_end;
	if (m_candles.isEmpty()) {
		m_view_start = 0;
		m_view_end = -1;
	} else if (m_focus_start >= 0 && m_focus_end >= m_focus_start) {
		int focus_span = m_focus_end - m_focus_start + 1;
		int target_count = (m_fixed_window_count > 0)
			? qMin(m_candles.size(), qMax(focus_span, m_fixed_window_count))
			: qMin(m_candles.size(), qMax(24, (focus_span + 4) * 8));
		int focus_center = (m_focus_start + m_focus_end) / 2;
		int start = qMax(0, focus_center - (target_count / 2));
		int end = qMin(m_candles.size() - 1, start + target_count - 1);

		if (end - start + 1 < target_count) {
			start = qMax(0, end - target_count + 1);
		}

		m_view_start = start;
		m_view_end = end;
	} else {
		m_view_start = 0;
		m_view_end = m_candles.size() - 1;
	}
	update();
}

void cluster_chart_widget::set_seed_focus(int seed_start, int seed_end)
{
	m_seed_start = seed_start;
	m_seed_end = seed_end;
	update();
}

void cluster_chart_widget::set_focus_range(int focus_start, int focus_end)
{
	m_focus_start = focus_start;
	m_focus_end = focus_end;
	update();
}

void cluster_chart_widget::set_wave_overlay(const QVector<wave_overlay_item> &items)
{
	m_wave_overlay_items = items;
	update();
}

void cluster_chart_widget::set_micro_harvest_overlays(const QVector<micro_harvest_island_overlay_item> &islands,
	const QVector<micro_harvest_trade_overlay_item> &trades)
{
	m_micro_harvest_islands = islands;
	m_micro_harvest_trades = trades;
	update();
}

void cluster_chart_widget::set_interactive(bool interactive)
{
	m_interactive = interactive;
}

void cluster_chart_widget::set_fixed_window_count(int count)
{
	m_fixed_window_count = qMax(0, count);
}

void cluster_chart_widget::set_dim_non_focus(bool enabled)
{
	m_dim_non_focus = enabled;
}

void cluster_chart_widget::set_footer_visible(bool visible)
{
	m_footer_visible = visible;
}

void cluster_chart_widget::set_click_payload(int value)
{
	m_click_payload = value;
}

void cluster_chart_widget::paintEvent(QPaintEvent *event)
{
	QPainter p(this);
	QRect area = rect();
	int left_pad = 48;
	int right_pad = 12;
	int top_pad = 16;
	int bottom_pad = 28;
	int plot_w;
	int plot_h;
	int min_price = 0;
	int max_price = 0;
	int price_span;
	int visible_start;
	int visible_end;
	int visible_count;
	int i;

	Q_UNUSED(event);

	p.fillRect(area, QColor(255, 255, 255));
	p.setRenderHint(QPainter::Antialiasing, false);

	if (m_candles.isEmpty()) {
		p.setPen(QColor(90, 90, 90));
		p.drawText(area.adjusted(16, 16, -16, -16), Qt::AlignCenter, "Select a cluster member to view its candles");
		return;
	}

	plot_w = qMax(1, area.width() - left_pad - right_pad);
	plot_h = qMax(1, area.height() - top_pad - bottom_pad);
	visible_start = qBound(0, m_view_start, m_candles.size() - 1);
	visible_end = qBound(visible_start, m_view_end, m_candles.size() - 1);
	visible_count = visible_end - visible_start + 1;

	min_price = m_candles[visible_start].low_pips;
	max_price = m_candles[visible_start].high_pips;
	for (i = visible_start + 1; i <= visible_end; ++i) {
		min_price = qMin(min_price, m_candles[i].low_pips);
		max_price = qMax(max_price, m_candles[i].high_pips);
	}
	if (max_price == min_price) {
		max_price += 1;
		min_price -= 1;
	}
	price_span = max_price - min_price;

	p.setPen(QColor(220, 215, 205));
	for (i = 0; i < 4; ++i) {
		int y = top_pad + (i * plot_h) / 3;

		p.drawLine(left_pad, y, left_pad + plot_w, y);
	}

	if (m_focus_start >= 0 && m_focus_end >= m_focus_start && m_focus_start <= visible_end && m_focus_end >= visible_start) {
		int clamped_start = qMax(m_focus_start, visible_start);
		int clamped_end = qMin(m_focus_end, visible_end);
		double candle_w = (double)plot_w / (double)visible_count;
		int x0 = left_pad + (int)((clamped_start - visible_start) * candle_w);
		int x1 = left_pad + (int)((clamped_end - visible_start + 1) * candle_w);

		p.fillRect(QRect(x0, top_pad, qMax(1, x1 - x0), plot_h), QColor(255, 236, 179, 120));
	}
	{
		double candle_w = (double)plot_w / (double)visible_count;
		QPen line_pen(QColor(128, 128, 128, 110), 1, Qt::DotLine);
		QFont label_font = p.font();

		label_font.setPointSize(qMax(8, label_font.pointSize() - 1));
		p.setFont(label_font);
		for (const wave_overlay_item &item : m_wave_overlay_items) {
			int start_index = item.start_index;
			int end_index = item.end_index;
			int x0;
			int x1;
			QRect text_rect;

			if (end_index < visible_start || start_index > visible_end || start_index > end_index) {
				continue;
			}
			start_index = qMax(start_index, visible_start);
			end_index = qMin(end_index, visible_end);
			x0 = left_pad + (int)((start_index - visible_start) * candle_w);
			x1 = left_pad + (int)((end_index - visible_start + 1) * candle_w);
			p.setPen(line_pen);
			p.drawLine(x0, top_pad, x0, top_pad + plot_h);
			p.drawLine(x1, top_pad, x1, top_pad + plot_h);
			if (!item.label.isEmpty()) {
				p.setPen(QColor(70, 70, 70));
				text_rect = QRect(x0 + 2, 0, qMax(24, x1 - x0 - 4), top_pad - 2);
				p.drawText(text_rect, Qt::AlignHCenter | Qt::AlignVCenter, item.label);
			}
		}
	}
	{
		double candle_w = (double)plot_w / (double)visible_count;
		auto to_y = [&](int price) {
			double t = (double)(price - min_price) / (double)price_span;

			return top_pad + plot_h - (int)(t * plot_h);
		};

		for (const micro_harvest_island_overlay_item &item : m_micro_harvest_islands) {
			int start_index = item.start_index;
			int end_index = item.end_index;
			int x0;
			int x1;
			int y0;
			int y1;
			QColor fill = item.selected ? QColor(255, 204, 128, 80) : QColor(189, 189, 189, 55);

			if (end_index < visible_start || start_index > visible_end || start_index > end_index) {
				continue;
			}
			start_index = qMax(start_index, visible_start);
			end_index = qMin(end_index, visible_end);
			x0 = left_pad + (int)((start_index - visible_start) * candle_w);
			x1 = left_pad + (int)((end_index - visible_start + 1) * candle_w);
			y0 = to_y(item.high_pips);
			y1 = to_y(item.low_pips);
			p.fillRect(QRect(x0, y0, qMax(1, x1 - x0), qMax(1, y1 - y0)), fill);
		}
	}
	for (i = visible_start; i <= visible_end; ++i) {
		const candle_item &c = m_candles[i];
		int local_index = i - visible_start;
		double candle_w = (double)plot_w / (double)visible_count;
		int center_x = left_pad + (int)((local_index + 0.5) * candle_w);
		int body_w = qMax(3, (int)(candle_w * 0.65));
		auto to_y = [&](int price) {
			double t = (double)(price - min_price) / (double)price_span;

			return top_pad + plot_h - (int)(t * plot_h);
		};
		int y_high = to_y(c.high_pips);
		int y_low = to_y(c.low_pips);
		int y_open = to_y(c.open_pips);
		int y_close = to_y(c.close_pips);
		int body_top = qMin(y_open, y_close);
		int body_bottom = qMax(y_open, y_close);
		bool in_focus = (m_focus_start >= 0 && m_focus_end >= m_focus_start && i >= m_focus_start && i <= m_focus_end);
		bool in_seed = (m_seed_start >= 0 && m_seed_end >= m_seed_start && i >= m_seed_start && i <= m_seed_end);
		int alpha = (m_dim_non_focus && !in_focus) ? 128 : 255;
		QColor body_color = in_seed ? QColor(255, 64, 160, alpha) :
			((c.close_pips >= c.open_pips) ? QColor(35, 130, 76, alpha) : QColor(178, 52, 46, alpha));
		QColor wick_color = QColor(50, 50, 50, alpha);
		QColor border_color = QColor(40, 40, 40, alpha);

		p.setPen(QPen(wick_color, 1));
		p.drawLine(center_x, y_high, center_x, y_low);
		p.fillRect(QRect(center_x - body_w / 2, body_top, body_w, qMax(2, body_bottom - body_top)), body_color);
		p.setPen(QPen(border_color, 1));
		p.drawRect(QRect(center_x - body_w / 2, body_top, body_w, qMax(2, body_bottom - body_top)));
	}
	{
		auto to_y = [&](int price) {
			double t = (double)(price - min_price) / (double)price_span;

			return top_pad + plot_h - (int)(t * plot_h);
		};
		double candle_w = (double)plot_w / (double)visible_count;

		for (const micro_harvest_trade_overlay_item &item : m_micro_harvest_trades) {
			int open_index = item.open_index;
			int close_index = item.close_index;
			int open_x;
			int open_y;
			QColor trade_color = (item.direction >= 0) ? QColor(0, 150, 136) : QColor(198, 40, 40);

			if (open_index < visible_start || open_index > visible_end) {
				continue;
			}
			open_x = left_pad + (int)(((open_index - visible_start) + 0.5) * candle_w);
			open_y = to_y(item.open_price_pips);
			p.setPen(QPen(trade_color, 2));
			p.setBrush(trade_color);
			p.drawEllipse(QPoint(open_x, open_y), 4, 4);
			if (item.closed && close_index >= visible_start && close_index <= visible_end) {
				int close_x = left_pad + (int)(((close_index - visible_start) + 0.5) * candle_w);
				int close_y = to_y(item.close_price_pips);

				p.drawLine(open_x, open_y, close_x, close_y);
				p.setBrush(Qt::white);
				p.drawEllipse(QPoint(close_x, close_y), 4, 4);
			}
		}
	}

	if (m_footer_visible) {
		p.setPen(QColor(80, 80, 80));
		p.drawText(8, top_pad + 4, QString::number(max_price));
		p.drawText(8, top_pad + plot_h, QString::number(min_price));
		p.drawText(left_pad,
			area.height() - 8,
			QString("Candles: %1 / %2 (wheel to zoom, drag to pan)").arg(visible_count).arg(m_candles.size()));
	}
}

void cluster_chart_widget::wheelEvent(QWheelEvent *event)
{
	int total;
	int current_count;
	int focus_span;
	int min_count;
	int new_count;
	double mouse_ratio;
	int anchor;
	int new_start;

	if (m_candles.isEmpty()) {
		event->ignore();
		return;
	}
	if (!m_interactive) {
		event->ignore();
		return;
	}

	total = m_candles.size();
	current_count = qMax(1, m_view_end - m_view_start + 1);
	focus_span = (m_focus_start >= 0 && m_focus_end >= m_focus_start) ? (m_focus_end - m_focus_start + 1) : 1;
	min_count = qMax(5, focus_span + 4);

	if (event->angleDelta().y() > 0) {
		new_count = qMax(min_count, (int)(current_count * 0.8));
	} else if (event->angleDelta().y() < 0) {
		new_count = qMin(total, (int)(current_count * 1.25));
		if (new_count == current_count && current_count < total) {
			new_count += 1;
		}
	} else {
		event->ignore();
		return;
	}

	mouse_ratio = (width() > 0) ? ((double)event->position().x() / (double)width()) : 0.5;
	mouse_ratio = qBound(0.0, mouse_ratio, 1.0);
	anchor = m_view_start + (int)(mouse_ratio * current_count);
	if (m_focus_start >= 0 && m_focus_end >= m_focus_start) {
		int focus_center = (m_focus_start + m_focus_end) / 2;

		if (focus_center >= m_view_start && focus_center <= m_view_end) {
			anchor = focus_center;
		}
	}

	new_start = anchor - new_count / 2;
	new_start = qMax(0, qMin(new_start, total - new_count));
	m_view_start = new_start;
	m_view_end = new_start + new_count - 1;
	if (m_focus_start >= 0 && m_focus_start < total && m_focus_end >= m_focus_start) {
		if (m_focus_start < m_view_start) {
			m_view_start = m_focus_start;
			m_view_end = qMin(total - 1, m_view_start + new_count - 1);
		}
		if (m_focus_end > m_view_end) {
			m_view_end = m_focus_end;
			m_view_start = qMax(0, m_view_end - new_count + 1);
		}
	}
	update();
	event->accept();
}

void cluster_chart_widget::mousePressEvent(QMouseEvent *event)
{
	if (m_interactive && event->button() == Qt::LeftButton && !m_candles.isEmpty()) {
		m_drag_panning = true;
		m_drag_last_x = event->pos().x();
		setCursor(Qt::ClosedHandCursor);
		event->accept();
		return;
	}
	if (!m_interactive && event->button() == Qt::LeftButton && m_click_payload >= 0) {
		emit chart_clicked(m_click_payload);
		event->accept();
		return;
	}
	QWidget::mousePressEvent(event);
}

void cluster_chart_widget::mouseMoveEvent(QMouseEvent *event)
{
	int area_w;
	int left_pad = 48;
	int right_pad = 12;
	int visible_start;
	int visible_end;
	int visible_count;
	int local_index;

	if (m_drag_panning && m_interactive && !m_candles.isEmpty()) {
		int total = m_candles.size();
		int current_count = qMax(1, m_view_end - m_view_start + 1);
		int plot_w = qMax(1, width() - left_pad - right_pad);
		int delta_x = event->pos().x() - m_drag_last_x;
		int candle_shift;
		int new_start;

		candle_shift = (int)((-(double)delta_x * current_count) / (double)plot_w);
		if (candle_shift != 0) {
			new_start = qBound(0, m_view_start + candle_shift, qMax(0, total - current_count));
			m_view_start = new_start;
			m_view_end = qMin(total - 1, new_start + current_count - 1);
			m_drag_last_x = event->pos().x();
			update();
		}
		event->accept();
		return;
	}
	if (m_candles.isEmpty()) {
		emit candle_hovered(-1);
		QWidget::mouseMoveEvent(event);
		return;
	}
	area_w = width();
	visible_start = qBound(0, m_view_start, m_candles.size() - 1);
	visible_end = qBound(visible_start, m_view_end, m_candles.size() - 1);
	visible_count = visible_end - visible_start + 1;
	if (visible_count <= 0) {
		emit candle_hovered(-1);
		QWidget::mouseMoveEvent(event);
		return;
	}
	if (event->pos().x() < left_pad || event->pos().x() > area_w - right_pad) {
		emit candle_hovered(-1);
		QWidget::mouseMoveEvent(event);
		return;
	}
	local_index = (int)(((event->pos().x() - left_pad) * visible_count) /
		qMax(1, area_w - left_pad - right_pad));
	local_index = qBound(0, local_index, visible_count - 1);
	emit candle_hovered(m_candles[visible_start + local_index].candle_index);
	QWidget::mouseMoveEvent(event);
}

void cluster_chart_widget::mouseReleaseEvent(QMouseEvent *event)
{
	if (m_drag_panning && event->button() == Qt::LeftButton) {
		m_drag_panning = false;
		unsetCursor();
		event->accept();
		return;
	}
	QWidget::mouseReleaseEvent(event);
}

void cluster_chart_widget::leaveEvent(QEvent *event)
{
	if (!m_drag_panning) {
		unsetCursor();
	}
	emit candle_hovered(-1);
	QWidget::leaveEvent(event);
}
