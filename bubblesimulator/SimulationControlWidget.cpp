#include "SimulationControlWidget.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

SimulationControlWidget::SimulationControlWidget(QWidget *parent)
	: QWidget(parent)
{
	QVBoxLayout *layout = new QVBoxLayout(this);
	QHBoxLayout *button_row = new QHBoxLayout;
	QPushButton *start_button = new QPushButton(QStringLiteral("Start"));
	QPushButton *pause_button = new QPushButton(QStringLiteral("Pause"));
	QPushButton *reset_button = new QPushButton(QStringLiteral("Reset"));
	QPushButton *step_button = new QPushButton(QStringLiteral("Step"));
	QPushButton *run_100_button = new QPushButton(QStringLiteral("Run 100 Steps"));
	QPushButton *copy_params_button = new QPushButton(QStringLiteral("Copy Params"));

	button_row->addWidget(start_button);
	button_row->addWidget(pause_button);
	button_row->addWidget(reset_button);
	button_row->addWidget(step_button);
	button_row->addWidget(run_100_button);
	button_row->addWidget(copy_params_button);

	QHBoxLayout *speed_row = new QHBoxLayout;
	QLabel *speed_label = new QLabel(QStringLiteral("Ticks / second"));
	ticks_per_second_spin_ = new QSpinBox;
	ticks_per_second_spin_->setRange(1, 500);
	ticks_per_second_spin_->setValue(20);
	speed_row->addWidget(speed_label);
	speed_row->addWidget(ticks_per_second_spin_);
	speed_row->addStretch(1);

	layout->addLayout(button_row);
	layout->addLayout(speed_row);

	connect(start_button, &QPushButton::clicked, this, &SimulationControlWidget::start_requested);
	connect(pause_button, &QPushButton::clicked, this, &SimulationControlWidget::pause_requested);
	connect(reset_button, &QPushButton::clicked, this, &SimulationControlWidget::reset_requested);
	connect(step_button, &QPushButton::clicked, this, &SimulationControlWidget::step_requested);
	connect(run_100_button, &QPushButton::clicked, this, &SimulationControlWidget::run_100_requested);
	connect(copy_params_button, &QPushButton::clicked, this, &SimulationControlWidget::copy_params_requested);
	connect(ticks_per_second_spin_, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value) {
		emit interval_changed(1000 / qMax(1, value));
	});
}

int SimulationControlWidget::timer_interval_ms() const
{
	return 1000 / qMax(1, ticks_per_second_spin_->value());
}
