#ifndef SIMULATIONCONTROLWIDGET_H
#define SIMULATIONCONTROLWIDGET_H

#include <QWidget>

class QPushButton;
class QSpinBox;

class SimulationControlWidget : public QWidget
{
	Q_OBJECT

public:
	explicit SimulationControlWidget(QWidget *parent = nullptr);

	int timer_interval_ms() const;

signals:
	void start_requested();
	void pause_requested();
	void reset_requested();
	void step_requested();
	void run_100_requested();
	void copy_params_requested();
	void interval_changed(int interval_ms);

private:
	QSpinBox *ticks_per_second_spin_;
};

#endif
