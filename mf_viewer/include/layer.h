#ifndef LAYER_H
#define LAYER_H

#include <QString>
#include <QColor>
#include <vector>
#include "candle.h"

struct LayerColors {
	QColor upColor;
	QColor downColor;
	QColor dojiColor;

	LayerColors()
		: upColor(0, 0, 255),      // Blue
		  downColor(200, 0, 0),    // Red
		  dojiColor(0, 0, 0)       // Black
	{}
};

class Layer {
public:
	Layer(const QString &name, const std::vector<Candle> &candles)
		: _name(name), _candles(candles), _visible(true), _colors()
	{}

	const QString& name() const { return _name; }
	const std::vector<Candle>& candles() const { return _candles; }
	bool isVisible() const { return _visible; }
	void setVisible(bool visible) { _visible = visible; }

	const LayerColors& colors() const { return _colors; }
	void setColors(const LayerColors &colors) { _colors = colors; }

private:
	QString _name;
	std::vector<Candle> _candles;
	bool _visible;
	LayerColors _colors;
};

#endif // LAYER_H
