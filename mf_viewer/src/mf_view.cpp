#include "mf_view.h"

#include <QPainter>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QToolTip>
#include <QDateTime>
#include <algorithm>
#include <cmath>

static const int TOP_MARGIN = 20;
static const int BOTTOM_MARGIN = 20;
static const int LEFT_MARGIN = 40;
static const int RIGHT_MARGIN = 20;

MfView::MfView(QWidget *parent)
	: QWidget(parent),
	  _firstVisibleIndex(0),
	  _visibleCount(100),
	  _pixelsPerCandle(5.0),
	  _verticalScale(1.0),
	  _fixedMinPrice(0.0),
	  _fixedMaxPrice(1.0),
	  _useFixedExtent(false),
	  _dragging(false),
	  _lastHoveredLayerIndex(-1),
	  _lastHoveredCandleIndex(-1),
	  _selectionMode(false),
	  _selectionStartIdx(-1),
	  _selectionEndIdx(-1)
{
	setMinimumHeight(200);
	setMinimumWidth(300);
	setMouseTracking(true);
	setFocusPolicy(Qt::StrongFocus);
}

void MfView::setLayers(const std::vector<std::shared_ptr<Layer>> &layers)
{
	_layers = layers;
	_firstVisibleIndex = 0;
	int totalCandles = getTotalCandleCount();
	if (totalCandles == 0) {
		_visibleCount = 0;
	} else {
		_visibleCount = std::min<int>(totalCandles, 100);
	}
	// Only recalculate extent if this is a significant change
	// (adding/removing layers, not just toggling visibility)
	// We'll detect this by checking if we're being called from addLayer or removeLayer
	// For now, don't recalculate - let the caller decide
	updateVisibleRange();
	update();
}

int MfView::getTotalCandleCount() const
{
	int maxCount = 0;
	for (const auto &layer : _layers) {
		if (layer->isVisible()) {
			maxCount = std::max(maxCount, static_cast<int>(layer->candles().size()));
		}
	}
	return maxCount;
}

void MfView::setInstrumentName(const std::string &name)
{
	_instrumentName = name;
	update();
}

void MfView::recalculateExtent()
{
	_useFixedExtent = false;
	update();
}

void MfView::setSelectionMode(bool enabled)
{
	_selectionMode = enabled;
	if (enabled) {
		setCursor(Qt::CrossCursor);
	} else {
		setCursor(Qt::ArrowCursor);
	}
	update();
}

void MfView::setSelectionRange(int startIdx, int endIdx)
{
	_selectionStartIdx = startIdx;
	_selectionEndIdx = endIdx;
	update();
}

void MfView::scrollTo(int index)
{
	if (getTotalCandleCount() == 0) {
		return;
	}
	_firstVisibleIndex = index;
	_useFixedExtent = false; // Recalculate extent for new visible range
	updateVisibleRange();
	update();
}

void MfView::updateVisibleRange()
{
	int totalCandles = getTotalCandleCount();
	if (totalCandles == 0) {
		_firstVisibleIndex = 0;
		_visibleCount = 0;
		return;
	}

	if (_firstVisibleIndex < 0) {
		_firstVisibleIndex = 0;
	}
	int maxFirst = totalCandles - _visibleCount;
	if (maxFirst < 0) {
		maxFirst = 0;
	}
	if (_firstVisibleIndex > maxFirst) {
		_firstVisibleIndex = maxFirst;
	}

	int plotWidth = width() - LEFT_MARGIN - RIGHT_MARGIN;
	if (plotWidth <= 0) {
		_pixelsPerCandle = 1.0;
	} else {
		_pixelsPerCandle = static_cast<double>(plotWidth) / std::max(1, _visibleCount);
	}

	emit viewScrolled(_firstVisibleIndex);
}

void MfView::computePriceRange(double &minPrice, double &maxPrice) const
{
	bool foundAny = false;
	minPrice = 0.0;
	maxPrice = 1.0;

	if (_visibleCount <= 0) {
		return;
	}

	int start = _firstVisibleIndex;
	int end = _firstVisibleIndex + _visibleCount;

	for (const auto &layer : _layers) {
		if (!layer->isVisible()) {
			continue;
		}

		const auto &candles = layer->candles();
		int layerEnd = std::min(end, static_cast<int>(candles.size()));

		for (int i = start; i < layerEnd; ++i) {
			const Candle &c = candles[i];
			if (!foundAny) {
				minPrice = c.low;
				maxPrice = c.high;
				foundAny = true;
			} else {
				if (c.low < minPrice) {
					minPrice = c.low;
				}
				if (c.high > maxPrice) {
					maxPrice = c.high;
				}
			}
		}
	}

	if (!foundAny || maxPrice <= minPrice) {
		maxPrice = minPrice + 1.0;
	}
}

void MfView::paintEvent(QPaintEvent *event)
{
	Q_UNUSED(event);

	QPainter p(this);
	p.fillRect(rect(), Qt::white);

	// Instrument name top-left
	if (!_instrumentName.empty()) {
		p.setPen(Qt::black);
		p.drawText(10, 15, QString::fromStdString(_instrumentName));
	}

	if (_visibleCount <= 0 || _layers.empty()) {
		return;
	}

	double minPrice = 0.0;
	double maxPrice = 1.0;

	if (_useFixedExtent) {
		// Use the fixed extent (preserved when toggling layer visibility)
		minPrice = _fixedMinPrice;
		maxPrice = _fixedMaxPrice;
	} else {
		// Compute extent from visible data and save it
		computePriceRange(minPrice, maxPrice);
		_fixedMinPrice = minPrice;
		_fixedMaxPrice = maxPrice;
		_useFixedExtent = true;
	}

	int plotLeft = LEFT_MARGIN;
	int plotRight = width() - RIGHT_MARGIN;
	int plotTop = TOP_MARGIN;
	int plotBottom = height() - BOTTOM_MARGIN;
	int plotHeight = plotBottom - plotTop;

	if (width() - LEFT_MARGIN - RIGHT_MARGIN <= 0 || plotHeight <= 0) {
		return;
	}

	double priceRange = maxPrice - minPrice;

	// Apply vertical scale
	double effectiveHeight = plotHeight * _verticalScale;
	double verticalOffset = (plotHeight - effectiveHeight) / 2.0;

	// Draw frame
	p.setPen(Qt::black);
	p.drawRect(plotLeft, plotTop, plotRight - plotLeft, plotHeight);

	// Draw selection range highlight (before candles so it's behind them)
	if (_selectionStartIdx >= 0 || _selectionEndIdx >= 0) {
		int highlightStart = std::max(_selectionStartIdx, _firstVisibleIndex);
		int highlightEnd = (_selectionEndIdx >= 0) ? std::min(_selectionEndIdx, _firstVisibleIndex + _visibleCount)
		                                           : std::min(_selectionStartIdx + 1, _firstVisibleIndex + _visibleCount);

		if (highlightEnd > highlightStart) {
			int xStart = plotLeft + (highlightStart - _firstVisibleIndex) * _pixelsPerCandle;
			int xEnd = plotLeft + (highlightEnd - _firstVisibleIndex) * _pixelsPerCandle;
			QColor highlightColor(100, 149, 237, 80); // Light blue with transparency
			p.fillRect(xStart, plotTop, xEnd - xStart, plotHeight, highlightColor);
		}
	}

	auto priceToY = [&](double price) -> int {
		double rel = (maxPrice - price) / priceRange;
		return plotTop + static_cast<int>(verticalOffset + rel * effectiveHeight + 0.5);
	};

	// Draw all layers
	for (const auto &layer : _layers) {
		if (!layer->isVisible()) {
			continue;
		}

		const auto &candles = layer->candles();
		const LayerColors &colors = layer->colors();

		for (int i = 0; i < _visibleCount; ++i) {
			int idx = _firstVisibleIndex + i;
			if (idx >= static_cast<int>(candles.size())) {
				break;
			}
			const Candle &c = candles[idx];

			double xCenter = plotLeft + (i + 0.5) * _pixelsPerCandle;
			double xHalf = std::max(1.0, _pixelsPerCandle * 0.3);

			int yHigh = priceToY(c.high);
			int yLow = priceToY(c.low);
			int yOpen = priceToY(c.open);
			int yClose = priceToY(c.close);

			QColor bodyColor;
			if (c.open == c.close) {
				bodyColor = colors.dojiColor;
			} else {
				bool up = c.close > c.open;
				bodyColor = up ? colors.upColor : colors.downColor;
			}

			p.setPen(bodyColor);
			p.drawLine(QPointF(xCenter, yHigh), QPointF(xCenter, yLow));

			int bodyTop = std::min(yOpen, yClose);
			int bodyBottom = std::max(yOpen, yClose);
			if (bodyBottom == bodyTop) {
				bodyBottom = bodyTop + 1;
			}

			p.fillRect(QRectF(xCenter - xHalf, bodyTop, 2.0 * xHalf, bodyBottom - bodyTop), bodyColor);
		}
	}

	// Render predictions overlay
	if (_predictionLayer.isVisible() && _predictionLayer.hasPredictions()) {
		renderPredictions(p, plotLeft, plotTop, plotHeight, minPrice, maxPrice);
	}
}

void MfView::renderPredictions(QPainter &p, int plotLeft, int plotTop,
                               int plotHeight, double minPrice, double maxPrice)
{
	double priceRange = maxPrice - minPrice;
	if (priceRange <= 0) return;

	// Semi-transparent black for predictions
	QColor predColor(0, 0, 0, 100);  // 40% alpha

	auto priceToY = [&](double price) -> int {
		double rel = (maxPrice - price) / priceRange;
		return plotTop + static_cast<int>(rel * plotHeight + 0.5);
	};

	// Get reference layer for actual open prices
	if (_layers.empty()) return;
	const auto &refLayer = _layers[0];
	const auto &refCandles = refLayer->candles();

	for (int i = 0; i < _visibleCount; ++i) {
		int idx = _firstVisibleIndex + i;

		if (!_predictionLayer.hasPredictionAt(idx)) continue;
		if (idx >= static_cast<int>(refCandles.size())) continue;

		PredictedCandle pred = _predictionLayer.getPredictionAt(idx);
		const Candle &real = refCandles[idx];

		// Reconstruct predicted OHLC using actual open as reference
		double predOpen = real.open;
		double predHigh = predOpen + pred.high_open_diff;
		double predClose = predOpen + pred.close_open_diff;
		double predLow = predClose + pred.low_close_diff;

		double xCenter = plotLeft + (i + 0.5) * _pixelsPerCandle;
		double xHalf = std::max(1.0, _pixelsPerCandle * 0.2);  // Thinner than real candles

		int yHigh = priceToY(predHigh);
		int yLow = priceToY(predLow);
		int yOpen = priceToY(predOpen);
		int yClose = priceToY(predClose);

		// Draw wick
		p.setPen(predColor);
		p.drawLine(QPointF(xCenter, yHigh), QPointF(xCenter, yLow));

		// Draw body
		int bodyTop = std::min(yOpen, yClose);
		int bodyBottom = std::max(yOpen, yClose);
		if (bodyBottom == bodyTop) {
			bodyBottom = bodyTop + 1;
		}

		p.fillRect(QRectF(xCenter - xHalf, bodyTop, 2.0 * xHalf, bodyBottom - bodyTop), predColor);
	}
}

void MfView::setPredictions(int startIndex, const std::vector<PredictedCandle> &predictions)
{
	_predictionLayer.setPredictions(startIndex, predictions);
	update();
}

void MfView::clearPredictions()
{
	_predictionLayer.clear();
	update();
}

void MfView::setPredictionsVisible(bool visible)
{
	_predictionLayer.setVisible(visible);
	update();
}

int MfView::candleIndexAtX(int x, int &outLayerIndex) const
{
	int plotLeft = LEFT_MARGIN;
	int plotRight = width() - RIGHT_MARGIN;

	outLayerIndex = -1;
	if (x < plotLeft || x >= plotRight || _visibleCount <= 0) {
		return -1;
	}
	double offset = static_cast<double>(x - plotLeft);
	int localIndex = static_cast<int>(offset / _pixelsPerCandle);
	int idx = _firstVisibleIndex + localIndex;

	// Find first visible layer that has this candle index
	for (size_t layerIdx = 0; layerIdx < _layers.size(); ++layerIdx) {
		const auto &layer = _layers[layerIdx];
		if (layer->isVisible() && idx >= 0 && idx < static_cast<int>(layer->candles().size())) {
			outLayerIndex = static_cast<int>(layerIdx);
			return idx;
		}
	}

	return -1;
}


int MfView::getVisibleCount() const
{
	return _visibleCount;
}

int MfView::getCandleCount() const
{
	return getTotalCandleCount();
}

void MfView::keyPressEvent(QKeyEvent *event)
{
	switch (event->key()) {
	case Qt::Key_Left:
		scrollByCandles(-10); // Scroll 10 candles left
		break;
	case Qt::Key_Right:
		scrollByCandles(10); // Scroll 10 candles right
		break;
	case Qt::Key_Home:
		scrollTo(0);
		break;
	case Qt::Key_End:
		{
			int totalCandles = getTotalCandleCount();
			if (totalCandles > 0) {
				scrollTo(totalCandles - _visibleCount);
			}
		}
		break;
	default:
		QWidget::keyPressEvent(event);
	}
	event->accept();
}

void MfView::wheelEvent(QWheelEvent *event)
{
	int delta = event->angleDelta().y();
	if (delta == 0) {
		event->accept();
		return;
	}

	int steps = delta > 0 ? 1 : -1;

	// Shift+wheel for vertical zoom
	if (event->modifiers() & Qt::ShiftModifier) {
		verticalZoom(steps);
	} else {
		// Normal wheel for horizontal zoom
		zoomAt(event->position().x(), steps);
	}
	event->accept();
}

void MfView::zoomAt(int mouseX, int deltaSteps)
{
	int totalCandles = getTotalCandleCount();
	if (totalCandles == 0) {
		return;
	}

	double factor = (deltaSteps > 0) ? 0.8 : 1.25;

	int newVisible = static_cast<int>(_visibleCount * factor);
	if (newVisible < 10) {
		newVisible = 10;
	}
	if (newVisible > totalCandles) {
		newVisible = totalCandles;
	}

	int plotLeft = LEFT_MARGIN;
	int plotRight = width() - RIGHT_MARGIN;
	if (plotRight <= plotLeft) {
		return;
	}
	double rel = static_cast<double>(mouseX - plotLeft) / std::max(1, plotRight - plotLeft);
	if (rel < 0.0) {
		rel = 0.0;
	}
	if (rel > 1.0) {
		rel = 1.0;
	}

	int centerIndex = _firstVisibleIndex + static_cast<int>(_visibleCount * rel);
	int newFirst = centerIndex - static_cast<int>(newVisible * rel);

	_visibleCount = newVisible;
	_firstVisibleIndex = newFirst;

	_useFixedExtent = false; // Recalculate extent for new visible range
	updateVisibleRange();
	update();
}

void MfView::verticalZoom(int deltaSteps)
{
	// Zoom in = increase scale (more detail), zoom out = decrease scale (fit more)
	double factor = (deltaSteps > 0) ? 1.2 : 0.8;
	_verticalScale *= factor;

	// Limit range: 0.2 (20% of height) to 5.0 (500% of height)
	if (_verticalScale < 0.2) {
		_verticalScale = 0.2;
	}
	if (_verticalScale > 5.0) {
		_verticalScale = 5.0;
	}

	update();
}

void MfView::mousePressEvent(QMouseEvent *event)
{
	if (event->button() == Qt::LeftButton) {
		_dragging = true;
		_lastMousePos = event->pos();
		event->accept();
	} else {
		event->ignore();
	}
}

void MfView::mouseMoveEvent(QMouseEvent *event)
{
	if (_dragging && !_selectionMode) {
		int dx = event->pos().x() - _lastMousePos.x();
		_lastMousePos = event->pos();

		double candlesDelta = dx / std::max(1.0, _pixelsPerCandle);
		if (candlesDelta != 0.0) {
			scrollByCandles(static_cast<int>(-candlesDelta));
		}
		event->accept();
	} else {
		int layerIndex = -1;
		int candleIndex = candleIndexAtX(event->pos().x(), layerIndex);

		// Show tooltip in selection mode
		if (_selectionMode && candleIndex >= 0 && layerIndex >= 0 &&
		    layerIndex < static_cast<int>(_layers.size())) {
			const auto &layer = _layers[layerIndex];
			const auto &candles = layer->candles();
			if (candleIndex < static_cast<int>(candles.size())) {
				const Candle &c = candles[candleIndex];
				QDateTime dt = QDateTime::fromMSecsSinceEpoch(c.timestamp_ms);
				QString tooltip = QString("Index: %1\n%2")
					.arg(candleIndex)
					.arg(dt.toString("yyyy-MM-dd HH:mm"));
				QToolTip::showText(event->globalPos(), tooltip, this);
			}
		}

		if (candleIndex != _lastHoveredCandleIndex || layerIndex != _lastHoveredLayerIndex) {
			if (candleIndex >= 0 && layerIndex >= 0 && layerIndex < static_cast<int>(_layers.size())) {
				const auto &layer = _layers[layerIndex];
				const auto &candles = layer->candles();
				if (candleIndex < static_cast<int>(candles.size())) {
					emit candleHovered(candles[candleIndex], layerIndex, candleIndex);
				}
			}
			_lastHoveredCandleIndex = candleIndex;
			_lastHoveredLayerIndex = layerIndex;
		}
		event->ignore();
	}
}

void MfView::mouseReleaseEvent(QMouseEvent *event)
{
	if (event->button() == Qt::LeftButton) {
		_dragging = false;

		// Click detection (no drag)
		int layerIndex = -1;
		int candleIndex = candleIndexAtX(event->pos().x(), layerIndex);
		if (candleIndex >= 0 && layerIndex >= 0 && layerIndex < static_cast<int>(_layers.size())) {
			const auto &layer = _layers[layerIndex];
			const auto &candles = layer->candles();
			if (candleIndex < static_cast<int>(candles.size())) {
				// In selection mode, emit pointSelected instead of candleClicked
				if (_selectionMode) {
					emit pointSelected(candleIndex, candles[candleIndex].timestamp_ms);
				} else {
					emit candleClicked(candles[candleIndex], layerIndex, candleIndex);
				}
			}
		}
		event->accept();
	} else {
		event->ignore();
	}
}

void MfView::mouseDoubleClickEvent(QMouseEvent *event)
{
	Q_UNUSED(event);
	// Reset zoom/scroll
	_firstVisibleIndex = 0;
	int totalCandles = getTotalCandleCount();
	_visibleCount = std::min<int>(totalCandles, 100);
	_verticalScale = 1.0;
	updateVisibleRange();
	update();
}

void MfView::scrollByCandles(int delta)
{
	if (getTotalCandleCount() == 0 || _visibleCount <= 0) {
		return;
	}
	_firstVisibleIndex += delta;
	_useFixedExtent = false; // Recalculate Y extent for new visible range
	updateVisibleRange();
	update();
}

void MfView::resizeEvent(QResizeEvent *event)
{
	QWidget::resizeEvent(event);
	updateVisibleRange();
}
