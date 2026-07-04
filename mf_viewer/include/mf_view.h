#ifndef MF_VIEW_H
#define MF_VIEW_H

#include <QWidget>
#include <vector>
#include <string>
#include <memory>
#include "candle.h"
#include "layer.h"
#include "prediction_layer.h"

class MfView : public QWidget {
	Q_OBJECT

public:
	explicit MfView(QWidget *parent = nullptr);

	void	setLayers(const std::vector<std::shared_ptr<Layer>> &layers);
	void	setInstrumentName(const std::string &name);
	void	recalculateExtent(); // Force recalculation of price extent

	int getVisibleCount() const;
	int getCandleCount() const;

	// Selection mode for HMM training
	void	setSelectionMode(bool enabled);
	void	setSelectionRange(int startIdx, int endIdx);

	// Prediction layer
	void	setPredictions(int startIndex, const std::vector<PredictedCandle> &predictions);
	void	clearPredictions();
	void	setPredictionsVisible(bool visible);

public slots:
	void	scrollTo(int index);

signals:
	void	candleClicked(const Candle &candle, int layerIndex, int candleIndex);
	void	viewScrolled(int firstVisibleIndex);
	void	candleHovered(const Candle &candle, int layerIndex, int candleIndex);
	void	pointSelected(int candleIndex, int64_t timestamp);

protected:
	void	paintEvent(QPaintEvent *event) override;
	void	resizeEvent(QResizeEvent *event) override;
	void	wheelEvent(QWheelEvent *event) override;
	void	mousePressEvent(QMouseEvent *event) override;
	void	mouseMoveEvent(QMouseEvent *event) override;
	void	mouseReleaseEvent(QMouseEvent *event) override;
	void	mouseDoubleClickEvent(QMouseEvent *event) override;
	void	keyPressEvent(QKeyEvent *event) override;

private:
	std::vector<std::shared_ptr<Layer>>	_layers;
	std::string		_instrumentName;

	int		_firstVisibleIndex;
	int		_visibleCount;
	double		_pixelsPerCandle;
	double		_verticalScale;
	double		_fixedMinPrice;
	double		_fixedMaxPrice;
	bool		_useFixedExtent;

	bool		_dragging;
	QPoint		_lastMousePos;
	int		_lastHoveredLayerIndex;
	int		_lastHoveredCandleIndex;

	// Selection mode state
	bool		_selectionMode;
	int		_selectionStartIdx;
	int		_selectionEndIdx;

	// Prediction layer
	PredictionLayer	_predictionLayer;

	void		updateVisibleRange();
	void		renderPredictions(QPainter &p, int plotLeft, int plotTop,
	                              int plotHeight, double minPrice, double maxPrice);
	void		scrollByCandles(int delta);
	void		zoomAt(int mouseX, int deltaSteps);
	void		verticalZoom(int deltaSteps);
	int		candleIndexAtX(int x, int &outLayerIndex) const;
	void		computePriceRange(double &minPrice, double &maxPrice) const;
	int		getTotalCandleCount() const;
};

#endif // MF_VIEW_H
